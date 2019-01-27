#include "stdafx.h"
#include "STileViewEx.h"
#include <algorithm>

namespace SOUI
{
#ifndef SCROLL_TIMER_ID
#define SCROLL_TIMER_ID 110
#endif // SCROLL_TIMER_ID

	class STileViewDataSetObserverEx : public TObjRefImpl<ILvDataSetObserver>
	{
	public:
		STileViewDataSetObserverEx(STileViewEx *pView) : m_pOwner(pView)
		{
		}
		virtual void onChanged();
		virtual void onInvalidated();
		virtual void OnItemChanged(int iItem) override;

	protected:
		STileViewEx * m_pOwner;

	};

	//////////////////////////////////////////////////////////////////////////
	void STileViewDataSetObserverEx::onChanged()
	{
		m_pOwner->onDataSetChanged();
	}

	void STileViewDataSetObserverEx::onInvalidated()
	{
		m_pOwner->onDataSetInvalidated();
	}

	void STileViewDataSetObserverEx::OnItemChanged(int iItem)
	{
		
	}


	//////////////////////////////////////////////////////////////////////////
	STileViewEx::STileViewEx()
		: m_iFirstVisible(-1)
		, m_pHoverItem(NULL)
		, m_itemCapture(NULL)
		, m_nMarginSize(0.0f, SLayoutSize::px)
		, m_bWantTab(FALSE)
		, m_colorDropBk(RGBA(0, 0, 255, 255))
		, m_DragBkAlpha(66)
		, m_bDrag(FALSE), m_bDraging(FALSE), m_bBeginDropItem(FALSE), m_bDropIteming(FALSE)

	{
		m_bFocusable = TRUE;
		m_observer.Attach(new STileViewDataSetObserverEx(this));
		m_dwUpdateInterval = 40;
		m_evtSet.addEvent(EVENTID(EventLVSelChanging));
		m_evtSet.addEvent(EVENTID(EventLVSelChanged));
	}

	STileViewEx::~STileViewEx()
	{
		m_observer = NULL;
		m_tvItemLocator = NULL;
	}

	BOOL STileViewEx::SetAdapter(ILvAdapter *adapter)
	{
		if (!m_tvItemLocator)
		{
			SASSERT_FMT(FALSE, _T("error: A item locator is in need before setting adapter!!!"));
			return FALSE;
		}

		if (m_adapter)
		{
			m_adapter->unregisterDataSetObserver(m_observer);

			//free all itemPanels in recycle
			for (size_t i = 0; i < m_itemRecycle.GetCount(); i++)
			{
				SList<SItemPanel *> *lstItemPanels = m_itemRecycle.GetAt(i);
				SPOSITION pos = lstItemPanels->GetHeadPosition();
				while (pos)
				{
					SItemPanel *pItemPanel = lstItemPanels->GetNext(pos);
					pItemPanel->DestroyWindow();
				}
				delete lstItemPanels;
			}
			m_itemRecycle.RemoveAll();

			//free all visible itemPanels
			SPOSITION pos = m_lstItems.GetHeadPosition();
			while (pos)
			{
				ItemInfo ii = m_lstItems.GetNext(pos);
				ii.pItem->DestroyWindow();
			}
			m_lstItems.RemoveAll();
		}

		m_adapter = adapter;
		if (m_tvItemLocator)
		{
			m_tvItemLocator->SetTileViewWidth(GetClientRect().Width());
			m_tvItemLocator->SetAdapter(adapter);
		}
		if (m_adapter)
		{
			m_adapter->InitByTemplate(m_xmlTemplate.first_child());
			m_adapter->registerDataSetObserver(m_observer);
			for (int i = 0; i < m_adapter->getViewTypeCount(); i++)
			{
				m_itemRecycle.Add(new SList<SItemPanel *>());
			}
			onDataSetChanged();
		}
		return TRUE;
	}

	void STileViewEx::UpdateScrollBar()
	{
		CRect rcClient = SWindow::GetClientRect();
		CSize size = rcClient.Size();
		CSize szView;
		szView.cx = rcClient.Width();
		szView.cy = m_tvItemLocator ? m_tvItemLocator->GetTotalHeight() : 0;

		//  关闭滚动条
		m_wBarVisible = SSB_NULL;

		if (size.cy < szView.cy)
		{
			//  需要纵向滚动条
			m_wBarVisible |= SSB_VERT;
			m_siVer.nMin = 0;
			m_siVer.nMax = szView.cy - 1;
			m_siVer.nPage = size.cy;
			m_siVer.nPos = (std::min)(m_siVer.nPos, m_siVer.nMax - (int)m_siVer.nPage);
		}
		else
		{
			//  不需要纵向滚动条
			m_siVer.nPage = size.cy;
			m_siVer.nMin = 0;
			m_siVer.nMax = size.cy - 1;
			m_siVer.nPos = 0;
		}

		SetScrollPos(TRUE, m_siVer.nPos, FALSE);

		//  重新计算客户区及非客户区
		SSendMessage(WM_NCCALCSIZE);

		InvalidateRect(NULL);
	}

	void STileViewEx::onDataSetChanged()
	{
		if (!m_adapter)
		{
			return;
		}
		if (m_tvItemLocator)
		{
			m_tvItemLocator->OnDataSetChanged();
		}
		if (!m_lSelItems.IsEmpty())
			DelBiggerThan(m_adapter->getCount() - 1);

		UpdateScrollBar();
		UpdateVisibleItems();
	}

	void STileViewEx::onDataSetInvalidated()
	{
		m_bDatasetInvalidated = TRUE;
		Invalidate();
	}
#define  MakeRect(rc,pt1,pt2) rc.left=min(pt1.x,pt2.x);\
							  rc.right=max(pt1.x,pt2.x);\
							  rc.top=min(pt1.y,pt2.y);\
							  rc.bottom=max(pt1.y,pt2.y);
	void STileViewEx::OnPaint(IRenderTarget *pRT)
	{
		if (m_bDatasetInvalidated)
		{
			UpdateVisibleItems();
			m_bDatasetInvalidated = FALSE;
		}
		SPainter duiDC;
		BeforePaint(pRT, duiDC);

		int iFirst = m_iFirstVisible;
		if (iFirst != -1)
		{
			CRect rcClient;
			GetClientRect(&rcClient);
			pRT->PushClipRect(&rcClient, RGN_AND);

			CRect rcClip, rcInter;
			pRT->GetClipBox(&rcClip);

			int nOffset = m_tvItemLocator->Item2Position(iFirst) - m_siVer.nPos;
			int nLastBottom = rcClient.top + m_tvItemLocator->GetMarginSize() + nOffset;

			SPOSITION pos = m_lstItems.GetHeadPosition();
			int i = 0;
			for (; pos; i++)
			{
				ItemInfo ii = m_lstItems.GetNext(pos);
				CRect rcItem = m_tvItemLocator->GetItemRect(iFirst + i);
				rcItem.OffsetRect(rcClient.left, 0);
				rcItem.MoveToY(nLastBottom);
				if (m_tvItemLocator->IsLastInRow(iFirst + i))
				{
					nLastBottom = rcItem.bottom + m_tvItemLocator->GetMarginSize();
				}

				rcInter.IntersectRect(&rcClip, &rcItem);
				if (!rcInter.IsRectEmpty())
				{
					ii.pItem->Draw(pRT, rcItem);
				}
			}
			pRT->PopClip();
		}

		if (m_bDrag)
		{
			CRect updataRc;
			CRect rcClient;
			GetClientRect(&rcClient);
			MakeRect(updataRc, m_dragStartPos, m_dragEndPos);
			bool drawTopLine = true, drawLeftLine = true, drawRigthLine = true, drawBottomLine = true;
			if (updataRc.top < rcClient.top)
				drawTopLine = false;
			if (updataRc.left < rcClient.left)
				drawLeftLine = false;
			if (updataRc.right > rcClient.right)
				drawRigthLine = false;
			if (updataRc.bottom > rcClient.bottom)
				drawBottomLine = false;
			updataRc.IntersectRect(updataRc, rcClient);
			SColor colorDropBk = m_colorDropBk;
			colorDropBk.updateAlpha(m_DragBkAlpha);
			if (!updataRc.IsRectEmpty())
			{
				pRT->FillSolidRect(updataRc, colorDropBk.toCOLORREF());
				POINT line[2];
				size_t linenum = 2;
				CAutoRefPtr<IPen> linepen, oldpen;
				pRT->CreatePen(PS_SOLID, m_colorDropBk, 1, &linepen);
				pRT->SelectObject(linepen, (IRenderObj**)&oldpen);
				if (drawTopLine)
				{
					line[0].x = updataRc.left;
					line[1].y = line[0].y = updataRc.top;
					line[1].x = updataRc.right;
					pRT->DrawLines(line, linenum);
				}
				if (drawBottomLine)
				{
					line[0].x = updataRc.left + 1;
					line[1].y = line[0].y = updataRc.bottom - 1;
					line[1].x = updataRc.right - 1;
					pRT->DrawLines(line, linenum);
				}
				if (drawLeftLine)
				{
					line[1].x = line[0].x = updataRc.left;
					line[0].y = updataRc.top;
					line[1].y = updataRc.bottom;
					pRT->DrawLines(line, linenum);
				}
				if (drawRigthLine)
				{
					line[1].x = line[0].x = updataRc.right - 1;
					line[0].y = updataRc.top + 1;
					line[1].y = updataRc.bottom - 1;
					pRT->DrawLines(line, linenum);
				}
				pRT->SelectObject(oldpen);
			}
		}
		AfterPaint(pRT, duiDC);
	}

	BOOL STileViewEx::OnScroll(BOOL bVertical, UINT uCode, int nPos)
	{
		int nOldPos = m_siVer.nPos;
		__super::OnScroll(bVertical, uCode, nPos);
		int nNewPos = m_siVer.nPos;

		if (nOldPos != nNewPos)
		{
			UpdateVisibleItems();

			//加速滚动时UI的刷新
			if (uCode == SB_THUMBTRACK)
			{
				ScrollUpdate();
			}
			if (m_bDrag)
			{
				CPoint pt;
				GetCursorPos(&pt);
				ScreenToClient(GetContainer()->GetHostHwnd(), &pt);
				pt.x = m_dragStartPos.x;
				SItemPanel *pItem = HitTest(pt);
				if (pItem)
				{
					//m_adapter->UpataSel(pItem->GetItemIndex());
				}
				else
				{
					ItemInfo ii;
					if (GetClientRect().top > pt.y)
						ii = m_lstItems.GetHead();
					else ii = m_lstItems.GetTail();
					//m_adapter->UpataSel(ii.pItem->GetItemIndex());
				}
				m_dragStartPos.y -= nNewPos - nOldPos;
			}
			return TRUE;
		}
		return FALSE;
	}

	void STileViewEx::UpdateVisibleItems()
	{
		if (!m_adapter)
		{
			return;
		}
		int iOldFirstVisible = m_iFirstVisible;
		int iOldLastVisible = m_iFirstVisible + (int)m_lstItems.GetCount();

		int iNewFirstVisible = m_tvItemLocator->Position2Item(m_siVer.nPos);
		int iNewLastVisible = iNewFirstVisible;

		int pos = m_tvItemLocator->Item2Position(iNewFirstVisible);
		int iHoverItem = m_pHoverItem ? (int)m_pHoverItem->GetItemIndex() : -1;

		ItemInfo *pItemInfos = new ItemInfo[m_lstItems.GetCount()];
		SPOSITION spos = m_lstItems.GetHeadPosition();
		int i = 0;
		while (spos)
		{
			pItemInfos[i++] = m_lstItems.GetNext(spos);
		}

		m_lstItems.RemoveAll();

		if (iNewFirstVisible != -1)
		{
			while (pos < m_siVer.nPos + (int)m_siVer.nPage && iNewLastVisible < m_adapter->getCount())
			{
				DWORD dwState = WndState_Normal;
				if (iHoverItem == iNewLastVisible) dwState |= WndState_Hover;
				if (IsSel(iNewLastVisible)) dwState |= WndState_Check;

				ItemInfo ii = { NULL, -1 };
				ii.nType = m_adapter->getItemViewType(iNewLastVisible, dwState);
				if (iNewLastVisible >= iOldFirstVisible && iNewLastVisible < iOldLastVisible)
				{
					//use the old visible item
					int iItem = iNewLastVisible - iOldFirstVisible;
					SASSERT(iItem >= 0 && iItem <= (iOldLastVisible - iOldFirstVisible));
					if (ii.nType == pItemInfos[iItem].nType)
					{
						ii = pItemInfos[iItem];
						pItemInfos[iItem].pItem = NULL;//标记该行已经被重用
					}
				}
				if (!ii.pItem)
				{
					//create new visible item
					SList<SItemPanel *> *lstRecycle = m_itemRecycle.GetAt(ii.nType);
					if (lstRecycle->IsEmpty())
					{
						//创建一个新的列表项
						ii.pItem = SItemPanel::Create(this, pugi::xml_node(), this);
						ii.pItem->GetEventSet()->subscribeEvent(EventCmd::EventID, Subscriber(&STileViewEx::OnItemClick, this));
					}
					else
					{
						ii.pItem = lstRecycle->RemoveHead();
					}
					ii.pItem->SetItemIndex(iNewLastVisible);
				}
				ii.pItem->SetVisible(TRUE);
				CRect rcItem = m_tvItemLocator->GetItemRect(iNewLastVisible);
				rcItem.MoveToXY(0, 0);
				ii.pItem->Move(rcItem);

				//设置状态，同时暂时禁止应用响应statechanged事件。
				ii.pItem->GetEventSet()->setMutedState(true);
				ii.pItem->ModifyItemState(dwState, 0);
				ii.pItem->GetEventSet()->setMutedState(false);

				m_adapter->getView(iNewLastVisible, ii.pItem, m_xmlTemplate.first_child());
				ii.pItem->DoColorize(GetColorizeColor());

				ii.pItem->UpdateLayout();
				if (IsSel(iNewLastVisible))
				{
					ii.pItem->ModifyItemState(WndState_Check, 0);
				}

				m_lstItems.AddTail(ii);

				if (m_tvItemLocator->IsLastInRow(iNewLastVisible))
				{
					pos += rcItem.Height() + m_tvItemLocator->GetMarginSize();
				}

				iNewLastVisible++;
			}
		}

		//move old visible items which were not reused to recycle
		for (int i = 0; i < (iOldLastVisible - iOldFirstVisible); i++)
		{
			ItemInfo ii = pItemInfos[i];
			if (!ii.pItem)
			{
				continue;
			}
			if (ii.pItem == m_pHoverItem)
			{
				ii.pItem->DoFrameEvent(WM_MOUSELEAVE, 0, 0);
				m_pHoverItem = NULL;
			}
			ii.pItem->GetEventSet()->setMutedState(true);
			ii.pItem->ModifyItemState(0, WndState_Check);
			ii.pItem->GetFocusManager()->SetFocusedHwnd(0);

			ii.pItem->SetVisible(FALSE);
			ii.pItem->GetEventSet()->setMutedState(false);
			m_itemRecycle[ii.nType]->AddTail(ii.pItem);
		}
		delete[] pItemInfos;

		m_iFirstVisible = iNewFirstVisible;
	}

	void STileViewEx::OnSize(UINT nType, CSize size)
	{
		__super::OnSize(nType, size);

		CRect rcClient = SWindow::GetClientRect();
		m_tvItemLocator->SetTileViewWidth(rcClient.Width());//重设TileView宽度
		UpdateScrollBar();//重设滚动条

		UpdateVisibleItems();
	}

	void STileViewEx::OnDestroy()
	{
		if (m_adapter)
		{
			m_adapter->unregisterDataSetObserver(m_observer);
		}

		//destroy all itempanel
		SPOSITION pos = m_lstItems.GetHeadPosition();
		while (pos)
		{
			ItemInfo ii = m_lstItems.GetNext(pos);
			ii.pItem->Release();
		}
		m_lstItems.RemoveAll();

		for (int i = 0; i < (int)m_itemRecycle.GetCount(); i++)
		{
			SList<SItemPanel *> *pLstTypeItems = m_itemRecycle[i];
			SPOSITION pos = pLstTypeItems->GetHeadPosition();
			while (pos)
			{
				SItemPanel *pItem = pLstTypeItems->GetNext(pos);
				pItem->Release();
			}
			delete pLstTypeItems;
		}
		m_itemRecycle.RemoveAll();
		__super::OnDestroy();
	}

	//////////////////////////////////////////////////////////////////////////
	void STileViewEx::OnItemRequestRelayout(SItemPanel *pItem)
	{
		//pItem->UpdateChildrenPosition();
	}

	BOOL STileViewEx::IsItemRedrawDelay()
	{
		return TRUE;
	}

	CRect STileViewEx::CalcItemDrawRect(int iItem)
	{
		//相对整个窗体的实际绘制位置
		int nOffset = m_tvItemLocator->Item2Position(iItem) - m_siVer.nPos;

		CRect rcClient = GetClientRect();
		//获取left/right
		CRect rcItem = m_tvItemLocator->GetItemRect(iItem);
		rcItem.OffsetRect(rcClient.TopLeft());
		//修正top/bottom
		rcItem.MoveToY(rcClient.top + m_tvItemLocator->GetMarginSize() + nOffset);
		return rcItem;
	}

	BOOL STileViewEx::OnItemGetRect(SItemPanel *pItem, CRect &rcItem)
	{
		int iPosition = (int)pItem->GetItemIndex();
		rcItem = CalcItemDrawRect(iPosition);
		return TRUE;
	}

	void STileViewEx::OnItemSetCapture(SItemPanel *pItem, BOOL bCapture)
	{
		if (bCapture)
		{
			GetContainer()->OnSetSwndCapture(m_swnd);
			m_itemCapture = pItem;
		}
		else
		{
			GetContainer()->OnReleaseSwndCapture();
			m_itemCapture = NULL;
		}
	}

	void STileViewEx::RedrawItem(SItemPanel *pItem)
	{
		pItem->InvalidateRect(NULL);
	}

	void STileViewEx::UpDataSel(int iOldSel, int iNewSel)
	{
		SPOSITION pos = m_lSelItems.Find(iOldSel);
		if (pos != NULL)
			*(int*)pos = iNewSel;
		else m_lSelItems.AddHead(iNewSel);
	}

	void STileViewEx::AddSel(int iItem)
	{
		if (m_lSelItems.Find(iItem) == NULL)
			m_lSelItems.AddTail(iItem);
	}

	void STileViewEx::RemoveSel(int iItem)
	{		
		SPOSITION pos = m_lSelItems.Find(iItem);
		if (pos)
			m_lSelItems.RemoveAt(pos);
	}

	void STileViewEx::DelBiggerThan(int iItem)
	{
		SPOSITION pos = m_lSelItems.GetHeadPosition();
		SPOSITION lastpos = NULL;
		while (pos)
		{
			lastpos = pos;
			if (m_lSelItems.GetNext(pos) > iItem)
				m_lSelItems.RemoveAt(lastpos);
		}
	}

	SItemPanel *STileViewEx::HitTest(CPoint &pt)
	{
		SPOSITION pos = m_lstItems.GetHeadPosition();
		while (pos)
		{
			ItemInfo ii = m_lstItems.GetNext(pos);
			CRect rcItem = ii.pItem->GetItemRect();
			if (rcItem.PtInRect(pt))
			{
				pt -= rcItem.TopLeft();
				return ii.pItem;
			}
		}
		return NULL;
	}

	LRESULT STileViewEx::OnMouseEvent(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		SetMsgHandled(FALSE);
		if (!m_adapter)
		{
			return 0;
		}
		LRESULT lRet = 0;
		CPoint pt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));

		SItemPanel * pHover = HitTest(pt);
		CPoint itempt = pt;
		if (pHover)
		{
			CRect rcItem = pHover->GetItemRect();
			//恢复PT
			pt += rcItem.TopLeft();
		}
		if ((uMsg == WM_MOUSEMOVE) && (wParam == MK_LBUTTON))
		{
			//已经开始选中拖动操作
			if (m_bDrag)
			{
				UpdataDragSel();
				m_bOutBottom = m_bOutTop = m_bOutLeft = m_bOutRight = FALSE;
				CRect updataRc;
				MakeRect(updataRc, m_dragStartPos, m_dragEndPos);
				updataRc.InflateRect(1, 1, 1, 1);
				InvalidateRect(updataRc);
				m_dragEndPos = pt;
				MakeRect(updataRc, m_dragStartPos, m_dragEndPos);
				updataRc.InflateRect(1, 1, 1, 1);
				InvalidateRect(updataRc);
				updataRc = GetClientRect();
				if (false)//pHover
				{
					//如果是鼠标没有超出边界
					//m_adapter->UpataSel(pHover->GetItemIndex());
				}
				else
				{
					pt.x = m_dragStartPos.x;
					pHover = HitTest(pt);
					if (pHover)
					{
						//m_adapter->UpataSel(pHover->GetItemIndex());
					}

					//超出左边
					if (m_dragEndPos.x < updataRc.left)
					{
						m_bOutLeft = TRUE;
					}
					//超出右边
					else if (m_dragEndPos.x > updataRc.right)
					{
						m_bOutRight = TRUE;
					}
					//上
					if (m_dragEndPos.y < updataRc.top)
					{
						m_bOutTop = TRUE;
					}
					//下
					else if (m_dragEndPos.y > updataRc.bottom)
					{
						m_bOutBottom = TRUE;
					}

				}
				if (!m_bDraging)
				{
					SetTimer2(SCROLL_TIMER_ID, 30);
					m_bDraging = true;
				}
			}
			else if (m_bBeginDropItem)
			{
				m_bDropIteming = TRUE;
			}
		}
		if ((uMsg == WM_LBUTTONDOWN) && ((wParam == 4) || (wParam == 5)))
		{
			if (pHover)
			{
				if (GetSel() != -1)
				{
					int idxStart = min(pHover->GetItemIndex(), GetSel());
					int idxEnd = max(pHover->GetItemIndex(), GetSel())+1;
					m_lSelItems.RemoveAll();
					for(int i= idxStart;i<idxEnd; i++)
						m_lSelItems.AddTail(i);

					SPOSITION pos = m_lstItems.GetHeadPosition();
					SItemPanel *pItem;
					--idxEnd;
					while (pos)
					{
						pItem = m_lstItems.GetNext(pos).pItem;
						int idx=pItem->GetItemIndex();
						if (pItem && (pItem->GetState()&WndState_Check)&&((idx<idxStart)||(idx>idxEnd)))
						{							
							pItem->ModifyItemState(0, WndState_Check);
							RedrawItem(pItem);
						}
						else if(pItem && (!(pItem->GetState()&WndState_Check)) && ((idx>=idxStart)&&(idx<=idxEnd)))
						{
							pItem->ModifyItemState(WndState_Check,0);
							RedrawItem(pItem);
						}
					}

				}
			}
		}
		else if ((uMsg == WM_LBUTTONDOWN) && ((wParam == 9) || (wParam == 8)))
		{
			if (pHover)
			{
				if (IsSel(pHover->GetItemIndex()))
				{
					RemoveSel(pHover->GetItemIndex());
					pHover->ModifyState(0, WndState_Check, TRUE);
				}					
				else 
				{
					AddSel(pHover->GetItemIndex());
					pHover->ModifyState(WndState_Check, 0, TRUE);					
				}
			}
		}
		else if (uMsg == WM_LBUTTONDOWN && wParam == MK_LBUTTON)
		{
			m_nOffset = pt.y - GetClientRect().top + m_siVer.nPos;
			m_dragEndPos = m_dragStartPos = pt;
			if (pHover&&IsSel(pHover->GetItemIndex()))
			{
				m_bBeginDropItem = TRUE;
			}
			if (pHover && !m_bBeginDropItem)
			{
				m_bDrag = TRUE;
			}			
		}
		if (pHover != m_pHoverItem)
		{
			SItemPanel * nOldHover = m_pHoverItem;
			m_pHoverItem = pHover;
			if (nOldHover)
			{
				nOldHover->DoFrameEvent(WM_MOUSELEAVE, 0, 0);
				RedrawItem(nOldHover);
			}
			if (m_pHoverItem)
			{
				m_pHoverItem->DoFrameEvent(WM_MOUSEHOVER, wParam, MAKELPARAM(itempt.x, itempt.y));
				RedrawItem(m_pHoverItem);
			}
		}
		if (m_pHoverItem)
		{
			if(!m_bDraging&&!m_bBeginDropItem)
				m_pHoverItem->DoFrameEvent(uMsg, wParam, MAKELPARAM(itempt.x, itempt.y));
			else if (m_bBeginDropItem&&!m_bDropIteming)
				m_pHoverItem->DoFrameEvent(uMsg, wParam, MAKELPARAM(itempt.x, itempt.y));
		}
		else if (uMsg == WM_LBUTTONDOWN || uMsg == WM_RBUTTONDOWN || uMsg == WM_MBUTTONDOWN)
		{
			// 点击空白区域取消选中
			if (!m_pHoverItem)
			{
				m_bDrag = TRUE;
				SetSel(-1, TRUE);
			}
		}
		__super::ProcessSwndMessage(uMsg, wParam, lParam, lRet);
		if (uMsg == WM_LBUTTONUP || uMsg == WM_RBUTTONUP || uMsg == WM_MBUTTONUP)
		{
			if (uMsg == WM_LBUTTONUP)
			{
				if (m_bDraging)
				{
					EndUpdataDragSel();
					m_bDrag = FALSE;
					KillTimer2(SCROLL_TIMER_ID);
					CRect updataRc;
					MakeRect(updataRc, m_dragStartPos, m_dragEndPos);
					updataRc.InflateRect(1, 1, 1, 1);
					InvalidateRect(updataRc);
				}				
				m_bDraging = FALSE;
				m_bBeginDropItem = FALSE;
				m_bDropIteming = FALSE;
				//OnItemSetCapture(NULL, FALSE);
			}
			//交给panel处理
			__super::ProcessSwndMessage(uMsg, wParam, lParam, lRet);
		}
		SetMsgHandled(TRUE);
		return 0;
	}

	LRESULT STileViewEx::OnKeyEvent(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		LRESULT lRet = 0;
		SItemPanel *pItem = GetItemPanel(GetSel());
		if (pItem)
		{
			lRet = pItem->DoFrameEvent(uMsg, wParam, lParam);
			SetMsgHandled(pItem->IsMsgHandled());
		}
		else
		{
			SetMsgHandled(FALSE);
		}
		return lRet;
	}

	void STileViewEx::OnMouseLeave()
	{
		__super::OnMouseLeave();

		if (m_pHoverItem)
		{
			m_pHoverItem->DoFrameEvent(WM_MOUSELEAVE, 0, 0);
			m_pHoverItem = NULL;
		}
	}

	void STileViewEx::OnKeyDown(TCHAR nChar, UINT nRepCnt, UINT nFlags)
	{
		if (!m_adapter)
		{
			SetMsgHandled(FALSE);
			return;
		}

		int iSelItem = GetSel();

		if (iSelItem != -1 && m_bWantTab)
		{
			SItemPanel *pItem = GetItemPanel(iSelItem);
			if (pItem)
			{
				pItem->DoFrameEvent(WM_KEYDOWN, nChar, MAKELONG(nFlags, nRepCnt));
				if (pItem->IsMsgHandled())
				{
					return;
				}
			}
		}

		int  nNewSelItem = -1;
		SWindow *pOwner = GetOwner();
		if (pOwner && (nChar == VK_ESCAPE || nChar == VK_RETURN))
		{
			pOwner->SSendMessage(WM_KEYDOWN, nChar, MAKELONG(nFlags, nRepCnt));
			return;
		}

		if (nChar == VK_RIGHT && iSelItem < m_adapter->getCount() - 1)
		{
			nNewSelItem = iSelItem + 1;
		}
		else if (nChar == VK_LEFT && iSelItem > 0)
		{
			nNewSelItem = iSelItem - 1;
		}
		else if (nChar == VK_UP && iSelItem > 0)
		{
			nNewSelItem = m_tvItemLocator->GetUpItem(iSelItem);
		}
		else if (nChar == VK_DOWN && iSelItem < m_adapter->getCount() - 1)
		{
			nNewSelItem = m_tvItemLocator->GetDownItem(iSelItem);
		}
		else
		{
			switch (nChar)
			{
			case VK_PRIOR:
				OnScroll(TRUE, SB_PAGEUP, 0);
				break;
			case VK_NEXT:
				OnScroll(TRUE, SB_PAGEDOWN, 0);
				break;
			case VK_HOME:
				OnScroll(TRUE, SB_TOP, 0);
				break;
			case VK_END:
				OnScroll(TRUE, SB_BOTTOM, 0);
				break;
			}
			if (nChar == VK_PRIOR || nChar == VK_HOME)
			{
				if (!m_lstItems.IsEmpty())
				{
					nNewSelItem = (int)(m_lstItems.GetHead().pItem->GetItemIndex());
				}
			}
			else if (nChar == VK_NEXT || nChar == VK_END)
			{
				if (!m_lstItems.IsEmpty())
				{
					nNewSelItem = (int)(m_lstItems.GetTail().pItem->GetItemIndex());
				}
			}
		}

		if (nNewSelItem != -1)
		{
			EnsureVisible(nNewSelItem);
			SetSel(nNewSelItem);
		}
	}

	void STileViewEx::EnsureVisible(int iItem)
	{
		if (iItem < 0 || iItem >= m_adapter->getCount())
		{
			return;
		}

		CRect rcItem = m_tvItemLocator->GetItemRect(iItem);
		if (rcItem.top < m_siVer.nPos)
		{
			// scroll up
			OnScroll(TRUE, SB_THUMBPOSITION, rcItem.top);
		}
		if (rcItem.bottom > m_siVer.nPos + (int)m_siVer.nPage)
		{
			// scroll down
			OnScroll(TRUE, SB_THUMBPOSITION, rcItem.bottom - m_siVer.nPage);
		}
	}

	BOOL STileViewEx::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
	{
		SItemPanel *pSelItem = GetItemPanel(GetSel());
		if (pSelItem)
		{
			CRect rcItem = pSelItem->GetItemRect();
			CPoint pt2 = pt - rcItem.TopLeft();
			if (pSelItem->DoFrameEvent(WM_MOUSEWHEEL, MAKEWPARAM(nFlags, zDelta), MAKELPARAM(pt2.x, pt2.y)))
			{
				return TRUE;
			}
		}
		return __super::OnMouseWheel(nFlags, zDelta, pt);
	}

	int STileViewEx::GetScrollLineSize(BOOL bVertical)
	{
		return m_tvItemLocator->GetScrollLineSize();
	}

	SItemPanel *STileViewEx::GetItemPanel(int iItem)
	{
		if (iItem < 0 || iItem >= m_adapter->getCount())
		{
			return NULL;
		}
		SPOSITION pos = m_lstItems.GetHeadPosition();
		while (pos)
		{
			ItemInfo ii = m_lstItems.GetNext(pos);
			if ((int)ii.pItem->GetItemIndex() == iItem)
			{
				return ii.pItem;
			}
		}
		return NULL;
	}

	BOOL STileViewEx::CreateChildren(pugi::xml_node xmlNode)
	{
		pugi::xml_node xmlTemplate = xmlNode.child(L"template");
		if (xmlTemplate)
		{
			m_xmlTemplate.append_copy(xmlTemplate);
			//int nItemHei = xmlTemplate.attribute(L"itemHeight").as_int(-1);
			//int nItemWid = xmlTemplate.attribute(L"itemWidth").as_int(-1);
			//if(nItemHei > 0 && nItemWid > 0)
			{
				//创建一个定位器
				//STileViewItemLocator *pItemLocator = new  STileViewItemLocator(nItemHei, nItemWid, m_nMarginSize);
				STileViewItemLocator *pItemLocator = new STileViewItemLocator(
					xmlTemplate.attribute(L"itemHeight").as_string(L"10dp"),
					xmlTemplate.attribute(L"itemWidth").as_string(L"10dp"),
					m_nMarginSize);
				SetItemLocator(pItemLocator);
				pItemLocator->Release();
			}
		}
		return TRUE;
	}

	void STileViewEx::SetItemLocator(STileViewItemLocator *pItemLocator)
	{
		m_tvItemLocator = pItemLocator;
		if (m_tvItemLocator)
		{
			m_tvItemLocator->SetAdapter(GetAdapter());
		}
		onDataSetChanged();
	}

	BOOL STileViewEx::OnUpdateToolTip(CPoint pt, SwndToolTipInfo &tipInfo)
	{
		if (!m_pHoverItem)
		{
			return __super::OnUpdateToolTip(pt, tipInfo);
		}
		return m_pHoverItem->OnUpdateToolTip(pt, tipInfo);
	}

	void STileViewEx::SetSel(int iItem, BOOL bNotify)
	{
		if (!m_adapter)
		{
			return;
		}

		if (iItem >= m_adapter->getCount())
		{
			return;
		}

		if (iItem < 0)
		{
			iItem = -1;
		}
		int nOldSel =GetSel();
		int nNewSel = iItem;
		if (bNotify)
		{
			EventLVSelChanging evt(this);
			evt.iOldSel = nOldSel;
			evt.iNewSel = nNewSel;
			FireEvent(evt);
			if (evt.bCancel)
			{
				return;
			}
		}

		if (nOldSel == nNewSel)
		{
			return;
		}

		SPOSITION pos = m_lSelItems.GetHeadPosition();
		SItemPanel *pItem;
		while (pos)
		{
			pItem = GetItemPanel(m_lSelItems.GetNext(pos));
			if (pItem && (pItem->GetState()&WndState_Check))
			{
				pItem->GetFocusManager()->SetFocusedHwnd((SWND)-1);
				pItem->ModifyItemState(0, WndState_Check);
				RedrawItem(pItem);
			}
		}

		m_lSelItems.RemoveAll();
		m_lSelItems.AddTail(nNewSel);
		//UpDataSel(nOldSel,nNewSel);
		pItem = GetItemPanel(nNewSel);
		if (pItem)
		{
			pItem->ModifyItemState(WndState_Check, 0);
			RedrawItem(pItem);
		}

		if (bNotify)
		{
			EventLVSelChanged evt(this);
			evt.iOldSel = nOldSel;
			evt.iNewSel = nNewSel;
			FireEvent(evt);
		}

	}

	UINT STileViewEx::OnGetDlgCode()
	{
		if (m_bWantTab)
		{
			return SC_WANTALLKEYS;
		}
		else
		{
			return SC_WANTARROWS | SC_WANTSYSKEY;
		}
	}

	void STileViewEx::OnKillFocus(SWND wndFocus)
	{
		__super::OnKillFocus(wndFocus);


		if (m_bDrag)
		{
			m_bDrag = false;
			CRect updataRc;
			MakeRect(updataRc, m_dragStartPos, m_dragEndPos);
			InvalidateRect(updataRc);
			//m_adapter->SelectionEnd();
		}
		KillTimer2(SCROLL_TIMER_ID);
		int iSelItem = GetSel();
		if (iSelItem == -1)
		{
			return;
		}

		SItemPanel *pSelPanel = GetItemPanel(iSelItem);
		if (pSelPanel)
		{
			pSelPanel->GetFocusManager()->StoreFocusedView();
		}
	}

	void STileViewEx::OnSetFocus(SWND wndOld)
	{
		__super::OnSetFocus(wndOld);
		if (GetSel() == -1)
		{
			return;
		}
		SItemPanel *pSelPanel = GetItemPanel(GetSel());
		if (pSelPanel)
		{
			pSelPanel->GetFocusManager()->RestoreFocusedView();
		}
	}

	LRESULT STileViewEx::OnSetScale(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		int nScale = (int)wParam;
		m_tvItemLocator->SetScale(nScale);
		__super::OnSetScale(uMsg, wParam, lParam);
		return LRESULT();
	}

	void STileViewEx::OnTimer2(char cTimerID)
	{
		switch (cTimerID)
		{
		case SCROLL_TIMER_ID:
		{
			if (m_bOutBottom)
			{
				OnScroll(TRUE, SB_LINEDOWN, 0);
			}
			if (m_bOutTop)
			{
				OnScroll(TRUE, SB_LINEUP, 0);
			}
			//else
			//	OnScroll(TRUE,SB_THUMBPOSITION,m_siVer.nPos);
		}break;
		default:
			SetMsgHandled(FALSE);
			break;
		}
	}

	BOOL STileViewEx::IsSel(int id)
	{
		return m_lSelItems.Find(id) != NULL;
	}

	BOOL STileViewEx::OnSetCursor(const CPoint &pt)
	{
		BOOL bRet = FALSE;
		if (m_itemCapture)
		{
			CRect rcItem = m_itemCapture->GetItemRect();
			bRet = m_itemCapture->DoFrameEvent(WM_SETCURSOR, 0, MAKELPARAM(pt.x - rcItem.left, pt.y - rcItem.top)) != 0;
		}
		else if (m_pHoverItem)
		{
			CRect rcItem = m_pHoverItem->GetItemRect();
			bRet = m_pHoverItem->DoFrameEvent(WM_SETCURSOR, 0, MAKELPARAM(pt.x - rcItem.left, pt.y - rcItem.top)) != 0;
		}
		if (!bRet)
		{
			bRet = __super::OnSetCursor(pt);
		}
		return bRet;

	}

	bool STileViewEx::OnItemClick(EventArgs *pEvt)
	{
		SItemPanel *pItemPanel = sobj_cast<SItemPanel>(pEvt->sender);
		SASSERT(pItemPanel);
		int iItem = (int)pItemPanel->GetItemIndex();
		SetSel(iItem, TRUE);
		return true;
	}

	void STileViewEx::UpdataDragSel()
	{
		int iFirst = m_iFirstVisible;
		if (iFirst != -1)
		{
			CRect updataRc;
			CRect rcClient, rcItem, rcInter;
			GetClientRect(&rcClient);

			MakeRect(updataRc, m_dragStartPos, m_dragEndPos);
			//只更新可见部分加快速度
			m_lSelItems.RemoveAll();
			SPOSITION pos = m_lstItems.GetHeadPosition();

			int nOffset = m_tvItemLocator->Item2Position(iFirst) - m_siVer.nPos;
			int nLastBottom = rcClient.top + m_tvItemLocator->GetMarginSize() + nOffset;

			int i = 0;
			for (; pos; i++)
			{
				ItemInfo ii = m_lstItems.GetNext(pos);
				rcItem = m_tvItemLocator->GetItemRect(iFirst + i);
				rcItem.OffsetRect(rcClient.left, 0);
				rcItem.MoveToY(nLastBottom);
				if (m_tvItemLocator->IsLastInRow(iFirst + i))
				{
					nLastBottom = rcItem.bottom + m_tvItemLocator->GetMarginSize();
				}
				rcInter.IntersectRect(&updataRc, &rcItem);
				if (!rcInter.IsRectEmpty())
				{

					m_lSelItems.AddTail(ii.pItem->GetItemIndex());
					if (ii.pItem)
					{
						ii.pItem->ModifyItemState(WndState_Check, 0);
						RedrawItem(ii.pItem);
					}
				}
				else {

					ii.pItem->ModifyItemState(0, WndState_Check);
					RedrawItem(ii.pItem);
				}
			}
		}
	}

	void STileViewEx::EndUpdataDragSel()
	{
		m_lSelItems.RemoveAll();
		int iFirst = m_iFirstVisible;
		if (iFirst != -1)
		{
			int iFirstSelItem = -1;
			int iEndSelItem = -1;

			CRect updataRc;
			CRect rcClient, rcItem, rcInter;
			GetClientRect(&rcClient);//lineWid
			int endY = m_dragEndPos.y - rcClient.top + m_siVer.nPos;

			CPoint startPos ( min(m_dragStartPos.x, m_dragEndPos.x) ,min(m_nOffset,endY) );
			CPoint endPos ( max(m_dragStartPos.x, m_dragEndPos.x),max(m_nOffset, endY) );

			startPos.x -= rcClient.left;
			endPos.x -= rcClient.left;

			int rowStart = 0;
			int rowEnd = 0;
			int nCountInRow = m_tvItemLocator->GetCountInRow();
			int lineHei = m_tvItemLocator->GetItemLineHeight();
			int itemHei = m_tvItemLocator->GetItemHeight(0);

			rowStart = startPos.y / lineHei;
			if ((startPos.y % lineHei) > itemHei)
				++rowStart;

			rowEnd = endPos.y / lineHei;
			if ((endPos.y % lineHei) > m_tvItemLocator->GetMarginSize())
				++rowEnd;

			int colStart;
			int colEnd;
			int lineWid = m_tvItemLocator->GetItemWidth();
			colStart = startPos.x / lineWid;
			if ((startPos.x / lineWid) > lineWid)
				++colStart;
			colEnd = endPos.x / lineWid;
			if ((endPos.x % lineWid) > m_tvItemLocator->GetMarginSize())
				++colEnd;
			colEnd = min(colEnd, nCountInRow);
			colStart = min(colStart, nCountInRow);
			for (int i = rowStart; i < rowEnd; i++)
			{
				for (int j = colStart; j < colEnd; j++)
				{
					AddSel(i*nCountInRow + j);
				}
			}
		}
	}

	void STileViewEx::OnColorize(COLORREF cr)
	{
		__super::OnColorize(cr);
		DispatchMessage2Items(UM_SETCOLORIZE, cr, 0);
	}

	void STileViewEx::OnScaleChanged(int nScale)
	{
		__super::OnScaleChanged(nScale);
		DispatchMessage2Items(UM_SETSCALE, nScale, 0);
	}

	HRESULT STileViewEx::OnLanguageChanged()
	{
		HRESULT hret = __super::OnLanguageChanged();
		DispatchMessage2Items(UM_SETLANGUAGE, 0, 0);
		return hret;
	}

	void STileViewEx::DispatchMessage2Items(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		SPOSITION pos = m_lstItems.GetHeadPosition();
		while (pos)
		{
			ItemInfo ii = m_lstItems.GetNext(pos);
			ii.pItem->SDispatchMessage(uMsg, wParam, lParam);
		}
		for (UINT i = 0; i < m_itemRecycle.GetCount(); i++)
		{
			SList<SItemPanel*> *pLstTypeItems = m_itemRecycle[i];
			SPOSITION pos = pLstTypeItems->GetHeadPosition();
			while (pos)
			{
				SItemPanel *pItem = pLstTypeItems->GetNext(pos);
				pItem->SDispatchMessage(uMsg, wParam, lParam);
			}
		}
	}

}
