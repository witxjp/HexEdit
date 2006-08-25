// DataFormatView.cpp : implements tree view of data
//
// Copyright (c) 2004 by Andrew W. Phillips.
//
// No restrictions are placed on the noncommercial use of this code,
// as long as this text (from the above copyright notice to the
// disclaimer below) is preserved.
//
// This code may be redistributed as long as it remains unmodified
// and is not sold for profit without the author's written consent.
//
// This code, or any part of it, may not be used in any software that
// is sold for profit, without the author's written consent.
//
// DISCLAIMER: This file is provided "as is" with no expressed or
// implied warranty. The author accepts no liability for any damage
// or loss of business that this product may cause.
//

#include "stdafx.h"
#include "HexEdit.h"
#include "MainFrm.h"
#include "DataFormatView.h"
#include "Misc.h"
#include "Dialog.h"
#include "NewCellTypes/GridCellCombo.h"     // For CGridCellCombo
#include "NewCellTypes/GridCellDateTime.h"  // For CGridCellDateTime

#include "DFFDGlobal.h"
#include "DFFDStruct.h"
#include "DFFDUseStruct.h"
#include "DFFDFor.h"
#include "DFFDIf.h"
#include "DFFDSwitch.h"
#include "DFFDJump.h"
#include "DFFDData.h"
#include "DFFDEval.h"
#include "DFFDMisc.h"
#include "TParser.h"
#include "TParseDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern CHexEditApp theApp;

//-----------------------------------------------------------------------------
// We have to derive our grid class from CGridCtrl so that we can override 
// Mouse click handling in order to have a useable combo box in the grid header
// row and also to handle drag and drop of tree nodes.
IMPLEMENT_DYNCREATE(CGridCtrl2, CGridCtrl)

BEGIN_MESSAGE_MAP(CGridCtrl2, CGridCtrl)
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_CBN_SELCHANGE(IDC_INPLACE_CONTROL, OnSelchange)
END_MESSAGE_MAP()

#ifndef GRIDCONTROL_NO_DRAGDROP

// Start drag n drop
void CGridCtrl2::OnBeginDrag()
{
    if (!m_bAllowDragAndDrop)
        return;

    CCellRange sel = GetSelectedCellRange();
    int row = sel.GetMinRow();
    int col = sel.GetMinCol();
    int frc = GetFixedRowCount();

    // Check that we are dragging a part of the tree
    if (row >= frc)
    {
        ASSERT(pdoc_ != NULL);

        // Make sure only one row of the tree is selected
        if (sel.GetMinRow() != sel.GetMaxRow())
            return;

        // Disallow drag for invalid types
        if (pdoc_->df_type_[row-frc] == CHexEditDoc::DF_FILE ||
            pdoc_->df_type_[row-frc] == CHexEditDoc::DF_MORE ||
//            pdoc_->df_type_[row-frc] == CHexEditDoc::DF_DEFINE_STRUCT ||
            pdoc_->df_type_[row-frc] == CHexEditDoc::DF_EXTRA)
        {
            return;
        }

        SendMessageToParent(sel.GetTopLeft().row, sel.GetTopLeft().col,
            GVN_BEGINDRAG);

        // This is necessary to avoid confusing the base class
        m_MouseMode = MOUSE_DRAGGING;
        m_bLMouseButtonDown = FALSE;

        // Get global memory for data passed to drop target and store the elt ptr to the drag source in the memory
        HANDLE hdata = ::GlobalAlloc(GMEM_MOVEABLE, sizeof(MSXML::IXMLDOMElementPtr) + sizeof(CDataFormatView *));
        char *pp = (char *)::GlobalLock(hdata);

        // xxx This should be fixed to use placement new and placement delete (so there are no COM ptr leaks)
#if 0 // Placement new does not seem to be supported
        ::new (pp) MSXML::IXMLDOMElementPtr(pdoc_->df_elt_[row-frc].m_pelt);
#else
        memset(pp, '\0', sizeof(MSXML::IXMLDOMElementPtr));
        *(MSXML::IXMLDOMElementPtr*)pp = pdoc_->df_elt_[row-frc].m_pelt;
#endif
        memcpy(pp+sizeof(MSXML::IXMLDOMElementPtr), &pview_, sizeof(CDataFormatView *));
        ::GlobalUnlock(hdata);

        COleDataSource ods;
        ods.CacheGlobalData(m_cf_ours, hdata);

        if (pdoc_->df_type_[row-frc] == CHexEditDoc::DF_DEFINE_STRUCT)
        {
            // If dragging a "define_struct" just allow it to be copied
            ods.DoDragDrop(DROPEFFECT_COPY);
        }
        else
        {
            DWORD allowed_de = 0;

            // We can only move if the parent is a STRUCT/FILE/UNION and there is another sibling, since
            // deleting sub-elements of an IF/FORV/FORF or the last of STRUCT/FILE makes the XML invalid.

            int curr_indent = pdoc_->df_indent_[row-frc];
            int ii;                         // The parent node

            // Find the parent
            for (ii = row-frc-1; ii > -1; ii--)
            {
                if (pdoc_->df_indent_[ii] < curr_indent)
                {
                    // Parent node found
                    ASSERT(pdoc_->df_indent_[ii] == curr_indent-1);
                    break;
                }
            }
            ASSERT(ii > -1);   // We must have started at root node which should be DF_FILE (checked for above)

            // Now count the parent's children
            int children = 0;
            int jj;                         // Finds child nodes of the parent
            for (jj = ii + 1; jj < (int)pdoc_->df_indent_.size(); ++jj)
            {
                if (pdoc_->df_indent_[jj] == curr_indent)
                    ++children;
                else if (pdoc_->df_indent_[jj] < curr_indent)
                    break;
            }
            ASSERT(children >= 1);          // We must at least find the original node again

            if (children > 1 &&
                (pdoc_->df_type_[ii] == CHexEditDoc::DF_FILE || 
                pdoc_->df_type_[ii] == CHexEditDoc::DF_STRUCT || 
                pdoc_->df_type_[ii] == CHexEditDoc::DF_DEFINE_STRUCT || 
                pdoc_->df_type_[ii] == CHexEditDoc::DF_UNION))
            {
                allowed_de = DROPEFFECT_MOVE|DROPEFFECT_COPY;
            }
            else
            {
                // Must have STRUCT with one child or a single child element (IF, FOR)
                ASSERT(children == 1 ||
                    pdoc_->df_type_[ii] == CHexEditDoc::DF_USE_STRUCT || 
                    pdoc_->df_type_[ii] == CHexEditDoc::DF_IF || 
                    pdoc_->df_type_[ii] == CHexEditDoc::DF_SWITCH || 
                    pdoc_->df_type_[ii] == CHexEditDoc::DF_JUMP || 
                    pdoc_->df_type_[ii] == CHexEditDoc::DF_FORV || 
                    pdoc_->df_type_[ii] == CHexEditDoc::DF_FORF);
                allowed_de = DROPEFFECT_COPY;                      // allow copy only - removing original results in invalid format
            }
            ASSERT(allowed_de != 0);

            if (ods.DoDragDrop(allowed_de) == DROPEFFECT_MOVE)
            {
                // Drag and drop completed now and move requested so remove source
                ASSERT(children > 1 &&
                    (pdoc_->df_type_[ii] == CHexEditDoc::DF_FILE ||
                        pdoc_->df_type_[ii] == CHexEditDoc::DF_STRUCT || 
                        pdoc_->df_type_[ii] == CHexEditDoc::DF_DEFINE_STRUCT || 
                        pdoc_->df_type_[ii] == CHexEditDoc::DF_UNION));

				CSaveStateHint ssh;
				pdoc_->UpdateAllViews(NULL, 0, &ssh);
                pdoc_->df_elt_[ii].DeleteChild(pdoc_->df_elt_[row-frc]);  // delete the moved node
                pdoc_->ScanFile();                              // reload doc's internal data structures
                CDFFDHint dffdh;
                pdoc_->UpdateAllViews(NULL, 0, &dffdh);         // redisplay all tree views
				CRestoreStateHint rsh;
				pdoc_->UpdateAllViews(NULL, 0, &rsh);
            }
        }
    }
    else
        CGridCtrl::OnBeginDrag();
}

// Handle drag over grid
DROPEFFECT CGridCtrl2::OnDragOver(COleDataObject* pDataObject, DWORD dwKeyState,
                                 CPoint point)
{
    if (!m_bAllowDragAndDrop || !IsEditable())
        return DROPEFFECT_NONE;

    // Find which cell we are over and drop-highlight it
    CCellID cell = GetCellFromPt(point, FALSE);
    int frc = GetFixedRowCount();
    int fcc = GetFixedColumnCount();

    // Check if we are in the tree column
    if (cell.row >= frc && cell.col == fcc)
    {
        ASSERT(pdoc_ != NULL);

#if 0  // Drop hilighting does not restore the background colour properly
        // Remove drop highlight for last cell
        if (cell != m_LastDragOverCell && IsValid(m_LastDragOverCell))
        {
            UINT nState = GetItemState(m_LastDragOverCell.row, m_LastDragOverCell.col);
            SetItemState(m_LastDragOverCell.row, m_LastDragOverCell.col,
                         nState & ~GVIS_DROPHILITED);
            RedrawCell(m_LastDragOverCell);
        }
#endif

        // If not a valid drop target then disable
        if ((pdoc_->df_type_[cell.row-frc] != CHexEditDoc::DF_STRUCT && 
             pdoc_->df_type_[cell.row-frc] != CHexEditDoc::DF_DEFINE_STRUCT && 
             pdoc_->df_type_[cell.row-frc] != CHexEditDoc::DF_FILE && 
             pdoc_->df_type_[cell.row-frc] != CHexEditDoc::DF_UNION ) ||
            !pDataObject->IsDataAvailable(m_cf_ours))
        {
            m_LastDragOverCell = CCellID(-1,-1);
            return DROPEFFECT_NONE;
        }

        HANDLE hdata = pDataObject->GetGlobalData(m_cf_ours);
        CDataFormatView *psrcview = NULL;
        CXmlTree::CElt src_elt;
        if (hdata != NULL)
        {
            char *pp = (char *)::GlobalLock(hdata);
            src_elt = CXmlTree::CElt(*(MSXML::IXMLDOMElementPtr*)pp, pdoc_->ptree_);
            psrcview = *(CDataFormatView **)(pp+sizeof(MSXML::IXMLDOMElementPtr));
            ::GlobalUnlock(hdata);
            ::GlobalFree(hdata);
        }
        if (pview_ == psrcview)
        {
            // Disallow drag of a cell onto itself
            if (src_elt.m_pelt == pdoc_->df_elt_[cell.row-frc].m_pelt)
            {
                m_LastDragOverCell = CCellID(-1,-1);
                return DROPEFFECT_NONE;
            }
        }
        if (pview_ != psrcview && src_elt.GetName() == "define_struct")
        {
            // Prevent "use_struct" of "define_struct" from another template file
            m_LastDragOverCell = CCellID(-1,-1);
            return DROPEFFECT_NONE;
        }
#if 0
        if (cell != m_LastDragOverCell && IsValid(cell))
        {
            // Set the new cell as drop-highlighted
            UINT nState = GetItemState(cell.row, cell.col);
            SetItemState(cell.row, cell.col,
                         nState | GVIS_DROPHILITED);
            RedrawCell(cell);
        }
#endif

        m_LastDragOverCell = cell;

        if (dwKeyState & MK_CONTROL)
            return DROPEFFECT_COPY;
        else
            return DROPEFFECT_MOVE;
    }
    else
        return CGridCtrl::OnDragOver(pDataObject, dwKeyState, point);
}

// Something has just been dragged onto the grid
DROPEFFECT CGridCtrl2::OnDragEnter(COleDataObject* pDataObject, DWORD dwKeyState,
                                  CPoint point)
{
    if (!m_bAllowDragAndDrop || !IsEditable())
        return DROPEFFECT_NONE;

    // Find which cell we are over and drop-highlight it
    CCellID cell = GetCellFromPt(point, FALSE);
    int frc = GetFixedRowCount();
    int fcc = GetFixedColumnCount();

    // Check if we are in the tree column
    if (cell.row >= frc && cell.col == fcc)
    {
        ASSERT(pdoc_ != NULL);

        // If not a valid drop target then disable
        if ((pdoc_->df_type_[cell.row-frc] != CHexEditDoc::DF_STRUCT && 
             pdoc_->df_type_[cell.row-frc] != CHexEditDoc::DF_DEFINE_STRUCT && 
             pdoc_->df_type_[cell.row-frc] != CHexEditDoc::DF_FILE && 
             pdoc_->df_type_[cell.row-frc] != CHexEditDoc::DF_UNION ) ||
            !pDataObject->IsDataAvailable(m_cf_ours))
        {
            m_LastDragOverCell = CCellID(-1,-1);
            return DROPEFFECT_NONE;
        }

        // Check if we are dragging a struct onto itself
        HANDLE hdata = pDataObject->GetGlobalData(m_cf_ours);
        CDataFormatView *psrcview = NULL;
        CXmlTree::CElt src_elt;
        if (hdata != NULL)
        {
            char *pp = (char *)::GlobalLock(hdata);
            src_elt = CXmlTree::CElt(*(MSXML::IXMLDOMElementPtr*)pp, pdoc_->ptree_);
            psrcview = *(CDataFormatView **)(pp+sizeof(MSXML::IXMLDOMElementPtr));
            ::GlobalUnlock(hdata);
            ::GlobalFree(hdata);
        }
        if (pview_ == psrcview)
        {
            if (src_elt.m_pelt == pdoc_->df_elt_[cell.row-frc].m_pelt)
            {
                m_LastDragOverCell = CCellID(-1,-1);
                return DROPEFFECT_NONE;
            }
        }
        if (pview_ != psrcview && src_elt.GetName() == "define_struct")
        {
            // Prevent "use_struct" from "define_struct" in another template file
            m_LastDragOverCell = CCellID(-1,-1);
            return DROPEFFECT_NONE;
        }

#if 0
        // Set the new cell as drop-highlighted
        UINT nState = GetItemState(cell.row, cell.col);
        SetItemState(cell.row, cell.col,
                     nState | GVIS_DROPHILITED);
        RedrawCell(cell);
#endif

        m_LastDragOverCell = cell;

        if (dwKeyState & MK_CONTROL)
            return DROPEFFECT_COPY;
        else
            return DROPEFFECT_MOVE;
    }
    else
        return CGridCtrl::OnDragEnter(pDataObject, dwKeyState, point);
}

// Something has just been dropped onto the grid
BOOL CGridCtrl2::OnDrop(COleDataObject* pDataObject, DROPEFFECT de, CPoint point)
{
    int frc = GetFixedRowCount();
    int fcc = GetFixedColumnCount();

    if (m_LastDragOverCell.row >= frc && m_LastDragOverCell.col == fcc)
    {
        ASSERT(pdoc_ != NULL);

        // If not a valid drop target then cancel drop
        if ((pdoc_->df_type_[m_LastDragOverCell.row-frc] != CHexEditDoc::DF_STRUCT && 
             pdoc_->df_type_[m_LastDragOverCell.row-frc] != CHexEditDoc::DF_DEFINE_STRUCT && 
             pdoc_->df_type_[m_LastDragOverCell.row-frc] != CHexEditDoc::DF_FILE && 
             pdoc_->df_type_[m_LastDragOverCell.row-frc] != CHexEditDoc::DF_UNION ) ||
            !pDataObject->IsDataAvailable(m_cf_ours))
        {
            return FALSE;
        }

        m_MouseMode = MOUSE_NOTHING;

#if 0
        UINT nState = GetItemState(m_LastDragOverCell.row, m_LastDragOverCell.col);
        SetItemState(m_LastDragOverCell.row, m_LastDragOverCell.col,
            nState | GVIS_DROPHILITED);
        RedrawCell(m_LastDragOverCell);
#endif

        HANDLE hdata = pDataObject->GetGlobalData(m_cf_ours);
        if (hdata != NULL)
        {
            char *pp = (char *)::GlobalLock(hdata);
            CXmlTree::CElt src_elt(*(MSXML::IXMLDOMElementPtr*)pp, pdoc_->ptree_);
            CDataFormatView *psrcview = *(CDataFormatView **)(pp+sizeof(MSXML::IXMLDOMElementPtr));
            ::GlobalUnlock(hdata);
            ::GlobalFree(hdata);

            CXmlTree::CElt new_elt;
            if (src_elt.GetName() == "define_struct")
            {
                // Dropping "define_struct" to create "use_struct"
                ASSERT(pview_ == psrcview && de == DROPEFFECT_COPY);  // Can only drag into same document

                // Create "use_struct" node and add it to the tree
				CSaveStateHint ssh;
				pdoc_->UpdateAllViews(NULL, 0, &ssh);
                new_elt = pdoc_->df_elt_[m_LastDragOverCell.row-frc].InsertNewChild("use_struct");
                CString struct_name = src_elt.GetAttr("type_name");
                new_elt.SetAttr("type_name", struct_name);              // Set type of struct
                new_elt.SetAttr("name", UniqueName(struct_name, pdoc_->df_elt_[m_LastDragOverCell.row-frc]));
                pdoc_->ScanFile();
                CDFFDHint dffdh;
                pdoc_->UpdateAllViews(NULL, 0, &dffdh);
				CRestoreStateHint rsh;
				pdoc_->UpdateAllViews(NULL, 0, &rsh);
            }
            else if (pview_ == psrcview && de == DROPEFFECT_MOVE)
            {
                // Don't update the views here as it will be done after the
                // moved node is deleted (in OnBeginDrag above)
                new_elt = pdoc_->df_elt_[m_LastDragOverCell.row-frc].InsertClone(src_elt, NULL);
            }
            else
            {
                // Now add the node into the XML tree
				CSaveStateHint ssh;
				pdoc_->UpdateAllViews(NULL, 0, &ssh);
                new_elt = pdoc_->df_elt_[m_LastDragOverCell.row-frc].InsertClone(src_elt, NULL);
                pdoc_->ScanFile();
                CDFFDHint dffdh;
                pdoc_->UpdateAllViews(NULL, 0, &dffdh);
				CRestoreStateHint rsh;
				pdoc_->UpdateAllViews(NULL, 0, &rsh);
            }
            ASSERT(new_elt.GetName() != "binary_file_format");

            return TRUE;
        }
        else
            return FALSE;
    }
    else
        return CGridCtrl::OnDrop(pDataObject, de, point);
}

// Create a unique child name by adding a number to a base name
CString CGridCtrl2::UniqueName(LPCTSTR base, const CXmlTree::CElt elt)
{
    // Keep creating and trying new names starting at "base" with "1" added
    for (int ii = 1; ; ++ii)
    {
        CString retval;
        retval.Format("%s%d", base, ii);
        // Check if this child name exists
        for (CXmlTree::CElt child = elt.GetFirstChild(); !child.IsEmpty(); ++child)
        {
            if (child.GetAttr("name") == retval)
                break;      // a child with the name was found
        }
        // If not found then return this name
        if (child.IsEmpty())
            return retval;
    }
}
#endif

/////////////////////////////////////////////////////////////////////////////
// CGridCtrl2 message handlers
void CGridCtrl2::OnLButtonDown(UINT nFlags, CPoint point)
{
    CCellID cell = GetCellFromPt(point);    // Work out which cell was clicked

    // If combo box cell (and not in column/row resize area) then allow selection from drop list
    if (cell.row == 0 && cell.col == GetFixedColumnCount() &&
        m_MouseMode != MOUSE_OVER_COL_DIVIDE &&
        m_MouseMode != MOUSE_OVER_ROW_DIVIDE)
    {
        // Keep track of current row selected so it can be set after a drop-list elt is selected
        CCellRange sel = GetSelectedCellRange();

        if (sel.IsValid())
            sel_row_ = sel.GetMinRow();
        else
            sel_row_ = -1; //GetFixedRowCount();

        // Set up variables so that the drop-list is created in a fixed row
        m_idCurrentCell = cell;
        m_MouseMode = MOUSE_PREPARE_EDIT;
        return;
    }
    sel_row_ = -2;  // flag that we are not doing template selection drop list

    CGridCtrl::OnLButtonDown(nFlags, point);
}

void CGridCtrl2::OnLButtonUp(UINT nFlags, CPoint point)
{
    // When columns resized allow for Column headings to be changed to fit
    int col_resize = -1;
    if (m_MouseMode == MOUSE_SIZING_COL)
        col_resize = m_LeftClickDownCell.col;

    CGridCtrl::OnLButtonUp(nFlags, point);

    if (col_resize != -1)
        FixHeading(col_resize);
}

void CGridCtrl2::FixHeading(int col)
{
    int col_id = GetItemData(0, col);
    int col_width = GetColumnWidth(col);

    switch (col_id)
    {
    case CDataFormatView::COL_HEX_ADDRESS:
        if (col_width < 55)
            SetItemText(0, col, "Hex");
        else if (col_width < 79)
            SetItemText(0, col, "Hex Addr");
        else
            SetItemText(0, col, "Hex Address");
        break;
    case CDataFormatView::COL_DEC_ADDRESS:
        if (col_width < 55)
            SetItemText(0, col, "Dec");
        else if (col_width < 87)
            SetItemText(0, col, "Dec Addr");
        else
            SetItemText(0, col, "Decimal Address");
        break;
    }
}

BOOL CGridCtrl2::PreTranslateMessage(MSG* pMsg)
{
    if (pMsg->message == WM_KEYDOWN &&
        IsValid(m_idCurrentCell) && 
        m_idCurrentCell.col == GetFixedColumnCount())
    {
        // Note: we had to do this here instead of OnKeyDown as some of these keys are accelerators
        // (eg '+' key) and invoke commands (eg inc byte) so we never get to see the WM_KEYDOWN message.
        switch (pMsg->wParam)
        {
        case VK_HOME:
            // Go to first sibling
            pview_->goto_sibling(m_idCurrentCell.row - GetFixedRowCount(), 0);
            return TRUE;
        case VK_END:
            // Go to last sibling
            pview_->goto_sibling(m_idCurrentCell.row - GetFixedRowCount(), INT_MAX);
            return TRUE;
        case VK_RIGHT:
            if (::GetKeyState(VK_CONTROL) < 0)
            {
                pview_->next_error(m_idCurrentCell.row - GetFixedRowCount());
                return TRUE;
            }
            else
            {
                // Expand sub-nodes one level if collapsed, else go to first if expanded
                int row = m_idCurrentCell.row - GetFixedRowCount();
                if (!pview_->has_children(row))
                    return CGridCtrl::PreTranslateMessage(pMsg); // We can't do anything here if we have no children

                if (!pview_->expand_one(row))
                {
                    // Already expanded so move to first child
                    SetSelectedRange(m_idCurrentCell.row + 1, GetFixedColumnCount(),
                                    m_idCurrentCell.row + 1, GetColumnCount()-1);
                    SetFocusCell(m_idCurrentCell.row + 1, GetFixedColumnCount());
                }
                return TRUE;
            }
        case VK_LEFT:
            if (::GetKeyState(VK_CONTROL) < 0)
            {
                pview_->prev_error(m_idCurrentCell.row - GetFixedRowCount());
                return TRUE;
            }
            else
            {
                // Collapse sub-nodes if expanded, else go to parent node
                int row = m_idCurrentCell.row - GetFixedRowCount();

                if ((!pview_->has_children(row) || !pview_->collapse(row)) && row > 0)
                {
                    // No children or already collapsed so move to parent
                    pview_->goto_parent(row);
                }
                return TRUE;
            }
        case VK_MULTIPLY:
            // Expand all beneath
            pview_->expand_all(m_idCurrentCell.row - GetFixedRowCount());
            return TRUE;
        case VK_ADD:
            // Expand immed. children (ie one level only)
            pview_->expand_one(m_idCurrentCell.row - GetFixedRowCount());
            return TRUE;
        case VK_SUBTRACT:
            // Collapse all beneath
            pview_->collapse(m_idCurrentCell.row - GetFixedRowCount());
            return TRUE;
        }
    }
    return CGridCtrl::PreTranslateMessage(pMsg);
}

void CGridCtrl2::OnSelchange()
{
    if (sel_row_ != -2)
    {
        // Set focus to another cell so that drop-list control is destroyed now
        SetFocus();
        SetFocusCell(sel_row_, GetFixedColumnCount());
        if (sel_row_ != -1)
            EnsureVisible(GetFixedRowCount(), GetFixedColumnCount());
    }
}

//-----------------------------------------------------------------------------

// good column widths to use: 240,73,90,31,36,66,35,36,47,37,62,

/////////////////////////////////////////////////////////////////////////////
// CDataFormatView

#define MAX_HEX_FORMAT  8

IMPLEMENT_DYNCREATE(CDataFormatView, CView)

CDataFormatView::CDataFormatView()
{
    tree_init_ = false;
    psis_ = NULL;
    edit_row_type_changed_ = -1;
}

CDataFormatView::~CDataFormatView()
{
}

BEGIN_MESSAGE_MAP(CDataFormatView, CView)
    //{{AFX_MSG_MAP(CDataFormatView)
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
    ON_WM_DESTROY()
    ON_COMMAND(ID_VIEWTEST, OnViewtest)
    ON_COMMAND(ID_DFFD_SYNC, OnDffdSync)
    ON_UPDATE_COMMAND_UI(ID_DFFD_SYNC, OnUpdateDffdSync)
    ON_COMMAND(ID_DFFD_WEB, OnDffdWeb)
    ON_UPDATE_COMMAND_UI(ID_DFFD_WEB, OnUpdateDffdWeb)
    //}}AFX_MSG_MAP

    ON_NOTIFY(NM_CLICK, 100, OnGridClick)
    ON_NOTIFY(NM_DBLCLK, 100, OnGridDoubleClick)
    ON_NOTIFY(NM_RCLICK, 100, OnGridRClick)
    ON_NOTIFY(GVN_SELCHANGED, 100, OnGridEndSelChange)
    ON_NOTIFY(GVN_BEGINLABELEDIT, 100, OnGridBeginLabelEdit)
    ON_NOTIFY(GVN_ENDLABELEDIT, 100, OnGridEndLabelEdit)

    ON_COMMAND(ID_FILE_PRINT, CView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_DIRECT, CView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_PREVIEW, OnFilePrintPreview)

    // Messages forwarded to sister view
        ON_COMMAND(ID_ADDR_TOGGLE, OnAddrToggle)
        ON_UPDATE_COMMAND_UI(ID_ADDR_TOGGLE, OnUpdateAddrToggle)
        ON_COMMAND(ID_GRAPHIC_TOGGLE, OnGraphicToggle)
        ON_UPDATE_COMMAND_UI(ID_GRAPHIC_TOGGLE, OnUpdateGraphicToggle)
        ON_COMMAND(ID_CHAR_TOGGLE, OnCharToggle)
        ON_UPDATE_COMMAND_UI(ID_CHAR_TOGGLE, OnUpdateCharToggle)
        ON_COMMAND(ID_FONT, OnFont)
        ON_COMMAND(ID_AUTOFIT, OnAutoFit)
        ON_UPDATE_COMMAND_UI(ID_AUTOFIT, OnUpdateAutofit)
        ON_COMMAND(ID_ASC_EBC, OnAscEbc)
        ON_UPDATE_COMMAND_UI(ID_ASC_EBC, OnUpdateAscEbc)
        ON_COMMAND(ID_CONTROL, OnControl)
        ON_UPDATE_COMMAND_UI(ID_CONTROL, OnUpdateControl)
        ON_COMMAND(ID_MARK, OnMark)
        ON_COMMAND(ID_GOTO_MARK, OnGotoMark)
        ON_COMMAND(ID_EDIT_UNDO, OnEditUndo)
        ON_UPDATE_COMMAND_UI(ID_EDIT_UNDO, OnUpdateEditUndo)
        ON_COMMAND(ID_SEARCH_HEX, OnSearchHex)
        ON_COMMAND(ID_SEARCH_ASCII, OnSearchAscii)
        ON_COMMAND(ID_ALLOW_MODS, OnAllowMods)
        ON_UPDATE_COMMAND_UI(ID_ALLOW_MODS, OnUpdateAllowMods)
        ON_COMMAND(ID_CONTROL_TOGGLE, OnControlToggle)
        ON_COMMAND(ID_INSERT, OnInsert)
        ON_UPDATE_COMMAND_UI(ID_INSERT, OnUpdateInsert)
        ON_COMMAND(ID_DFFD_AUTO_SYNC, OnDffdAutoSync)
        ON_UPDATE_COMMAND_UI(ID_DFFD_AUTO_SYNC, OnUpdateDffdAutoSync)
        ON_COMMAND(ID_SEARCH_ICASE, OnSearchIcase)
//        ON_COMMAND(ID_EDIT_COMPARE, OnEditCompare)   // Don't allow compare unless in CHexEditView
//        ON_UPDATE_COMMAND_UI(ID_EDIT_COMPARE, OnUpdateEditCompare)
    ON_COMMAND(ID_TRACK_CHANGES, OnTrackChanges)
    ON_UPDATE_COMMAND_UI(ID_TRACK_CHANGES, OnUpdateTrackChanges)
        ON_COMMAND(ID_WINDOW_NEXT, OnWindowNext)
        ON_COMMAND(ID_INC_BYTE, OnIncByte)
        ON_COMMAND(ID_INC_16BIT, OnInc16bit)
        ON_COMMAND(ID_INC_32BIT, OnInc32bit)
        ON_COMMAND(ID_INC_64BIT, OnInc64bit)
        ON_COMMAND(ID_DEC_BYTE, OnDecByte)
        ON_COMMAND(ID_DEC_16BIT, OnDec16bit)
        ON_COMMAND(ID_DEC_32BIT, OnDec32bit)
        ON_COMMAND(ID_DEC_64BIT, OnDec64bit)
        ON_COMMAND(ID_FLIP_16BIT, OnFlip16bit)
        ON_COMMAND(ID_FLIP_32BIT, OnFlip32bit)
        ON_COMMAND(ID_FLIP_64BIT, OnFlip64bit)
        ON_UPDATE_COMMAND_UI(ID_INC_BYTE, OnUpdateByte)
        ON_UPDATE_COMMAND_UI(ID_INC_16BIT, OnUpdate16bit)
        ON_UPDATE_COMMAND_UI(ID_INC_32BIT, OnUpdate32bit)
        ON_UPDATE_COMMAND_UI(ID_INC_64BIT, OnUpdate64bit)
        ON_COMMAND(ID_SELECT_ALL, OnSelectAll)
        //ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
        //ON_COMMAND(ID_EDIT_CUT, OnEditCut)
        //ON_COMMAND(ID_EDIT_PASTE, OnEditPaste)
        //ON_UPDATE_COMMAND_UI(ID_EDIT_COPY, OnUpdateClipboard)
        //ON_UPDATE_COMMAND_UI(ID_EDIT_CUT, OnUpdateEditCut)
        //ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE, OnUpdateTextPaste)
	// Allow Copy from grid
	ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
	ON_UPDATE_COMMAND_UI(ID_EDIT_COPY, OnUpdateEditCopyOrCut)
        ON_UPDATE_COMMAND_UI(ID_PASTE_UNICODE, OnUpdateUnicodePaste)
        ON_COMMAND(ID_FONT_DEC, OnFontDec)
        ON_COMMAND(ID_FONT_INC, OnFontInc)
        ON_UPDATE_COMMAND_UI(ID_FONT_DEC, OnUpdateFontDec)
        ON_UPDATE_COMMAND_UI(ID_FONT_INC, OnUpdateFontInc)
        ON_COMMAND(ID_PASTE_ASCII, OnPasteAscii)
        ON_COMMAND(ID_PASTE_EBCDIC, OnPasteEbcdic)
        ON_COMMAND(ID_PASTE_UNICODE, OnPasteUnicode)
        ON_COMMAND(ID_COPY_CCHAR, OnCopyCchar)
        ON_COMMAND(ID_COPY_HEX, OnCopyHex)
        ON_COMMAND(ID_EDIT_WRITEFILE, OnEditWriteFile)
        ON_UPDATE_COMMAND_UI(ID_EDIT_READFILE, OnUpdateReadFile)
        ON_COMMAND(ID_EDIT_READFILE, OnReadFile)
        ON_COMMAND(ID_EXTENDTO_MARK, OnExtendToMark)
        ON_COMMAND(ID_SWAP_MARK, OnSwapMark)
        ON_COMMAND(ID_REDRAW, OnRedraw)
        ON_COMMAND(ID_SCROLL_DOWN, OnScrollDown)
        ON_COMMAND(ID_SCROLL_UP, OnScrollUp)
        ON_COMMAND(ID_SWAP, OnSwap)
        ON_COMMAND(ID_START_LINE, OnStartLine)
        ON_COMMAND(ID_DEL, OnDel)
        ON_UPDATE_COMMAND_UI(ID_SWAP, OnUpdateSwap)
        ON_COMMAND(ID_OEM_TOGGLE, OnOemToggle)
        ON_UPDATE_COMMAND_UI(ID_OEM_TOGGLE, OnUpdateOemToggle)
        ON_COMMAND(ID_INVERT, OnInvert)
        ON_COMMAND(ID_NEG_BYTE, OnNegByte)
        ON_COMMAND(ID_NEG_16BIT, OnNeg16bit)
        ON_COMMAND(ID_NEG_32BIT, OnNeg32bit)
        ON_COMMAND(ID_NEG_64BIT, OnNeg64bit)
        ON_COMMAND(ID_HIGHLIGHT, OnHighlight)
        ON_UPDATE_COMMAND_UI(ID_HIGHLIGHT, OnUpdateHighlight)
        ON_COMMAND(ID_HIGHLIGHT_CLEAR, OnHighlightClear)
        ON_COMMAND(ID_HIGHLIGHT_PREV, OnHighlightPrev)
        ON_COMMAND(ID_HIGHLIGHT_NEXT, OnHighlightNext)
        ON_UPDATE_COMMAND_UI(ID_HIGHLIGHT_PREV, OnUpdateHighlightPrev)
        ON_UPDATE_COMMAND_UI(ID_HIGHLIGHT_NEXT, OnUpdateHighlightNext)
        ON_COMMAND(ID_EDIT_GOTO, OnEditGoto)
        ON_COMMAND(ID_EDIT_FIND, OnEditFind)
        ON_COMMAND(ID_EDIT_REPLACE, OnEditReplace)
    ON_COMMAND(ID_ASC2EBC, OnAscii2Ebcdic)
    ON_UPDATE_COMMAND_UI(ID_ASC2EBC, OnUpdateConvert)
    ON_COMMAND(ID_EBC2ASC, OnEbcdic2Ascii)
    ON_COMMAND(ID_ANSI2IBM, OnAnsi2Ibm)
    ON_COMMAND(ID_IBM2ANSI, OnIbm2Ansi)
    ON_COMMAND(ID_ENCRYPT_ENCRYPT, OnEncrypt)
    ON_COMMAND(ID_ENCRYPT_DECRYPT, OnDecrypt)
    ON_UPDATE_COMMAND_UI(ID_ENCRYPT_ENCRYPT, OnUpdateEncrypt)
    ON_COMMAND(ID_EDIT_APPENDFILE, OnEditAppendFile)
    ON_COMMAND(ID_EDIT_APPENDSAMEFILE, OnEditAppendSameFile)
    ON_UPDATE_COMMAND_UI(ID_EDIT_APPENDSAMEFILE, OnUpdateEditAppendSameFile)
    ON_COMMAND(ID_UNDO_CHANGES, OnUndoChanges)
    ON_UPDATE_COMMAND_UI(ID_UNDO_CHANGES, OnUpdateUndoChanges)
    ON_COMMAND(ID_CALC_SEL, OnCalcSel)
    ON_UPDATE_COMMAND_UI(ID_CALC_SEL, OnUpdateCalcSel)
    ON_COMMAND(ID_DISPLAY_RESET, OnDisplayReset)
        ON_UPDATE_COMMAND_UI(ID_CONTROL_TOGGLE, OnUpdateControl)
        ON_UPDATE_COMMAND_UI(ID_DEC_BYTE, OnUpdateByte)
        ON_UPDATE_COMMAND_UI(ID_DEC_16BIT, OnUpdate16bit)
        ON_UPDATE_COMMAND_UI(ID_DEC_32BIT, OnUpdate32bit)
        ON_UPDATE_COMMAND_UI(ID_DEC_64BIT, OnUpdate64bit)
        ON_UPDATE_COMMAND_UI(ID_FLIP_16BIT, OnUpdate16bit)
        ON_UPDATE_COMMAND_UI(ID_FLIP_32BIT, OnUpdate32bit)
        ON_UPDATE_COMMAND_UI(ID_FLIP_64BIT, OnUpdate64bit)
        ON_UPDATE_COMMAND_UI(ID_SEARCH_SEL, OnUpdateClipboard)
        ON_UPDATE_COMMAND_UI(ID_PASTE_ASCII, OnUpdateTextPaste)
        ON_UPDATE_COMMAND_UI(ID_PASTE_EBCDIC, OnUpdateTextPaste)
        ON_UPDATE_COMMAND_UI(ID_COPY_CCHAR, OnUpdateClipboard)
        ON_UPDATE_COMMAND_UI(ID_COPY_HEX, OnUpdateClipboard)
        ON_UPDATE_COMMAND_UI(ID_EDIT_WRITEFILE, OnUpdateClipboard)
        ON_UPDATE_COMMAND_UI(ID_INVERT, OnUpdateByte)
        ON_UPDATE_COMMAND_UI(ID_NEG_BYTE, OnUpdateByte)
        ON_UPDATE_COMMAND_UI(ID_NEG_16BIT, OnUpdate16bit)
        ON_UPDATE_COMMAND_UI(ID_NEG_32BIT, OnUpdate32bit)
        ON_UPDATE_COMMAND_UI(ID_NEG_64BIT, OnUpdate64bit)
    ON_UPDATE_COMMAND_UI(ID_EBC2ASC, OnUpdateConvert)
    ON_UPDATE_COMMAND_UI(ID_ANSI2IBM, OnUpdateConvert)
    ON_UPDATE_COMMAND_UI(ID_IBM2ANSI, OnUpdateConvert)
    ON_UPDATE_COMMAND_UI(ID_ENCRYPT_DECRYPT, OnUpdateEncrypt)
    ON_UPDATE_COMMAND_UI(ID_EDIT_APPENDFILE, OnUpdateClipboard)
        ON_COMMAND(ID_XOR_BYTE, OnXorByte)
        ON_COMMAND(ID_XOR_16BIT, OnXor16bit)
        ON_COMMAND(ID_XOR_32BIT, OnXor32bit)
        ON_COMMAND(ID_XOR_64BIT, OnXor64bit)
        ON_UPDATE_COMMAND_UI(ID_XOR_BYTE, OnUpdateByteBinary)
        ON_UPDATE_COMMAND_UI(ID_XOR_16BIT, OnUpdate16bitBinary)
        ON_UPDATE_COMMAND_UI(ID_XOR_32BIT, OnUpdate32bitBinary)
        ON_UPDATE_COMMAND_UI(ID_XOR_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_ASSIGN_BYTE, OnAssignByte)
        ON_COMMAND(ID_ASSIGN_16BIT, OnAssign16bit)
        ON_COMMAND(ID_ASSIGN_32BIT, OnAssign32bit)
        ON_COMMAND(ID_ASSIGN_64BIT, OnAssign64bit)
        ON_UPDATE_COMMAND_UI(ID_ASSIGN_BYTE, OnUpdateByteBinary)
        ON_UPDATE_COMMAND_UI(ID_ASSIGN_16BIT, OnUpdate16bitBinary)
        ON_UPDATE_COMMAND_UI(ID_ASSIGN_32BIT, OnUpdate32bitBinary)
        ON_UPDATE_COMMAND_UI(ID_ASSIGN_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_RAND_BYTE, OnRandByte)
        ON_COMMAND(ID_RAND_FAST, OnRandFast)
//        ON_COMMAND(ID_RAND_16BIT, OnRand16bit)
//        ON_COMMAND(ID_RAND_32BIT, OnRand32bit)
//        ON_COMMAND(ID_RAND_64BIT, OnRand64bit)
        ON_UPDATE_COMMAND_UI(ID_RAND_BYTE, OnUpdateByte)
        ON_UPDATE_COMMAND_UI(ID_RAND_FAST, OnUpdateByte)
//        ON_UPDATE_COMMAND_UI(ID_RAND_16BIT, OnUpdate16bitBinary)
//        ON_UPDATE_COMMAND_UI(ID_RAND_32BIT, OnUpdate32bitBinary)
//        ON_UPDATE_COMMAND_UI(ID_RAND_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_ADD_BYTE, OnAddByte)
        ON_COMMAND(ID_ADD_16BIT, OnAdd16bit)
        ON_COMMAND(ID_ADD_32BIT, OnAdd32bit)
        ON_COMMAND(ID_ADD_64BIT, OnAdd64bit)
        ON_UPDATE_COMMAND_UI(ID_ADD_BYTE, OnUpdateByteBinary)
        ON_UPDATE_COMMAND_UI(ID_ADD_16BIT, OnUpdate16bitBinary)
        ON_UPDATE_COMMAND_UI(ID_ADD_32BIT, OnUpdate32bitBinary)
        ON_UPDATE_COMMAND_UI(ID_ADD_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_SUBTRACT_BYTE, OnSubtractByte)
        ON_COMMAND(ID_SUBTRACT_16BIT, OnSubtract16bit)
        ON_COMMAND(ID_SUBTRACT_32BIT, OnSubtract32bit)
        ON_COMMAND(ID_SUBTRACT_64BIT, OnSubtract64bit)
        ON_UPDATE_COMMAND_UI(ID_SUBTRACT_BYTE, OnUpdateByteBinary)
        ON_UPDATE_COMMAND_UI(ID_SUBTRACT_16BIT, OnUpdate16bitBinary)
        ON_UPDATE_COMMAND_UI(ID_SUBTRACT_32BIT, OnUpdate32bitBinary)
        ON_UPDATE_COMMAND_UI(ID_SUBTRACT_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_AND_BYTE, OnAndByte)
        ON_COMMAND(ID_AND_16BIT, OnAnd16bit)
        ON_COMMAND(ID_AND_32BIT, OnAnd32bit)
        ON_COMMAND(ID_AND_64BIT, OnAnd64bit)
        ON_UPDATE_COMMAND_UI(ID_AND_BYTE, OnUpdateByteBinary)
        ON_UPDATE_COMMAND_UI(ID_AND_16BIT, OnUpdate16bitBinary)
        ON_UPDATE_COMMAND_UI(ID_AND_32BIT, OnUpdate32bitBinary)
        ON_UPDATE_COMMAND_UI(ID_AND_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_OR_BYTE, OnOrByte)
        ON_COMMAND(ID_OR_16BIT, OnOr16bit)
        ON_COMMAND(ID_OR_32BIT, OnOr32bit)
        ON_COMMAND(ID_OR_64BIT, OnOr64bit)
        ON_UPDATE_COMMAND_UI(ID_OR_BYTE, OnUpdateByteBinary)
        ON_UPDATE_COMMAND_UI(ID_OR_16BIT, OnUpdate16bitBinary)
        ON_UPDATE_COMMAND_UI(ID_OR_32BIT, OnUpdate32bitBinary)
        ON_UPDATE_COMMAND_UI(ID_OR_64BIT, OnUpdate64bitBinary)

        ON_COMMAND(ID_MUL_BYTE, OnMulByte)
        ON_UPDATE_COMMAND_UI(ID_MUL_BYTE, OnUpdateByteBinary)
        ON_COMMAND(ID_MUL_16BIT, OnMul16bit)
        ON_UPDATE_COMMAND_UI(ID_MUL_16BIT, OnUpdate16bitBinary)
        ON_COMMAND(ID_MUL_32BIT, OnMul32bit)
        ON_UPDATE_COMMAND_UI(ID_MUL_32BIT, OnUpdate32bitBinary)
        ON_COMMAND(ID_MUL_64BIT, OnMul64bit)
        ON_UPDATE_COMMAND_UI(ID_MUL_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_DIV_BYTE, OnDivByte)
        ON_UPDATE_COMMAND_UI(ID_DIV_BYTE, OnUpdateByteBinary)
        ON_COMMAND(ID_DIV_16BIT, OnDiv16bit)
        ON_UPDATE_COMMAND_UI(ID_DIV_16BIT, OnUpdate16bitBinary)
        ON_COMMAND(ID_DIV_32BIT, OnDiv32bit)
        ON_UPDATE_COMMAND_UI(ID_DIV_32BIT, OnUpdate32bitBinary)
        ON_COMMAND(ID_DIV_64BIT, OnDiv64bit)
        ON_UPDATE_COMMAND_UI(ID_DIV_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_MOD_BYTE, OnModByte)
        ON_UPDATE_COMMAND_UI(ID_MOD_BYTE, OnUpdateByteBinary)
        ON_COMMAND(ID_MOD_16BIT, OnMod16bit)
        ON_UPDATE_COMMAND_UI(ID_MOD_16BIT, OnUpdate16bitBinary)
        ON_COMMAND(ID_MOD_32BIT, OnMod32bit)
        ON_UPDATE_COMMAND_UI(ID_MOD_32BIT, OnUpdate32bitBinary)
        ON_COMMAND(ID_MOD_64BIT, OnMod64bit)
        ON_UPDATE_COMMAND_UI(ID_MOD_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_REV_BYTE, OnRevByte)
        ON_UPDATE_COMMAND_UI(ID_REV_BYTE, OnUpdateByte)
        ON_COMMAND(ID_REV_16BIT, OnRev16bit)
        ON_UPDATE_COMMAND_UI(ID_REV_16BIT, OnUpdate16bit)
        ON_COMMAND(ID_REV_32BIT, OnRev32bit)
        ON_UPDATE_COMMAND_UI(ID_REV_32BIT, OnUpdate32bit)
        ON_COMMAND(ID_REV_64BIT, OnRev64bit)
        ON_UPDATE_COMMAND_UI(ID_REV_64BIT, OnUpdate64bit)
        ON_COMMAND(ID_SUBTRACT_X_BYTE, OnSubtractXByte)
        ON_UPDATE_COMMAND_UI(ID_SUBTRACT_X_BYTE, OnUpdateByteBinary)
        ON_COMMAND(ID_SUBTRACT_X_16BIT, OnSubtractX16bit)
        ON_UPDATE_COMMAND_UI(ID_SUBTRACT_X_16BIT, OnUpdate16bitBinary)
        ON_COMMAND(ID_SUBTRACT_X_32BIT, OnSubtractX32bit)
        ON_UPDATE_COMMAND_UI(ID_SUBTRACT_X_32BIT, OnUpdate32bitBinary)
        ON_COMMAND(ID_SUBTRACT_X_64BIT, OnSubtractX64bit)
        ON_UPDATE_COMMAND_UI(ID_SUBTRACT_X_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_DIV_BYTE, OnDivXByte)
        ON_UPDATE_COMMAND_UI(ID_DIV_X_BYTE, OnUpdateByteBinary)
        ON_COMMAND(ID_DIV_X_16BIT, OnDivX16bit)
        ON_UPDATE_COMMAND_UI(ID_DIV_X_16BIT, OnUpdate16bitBinary)
        ON_COMMAND(ID_DIV_X_32BIT, OnDivX32bit)
        ON_UPDATE_COMMAND_UI(ID_DIV_X_32BIT, OnUpdate32bitBinary)
        ON_COMMAND(ID_DIV_X_64BIT, OnDivX64bit)
        ON_UPDATE_COMMAND_UI(ID_DIV_X_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_MOD_X_BYTE, OnModXByte)
        ON_UPDATE_COMMAND_UI(ID_MOD_X_BYTE, OnUpdateByteBinary)
        ON_COMMAND(ID_MOD_X_16BIT, OnModX16bit)
        ON_UPDATE_COMMAND_UI(ID_MOD_X_16BIT, OnUpdate16bitBinary)
        ON_COMMAND(ID_MOD_X_32BIT, OnModX32bit)
        ON_UPDATE_COMMAND_UI(ID_MOD_X_32BIT, OnUpdate32bitBinary)
        ON_COMMAND(ID_MOD_X_64BIT, OnModX64bit)
        ON_UPDATE_COMMAND_UI(ID_MOD_X_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_GTR_BYTE, OnGtrByte)
        ON_UPDATE_COMMAND_UI(ID_GTR_BYTE, OnUpdateByteBinary)
        ON_COMMAND(ID_GTR_16BIT, OnGtr16bit)
        ON_UPDATE_COMMAND_UI(ID_GTR_16BIT, OnUpdate16bitBinary)
        ON_COMMAND(ID_GTR_32BIT, OnGtr32bit)
        ON_UPDATE_COMMAND_UI(ID_GTR_32BIT, OnUpdate32bitBinary)
        ON_COMMAND(ID_GTR_64BIT, OnGtr64bit)
        ON_UPDATE_COMMAND_UI(ID_GTR_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_LESS_BYTE, OnLessByte)
        ON_UPDATE_COMMAND_UI(ID_LESS_BYTE, OnUpdateByteBinary)
        ON_COMMAND(ID_LESS_16BIT, OnLess16bit)
        ON_UPDATE_COMMAND_UI(ID_LESS_16BIT, OnUpdate16bitBinary)
        ON_COMMAND(ID_LESS_32BIT, OnLess32bit)
        ON_UPDATE_COMMAND_UI(ID_LESS_32BIT, OnUpdate32bitBinary)
        ON_COMMAND(ID_LESS_64BIT, OnLess64bit)
        ON_UPDATE_COMMAND_UI(ID_LESS_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_GTRU_BYTE, OnGtrUByte)
        ON_UPDATE_COMMAND_UI(ID_GTRU_BYTE, OnUpdateByteBinary)
        ON_COMMAND(ID_GTRU_16BIT, OnGtrU16bit)
        ON_UPDATE_COMMAND_UI(ID_GTRU_16BIT, OnUpdate16bitBinary)
        ON_COMMAND(ID_GTRU_32BIT, OnGtrU32bit)
        ON_UPDATE_COMMAND_UI(ID_GTRU_32BIT, OnUpdate32bitBinary)
        ON_COMMAND(ID_GTRU_64BIT, OnGtrU64bit)
        ON_UPDATE_COMMAND_UI(ID_GTRU_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_LESSU_BYTE, OnLessUByte)
        ON_UPDATE_COMMAND_UI(ID_LESSU_BYTE, OnUpdateByteBinary)
        ON_COMMAND(ID_LESSU_16BIT, OnLessU16bit)
        ON_UPDATE_COMMAND_UI(ID_LESSU_16BIT, OnUpdate16bitBinary)
        ON_COMMAND(ID_LESSU_32BIT, OnLessU32bit)
        ON_UPDATE_COMMAND_UI(ID_LESSU_32BIT, OnUpdate32bitBinary)
        ON_COMMAND(ID_LESSU_64BIT, OnLessU64bit)
        ON_UPDATE_COMMAND_UI(ID_LESSU_64BIT, OnUpdate64bitBinary)

        ON_COMMAND(ID_ROL_BYTE, OnRolByte)
        ON_COMMAND(ID_ROL_16BIT, OnRol16bit)
        ON_COMMAND(ID_ROL_32BIT, OnRol32bit)
        ON_COMMAND(ID_ROL_64BIT, OnRol64bit)
        ON_UPDATE_COMMAND_UI(ID_ROL_BYTE, OnUpdateByteBinary)
        ON_UPDATE_COMMAND_UI(ID_ROL_16BIT, OnUpdate16bitBinary)
        ON_UPDATE_COMMAND_UI(ID_ROL_32BIT, OnUpdate32bitBinary)
        ON_UPDATE_COMMAND_UI(ID_ROL_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_ROR_BYTE, OnRolByte)
        ON_COMMAND(ID_ROR_16BIT, OnRol16bit)
        ON_COMMAND(ID_ROR_32BIT, OnRol32bit)
        ON_COMMAND(ID_ROR_64BIT, OnRol64bit)
        ON_UPDATE_COMMAND_UI(ID_ROR_BYTE, OnUpdateByteBinary)
        ON_UPDATE_COMMAND_UI(ID_ROR_16BIT, OnUpdate16bitBinary)
        ON_UPDATE_COMMAND_UI(ID_ROR_32BIT, OnUpdate32bitBinary)
        ON_UPDATE_COMMAND_UI(ID_ROR_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_LSL_BYTE, OnLslByte)
        ON_COMMAND(ID_LSL_16BIT, OnLsl16bit)
        ON_COMMAND(ID_LSL_32BIT, OnLsl32bit)
        ON_COMMAND(ID_LSL_64BIT, OnLsl64bit)
        ON_UPDATE_COMMAND_UI(ID_LSL_BYTE, OnUpdateByteBinary)
        ON_UPDATE_COMMAND_UI(ID_LSL_16BIT, OnUpdate16bitBinary)
        ON_UPDATE_COMMAND_UI(ID_LSL_32BIT, OnUpdate32bitBinary)
        ON_UPDATE_COMMAND_UI(ID_LSL_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_LSR_BYTE, OnLsrByte)
        ON_COMMAND(ID_LSR_16BIT, OnLsr16bit)
        ON_COMMAND(ID_LSR_32BIT, OnLsr32bit)
        ON_COMMAND(ID_LSR_64BIT, OnLsr64bit)
        ON_UPDATE_COMMAND_UI(ID_LSR_BYTE, OnUpdateByteBinary)
        ON_UPDATE_COMMAND_UI(ID_LSR_16BIT, OnUpdate16bitBinary)
        ON_UPDATE_COMMAND_UI(ID_LSR_32BIT, OnUpdate32bitBinary)
        ON_UPDATE_COMMAND_UI(ID_LSR_64BIT, OnUpdate64bitBinary)
        ON_COMMAND(ID_ASR_BYTE, OnAsrByte)
        ON_COMMAND(ID_ASR_16BIT, OnAsr16bit)
        ON_COMMAND(ID_ASR_32BIT, OnAsr32bit)
        ON_COMMAND(ID_ASR_64BIT, OnAsr64bit)
        ON_UPDATE_COMMAND_UI(ID_ASR_BYTE, OnUpdateByteBinary)
        ON_UPDATE_COMMAND_UI(ID_ASR_16BIT, OnUpdate16bitBinary)
        ON_UPDATE_COMMAND_UI(ID_ASR_32BIT, OnUpdate32bitBinary)
        ON_UPDATE_COMMAND_UI(ID_ASR_64BIT, OnUpdate64bitBinary)

        ON_COMMAND(ID_JUMP_HEX_ADDR, OnJumpHexAddr)
//        ON_COMMAND(ID_SEARCH_START, OnSearch)     // Handle message from BCG search combo
        ON_COMMAND(ID_JUMP_HEX_START, OnJumpHex)  // Handle message from BCG hex combo
        ON_COMMAND(ID_JUMP_DEC_START, OnJumpDec)  // Handle message from BCG dec combo
        ON_COMMAND(ID_SELECT_LINE, OnSelectLine)
        ON_COMMAND(ID_DISPLAY_HEX, OnDisplayHex)
        ON_UPDATE_COMMAND_UI(ID_DISPLAY_HEX, OnUpdateDisplayHex)
        ON_COMMAND(ID_DISPLAY_CHAR, OnDisplayChar)
        ON_UPDATE_COMMAND_UI(ID_DISPLAY_CHAR, OnUpdateDisplayChar)
        ON_COMMAND(ID_DISPLAY_BOTH, OnDisplayBoth)
        ON_UPDATE_COMMAND_UI(ID_DISPLAY_BOTH, OnUpdateDisplayBoth)
        ON_COMMAND(ID_DISPLAY_STACKED, OnDisplayStacked)
        ON_UPDATE_COMMAND_UI(ID_DISPLAY_STACKED, OnUpdateDisplayStacked)
        ON_COMMAND(ID_CHARSET_ASCII, OnCharsetAscii)
        ON_UPDATE_COMMAND_UI(ID_CHARSET_ASCII, OnUpdateCharsetAscii)
        ON_COMMAND(ID_CHARSET_ANSI, OnCharsetAnsi)
        ON_UPDATE_COMMAND_UI(ID_CHARSET_ANSI, OnUpdateCharsetAnsi)
        ON_COMMAND(ID_CHARSET_OEM, OnCharsetOem)
        ON_UPDATE_COMMAND_UI(ID_CHARSET_OEM, OnUpdateCharsetOem)
        ON_COMMAND(ID_CHARSET_EBCDIC, OnCharsetEbcdic)
        ON_UPDATE_COMMAND_UI(ID_CHARSET_EBCDIC, OnUpdateCharsetEbcdic)
        ON_COMMAND(ID_CONTROL_NONE, OnControlNone)
        ON_UPDATE_COMMAND_UI(ID_CONTROL_NONE, OnUpdateControlNone)
        ON_COMMAND(ID_CONTROL_ALPHA, OnControlAlpha)
        ON_UPDATE_COMMAND_UI(ID_CONTROL_ALPHA, OnUpdateControlAlpha)
        ON_COMMAND(ID_CONTROL_C, OnControlC)
        ON_UPDATE_COMMAND_UI(ID_CONTROL_C, OnUpdateControlC)

        ON_COMMAND(ID_TOGGLE_ENDIAN, OnToggleEndian)
        ON_COMMAND(ID_BIG_ENDIAN, OnBigEndian)
        ON_COMMAND(ID_LITTLE_ENDIAN, OnLittleEndian)
        ON_UPDATE_COMMAND_UI(ID_TOGGLE_ENDIAN, OnUpdateToggleEndian)
        ON_UPDATE_COMMAND_UI(ID_BIG_ENDIAN, OnUpdateBigEndian)
        ON_UPDATE_COMMAND_UI(ID_LITTLE_ENDIAN, OnUpdateLittleEndian)

        ON_COMMAND(ID_CHECKSUM8, OnChecksum8)
        ON_COMMAND(ID_CHECKSUM16, OnChecksum16)
        ON_COMMAND(ID_CHECKSUM32, OnChecksum32)
        ON_COMMAND(ID_CHECKSUM64, OnChecksum64)
        ON_UPDATE_COMMAND_UI(ID_CHECKSUM8, OnUpdateByteNZ)
        ON_UPDATE_COMMAND_UI(ID_CHECKSUM16, OnUpdate16bitNZ)
        ON_UPDATE_COMMAND_UI(ID_CHECKSUM32, OnUpdate32bitNZ)
        ON_UPDATE_COMMAND_UI(ID_CHECKSUM64, OnUpdate64bitNZ)
        ON_COMMAND(ID_CRC_CCITT, OnCrcCcitt)
//        ON_COMMAND(ID_CRC16, OnCrc16)
        ON_COMMAND(ID_CRC32, OnCrc32)
        ON_UPDATE_COMMAND_UI(ID_CRC_CCITT, OnUpdateByteNZ)
//        ON_UPDATE_COMMAND_UI(ID_CRC16, OnUpdateByte)
        ON_UPDATE_COMMAND_UI(ID_CRC32, OnUpdateByteNZ)

        ON_UPDATE_COMMAND_UI(ID_INSERT_BLOCK, OnUpdateInsertBlock)
        ON_COMMAND(ID_INSERT_BLOCK, OnInsertBlock)

END_MESSAGE_MAP()

void CDataFormatView::InitTree()
{
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);

    COLORREF bg1, bg2, bg3;
    calc_colours(bg1, bg2, bg3);
 
    // Set background colours
    grid_.SetGridBkColor(bg1);          // Area of grid past end and past right (defaults to ugly grey)
    grid_.SetTextBkColor(bg1);          // This is the only way to set the background of the tree

    if (pdoc == NULL || pdoc->ptree_ == NULL)
    {
        return;
    }

    CWaitCursor wait;

    // Init progress bar
    CMainFrame *mm = (CMainFrame *)AfxGetMainWnd();
    mm->m_wndStatusBar.EnablePaneProgressBar(0);
    clock_t last_checked = clock();

    // Make the default cell height zero while the tree is built, otherwise TreeDisplayOutline(1)
    // tries to call SetRowHeight to hide most of the rows.  This results in ResetScrollBars()
    // being called for every row of the grid which takes a long time for a big grid.
    CGridDefaultCell *pdefault_cell = (CGridDefaultCell *)grid_.GetDefaultCell(FALSE, FALSE);
    default_height_ = pdefault_cell->GetHeight();
    pdefault_cell->SetHeight(0);

    tree_col_.TreeSetup(&grid_, grid_.GetFixedColumnCount(), pdoc->df_indent_.size(), 1, &pdoc->df_indent_[0], TRUE, FALSE);
    tree_col_.TreeDisplayOutline(1);

	GV_ITEM item;
#if _MSC_VER >= 1300
	GV_ITEMW itemw;
    itemw.nState = 0;
    itemw.crFgClr = psis_->GetDefaultTextCol();
#endif
    // Fill the tree/grid with info about each element
//    item.nState = GVIS_READONLY;      // Whether or not editing is allowed is now handled by OnGridBeginLabelEdit
    item.nState = 0;

    // Set colour of top tree cell - sets colour for whole tree
    item.row = grid_.GetFixedRowCount();
    item.col = grid_.GetFixedColumnCount();
    item.mask = GVIF_BKCLR|GVIF_FGCLR;  // Just changing fg and bg colours
    item.crBkClr = bg1;
    item.crFgClr = psis_->GetDefaultTextCol();
    grid_.SetItem(&item);

    char disp[128];                     // Holds output of sprintf
    int consec_count = 0;               // Counts consecutive data lines so we can paint backgrounds

    // These are used to work out element names for array elements
    std::vector<CString> for_name(size_t(pdoc->max_indent_+1));
    std::vector<int>     for_count(size_t(pdoc->max_indent_+1));

    ASSERT(pdoc->df_type_.size() == pdoc->df_elt_.size());
    ASSERT(pdoc->df_indent_.size() == pdoc->df_elt_.size());

    bool past_defns = false;           // Signals that past struct defns - so we can show greyed rows

    for (int ii = 0; ii < (int)pdoc->df_type_.size(); ++ii)
    {
        if (pdoc->df_indent_[ii] == 2 && pdoc->df_type_[ii] != CHexEditDoc::DF_DEFINE_STRUCT)
            past_defns = true;

        // Set row of cells that we are modifying
        item.row = ii + grid_.GetFixedRowCount();
#if _MSC_VER >= 1300
        itemw.row = ii + grid_.GetFixedRowCount();
#endif
#ifdef _DEBUG
        ASSERT(grid_.GetFixedColumnCount() == 1);
        item.mask = GVIF_FORMAT|GVIF_TEXT;
        item.nFormat = DT_CENTER|DT_VCENTER|DT_SINGLELINE;
        item.strText.Format("%ld", long(ii));
        item.col = 0;
        grid_.SetItem(&item);
#endif

        // Give data rows alternating background colours
        if (abs(pdoc->df_type_[ii]) < CHexEditDoc::DF_DATA)
        {
            consec_count = 0;
            item.crBkClr = bg1;  // Non-data row has normal background colour
#if _MSC_VER >= 1300
            itemw.crBkClr = bg1;  // Non-data row has normal background colour
#endif
        }
        else if (++consec_count%2 == 1)
		{
            item.crBkClr = bg2;
#if _MSC_VER >= 1300
            itemw.crBkClr = bg2;
#endif
		}
        else
		{
            item.crBkClr = bg3;
#if _MSC_VER >= 1300
            itemw.crBkClr = bg3;
#endif
		}

        if (ii > 0)
        {
            for_count[pdoc->df_indent_[ii]] = 0;
            // Work out names of array elements if nec.
            if (pdoc->df_type_[ii] != CHexEditDoc::DF_FORF && pdoc->df_type_[ii] != CHexEditDoc::DF_FORV)
                for_name[pdoc->df_indent_[ii]] = CString("");
            else if (for_name[pdoc->df_indent_[ii]-1].IsEmpty())
                for_name[pdoc->df_indent_[ii]] = pdoc->df_elt_[ii].GetAttr("name");
            else
                for_name[pdoc->df_indent_[ii]].Format("%s[%d]", for_name[pdoc->df_indent_[ii]-1], for_count[pdoc->df_indent_[ii]-1]);

            if (!for_name[pdoc->df_indent_[ii]-1].IsEmpty())
                ++for_count[pdoc->df_indent_[ii]-1];
        }

        for (item.col = grid_.GetFixedColumnCount(); item.col < grid_.GetColumnCount(); ++item.col)
        {
            // Work out what we are displaying in this column
            int col_id = grid_.GetItemData(0, item.col);

            // Set defaults and values that are the same for all/most fields
			item.strText.Empty();
            item.nFormat = DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
            item.mask = GVIF_STATE|GVIF_FORMAT|GVIF_TEXT|GVIF_BKCLR|GVIF_FGCLR;
            item.crFgClr = psis_->GetDefaultTextCol();

            switch (col_id)
            {
            case COL_TREE:
#if _MSC_VER >= 1300
				itemw.nFormat = DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
                itemw.mask = GVIF_STATE|GVIF_FORMAT|GVIF_IMAGE|GVIF_TEXT;
				itemw.col = item.col;
#else
                item.mask = GVIF_STATE|GVIF_FORMAT|GVIF_IMAGE|GVIF_TEXT;
#endif
                if (!for_name[pdoc->df_indent_[ii]].IsEmpty())
                    item.strText = for_name[pdoc->df_indent_[ii]];
                else if (for_name[pdoc->df_indent_[ii]-1].IsEmpty())
                    item.strText = pdoc->df_elt_[ii].GetAttr("name");
                else
                    item.strText.Format("%s[%d]",
					                    for_name[pdoc->df_indent_[ii]-1],
										for_count[pdoc->df_indent_[ii]-1]-1 );
                ASSERT(ii == 0 || item.strText == get_name(ii));
#if _MSC_VER >= 1300
				// Init cell of tree column (InitTree) then expand tree to display the row if
				// - InitTree return true to indicate the element has some sort of problem
				// - address is -1 which indicates that the element is not present (eg past EOF, non-taken IF etc)
				// - AND if we are past the struct defns at the start (since they have address -1 ??)
				itemw.strText = item.strText;   // Copy string to wide string
				if ((InitTreeCol(ii, itemw) || pdoc->df_address_[ii] == -1) && past_defns)
                    show_row(ii);
#else
				if ((InitTreeCol(ii, item) || pdoc->df_address_[ii] == -1) && past_defns)
                    show_row(ii);
#endif
                break;

            case COL_HEX_ADDRESS:
                if (abs(pdoc->df_type_[ii]) != CHexEditDoc::DF_EVAL)
                {
                    grid_.FixHeading(item.col);
                    item.nFormat = DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
                    item.crFgClr = psis_->GetHexAddrCol();

                    // Use sprintf as CString::Format does not support 64 bit ints (%I64)
                    if (pdoc->df_address_[ii] == -1)
                        disp[0] = '\0';
                    else if (theApp.hex_ucase_)
                        sprintf(disp, "%I64X", __int64(pdoc->df_address_[ii]));
                    else
                        sprintf(disp, "%I64x", __int64(pdoc->df_address_[ii]));
                    item.strText = disp;
                    AddSpaces(item.strText);
                }
                break;

            case COL_DEC_ADDRESS:
                if (abs(pdoc->df_type_[ii]) != CHexEditDoc::DF_EVAL)
                {
                    grid_.FixHeading(item.col);
                    item.nFormat = DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
                    item.crFgClr = psis_->GetDecAddrCol();

                    // Use sprintf as CString::Format does not support 64 bit ints (%I64)
                    if (pdoc->df_address_[ii] == -1)
                        disp[0] = '\0';
                    else
                        sprintf(disp, "%I64d", __int64(pdoc->df_address_[ii]));
                    item.strText = disp;
                    AddCommas(item.strText);
                }
                break;
            case COL_SIZE:
                if (abs(pdoc->df_type_[ii]) >= CHexEditDoc::DF_BITFIELD8 && abs(pdoc->df_type_[ii]) <= CHexEditDoc::DF_BITFIELD64)
				{
                    item.nFormat = DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
					item.strText.Format("%d bits of %d byte(s)", int(pdoc->df_extra_[ii]>>8), abs(int(pdoc->df_size_[ii])));
				}
                else if (abs(pdoc->df_type_[ii]) != CHexEditDoc::DF_EVAL)
                {
                    item.nFormat = DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;

                    // Use sprintf as CString::Format does not support 64 bit ints (%I64)
                    if (pdoc->df_size_[ii] == 0)
                        disp[0] = '\0';
                    else if (pdoc->df_size_[ii] < 0)
                        sprintf(disp, "%I64d byte(s)", __int64(-pdoc->df_size_[ii]));
                    else
                        sprintf(disp, "%I64d byte(s)", __int64(pdoc->df_size_[ii]));
                    item.strText = disp;
                    AddCommas(item.strText);
                }
                break;

            case COL_TYPE:
                item.nFormat = DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
                if (abs(pdoc->df_type_[ii]) >= CHexEditDoc::DF_DATA)
                {
                    // Compose type name
                    switch (abs(pdoc->df_type_[ii]))
                    {
                    case CHexEditDoc::DF_NO_TYPE:
                        item.strText = "Unspecified";
                        break;
                    case CHexEditDoc::DF_CHARA:
                        item.strText = "ASCII character";
                        break;
                    case CHexEditDoc::DF_CHAR:  // no longer used
                        ASSERT(0);
						// fall through
                    case CHexEditDoc::DF_CHARN:
                        item.strText = "ANSI character";
                        break;
                    case CHexEditDoc::DF_CHARO:
                        item.strText = "IBM/OEM character";
                        break;
                    case CHexEditDoc::DF_CHARE:
                        item.strText = "EBCDIC character";
                        break;
                    case CHexEditDoc::DF_WCHAR:
                        item.strText = "Unicode character";
                        break;
                    case CHexEditDoc::DF_STRINGA:
                        item.strText = "ASCII string";
                        break;
                    case CHexEditDoc::DF_STRING:  // no longer used
                        ASSERT(0);
						// fall through
                    case CHexEditDoc::DF_STRINGN:
                        item.strText = "ANSI string";
                        break;
                    case CHexEditDoc::DF_STRINGO:
                        item.strText = "IBM/OEM string";
                        break;
                    case CHexEditDoc::DF_STRINGE:
                        item.strText = "EBCDIC string";
                        break;
                    case CHexEditDoc::DF_WSTRING:
                        item.strText = "Unicode string";
                        break;
                    case CHexEditDoc::DF_INT8:
                        item.strText = "Signed 8-bit integer";
                        break;
                    case CHexEditDoc::DF_INT16:
                        item.strText = "Signed 16-bit integer";
                        break;
                    case CHexEditDoc::DF_INT32:
                        item.strText = "Signed 32-bit integer";
                        break;
                    case CHexEditDoc::DF_INT64:
                        item.strText = "Signed 64-bit integer";
                        break;
                    case CHexEditDoc::DF_MINT8:
                        item.strText = "Sign+magnitude 8-bit integer";
                        break;
                    case CHexEditDoc::DF_MINT16:
                        item.strText = "Sign+magnitude 16-bit integer";
                        break;
                    case CHexEditDoc::DF_MINT32:
                        item.strText = "Sign+magnitude 32-bit integer";
                        break;
                    case CHexEditDoc::DF_MINT64:
                        item.strText = "Sign+magnitude 64-bit integer";
                        break;
                    case CHexEditDoc::DF_UINT8:
                        item.strText = "Unsigned 8-bit integer";
                        break;
                    case CHexEditDoc::DF_UINT16:
                        item.strText = "Unsigned 16-bit integer";
                        break;
                    case CHexEditDoc::DF_UINT32:
                        item.strText = "Unsigned 32-bit integer";
                        break;
                    case CHexEditDoc::DF_UINT64:
                        item.strText = "Unsigned 64-bit integer";
                        break;
                    case CHexEditDoc::DF_BITFIELD8:
                    case CHexEditDoc::DF_BITFIELD16:
                    case CHexEditDoc::DF_BITFIELD32:
                    case CHexEditDoc::DF_BITFIELD64:
						item.strText.Format("%d-bit bit-field", int(pdoc->df_extra_[ii]>>8));
                        break;
                    case CHexEditDoc::DF_REAL32:
                        item.strText = "IEEE 32-bit real";
                        break;
                    case CHexEditDoc::DF_REAL64:
                        item.strText = "IEEE 64-bit real";
                        break;
                    case CHexEditDoc::DF_IBMREAL32:
                        item.strText = "IBM 32-bit real";
                        break;
                    case CHexEditDoc::DF_IBMREAL64:
                        item.strText = "IBM 64-bit real";
                        break;
                    case CHexEditDoc::DF_DATEC:
                        item.strText = "Common time_t";
                        break;
                    case CHexEditDoc::DF_DATECMIN:
                        item.strText = "time_t (mins since 1/1/1970)";
                        break;
                    case CHexEditDoc::DF_DATEC51:
                        item.strText = "MSC 5.1 time_t (secs since 1/1/1980)";
                        break;
                    case CHexEditDoc::DF_DATEC7:
                        item.strText = "MSC 7 time_t (secs since 31/12/1899)";
                        break;
                    case CHexEditDoc::DF_DATEOLE:
                        item.strText = "OLE DATE (IEEE 64 bit float)";
                        break;
                    case CHexEditDoc::DF_DATESYSTEMTIME:
                        item.strText = "SYSTEMTIME structure";
                        break;
                    case CHexEditDoc::DF_DATEFILETIME:
                        item.strText = "FILETIME (nSecs since 1/1/1601)";
                        break;
                    case CHexEditDoc::DF_DATEMSDOS:
                        item.strText = "MSDOS file system date/time";
                        break;
                    case CHexEditDoc::DF_DATEC64:
                        item.strText = "64bit C/C++ time (time64_t)";
                        break;
                    }

                    // Add info about endianness
                    switch (abs(pdoc->df_type_[ii]))
                    {
                    case CHexEditDoc::DF_WCHAR:
                    case CHexEditDoc::DF_WSTRING:
                    case CHexEditDoc::DF_INT16:
                    case CHexEditDoc::DF_INT32:
                    case CHexEditDoc::DF_INT64:
                    case CHexEditDoc::DF_MINT16:
                    case CHexEditDoc::DF_MINT32:
                    case CHexEditDoc::DF_MINT64:
                    case CHexEditDoc::DF_UINT16:
                    case CHexEditDoc::DF_UINT32:
                    case CHexEditDoc::DF_UINT64:
                    case CHexEditDoc::DF_BITFIELD16:
                    case CHexEditDoc::DF_BITFIELD32:
                    case CHexEditDoc::DF_BITFIELD64:
                    case CHexEditDoc::DF_REAL32:
                    case CHexEditDoc::DF_REAL64:
                        if (pdoc->df_type_[ii] < 0)
                            item.strText += " big-endian";
                        break;
                    case CHexEditDoc::DF_IBMREAL32:
                    case CHexEditDoc::DF_IBMREAL64:
                        if (pdoc->df_type_[ii] > 0)
                            item.strText += " little-endian";  // Only show the unusual case (little-endian)
                        break;
                    case CHexEditDoc::DF_DATEC:
                    case CHexEditDoc::DF_DATECMIN:
                    case CHexEditDoc::DF_DATEC51:
                    case CHexEditDoc::DF_DATEC7:
                    case CHexEditDoc::DF_DATEOLE:
                    case CHexEditDoc::DF_DATESYSTEMTIME:
                    case CHexEditDoc::DF_DATEFILETIME:
                    case CHexEditDoc::DF_DATEC64:
                        if (pdoc->df_type_[ii] < 0)
                            item.strText += " big-endian";
                        break;
                    case CHexEditDoc::DF_DATEMSDOS:
                        ASSERT(pdoc->df_type_[ii] > 0);
                        break;
                    }
                }
                break;

            case COL_TYPE_NAME:
                item.nFormat = DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
                if (abs(pdoc->df_type_[ii]) >= CHexEditDoc::DF_DATA || 
                    pdoc->df_type_[ii] == CHexEditDoc::DF_STRUCT ||
                    pdoc->df_type_[ii] == CHexEditDoc::DF_DEFINE_STRUCT ||
                    pdoc->df_type_[ii] == CHexEditDoc::DF_USE_STRUCT ||
                    pdoc->df_type_[ii] == CHexEditDoc::DF_UNION ||
                    pdoc->df_type_[ii] == CHexEditDoc::DF_FORF ||
                    pdoc->df_type_[ii] == CHexEditDoc::DF_FORV   )
                {
                    item.strText = pdoc->df_elt_[ii].GetAttr("type_name");
                }
                break;

            case COL_DATA:
                // Set up the display string
#if _MSC_VER >= 1300
			    itemw.mask = GVIF_STATE|GVIF_FORMAT|GVIF_TEXT|GVIF_BKCLR|GVIF_FGCLR;
				itemw.strText.Empty();
				itemw.col = item.col;
			    itemw.nFormat = DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
#endif
                if (abs(pdoc->df_type_[ii]) >= CHexEditDoc::DF_DATA)
				{
#if _MSC_VER >= 1300
					InitDataCol(ii, itemw);
#else
					InitDataCol(ii, item);
#endif
				}

                /*{
                    item.nFormat = DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
                    item.strText = GetDataString(ii, pdoc->df_elt_[ii].GetAttr("display"));
                }*/
                break;

            case COL_UNITS:
                if (abs(pdoc->df_type_[ii]) >= CHexEditDoc::DF_DATA)
                    item.strText = pdoc->df_elt_[ii].GetAttr("units");
                break;

            case COL_DOMAIN:
                item.nFormat = DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
                if (abs(pdoc->df_type_[ii]) >= CHexEditDoc::DF_DATA)
                    item.strText = pdoc->df_elt_[ii].GetAttr("domain");
                break;

            case COL_FLAGS:
                item.nFormat = DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS;
                // xxx display flags such as read-only
                break;

            case COL_COMMENT:
                if (pdoc->df_type_[ii] == CHexEditDoc::DF_EXTRA)
                    item.strText = "Bytes past where the end of file was expected.";
                else if (pdoc->df_type_[ii] == CHexEditDoc::DF_EXTRA)
                    item.strText = "Extra (fixed size) FOR elements not evaluated.";
                else
                    item.strText = pdoc->df_elt_[ii].GetAttr("comment");
                break;

			case COL_LEVEL:
				item.strText.Format("%d", pdoc->df_indent_[ii]);
				break;

            default:
                ASSERT(0);              // Unknown column type??!
            }

#if _MSC_VER >= 1300
			if (col_id == COL_TREE || col_id == COL_DATA)
				grid_.SetItem(&itemw);
			else
				grid_.SetItem(&item);
#else
            grid_.SetItem(&item);
#endif
        }

        // Update scan progress no more than once a second
        if (double(clock() - last_checked)/CLOCKS_PER_SEC > 1)
        {
            mm->m_wndStatusBar.SetPaneProgress(0, long((ii*100)/pdoc->df_type_.size()));
            last_checked = clock();
        }
    }

    mm->m_wndStatusBar.EnablePaneProgressBar(0, -1);

    // Set default cell height back to what it was before we set it to zero (above).
    pdefault_cell->SetHeight(default_height_);

    // This just display the top level folder row (& perhaps extra data past expected EOF row)
#if 0
    tree_col_.TreeDisplayOutline(1);
#else
    tree_col_.TreeRefreshRows();
#endif
    //    tree_col_.SetTreeLineColor(RGB(0x80, 0x80, 0x80) );

    tree_init_ = true;
}

// xxx this does not seem to be used???
COleDateTime CDataFormatView::GetDate(int ii)
{
    CHexEditDoc *pdoc = GetDocument();

    COleDateTime retval;
    retval.SetStatus(COleDateTime::invalid); 

    // Make sure its a date
    ASSERT(abs(pdoc->df_type_[ii]) >= CHexEditDoc::DF_DATEC && 
           abs(pdoc->df_type_[ii]) < CHexEditDoc::DF_LAST_DATE);

    time_t tt;                          // Current time field in compiler format
    FILETIME ft;                        // Used in conversion of MSDOS date/time

    unsigned char buf[16];                  // Holds actual binary data for field from file
    FILE_ADDRESS tmp = pdoc->df_size_[ii];  // Create tmp to avoid compiler bug (VC6)
    size_t df_size = min((size_t)mac_abs(tmp), sizeof(buf));    // No of bytes used for display of data element

    if (pdoc->df_address_[ii] != -1 &&
        pdoc->GetData(buf, df_size, pdoc->df_address_[ii]) == df_size)
    {
        if (pdoc->df_type_[ii] < 0)
        {
			// Big-endian
            ASSERT(mac_abs(pdoc->df_size_[ii]) <= sizeof(buf));
			if (mac_abs(pdoc->df_type_[ii]) == CHexEditDoc::DF_DATESYSTEMTIME)
			{
				ASSERT(df_size == 16);
				flip_each_word(buf, df_size); // Reverse bytes of each field
			}
			else
				flip_bytes(buf, df_size);  // Convert big-endian to little-endian for display
        }
        switch (abs(pdoc->df_type_[ii]))
        {
        case CHexEditDoc::DF_DATEC:
            retval = COleDateTime(*((time_t *)buf));
            break;
        case CHexEditDoc::DF_DATECMIN:
            tt = *((long *)buf)*60;
            retval = COleDateTime(tt);
            break;
        case CHexEditDoc::DF_DATEC51:
            tt = *((long *)buf) + (365*10 + 2)*24L*60L*60L;
            retval = COleDateTime(tt);
            break;
        case CHexEditDoc::DF_DATEC7:
            tt = *((unsigned long *)buf) - (365*70 + 17 + 2)*24UL*60UL*60UL;
            retval = COleDateTime(tt);
            break;
        case CHexEditDoc::DF_DATEOLE:
            retval = COleDateTime(*((DATE *)buf));
            break;
        case CHexEditDoc::DF_DATESYSTEMTIME:
            retval = COleDateTime(*((SYSTEMTIME *)buf));
            break;
        case CHexEditDoc::DF_DATEFILETIME:
            retval = COleDateTime(*((FILETIME *)buf));
            break;
        case CHexEditDoc::DF_DATEMSDOS:
            if (DosDateTimeToFileTime(*(LPWORD(buf+2)), *(LPWORD(buf)), &ft))
                retval = ft;
            break;
        default:
            ASSERT(0);
        }
    }
    return retval;
}


// InitTreeCol - ii is current data element being processed, item is used to format grid cell
// - returns true to indicate that the tree should be expanded to show this element (ie an error)
#if _MSC_VER >= 1300
bool CDataFormatView::InitTreeCol(int ii, GV_ITEMW & item)
#else
bool CDataFormatView::InitTreeCol(int ii, GV_ITEM & item)
#endif
{
    CHexEditDoc *pdoc = GetDocument();
    switch (abs(pdoc->df_type_[ii]))
    {
    case CHexEditDoc::DF_FILE:
        if (pdoc->df_size_[ii] == 0 || pdoc->df_address_[ii] == -1)
            item.iImage = IMAGE_FILE_GREY;
        else
            item.iImage = IMAGE_FILE;
        item.strText += ExprStringType("  ") + pdoc->df_info_[ii];
        break;
    case CHexEditDoc::DF_JUMP:
        if (pdoc->df_address_[ii] == -1)
            item.iImage = IMAGE_JUMP_GREY;
        else
            item.iImage = IMAGE_JUMP;
        item.strText = pdoc->df_info_[ii];
        break;

    case CHexEditDoc::DF_EVAL:
        if (pdoc->df_address_[ii] == -1)
            item.iImage = IMAGE_EVAL_GREY;
        else
            item.iImage = IMAGE_EVAL;
        item.strText = pdoc->df_info_[ii];
        break;

    case CHexEditDoc::DF_DEFINE_STRUCT:
        item.iImage = IMAGE_DEFINE_STRUCT;
        item.strText = pdoc->df_elt_[ii].GetAttr("type_name");
        break;

    case CHexEditDoc::DF_USE_STRUCT:
    case CHexEditDoc::DF_STRUCT:
        if (pdoc->df_size_[ii] == 0 || pdoc->df_address_[ii] == -1)
            item.iImage = IMAGE_STRUCT_GREY;
        else
            item.iImage = IMAGE_STRUCT;
        item.strText += ExprStringType("  ") + pdoc->df_info_[ii];
        break;

    case CHexEditDoc::DF_FORF:
    case CHexEditDoc::DF_FORV:
        if (pdoc->df_size_[ii] == 0 || pdoc->df_address_[ii] == -1)
            item.iImage = IMAGE_FOR_GREY;
        else
            item.iImage = IMAGE_FOR;
        item.strText += ExprStringType("s");
        break;
    case CHexEditDoc::DF_MORE:
        if (pdoc->df_size_[ii] == 0 || pdoc->df_address_[ii] == -1)
            item.iImage = IMAGE_MORE_GREY;
        else
            item.iImage = IMAGE_MORE;
        item.strText.Format(ExprStringType(" [%ld] more"), long(pdoc->df_extra_[ii]));
        break;

    case CHexEditDoc::DF_IF:
        {
            // xxx we need a recursive routine that checks IF and ELSE branches???
            //int jj;
            //for (jj = ii + 1; jj < (int)pdoc->df_type_.size() && pdoc->df_type_[jj] == CHexEditDoc::DF_IF; ++jj)
            //    ;  // null statement

            //ASSERT(jj < (int)pdoc->df_type_.size());
            //if (jj < (int)pdoc->df_type_.size())
            //{
            //    item.strText = pdoc->df_elt_[jj].GetAttr("name");
            //    if (pdoc->df_type_[jj] == CHexEditDoc::DF_FORF || pdoc->df_type_[jj] == CHexEditDoc::DF_FORV)
            //        item.strText += ExprStringType("s");
            //}
        }
        if (pdoc->df_size_[ii] == 0 || pdoc->df_address_[ii] == -1)
            item.iImage = IMAGE_IF_GREY;
        else
            item.iImage = IMAGE_IF;
        break;

    case CHexEditDoc::DF_SWITCH:
        if (pdoc->df_size_[ii] == 0 || pdoc->df_address_[ii] == -1)
            item.iImage = IMAGE_SWITCH_GREY;
        else
            item.iImage = IMAGE_SWITCH;
        break;

    case CHexEditDoc::DF_NO_TYPE:
        if (pdoc->df_size_[ii] == 0 || pdoc->df_address_[ii] == -1)
            item.iImage = IMAGE_NO_TYPE_GREY;
        else
            item.iImage = IMAGE_NO_TYPE;
        break;

    case CHexEditDoc::DF_EXTRA:
        item.iImage = IMAGE_EXTRA;
        item.strText = ExprStringType(" ");
        break;

    default:
        if (pdoc->df_address_[ii] == -1 || pdoc->df_size_[ii] < 0)
        {
            item.iImage = IMAGE_DATA_GREY;
            // Return true to indicate to show this row
			return true;
        }
        else if (abs(pdoc->df_type_[ii]) < CHexEditDoc::DF_CHAR)
            item.iImage = IMAGE_DATA_STRING;
        else if (abs(pdoc->df_type_[ii]) >= CHexEditDoc::DF_REAL32)
            item.iImage = IMAGE_DATA_REAL;
        else if (abs(pdoc->df_type_[ii]) >= CHexEditDoc::DF_DATEC)
            item.iImage = IMAGE_DATA_DATE;
        else
            item.iImage = IMAGE_DATA;
        break;
    }
	return false;
}

#if _MSC_VER >= 1300
void CDataFormatView::InitDataCol(int ii, GV_ITEMW & item)
#else
void CDataFormatView::InitDataCol(int ii, GV_ITEM & item)
#endif
{
    CHexEditDoc *pdoc = GetDocument();
    signed char df_type = abs(pdoc->df_type_[ii]);      // Data types may be -ve to indicate big endian
    bool big_endian = pdoc->df_type_[ii] < 0;
	CString strFormat = pdoc->df_elt_[ii].GetAttr("display");

    char disp[128];                     // Holds output of sprintf
    unsigned char buf[128];             // Temp to hold actual binary data for field from file
    unsigned char *pp;                  // Used for search for string terminator
#if _MSC_VER >= 1300
	wchar_t *pwc;                       // Used for terminator search in wide string
#endif
    double real_val;                    // Value of any floating point number displayed

    time_t tt;                          // Current time field in compiler format
    COleDateTime odt;                   // Used in conversion of some date types
    struct tm *timeptr;                 // Returned from localtime() [used with time fields]
    FILETIME ft;                        // Used in conversion of MSDOS date/time

    // If no format specifed for this field get a default format
    if (strFormat.IsEmpty())
    {
        switch (df_type)
        {
        case CHexEditDoc::DF_CHAR:  // no longer used
            ASSERT(0);
			// fall through
        case CHexEditDoc::DF_CHARA:
        case CHexEditDoc::DF_CHARN:
        case CHexEditDoc::DF_CHARO:
        case CHexEditDoc::DF_CHARE:
        case CHexEditDoc::DF_WCHAR:                  // converted to wide string later
            strFormat = theApp.default_char_format_;
            break;
        case CHexEditDoc::DF_STRING:
            ASSERT(0);
			// fall through
        case CHexEditDoc::DF_STRINGA:
        case CHexEditDoc::DF_STRINGN:
        case CHexEditDoc::DF_STRINGO:
        case CHexEditDoc::DF_STRINGE:
        case CHexEditDoc::DF_WSTRING:            // was "%S" but now use std format string converted to wide string (below)
            strFormat = theApp.default_string_format_;
            break;
        case CHexEditDoc::DF_INT8:
        case CHexEditDoc::DF_INT16:
        case CHexEditDoc::DF_INT32:
        case CHexEditDoc::DF_INT64:
        case CHexEditDoc::DF_MINT8:
        case CHexEditDoc::DF_MINT16:
        case CHexEditDoc::DF_MINT32:
        case CHexEditDoc::DF_MINT64:
            strFormat = theApp.default_int_format_;
            break;
        case CHexEditDoc::DF_UINT8:
        case CHexEditDoc::DF_UINT16:
        case CHexEditDoc::DF_UINT32:
        case CHexEditDoc::DF_UINT64:
        case CHexEditDoc::DF_BITFIELD8:
        case CHexEditDoc::DF_BITFIELD16:
        case CHexEditDoc::DF_BITFIELD32:
        case CHexEditDoc::DF_BITFIELD64:
            strFormat = theApp.default_unsigned_format_;
            break;
        case CHexEditDoc::DF_REAL32:
        case CHexEditDoc::DF_REAL64:
        case CHexEditDoc::DF_IBMREAL32:
        case CHexEditDoc::DF_IBMREAL64:
            strFormat = theApp.default_real_format_;
            break;
        case CHexEditDoc::DF_DATEC:
        case CHexEditDoc::DF_DATECMIN:
        case CHexEditDoc::DF_DATEC51:
        case CHexEditDoc::DF_DATEC7:
        case CHexEditDoc::DF_DATEOLE:
        case CHexEditDoc::DF_DATESYSTEMTIME:
        case CHexEditDoc::DF_DATEFILETIME:
        case CHexEditDoc::DF_DATEMSDOS:
        case CHexEditDoc::DF_DATEC64:
            strFormat = theApp.default_date_format_;
            break;
        case CHexEditDoc::DF_NO_TYPE:
            break;
        default:
            ASSERT(0);
            break;
        }
    }

//    size_t df_size = min((size_t)mac_abs(pdoc->df_size_[ii]), sizeof(buf));  // This causes crash in release build (but worked without the size_t cast)

    FILE_ADDRESS tmp = pdoc->df_size_[ii];  // Create tmp to avoid compiler bug (VC6)
    size_t df_size = min((size_t)mac_abs(tmp), sizeof(buf));    // No of bytes used for display of data element

	// Check CHAR and INT types for enum
    if (df_type >= CHexEditDoc::DF_CHAR && df_type < CHexEditDoc::DF_LAST_INT)
    {
        // Check if enum string is specified in domain attribute
        CString domain_str = pdoc->df_elt_[ii].GetAttr("domain");

        if (!domain_str.IsEmpty() && domain_str[0] == '{' &&
            pdoc->df_address_[ii] != -1 &&
            pdoc->GetData(buf, df_size, pdoc->df_address_[ii]) == df_size)
        {
            if (big_endian)
            {
                ASSERT(mac_abs(pdoc->df_size_[ii]) <= sizeof(buf));
                flip_bytes(buf, df_size);  // Convert big-endian to little-endian before checking enum values
            }

            // Get value of data
            __int64 val;

            switch (df_type)
            {
			case CHexEditDoc::DF_CHAR:  // no longer used
				ASSERT(0);
				// fall through
			case CHexEditDoc::DF_CHARA:
			case CHexEditDoc::DF_CHARN:
			case CHexEditDoc::DF_CHARO:
			case CHexEditDoc::DF_CHARE:
                val = *buf;
                break;
            case CHexEditDoc::DF_WCHAR:
                val = *((short *)buf);
                break;

            case CHexEditDoc::DF_INT8:
                val = *((signed char *)buf);
                break;
            case CHexEditDoc::DF_INT16:
                val = *((short *)buf);
                break;
            case CHexEditDoc::DF_INT32:
                val = *((long *)buf);
                break;
            case CHexEditDoc::DF_INT64:
                val = *((__int64 *)buf);
                break;

            case CHexEditDoc::DF_MINT8:
                if ((val = *((signed char *)buf)) < 0)
                    val = -(val&0x7F);
                break;
            case CHexEditDoc::DF_MINT16:
                if ((val = *((short *)buf)) < 0)
                    val = -(val&0x7FFF);
                break;
            case CHexEditDoc::DF_MINT32:
                if ((val = *((long *)buf)) < 0)
                    val = -(val&0x7fffFFFFL);
                break;
            case CHexEditDoc::DF_MINT64:
                if ((val = *((__int64 *)buf)) < 0)
                    val = -(val&0x7fffFFFFffffFFFFi64);
                break;

            case CHexEditDoc::DF_UINT8:
                val = *((unsigned char *)buf);
                break;
            case CHexEditDoc::DF_UINT16:
                val = *((unsigned short *)buf);
                break;
            case CHexEditDoc::DF_UINT32:
                val = *((unsigned long *)buf);
                break;
            case CHexEditDoc::DF_UINT64:
                val = *((unsigned __int64 *)buf);
                break;

				// xxx bitfields do not handle straddle yet
            case CHexEditDoc::DF_BITFIELD8:
				ASSERT(signed char(pdoc->df_extra_[ii]) >= 0 && (pdoc->df_extra_[ii]&0xFF)+(pdoc->df_extra_[ii]>>8) <= 8);
                val = (*((unsigned char *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1);
                break;
            case CHexEditDoc::DF_BITFIELD16:
				ASSERT(signed char(pdoc->df_extra_[ii]) >= 0 && (pdoc->df_extra_[ii]&0xFF)+(pdoc->df_extra_[ii]>>8) <= 16);
                val = (*((unsigned short *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1);
                break;
            case CHexEditDoc::DF_BITFIELD32:
				ASSERT(signed char(pdoc->df_extra_[ii]) >= 0 && (pdoc->df_extra_[ii]&0xFF)+(pdoc->df_extra_[ii]>>8) <= 32);
                val = (*((unsigned long *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1);
                break;
            case CHexEditDoc::DF_BITFIELD64:
				ASSERT(signed char(pdoc->df_extra_[ii]) >= 0 && (pdoc->df_extra_[ii]&0xFF)+(pdoc->df_extra_[ii]>>8) <= 64);
                val = (*((unsigned __int64 *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1);
                break;
            }

            CHexEditDoc::enum_t &ev = pdoc->get_enum(pdoc->df_elt_[ii]);
            CHexEditDoc::enum_t::const_iterator pe = ev.find(val);
            if (pe != ev.end())
			{
                item.strText = pe->second;
				return;
			}
			// We failed to find a value in the enum list so just continue below and display the number.
			// Note that there is no need to "unflip" big-endian fields as they are read anew again below.
        }
    }

    if (df_type >= CHexEditDoc::DF_DATA &&
        pdoc->df_address_[ii] != -1 &&
        pdoc->GetData(buf, df_size, pdoc->df_address_[ii]) == df_size)
    {
        if (big_endian && df_type == CHexEditDoc::DF_WSTRING)
        {
            ASSERT(df_size%2 == 0);
			flip_each_word(buf, df_size);  // Reverse bytes of each character
        }
        else if (big_endian && df_type == CHexEditDoc::DF_DATESYSTEMTIME)
        {
            ASSERT(df_size == 16);
			flip_each_word(buf, df_size);  // Reverse bytes of each field
        }
		else if (big_endian)
            flip_bytes(buf, df_size);  // Convert big-endian to little-endian for display

        long val32;
        __int64 val64;
		CString ss;

        // Create display string
        switch (df_type)
        {
        case CHexEditDoc::DF_NO_TYPE:
            ASSERT(MAX_HEX_FORMAT <= 16);  // Make sure we don't try to display more byte than are passed to Format()
            ss.Format(hex_format(df_size),
                                buf[0], buf[1], buf[2], buf[3],
                                buf[4], buf[5], buf[6], buf[7],
                                buf[8], buf[9], buf[10], buf[11],
                                buf[12], buf[13], buf[14], buf[15]);
            break;

        case CHexEditDoc::DF_CHAR:  // no longer used
            ASSERT(0);
			// fall through
        case CHexEditDoc::DF_CHARA:
        case CHexEditDoc::DF_CHARO:   // xxx needs to change font of text control
            if (*buf < 32 || *buf > 126)
                ss.Format("%ld", long(*buf));
            else
                ss.Format(strFormat, *buf);
            break;
        case CHexEditDoc::DF_CHARN:
            if (isprint(*buf))
                ss.Format(strFormat, *buf);
            else
                ss.Format("%ld", long(*buf));
            break;
        case CHexEditDoc::DF_CHARE:
            if (e2a_tab[*buf] != '\0')
                ss.Format(strFormat, e2a_tab[*buf]);
            else
                ss.Format("%ld", long(*buf));
            break;
        case CHexEditDoc::DF_WCHAR:
#if _MSC_VER >= 1300
            if (iswprint(*(short *)buf))
				item.strText.Format(CStringW(strFormat), *((short *)buf));
            else
                item.strText.Format(L"%ld", long(*buf));
#else
            if (isprint(*buf))
				item.strText.Format(strFormat, *((short *)buf));
            else
                item.strText.Format("%ld", long(*buf));
#endif
            break;

        case CHexEditDoc::DF_STRING:
            ASSERT(0);
			// fall through
        case CHexEditDoc::DF_STRINGA:
        case CHexEditDoc::DF_STRINGO:  // xxx Change the font of the cell here
        case CHexEditDoc::DF_STRINGN:
            pp = (unsigned char *)memchr(buf, pdoc->df_extra_[ii], df_size);
            if (mac_abs(pdoc->df_size_[ii]) > sizeof(buf) && pp == NULL)
            {
                ASSERT(df_size == sizeof(buf));
                // String is longer than buffer show what we have plus ...
                ss.Format(strFormat, CString((char *)buf, sizeof(buf))+"...");
            }
            else if (df_size == sizeof(buf) && pp == NULL)
            {
                ASSERT(mac_abs(pdoc->df_size_[ii]) == sizeof(buf));
                // String is same length as buffer - no terminator
                ss.Format(strFormat, CString((char *)buf, sizeof(buf)));
            }
            else if (pp == NULL)
            {
                ASSERT(df_size < sizeof(buf));
                // No terminator - so add terminator at field size
                buf[df_size] = '\0';
                ss.Format(strFormat, buf);
            }
            else
            {
                // Terminator found - just use string up to it
                *pp = '\0';
                ss.Format(strFormat, buf);
            }
            break;
        case CHexEditDoc::DF_STRINGE:
            {
                // Find end of string to display
                pp = (unsigned char *)memchr(buf, pdoc->df_extra_[ii], df_size);
                CString extra;
                if (pp == NULL)
                {
                    pp = buf + df_size;
                    if (mac_abs(pdoc->df_size_[ii]) > df_size)
                        extra = CString("...");
                }

                // Convert buf to EBCDIC removing invalid characters
                unsigned char buf2[sizeof(buf)+1];
                unsigned char *po = buf2;
                for (const unsigned char *ptmp = buf; ptmp < pp; ++ptmp)
                    if (e2a_tab[*ptmp] != '\0')
                        *po++ = e2a_tab[*ptmp];
                *po++ = '\0';

                ss.Format(strFormat, buf2 + extra);
            }
            break;
        case CHexEditDoc::DF_WSTRING:
#if _MSC_VER >= 1300
			ASSERT(df_size%2 == 0); // size must be even for a Unicode string
            pwc = wmemchr((wchar_t *)buf, pdoc->df_extra_[ii], df_size/2);
            if (mac_abs(pdoc->df_size_[ii]) > sizeof(buf) && pwc == NULL)   // Just take the first bit of the string
			{
                item.strText.Format(CStringW(strFormat), CStringW((wchar_t *)buf, sizeof(buf)/2) + CStringW(L"..."));
			}
            else if (df_size == sizeof(buf) && pwc == NULL)
            {
                ASSERT(mac_abs(pdoc->df_size_[ii]) == sizeof(buf));
                // String is same length as buffer - no terminator
                item.strText.Format(CStringW(strFormat), CStringW((wchar_t *)buf, sizeof(buf)/2));
            }
            else if (pwc == NULL)
            {
                ASSERT(df_size < sizeof(buf));
                // No terminator - so add terminator at field size
                ((wchar_t *)buf)[df_size/2] = L'\0';
                item.strText.Format(CStringW(strFormat), buf);
            }
			else
			{
				*pwc = L'\0';
                item.strText.Format(CStringW(strFormat), (wchar_t *)buf);
			}
#else
			// Make sure string is terminated
			buf[sizeof(buf)-1] = buf[sizeof(buf)-2] = '\0';
            item.strText.Format("%S", *((short *)buf));
#endif
			break;
 
        case CHexEditDoc::DF_INT8:
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    ss.Format("%2.2lX", long(*(signed char *)(buf)));
                else
                    ss.Format("%2.2lx", long(*(signed char *)(buf)));
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                ss.Format("%lo", long(*(signed char *)(buf)));
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(signed char *)(buf), 8);
            }
            else if (strFormat.Find('%') == -1)
            {
                ss.Format("%ld", long(*(signed char *)(buf)));
                AddCommas(ss);
            }
            else
                ss.Format(strFormat, long(*(signed char *)(buf)));
            break;
        case CHexEditDoc::DF_INT16:
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    ss.Format("%4.4lX", long(*(short *)(buf)));
                else
                    ss.Format("%4.4lx", long(*(short *)(buf)));
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                ss.Format("%lo", long(*(short *)(buf)));
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(short *)(buf), 16);
            }
            else if (strFormat.Find('%') == -1)
            {
                ss.Format("%ld", long(*(short *)(buf)));
                AddCommas(ss);
            }
            else
                ss.Format(strFormat, long(*(short *)(buf)));
            break;
        case CHexEditDoc::DF_INT32:
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    ss.Format("%8.8lX", long(*(long *)(buf)));
                else
                    ss.Format("%lx", long(*(long *)(buf)));
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                ss.Format("%lo", long(*(long *)(buf)));
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(long *)(buf), 32);
            }
            else if (strFormat.Find('%') == -1)
            {
                ss.Format("%ld", long(*(long *)(buf)));
                AddCommas(ss);
            }
            else
                ss.Format(strFormat, long(*(long *)(buf)));
            break;
        case CHexEditDoc::DF_INT64:
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    sprintf(disp, "%16.16I64X", (*(__int64 *)(buf)));
                else
                    sprintf(disp, "%16.16I64x", (*(__int64 *)(buf)));
                ss = disp;
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                sprintf(disp, "%I64o", (*(__int64 *)(buf)));
                ss = disp;
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(__int64 *)(buf), 64);
            }
            else if (strFormat.Find('%') == -1)
            {
                sprintf(disp, "%I64d", (*(__int64 *)(buf)));
                ss = disp;
                AddCommas(ss);
            }
            else
            {
                if (strchr("diouxX", *(const char *)strFormat.Right(1)) != NULL)
                    strFormat.Insert(strFormat.GetLength()-1, "I64");
                sprintf(disp, strFormat, (*(__int64 *)(buf)));
                ss = disp;
            }
            break;

        case CHexEditDoc::DF_MINT8:
            if ((val32 = long(*(signed char *)(buf))) < 0)
                val32 = -(val32&0x7F);
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    ss.Format("%2.2lX", val32);
                else
                    ss.Format("%2.2lx", val32);
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                ss.Format("%lo", val32);
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(signed char *)(buf), 8);
            }
            else if (strFormat.Find('%') == -1)
            {
                ss.Format("%ld", val32);
                AddCommas(ss);
            }
            else
                ss.Format(strFormat, val32);
            break;
        case CHexEditDoc::DF_MINT16:
            if ((val32 = long(*(short *)(buf))) < 0)
                val32 = -(val32&0x7FFF);
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    ss.Format("%4.4lX", val32);
                else
                    ss.Format("%4.4lx", val32);
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                ss.Format("%lo", val32);
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(short *)(buf), 16);
            }
            else if (strFormat.Find('%') == -1)
            {
                ss.Format("%ld", val32);
                AddCommas(ss);
            }
            else
                ss.Format(strFormat, val32);
            break;
        case CHexEditDoc::DF_MINT32:
            if ((val32 = *(long *)(buf)) < 0)
                val32 = -(val32&0x7fffFFFFL);
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    ss.Format("%8.8lX", val32);
                else
                    ss.Format("%lx", val32);
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                ss.Format("%lo", val32);
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(long *)(buf), 32);
            }
            else if (strFormat.Find('%') == -1)
            {
                ss.Format("%ld", val32);
                AddCommas(ss);
            }
            else
                ss.Format(strFormat, val32);
            break;
        case CHexEditDoc::DF_MINT64:
            if ((val64 = (*(__int64 *)(buf))) < 0)
                val64 = -(val64&0x7fffFFFFffffFFFFi64);
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    sprintf(disp, "%16.16I64X", val64);
                else
                    sprintf(disp, "%16.16I64x", val64);
                ss = disp;
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                sprintf(disp, "%I64o", val64);
                ss = disp;
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(__int64 *)(buf), 64);
            }
            else if (strFormat.Find('%') == -1)
            {
                sprintf(disp, "%I64d", val64);
                ss = disp;
                AddCommas(ss);
            }
            else
            {
                if (strchr("diouxX", *(const char *)strFormat.Right(1)) != NULL)
                    strFormat.Insert(strFormat.GetLength()-1, "I64");
                sprintf(disp, strFormat, val64);
                ss = disp;
            }
            break;

        case CHexEditDoc::DF_UINT8:
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    ss.Format("%2.2lX", long(*(unsigned char *)(buf)));
                else
                    ss.Format("%2.2lx", long(*(unsigned char *)(buf)));
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                ss.Format("%lo", long(*(unsigned char *)(buf)));
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(unsigned char *)(buf), 8);
            }
            else if (strFormat.Find('%') == -1)
            {
                ss.Format("%lu", long(*(unsigned char *)(buf)));
                AddCommas(ss);
            }
            else
                ss.Format(strFormat, long(*(unsigned char *)(buf)));
            break;
        case CHexEditDoc::DF_UINT16:
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    ss.Format("%4.4lX", long(*(unsigned short *)(buf)));
                else
                    ss.Format("%4.4lx", long(*(unsigned short *)(buf)));
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                ss.Format("%lo", long(*(unsigned short *)(buf)));
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(unsigned short *)(buf), 16);
            }
            else if (strFormat.Find('%') == -1)
            {
                ss.Format("%lu", long(*(unsigned short *)(buf)));
                AddCommas(ss);
            }
            else
                ss.Format(strFormat, long(*(unsigned short *)(buf)));
            break;

        case CHexEditDoc::DF_UINT32:
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    ss.Format("%8.8lX", long(*(unsigned long *)(buf)));
                else
                    ss.Format("%8.8lx", long(*(unsigned long *)(buf)));
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                ss.Format("%lo", long(*(unsigned long *)(buf)));
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(unsigned long *)(buf), 32);
            }
            else if (strFormat.Find('%') == -1)
            {
                ss.Format("%lu", long(*(unsigned long *)(buf)));
                AddCommas(ss);
            }
            else
                ss.Format(strFormat, long(*(unsigned long *)(buf)));
            break;
        case CHexEditDoc::DF_UINT64:
            if (strFormat.Left(3).CompareNoCase("hex") == 0)
            {
                if (theApp.hex_ucase_)
                    sprintf(disp, "%16.16I64X", (*(unsigned __int64 *)(buf)));
                else
                    sprintf(disp, "%16.16I64x", (*(unsigned __int64 *)(buf)));
                ss = disp;
                AddSpaces(ss);
            }
            else if (strFormat.Left(3).CompareNoCase("oct") == 0)
            {
                sprintf(disp, "%I64o", (*(unsigned __int64 *)(buf)));
                ss = disp;
            }
            else if (strFormat.Left(3).CompareNoCase("bin") == 0)
            {
                ss = bin_str(*(unsigned __int64 *)(buf), 64);
            }
            else if (strFormat.Find('%') == -1)
            {
                sprintf(disp, "%I64u", (*(unsigned __int64 *)(buf)));
                ss = disp;
                AddCommas(ss);
            }
            else
            {
                if (strchr("diouxX", *(const char *)strFormat.Right(1)) != NULL)
                    strFormat.Insert(strFormat.GetLength()-1, "I64");
                sprintf(disp, strFormat, (*(unsigned __int64 *)(buf)));
                ss = disp;
            }
            break;

				// xxx bitfields do not handle straddle yet
        case CHexEditDoc::DF_BITFIELD8:
			{
				long val = (*((unsigned char *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1);
				if (strFormat.Left(3).CompareNoCase("hex") == 0)
				{
					if (theApp.hex_ucase_)
						ss.Format("%.*lX", ((pdoc->df_extra_[ii]>>8)-1)/4+1, long(val));    // up to 2 hex digits (uppercase)
					else
						ss.Format("%.*lx", ((pdoc->df_extra_[ii]>>8)-1)/4+1, long(val));    // up to 2 hex digits (lowercase)
					AddSpaces(ss);
				}
				else if (strFormat.Left(3).CompareNoCase("oct") == 0)
				{
					ss.Format("%lo", long(val));
				}
				else if (strFormat.Left(3).CompareNoCase("bin") == 0)
				{
					ss = bin_str(val, pdoc->df_extra_[ii]>>8);							    // up to 8 bits
				}
				else if (strFormat.Find('%') == -1)
				{
					ss.Format("%lu", long(val));
					AddCommas(ss);
				}
				else
					ss.Format(strFormat, long(val));
			}
			break;
        case CHexEditDoc::DF_BITFIELD16:
			{
				long val = (*((unsigned short *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1);
				if (strFormat.Left(3).CompareNoCase("hex") == 0)
				{
					if (theApp.hex_ucase_)
						ss.Format("%.*lX", ((pdoc->df_extra_[ii]>>8)-1)/4+1, long(val));	// up to 4 hex digits (uppercase)
					else
						ss.Format("%.*lx", ((pdoc->df_extra_[ii]>>8)-1)/4+1, long(val));	// up to 4 hex digits (lowercase)
					AddSpaces(ss);
				}
				else if (strFormat.Left(3).CompareNoCase("oct") == 0)
				{
					ss.Format("%lo", long(val));
				}
				else if (strFormat.Left(3).CompareNoCase("bin") == 0)
				{
					ss = bin_str(val, pdoc->df_extra_[ii]>>8);								// up to 16 bits
				}
				else if (strFormat.Find('%') == -1)
				{
					ss.Format("%lu", long(val));
					AddCommas(ss);
				}
				else
					ss.Format(strFormat, long(val));
			}
			break;
        case CHexEditDoc::DF_BITFIELD32:
			{
				unsigned long val = (*((unsigned long *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1);
				if (strFormat.Left(3).CompareNoCase("hex") == 0)
				{
					if (theApp.hex_ucase_)
						ss.Format("%.*lX", ((pdoc->df_extra_[ii]>>8)-1)/4+1, long(val));	// up to 8 hex digits (uppercase)
					else
						ss.Format("%.*lx", ((pdoc->df_extra_[ii]>>8)-1)/4+1, long(val));	// up to 8 hex digits (lowercase)
					AddSpaces(ss);
				}
				else if (strFormat.Left(3).CompareNoCase("oct") == 0)
				{
					ss.Format("%lo", long(val));
				}
				else if (strFormat.Left(3).CompareNoCase("bin") == 0)
				{
					ss = bin_str(val, pdoc->df_extra_[ii]>>8);								// up to 32 bits
				}
				else if (strFormat.Find('%') == -1)
				{
					ss.Format("%lu", long(val));
					AddCommas(ss);
				}
				else
					ss.Format(strFormat, long(val));
			}
			break;
        case CHexEditDoc::DF_BITFIELD64:
			{
				unsigned __int64 val = (*((unsigned __int64 *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1);
				if (strFormat.Left(3).CompareNoCase("hex") == 0)
				{
					if (theApp.hex_ucase_)
						sprintf(disp, "%.*I64X", ((pdoc->df_extra_[ii]>>8)-1)/4+1, val);		// up to 16 hex digits
					else
						sprintf(disp, "%.*I64x", ((pdoc->df_extra_[ii]>>8)-1)/4+1, val);
					ss = disp;
					AddSpaces(ss);
				}
				else if (strFormat.Left(3).CompareNoCase("oct") == 0)
				{
					sprintf(disp, "%I64o", val);
					ss = disp;
				}
				else if (strFormat.Left(3).CompareNoCase("bin") == 0)
				{
					ss = bin_str(val, pdoc->df_extra_[ii]>>8);								// up to 64 bits
				}
				else if (strFormat.Find('%') == -1)
				{
					sprintf(disp, "%I64u", val);
					ss = disp;
					AddCommas(ss);
				}
				else
				{
					if (strchr("diouxX", *(const char *)strFormat.Right(1)) != NULL)
						strFormat.Insert(strFormat.GetLength()-1, "I64");
					sprintf(disp, strFormat, val);
					ss = disp;
				}
			}
			break;

        case CHexEditDoc::DF_REAL32:
            real_val = *(float *)(buf);
            switch (_fpclass(real_val))
            {
            case _FPCLASS_SNAN:
            case _FPCLASS_QNAN:
                ss = "NaN";
                break;
            case _FPCLASS_NINF:
                ss = "-Inf";
                break;
            case _FPCLASS_PINF:
                ss = "+Inf";
                break;
            default:
                if (strFormat.Find('%') == -1)
                    ss.Format("%.7g", real_val);
                else
                    ss.Format(strFormat, real_val);
                break;
            }
            break;
        case CHexEditDoc::DF_REAL64:
            real_val = *(double *)(buf);
            switch (_fpclass(real_val))
            {
            case _FPCLASS_SNAN:
            case _FPCLASS_QNAN:
                ss = "NaN";
                break;
            case _FPCLASS_NINF:
                ss = "-Inf";
                break;
            case _FPCLASS_PINF:
                ss = "+Inf";
                break;
            default:
                if (strFormat.Find('%') == -1)
                    ss.Format("%.15g", real_val);
                else
                    ss.Format(strFormat, real_val);
                break;
            }
            break;

        case CHexEditDoc::DF_IBMREAL32:
            if (strFormat.Find('%') == -1)
                ss.Format("%.7g", double(ibm_fp32(buf)));
            else
                ss.Format(strFormat, double(ibm_fp32(buf)));
            break;
        case CHexEditDoc::DF_IBMREAL64:
            if (strFormat.Find('%') == -1)
                ss.Format("%.16g", double(ibm_fp64(buf)));
            else
                ss.Format(strFormat, double(ibm_fp64(buf)));
            break;

        case CHexEditDoc::DF_DATEC:
            timeptr = localtime(((time_t *)buf));
            if (timeptr != NULL)
            {
                strftime(disp, sizeof(disp), strFormat, timeptr);
                ss = disp;
            }
            else
                ss = "Invalid";
            break;

        case CHexEditDoc::DF_DATEC51:
            tt = *((long *)buf) + (365*10 + 2)*24L*60L*60L;
            timeptr = localtime(&tt);
            if (timeptr != NULL)
            {
                strftime(disp, sizeof(disp), strFormat, timeptr);
                ss = disp;
            }
            else
                ss = "Invalid";
            break;
        case CHexEditDoc::DF_DATEC7:
            tt = *((unsigned long *)buf) - (365*70 + 17 + 2)*24UL*60UL*60UL;
            timeptr = localtime(&tt);
            if (timeptr != NULL)
            {
                strftime(disp, sizeof(disp), strFormat, timeptr);
                ss = disp;
            }
            else
                ss = "Invalid";
            break;
        case CHexEditDoc::DF_DATECMIN:
            tt = *((long *)buf)*60;
            timeptr = localtime(&tt);
            if (timeptr != NULL)
            {
                strftime(disp, sizeof(disp), strFormat, timeptr);
                ss = disp;
            }
            else
                ss = "Invalid";
            break;
        case CHexEditDoc::DF_DATEOLE:
            switch (_fpclass(*((double *)buf)))
            {
            case _FPCLASS_SNAN:
            case _FPCLASS_QNAN:
            case _FPCLASS_NINF:
            case _FPCLASS_PINF:
                ss = "Invalid";
                break;
            default:
                odt = *((DATE *)buf);
                ss = odt.Format(strFormat);
                break;
            }
            break;
        case CHexEditDoc::DF_DATESYSTEMTIME:
            odt = *((SYSTEMTIME *)buf);
            ss = odt.Format(strFormat);
            break;
        case CHexEditDoc::DF_DATEFILETIME:
            odt = *((FILETIME *)buf);
            ss = odt.Format(strFormat);
            break;
        case CHexEditDoc::DF_DATEMSDOS:
            if (DosDateTimeToFileTime(*(LPWORD(buf+2)), *(LPWORD(buf)), &ft))
            {
                odt = ft;
                ss = odt.Format(strFormat);
            }
            break;

        default:
            ASSERT(0);
            break;
        }
		if (df_type != CHexEditDoc::DF_WSTRING && df_type != CHexEditDoc::DF_WCHAR)
		{
			item.strText = ss;
		}
    }
}

static char *heading[] =
{
    "Click to Select",  // Only appears if nothing is selected in the drop list
    "Hex Address",
    "Decimal Address",
    "Size",
    "Type",
    "Type Name",
    "Data",
    "Units",
    "Domain",
    "Flags",
    "Comments",
	"Level",
    NULL
};

void CDataFormatView::InitColumnHeadings()
{
    ASSERT(sizeof(heading)/sizeof(*heading) == COL_LAST + 1);

    CString strWidths = theApp.GetProfileString("DataFormat", "ColumnWidths", "240,,,,,,0,,,,");
    int curr_col = grid_.GetFixedColumnCount();

    for (int ii = 0; ii < COL_LAST; ++ii)
    {
        CString ss;

        AfxExtractSubString(ss, strWidths, ii, ',');
        int width = atoi(ss);

        if (ii == 0 && width < 20) width = 20;

        {
            ASSERT(heading[ii] != NULL);
            grid_.SetColumnCount(curr_col + 1);
//            grid_.SetColumnWidth(curr_col, width);
            if (width == 0)
                grid_.SetColumnWidth(curr_col, 0);              // make column hidden
            else
                grid_.SetUserColumnWidth(curr_col, width);      // set user specified size (or -1 to indicate fit to cells)

            // Set column heading text (centred).  Also set item data so we know what goes in this column
            GV_ITEM item;
            item.row = 0;                                       // top row is header
            item.col = curr_col;                                // column we are changing
            item.mask = GVIF_PARAM|GVIF_FORMAT|GVIF_TEXT;       // change data+centered+text
            item.lParam = ii;                                   // data that says what's in this column
            item.nFormat = DT_CENTER|DT_VCENTER|DT_SINGLELINE;  // centre the heading
            item.strText = heading[ii];                         // text of the heading
            grid_.SetItem(&item);

            ++curr_col;
        }
    }
}

void CDataFormatView::set_colours()
{
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);

    COLORREF bg1, bg2, bg3;
    calc_colours(bg1, bg2, bg3);

    grid_.SetGridBkColor(bg1);          // Area of grid past end and past right (defaults to ugly grey)
    grid_.SetTextBkColor(bg1);          // This is the only way to set the background of the tree

    GV_ITEM item;
    item.mask = GVIF_BKCLR|GVIF_FGCLR;  // Just changing fg and bg colours

    // Set colour of top tree cell - sets colour for whole tree
    item.crBkClr = bg1;
    item.row = grid_.GetFixedRowCount();
    item.col = grid_.GetFixedColumnCount();
    item.crFgClr = psis_->GetDefaultTextCol();
    grid_.SetItem(&item);

    int consec_count = 0;               // Counts consecutive data lines so we can paint backgrounds

    // Set other columns a row at a time
    for (int ii = 0; ii < (int)pdoc->df_type_.size(); ++ii)
    {
        item.row = ii + grid_.GetFixedRowCount();

        // Set defaults and values that are the same for all/most fields
        if (abs(pdoc->df_type_[ii]) < CHexEditDoc::DF_DATA)
        {
            consec_count = 0;
            item.crBkClr = bg1;
        }
        else if (++consec_count%2 == 1)
            item.crBkClr = bg2;
        else
            item.crBkClr = bg3;

        for (item.col = grid_.GetFixedColumnCount(); item.col < grid_.GetColumnCount(); ++item.col)
        {
            // Work out what we are displaying in this column
            int col_id = grid_.GetItemData(0, item.col);

            if (col_id == COL_HEX_ADDRESS)
                item.crFgClr = psis_->GetHexAddrCol();
            else if (col_id == COL_DEC_ADDRESS)
                item.crFgClr = psis_->GetDecAddrCol();
            else if (col_id != COL_TREE)
                item.crFgClr = psis_->GetDefaultTextCol();

            if (col_id != COL_TREE)
                grid_.SetItem(&item);
        }
    }
    grid_.Refresh();
}

void CDataFormatView::calc_colours(COLORREF &bg1, COLORREF &bg2, COLORREF &bg3)
{
    int hue, luminance, saturation;

    // Work out background colours
    bg1 = bg2 = bg3 = psis_->GetBackgroundCol();
    get_hls(bg1, hue, luminance, saturation);
    if (theApp.alt_data_bg_cols_ && hue == -1)
    {
        // No colour (white/grey/black) so use 2 colours of similar brightness as alternate bg colours
        bg2 = get_rgb(17, max(min(luminance,95),5), 100);  // yellow
        bg3 = get_rgb(50, max(min(luminance,95),5), 100);  // cyan
    }
    else if (theApp.alt_data_bg_cols_)
    {
        // Use 2 other background colours of same brightness but diff colour
        bg2 = get_rgb(hue+33, max(min(luminance,95),5), 100);
        bg3 = get_rgb(hue+67, max(min(luminance,95),5), 100);
    }
}

// Formats bytes as a hex string.  The parameter (count) is the number of bytes but
// no more than MAX_HEX_FORMAT bytes are used and "..." is added if there are more.
// This is used for displaying data of tyep "none" in the tree view.
CString CDataFormatView::hex_format(size_t count)
{
    CString retval;
    int ii;

    const char *bhex;
    if (theApp.hex_ucase_)
        bhex = "%2.2X ";
    else
        bhex = "%2.2x ";

    for (ii = 0; ii < (int)count && ii < MAX_HEX_FORMAT; ++ii)
        retval += bhex;

    if (ii < (int)count)
        retval += "...";

    return retval;
}

// Handle context menu operations
void CDataFormatView::do_enclose(int item, int index, int parent, signed char parent_type)
{
    CHexEditDoc *pdoc = GetDocument();

    CXmlTree::CElt current_elt(pdoc->df_elt_[index]);
    CXmlTree::CElt parent_elt;
    if (parent_type == CHexEditDoc::DF_USE_STRUCT)
    {
        parent_type = CHexEditDoc::DF_DEFINE_STRUCT;

        // Find struct definition as real parent
        CString ss = pdoc->df_elt_[parent].GetAttr("type_name");
        parent_elt = pdoc->df_elt_[parent].GetOwner()->GetRoot().GetFirstChild();
        while (parent_elt.GetAttr("type_name") != ss)
        {
            if (parent_elt.IsEmpty() || parent_elt.GetName() != "define_struct")
            {
                ASSERT(0);
                return;
            }
            ++parent_elt;
        }
    }
    else
        parent_elt = pdoc->df_elt_[parent];

    // Work out where to put the dialog
    CRect rct;
    AfxGetMainWnd()->GetWindowRect(&rct);
    CPoint pt;
    pt.x = theApp.GetProfileInt("Window-Settings", "DFFDX", rct.left + 14);
    pt.y = theApp.GetProfileInt("Window-Settings", "DFFDY", rct.top + 14);

	CSaveStateHint ssh;
	pdoc->UpdateAllViews(NULL, 0, &ssh);

    // Create the new element
    int dlg_ret;
    CXmlTree::CElt new_elt;             // New element that is added

    // Clone the current node
    CXmlTree::CElt clone_elt = current_elt.Clone();

    switch (item)
    {
    case ID_DFFD_ENCLOSE_STRUCT:
        // Edit the struct element node
        new_elt = CXmlTree::CElt("struct", current_elt.GetOwner());
        new_elt.InsertChild(clone_elt, NULL);
        parent_elt.InsertChild(new_elt, &current_elt);
        parent_elt.DeleteChild(current_elt);

        {
            CDFFDStruct dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    case ID_DFFD_ENCLOSE_FOR:
        // Edit the for element node
        new_elt = CXmlTree::CElt("for", current_elt.GetOwner());
        new_elt.InsertChild(clone_elt, NULL);
        parent_elt.InsertChild(new_elt, &current_elt);
        parent_elt.DeleteChild(current_elt);

        {
            CDFFDFor dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    case ID_DFFD_ENCLOSE_IF:
        // Edit the if element node
        new_elt = CXmlTree::CElt("if", current_elt.GetOwner());
        new_elt.SetAttr("test", "true");
        new_elt.InsertChild(clone_elt, NULL);
        parent_elt.InsertChild(new_elt, &current_elt);
        parent_elt.DeleteChild(current_elt);

        {
            CDFFDIf dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    case ID_DFFD_ENCLOSE_JUMP:
        // Edit the jump element node
        new_elt = CXmlTree::CElt("jump", current_elt.GetOwner());
        new_elt.SetAttr("offset", "0");
        new_elt.InsertChild(clone_elt, NULL);
        parent_elt.InsertChild(new_elt, &current_elt);
        parent_elt.DeleteChild(current_elt);

        {
            CDFFDJump dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    }

    VERIFY(theApp.WriteProfileInt("Window-Settings", "DFFDX", pt.x));
    VERIFY(theApp.WriteProfileInt("Window-Settings", "DFFDY", pt.y));
    if (dlg_ret == IDCANCEL)
    {
        // If dialog cancelled restore things the way they were
        parent_elt.InsertChild(current_elt, &new_elt);
        parent_elt.DeleteChild(new_elt);
    }
    else
    {
        // Update the tree view to reflect the new XML
        pdoc->ScanFile();
        CDFFDHint dffdh;
        pdoc->UpdateAllViews(NULL, 0, &dffdh);
		CRestoreStateHint rsh;
		pdoc->UpdateAllViews(NULL, 0, &rsh);
    }
}

void CDataFormatView::do_insert(int item, int index, int parent, signed char parent_type)
{
    CHexEditDoc *pdoc = GetDocument();

    CXmlTree::CElt current_elt(pdoc->df_elt_[index]);
    CXmlTree::CElt parent_elt;
    if (parent_type == CHexEditDoc::DF_USE_STRUCT)
    {
        parent_type = CHexEditDoc::DF_DEFINE_STRUCT;

        // Find struct definition as real parent
        CString ss = pdoc->df_elt_[parent].GetAttr("type_name");
        parent_elt = pdoc->df_elt_[parent].GetOwner()->GetRoot().GetFirstChild();
        while (parent_elt.GetAttr("type_name") != ss)
        {
            if (parent_elt.IsEmpty() || parent_elt.GetName() != "define_struct")
            {
                ASSERT(0);
                return;
            }
            ++parent_elt;
        }
    }
    else
        parent_elt = pdoc->df_elt_[parent];

    // Work out where to put the dialog
    CRect rct;
    AfxGetMainWnd()->GetWindowRect(&rct);
    CPoint pt;
    pt.x = theApp.GetProfileInt("Window-Settings", "DFFDX", rct.left + 14);
    pt.y = theApp.GetProfileInt("Window-Settings", "DFFDY", rct.top + 14);

	CSaveStateHint ssh;
	pdoc->UpdateAllViews(NULL, 0, &ssh);

    // Create the new element
    int dlg_ret;
    CXmlTree::CElt new_elt;             // New element that is added

    switch (item)
    {
    case ID_DFFD_INSERT_DEFINE_STRUCT:
        // Insert a new define_struct element node
        new_elt = parent_elt.InsertNewChild("define_struct", &current_elt);

        {
            CDFFDStruct dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    case ID_DFFD_INSERT_STRUCT:
        // Insert a new struct element node
        new_elt = parent_elt.InsertNewChild("struct", &current_elt);

        {
            CDFFDStruct dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    case ID_DFFD_INSERT_USE_STRUCT:
        // Insert a new use_struct element node
        new_elt = parent_elt.InsertNewChild("use_struct", &current_elt);

        {
            CDFFDUseStruct dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    case ID_DFFD_INSERT_FOR:
        // Insert a new for element node
        new_elt = parent_elt.InsertNewChild("for", &current_elt);

        {
            CDFFDFor dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    case ID_DFFD_INSERT_IF:
        // Insert a new if element node
        new_elt = parent_elt.InsertNewChild("if", &current_elt);
        new_elt.SetAttr("test", "true");

        {
            CDFFDIf dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    case ID_DFFD_INSERT_SWITCH:
        // Insert a new switch element node
        new_elt = parent_elt.InsertNewChild("switch", &current_elt);

        {
            CDFFDSwitch dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    case ID_DFFD_INSERT_JUMP:
        // Insert a new jump node
        new_elt = parent_elt.InsertNewChild("jump", &current_elt);
        new_elt.SetAttr("offset", "0");

        {
            CDFFDJump dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    case ID_DFFD_INSERT_EVAL:
        // Insert a new eval element node
        new_elt = parent_elt.InsertNewChild("eval", &current_elt);
        new_elt.SetAttr("expr", "true");

        {
            CDFFDEval dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    case ID_DFFD_INSERT_DATA:
        // Insert a new data element node
        new_elt = parent_elt.InsertNewChild("data", &current_elt);
        new_elt.SetAttr("type", "int");

        {
            CDFFDData dlg(&new_elt, parent_type, this);
            dlg.SetModified();          // Force validation as it is incomplete
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
        }
        break;
    }

    VERIFY(theApp.WriteProfileInt("Window-Settings", "DFFDX", pt.x));
    VERIFY(theApp.WriteProfileInt("Window-Settings", "DFFDY", pt.y));
    if (dlg_ret == IDCANCEL)
    {
        // If dialog cancelled restore things the way they were
        parent_elt.DeleteChild(new_elt);
    }
    else
    {
        // Update the tree view to reflect the new XML
        pdoc->ScanFile();
        CDFFDHint dffdh;
        pdoc->UpdateAllViews(NULL, 0, &dffdh);
		CRestoreStateHint rsh;
		pdoc->UpdateAllViews(NULL, 0, &rsh);
    }
}

void CDataFormatView::do_edit(int index, signed char parent_type)
{
    CHexEditDoc *pdoc = GetDocument();
    int frc = grid_.GetFixedRowCount();
    int fcc = grid_.GetFixedColumnCount();

    bool continue_edit = true;        // For loop that allows the user to cycle through editing siblings
    bool changes_made = false;
    CXmlTree::CElt current_elt(pdoc->df_elt_[index]);

    // Work out where to put the dialog
    CRect rct;
    AfxGetMainWnd()->GetWindowRect(&rct);
    CPoint pt;
    pt.x = theApp.GetProfileInt("Window-Settings", "DFFDX", rct.left + 14);
    pt.y = theApp.GetProfileInt("Window-Settings", "DFFDY", rct.top + 14);

	CSaveStateHint ssh;
	pdoc->UpdateAllViews(NULL, 0, &ssh);
    while (continue_edit)
    {
        int dlg_ret;
        CString elt_type = current_elt.GetName();

        if (elt_type == "binary_file_format" || elt_type == "struct" || elt_type == "define_struct")
        {
            // Edit the struct element node
            CDFFDStruct dlg(&current_elt, parent_type, this);
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
            if (dlg_ret != IDCANCEL && dlg.IsModified())
                changes_made = true;
        }
        else if (elt_type == "use_struct")
        {
            // Edit the for element node
            CDFFDUseStruct dlg(&current_elt, parent_type, this);
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
            if (dlg_ret != IDCANCEL && dlg.IsModified())
                changes_made = true;
        }
        else if (elt_type == "for")
        {
            // Edit the for element node
            CDFFDFor dlg(&current_elt, parent_type, this);
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
            if (dlg_ret != IDCANCEL && dlg.IsModified())
                changes_made = true;
        }
        else if (elt_type == "if")
        {
            // Edit the if element node
            CDFFDIf dlg(&current_elt, parent_type, this);
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
            if (dlg_ret != IDCANCEL && dlg.IsModified())
                changes_made = true;
        }
        else if (elt_type == "switch")
        {
            // Edit the switch element node
            CDFFDSwitch dlg(&current_elt, parent_type, this);
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
            if (dlg_ret != IDCANCEL && dlg.IsModified())
                changes_made = true;
        }
        else if (elt_type == "jump")
        {
            // Edit the jump element node
            CDFFDJump dlg(&current_elt, parent_type, this);
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
            if (dlg_ret != IDCANCEL && dlg.IsModified())
                changes_made = true;
        }
        else if (elt_type == "eval")
        {
            // Edit the eval element node
            CDFFDEval dlg(&current_elt, parent_type, this);
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
            if (dlg_ret != IDCANCEL && dlg.IsModified())
                changes_made = true;
        }
        else if (elt_type == "data")
        {
            // Edit the data element node
            CDFFDData dlg(&current_elt, parent_type, this);
            dlg.SetPosition(pt);
            dlg_ret = dlg.DoModal();
            dlg.GetPosition(pt);
            if (dlg_ret != IDCANCEL && dlg.IsModified())
                changes_made = true;
        }

        // If the user pressed the next or previous button start editing the sibling
        if (dlg_ret == ID_DFFD_PREV)
        {
            // Now edit previous sibling
            current_elt--;
            ASSERT(!current_elt.IsEmpty());

            // Now select previous tree item at same level
            for (int ii = index - 1; ii >= 0; ii--)
            {
                if (pdoc->df_indent_[ii] < pdoc->df_indent_[index])
                {
                    ASSERT(0);
                    break;
                }
                else if (pdoc->df_indent_[ii] == pdoc->df_indent_[index])
                {
                    index = ii;
                    grid_.SetSelectedRange(ii + frc, fcc, ii + frc, grid_.GetColumnCount()-1);
                    break;
                }
            }
        }
        else if (dlg_ret == ID_DFFD_NEXT)
        {
            // Now edit next sibling
            ++current_elt;
            ASSERT(!current_elt.IsEmpty());

            // Now select next tree item at same level
            for (int ii = index + 1; ii < (int)pdoc->df_indent_.size(); ++ii)
            {
                if (pdoc->df_indent_[ii] < pdoc->df_indent_[index])
                {
                    ASSERT(0);
                    break;
                }
                else if (pdoc->df_indent_[ii] == pdoc->df_indent_[index])
                {
                    index = ii;
                    grid_.SetSelectedRange(ii + frc, fcc, ii + frc, grid_.GetColumnCount()-1);
                    break;
                }
            }
        }
        else if(dlg_ret == IDOK)
        {
            continue_edit = false;
        }
        else
        {
            ASSERT(dlg_ret == IDCANCEL);
            continue_edit = false;
        }
    }

    VERIFY(theApp.WriteProfileInt("Window-Settings", "DFFDX", pt.x));
    VERIFY(theApp.WriteProfileInt("Window-Settings", "DFFDY", pt.y));
    if (changes_made)
    {
        pdoc->ScanFile();
        CDFFDHint dffdh;
        pdoc->UpdateAllViews(NULL, 0, &dffdh);
		CRestoreStateHint rsh;
		pdoc->UpdateAllViews(NULL, 0, &rsh);
    }
}

// Return elt index of leaf elt containing address or elt past end if not found
size_t CDataFormatView::find_address(FILE_ADDRESS addr)
{
    std::vector<FILE_ADDRESS> *pv = &(GetDocument()->df_address_);
    size_t bot = 0;
    size_t top = pv->size();
    if (top == 0) return 0;             // Return end if empty

    while (bot < top)
    {
		size_t mid;
		FILE_ADDRESS midaddr;
		// Handle addr values of -1 (past EOF or not present)
		for (mid = (top+bot)/2; (midaddr = (*pv)[mid]) < 0; mid--)  // search backwards until we get valid addr
		{
			if (mid <= bot)
			{
				top = bot;     // just use bot as the value (we could search) from mid to top too!?
				goto stop_search;
			}
		}
        if (midaddr < addr)
            bot = mid + 1;
        else
            top = mid;
    }
stop_search:
    ASSERT(bot == top && bot >= 0 && bot <= pv->size());

    // If address is past last or between elements go to the lower one
    // (If address above last then the last element must continue to EOF.)
    if (bot == pv->size() || (bot > 0 && addr < (*pv)[bot]))
        bot--;

    // Find leaf at same address
	if ((*pv)[bot] != -1)
		while (bot < pv->size()-1 && (*pv)[bot] == (*pv)[bot+1])
			++bot;

    return bot;
}

// Select the data element that contains the address.
// Returns FALSE if the address is past EOF
void CDataFormatView::SelectAt(FILE_ADDRESS addr)
{
    if (!tree_init_) return;

//xxx    GetDocument()->CheckUpdate();       // Make sure tree view matches data

    size_t elt = find_address(addr);

// How could this happen?
    if (elt >= GetDocument()->df_address_.size())
	{
		ASSERT(0);
        return;
	}

    // Make sure the line is visible and select it
    show_row(elt);
    BOOL bb = psis_->AutoSync();
    psis_->SetAutoSync(FALSE);
    grid_.SetSelectedRange(elt + grid_.GetFixedRowCount(), grid_.GetFixedColumnCount(),
                           elt + grid_.GetFixedRowCount(), grid_.GetColumnCount()-1);
    psis_->SetAutoSync(bb);
}

//Says whether an address is read-only according to the template
BOOL CDataFormatView::ReadOnly(FILE_ADDRESS addr, FILE_ADDRESS end_addr /*=-1*/)
{
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);

    if (!tree_init_)
        return FALSE;                   // If no template then we can write to any byte

//xxx    pdoc->CheckUpdate();                // Make sure tree view matches data

    if (pdoc->df_type_.size() == 0)
    {
        ASSERT(0);
        return FALSE;                   // If no template then we can write to any byte
    }

    CString read_only_str;
    CString default_read_only_str = pdoc->df_elt_[0].GetAttr("default_read_only");
    size_t elt, last_elt = -1;

    if (end_addr <= addr) end_addr = addr + 1;

    // Check all the data elts (leaves) in the address range
    for ( ; addr < end_addr; ++addr)
    {
        elt = find_address(addr);
        if (elt == last_elt)
            continue;

        last_elt = elt;

        // Use template default if past end of expected data
        // Should this be fixed for DF_MORE (unexamined FORF elts) somehow???
        ASSERT(pdoc->df_type_.size() == pdoc->df_elt_.size());
        ASSERT(elt < pdoc->df_elt_.size());
        if (elt >= pdoc->df_type_.size() || pdoc->df_type_[elt] < CHexEditDoc::DF_DATA)
            read_only_str = "default";
        else
            read_only_str = pdoc->df_elt_[elt].GetAttr("read_only");

        // If read-only setting for this field is "default" use template default
        if (read_only_str.CompareNoCase("default") == 0)
            read_only_str = default_read_only_str;

        if (read_only_str.CompareNoCase("true") == 0)
            return TRUE;

        ASSERT(read_only_str.CompareNoCase("false") == 0);
    }
    return FALSE;
}

// Returns true if the node has at least one child node
bool CDataFormatView::has_children(int row)
{
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);
    if (row < (int)pdoc->df_indent_.size() && pdoc->df_indent_[row+1] > pdoc->df_indent_[row])
    {
        ASSERT(pdoc->df_indent_[row+1] == pdoc->df_indent_[row] + 1);
        return true;
    }
    else
        return false;
}

// Show all rows in branch under a node (ie greater indent)
void CDataFormatView::expand_all(int row)
{
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);
    int frc = grid_.GetFixedRowCount();

    // Expand all node and all children
    tree_col_.TreeDataExpandOneLevel(frc + row);  // Make sure this node is expanded

    ASSERT(row < (int)pdoc->df_indent_.size());
    for (int ii = row + 1; ii < (int)pdoc->df_indent_.size() && pdoc->df_indent_[ii] > pdoc->df_indent_[row]; ++ii)
        tree_col_.TreeDataExpandOneLevel(frc + ii);

    tree_col_.TreeRefreshRows();
    grid_.EnsureVisible(frc + row, 0);
}

// Expand to show children (ie one level only) - returns false if already expanded
bool CDataFormatView::expand_one(int row)
{
    bool retval = true;
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);
    int frc = grid_.GetFixedRowCount();

    // If there is a child
    if (row < (int)pdoc->df_indent_.size() && pdoc->df_indent_[row+1] > pdoc->df_indent_[row])
    {
        ASSERT(pdoc->df_indent_[row+1] == pdoc->df_indent_[row] + 1);  // indent should be just one more

        // Get cell for 1st child (row just below)
        CGridTreeCell* pcell = (CGridTreeCell*)grid_.GetCell(frc + row + 1, grid_.GetFixedColumnCount());
        ASSERT(pcell != NULL);
        if (pcell->IsViewable())
            retval = false;     // child is viewable so parent must be expanded
    }

    tree_col_.TreeDataExpandOneLevel(frc + row);
    tree_col_.TreeRefreshRows();
    grid_.EnsureVisible(frc + row, 0);
    return retval;
}

// Collapse to hide all children - returns false if already collapsed
bool CDataFormatView::collapse(int row)
{
    bool retval = true;
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);
    int frc = grid_.GetFixedRowCount();

    // If there is a child
    if (row < (int)pdoc->df_indent_.size() && pdoc->df_indent_[row+1] > pdoc->df_indent_[row])
    {
        ASSERT(pdoc->df_indent_[row+1] == pdoc->df_indent_[row] + 1);  // indent should be just one more

        // Get cell for 1st child (row just below)
        CGridTreeCell* pcell = (CGridTreeCell*)grid_.GetCell(frc + row + 1, grid_.GetFixedColumnCount());
        ASSERT(pcell != NULL);
        if (!pcell->IsViewable())
            retval = false;     // child is not viewable so parent must be collapsed
    }

    tree_col_.TreeDataCollapseAllSubLevels(frc + row);
    tree_col_.TreeRefreshRows();
    grid_.EnsureVisible(frc + row, 0);
    return retval;
}

// Shows a particular row in the tree view by making sure all ancestors are expanded
// The parameter (row) is the index into the df_* arrays of the row to be shown.
void CDataFormatView::show_row(int row)
{
    int frc = grid_.GetFixedRowCount();
	if (row + frc > grid_.GetRowCount())
		return;

    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);

    // Expand all parents in tree so that this invalid elt is displayed
    unsigned char ind;                  // Current indent level being checked
    int last = 0;                       // Last entry at current indent level

    tree_col_.TreeDataExpandOneLevel(frc);  // Expand top level

    // Find all ancestors (ie nearest entry at all lesser indent levels)
    for (ind = 2; ind < pdoc->df_indent_[row]; ++ind)
    {
        // Find ancestor at this indent (last entry at this indent before row)
        for (int jj = last+1; jj < row; ++jj)
            if (pdoc->df_indent_[jj] == ind)
                last = jj;
        tree_col_.TreeDataExpandOneLevel(frc + last);
    }
    tree_col_.TreeRefreshRows();
    grid_.EnsureVisible(frc + row, 0);
}

// Go to the parent of this row (ie closest row above that is indented less)
void CDataFormatView::goto_parent(int row)
{
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);

    // Find parent
    for (int parent = row - 1; parent >= 0; parent--)
        if (pdoc->df_indent_[parent] < pdoc->df_indent_[row])
        {
            ASSERT(pdoc->df_indent_[parent] == pdoc->df_indent_[row] - 1);

            // Select the parent row
            int frc = grid_.GetFixedRowCount();
            grid_.SetSelectedRange(frc + parent, grid_.GetFixedColumnCount(),
                                   frc + parent, grid_.GetColumnCount()-1);
            grid_.SetFocusCell    (frc + parent, grid_.GetFixedColumnCount());
            break;
        }
}

// Go to a specified sibling (ie same indent and same parent as the row)
void CDataFormatView::goto_sibling(int row, int sibling)
{
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);

    // Find parent
    for (int parent = row - 1; parent >= 0; parent--)
        if (pdoc->df_indent_[parent] < pdoc->df_indent_[row])
        {
            ASSERT(pdoc->df_indent_[parent] == pdoc->df_indent_[row] - 1);  // parent indent must be one less
            ASSERT(pdoc->df_indent_[parent+1] == pdoc->df_indent_[row]);    // first sibling has same indent

            int last_sib = parent + 1;          // row of sibling found
            int sib_count = 0;                  // how many siblings have been found so far
            for (int rr = parent + 1; rr < (int)pdoc->df_indent_.size(); ++rr)
                if (pdoc->df_indent_[rr] < pdoc->df_indent_[row])
                {
                    // No more siblings (indent higher than row)
                    break;
                }
                else if (pdoc->df_indent_[rr] == pdoc->df_indent_[row])
                {
                    last_sib = rr;
                    if (sib_count >= sibling)
                        break;
                    ++sib_count;
                }

            ASSERT(sib_count <= sibling);       // == if found, < if ran out of siblings

            // Select the sibling row
            int frc = grid_.GetFixedRowCount();
            grid_.SetSelectedRange(frc + last_sib, grid_.GetFixedColumnCount(),
                                   frc + last_sib, grid_.GetColumnCount()-1);
            grid_.SetFocusCell    (frc + last_sib, grid_.GetFixedColumnCount());
            break;
        }
}

// Got to previous error row (if any)
void CDataFormatView::prev_error(int row)
{
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);

    for (int rr = row - 1; rr >= 0; rr--)
        if (pdoc->df_size_[rr] < 0 || pdoc->df_address_[rr] == -1)
        {
            // Select the error row
            int frc = grid_.GetFixedRowCount();
            grid_.SetSelectedRange(frc + rr, grid_.GetFixedColumnCount(),
                                   frc + rr, grid_.GetColumnCount()-1);
            grid_.SetFocusCell    (frc + rr, grid_.GetFixedColumnCount());
            break;
        }
}

// Got to next error row (if any)
void CDataFormatView::next_error(int row)
{
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc != NULL);

    for (int rr = row + 1; rr < (int)pdoc->df_address_.size(); ++rr)
        if (pdoc->df_size_[rr] < 0 || pdoc->df_address_[rr] == -1)
        {
            // Select the error row
            int frc = grid_.GetFixedRowCount();
            grid_.SetSelectedRange(frc + rr, grid_.GetFixedColumnCount(),
                                   frc + rr, grid_.GetColumnCount()-1);
            grid_.SetFocusCell    (frc + rr, grid_.GetFixedColumnCount());
            break;
        }
}

// Save which parts of the tree are expanded (using node names)
void CDataFormatView::save_tree_state()
{
    CHexEditDoc *pdoc = GetDocument();

    tree_state_.clear();

    std::vector<int> curr_state;        // Node we are up to for each indent level (0 to curr-1)
    std::vector<bool> curr_done;        // Prevents more than one sibling being added (if one is viewable then all are)
    int curr = 0;                       // Current indent level

    for (int ii = 0; ii < (int)pdoc->df_indent_.size() && pdoc->df_type_[ii] != CHexEditDoc::DF_EXTRA; ++ii)
    {
        CGridTreeCell* pGridTreeCell = (CGridTreeCell*)grid_.
            GetCell(ii + grid_.GetFixedRowCount(), grid_.GetFixedColumnCount());
        ASSERT(pGridTreeCell != NULL);
        if (pdoc->df_indent_[ii] > curr && pGridTreeCell->IsViewable())
        {
            // Add this node
            curr = pdoc->df_indent_[ii];
            curr_state.push_back(ii);
            curr_done.push_back(false);
        }
        else if (pdoc->df_indent_[ii] == curr)
        {
			// Same indent - keep track of last node at this indent level
            curr_state[curr-1] = ii;
        }
        else if (pdoc->df_indent_[ii] < curr)
        {
			// Decreasing indent
            ASSERT(pGridTreeCell->IsViewable());   // can only get here if sibling was viewable hence this one should be too

			// The last node seen was farthest viewable node - we have all ancestors in curr_state so generate and save the full name
            if (curr > 1 && !curr_done.back())
                tree_state_.push_back(get_full_name(curr_state, true));

			// Prune back to our parent indent level before adding current node
            curr_state.resize(pdoc->df_indent_[ii]-1);
            curr_done.resize(pdoc->df_indent_[ii]-1);

            // Add this node (but mark as done)
            curr = pdoc->df_indent_[ii];
            curr_state.push_back(ii);
            curr_done.push_back(true);
        }
    }

    if (curr > 1 && !curr_done.back())
        tree_state_.push_back(get_full_name(curr_state, true));

#if 0
    for (int kk = 0; kk < tree_state_.size(); ++kk)
        AfxMessageBox(tree_state_[kk]);
#endif
}

// Restores tree state saved with save_tree_state
void CDataFormatView::restore_tree_state()
{
    CHexEditDoc *pdoc = GetDocument();
    int frc = grid_.GetFixedRowCount();

    for (std::vector<CString>::const_iterator pts = tree_state_.begin();
         pts != tree_state_.end(); ++pts)
    {
        int index = 0;                  // current node we are checking
        for (int ii = 0; ; ++ii)        // for each string element
        {
            CString ss;
            AfxExtractSubString(ss, *pts, ii, ',');
            if (ss.IsEmpty())
                break;

            if (index > 0 &&
                (pdoc->df_type_[index-1] == CHexEditDoc::DF_FORF || 
                 pdoc->df_type_[index-1] == CHexEditDoc::DF_FORV))
            {
				// We are looking in an array so ss must be an index
				ASSERT(!ss.IsEmpty() && isdigit(ss[0]));
                int array_index = atoi(ss);
                int count = 0;
				// Scan all array elts (nodes at the same indent)
                for (int jj = index; jj < (int)pdoc->df_indent_.size(); ++jj)
                {
                    if (pdoc->df_indent_[jj] < pdoc->df_indent_[index])
                    {
                        goto not_found;  // array index past end
                    }
                    else if (pdoc->df_indent_[jj] == pdoc->df_indent_[index] &&
                             count++ == array_index)
                    {
                        index = jj + 1;   // Move to 1st child
                        ASSERT(index < (int)pdoc->df_indent_.size());
                        ASSERT(pdoc->df_indent_[index] == pdoc->df_indent_[jj]+1);  // Double-check its a child
                        CGridTreeCell* pGridTreeCell = (CGridTreeCell*)grid_.
                            GetCell(index + frc, grid_.GetFixedColumnCount());
                        ASSERT(pGridTreeCell != NULL);
                        if (!pGridTreeCell->IsViewable())
                            tree_col_.TreeDataExpandOneLevel(frc + jj);
                        break;
                    }
                }
            }
            else if ((pdoc->df_type_[index] == CHexEditDoc::DF_IF ||
				      pdoc->df_type_[index] == CHexEditDoc::DF_SWITCH) && ss == "?")
            {
                ASSERT(index+1 < (int)pdoc->df_indent_.size());
                ASSERT(pdoc->df_indent_[index+1] == pdoc->df_indent_[index]+1);
                CGridTreeCell* pGridTreeCell = (CGridTreeCell*)grid_.
                    GetCell(index+1 + frc, grid_.GetFixedColumnCount());
                ASSERT(pGridTreeCell != NULL);
                if (!pGridTreeCell->IsViewable())
                    tree_col_.TreeDataExpandOneLevel(frc + index);
                ++index;
                ASSERT(index < (int)pdoc->df_indent_.size());
            }
            else
            {
                // Find the entry at the next level with this name
                for (int jj = index; jj < (int)pdoc->df_indent_.size(); ++jj)
                {
                    if (pdoc->df_indent_[jj] < pdoc->df_indent_[index])
                    {
                        goto not_found;
                    }
                    else if (pdoc->df_indent_[jj] == pdoc->df_indent_[index] &&
                             ss == pdoc->df_elt_[jj].GetAttr("name"))
                    {
                        index = jj + 1;    // continue scan from first child
                        ASSERT(index < (int)pdoc->df_indent_.size());
                        ASSERT(pdoc->df_indent_[index] == pdoc->df_indent_[jj]+1);
                        CGridTreeCell* pGridTreeCell = (CGridTreeCell*)grid_.
                            GetCell(index + frc, grid_.GetFixedColumnCount());
                        ASSERT(pGridTreeCell != NULL);
                        if (!pGridTreeCell->IsViewable())
                            tree_col_.TreeDataExpandOneLevel(frc + jj);
                        break;
                    }
                }
            }
        }
        not_found:
            ;
    }
    tree_col_.TreeRefreshRows();
}

// Returns a string representing the full variable name of a node
CString CDataFormatView::get_full_name(std::vector<int> &curr_state, bool use_comma /*=false*/)
{
    CHexEditDoc *pdoc = GetDocument();
    ASSERT(pdoc->df_type_[curr_state[0]] == CHexEditDoc::DF_FILE);

    CString retval = pdoc->df_elt_[0].GetAttr("name");
    int above = CHexEditDoc::DF_STRUCT;
    for (int jj = 1; jj < (int)curr_state.size() - 1; ++jj)
    {
        if (above == CHexEditDoc::DF_FORF || above == CHexEditDoc::DF_FORV)
        {
            int count = 0;
            int curr_indent = pdoc->df_indent_[curr_state[jj]];
            for (int kk = curr_state[jj] - 1; kk >= 0; kk--)
            {
                if (pdoc->df_indent_[kk] == curr_indent)
                    ++count;
                else if (pdoc->df_indent_[kk] < curr_indent)
                    break;
            }
            CString ss;
            if (use_comma)
                ss.Format(",%d", count);
            else
                ss.Format("[%d]", count);
            retval += ss;
        }
        else
        {
            CString name = pdoc->df_elt_[curr_state[jj]].GetAttr("name");
            if (!name.IsEmpty())
            {
                if (use_comma)
                    retval += CString(',') + name;
                else
                    retval += CString('.') + name;
            }
            else if (pdoc->df_type_[curr_state[jj]] == CHexEditDoc::DF_IF ||
				     pdoc->df_type_[curr_state[jj]] == CHexEditDoc::DF_SWITCH)
            {
                if (use_comma)
                    retval += CString(",?");
            }
            else if (!use_comma)
            {
                return CString("");     // Indicate that we can't make a valid name
            }
        }
        above = pdoc->df_type_[curr_state[jj]];
    }
    return retval;
}

#ifdef _DEBUG
CString CDataFormatView::get_name(int ii) 
{
    CHexEditDoc *pdoc = GetDocument();
    CString retval = pdoc->df_elt_[ii].GetAttr("name");
    unsigned char ind = pdoc->df_indent_[ii];
    int same_level = 0;

    for (int jj = ii - 1; pdoc->df_indent_[jj] >= ind && jj > 0; jj--)
        if (pdoc->df_indent_[jj] == ind)
            ++same_level;

    if (jj >= 0 && pdoc->df_type_[jj] == CHexEditDoc::DF_FORV || pdoc->df_type_[jj] == CHexEditDoc::DF_FORF)
        retval.Format("%s[%d]", get_name(jj), same_level);

    return retval;
}
#endif

/////////////////////////////////////////////////////////////////////////////
// CDataFormatView drawing

void CDataFormatView::OnDraw(CDC* pDC)
{
    CDocument* pDoc = GetDocument();
    // TODO: add draw code here
}

/////////////////////////////////////////////////////////////////////////////
// CDataFormatView diagnostics

#ifdef _DEBUG
void CDataFormatView::AssertValid() const
{
    CView::AssertValid();
}

void CDataFormatView::Dump(CDumpContext& dc) const
{
    CView::Dump(dc);
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CDataFormatView message handlers

void CDataFormatView::OnInitialUpdate() 
{
    CView::OnInitialUpdate();
    CHexEditDoc *pdoc = GetDocument();

    grid_.SetDocView(pdoc, this);

    // Create the Gridctrl window
    CRect rect;
    GetClientRect(rect);
    grid_.Create(rect, this, 100);

    imagelist_.Create(MAKEINTRESOURCE(IDB_IMAGELIST), 16, 1, RGB(0xFF, 0xFF, 0xFF));

    grid_.SetImageList(&imagelist_);
    btn_db_.SetGrid(&grid_);

    grid_.SetDoubleBuffering();
    grid_.SetAutoFit();
    grid_.SetEditable(TRUE);
    grid_.SetGridLines(GVL_VERT);
    grid_.SetTrackFocusCell(FALSE);
    grid_.SetFrameFocusCell(FALSE);
	// Someone asked to be able to copy multiple rows to the clipboard - so
	// disable these to allow multiple row selection
//    grid_.SetListMode(TRUE);
//    grid_.SetSingleRowSelection(FALSE);
    grid_.SetGridBkColor(RGB(255,255,255));

#ifdef _DEBUG
    grid_.SetFixedRowCount(1);
    grid_.SetFixedColumnCount(1);
    grid_.SetColumnWidth(0, 32);
#else
    grid_.SetFixedRowCount(1);
#endif

    InitColumnHeadings();
    grid_.SetColumnResize();

    grid_.EnableRowHide(FALSE);
    grid_.EnableColumnHide(FALSE);
    grid_.EnableHiddenRowUnhide(FALSE);
    grid_.EnableHiddenColUnhide(FALSE);
    grid_.EnableDragAndDrop();

    pdoc->ScanInit();
    InitTree();

    // Set up drop down list so the user can select a different DFFD
    if (!grid_.SetCellType(0, grid_.GetFixedColumnCount(), RUNTIME_CLASS(CGridCellCombo)))
        return;

    CStringArray desc_list;   // Array of descriptions of format files

    // If we change the list to store both a file name and a description then this will have to
    // be changed later to use the description vector not the file name vector
    for (std::vector<CString>::const_iterator pp = theApp.xml_file_name_.begin();
         pp != theApp.xml_file_name_.end(); ++pp)
    {
        desc_list.Add(*pp);
    }

    CGridCellCombo *pCell = (CGridCellCombo*) grid_.GetCell(0, grid_.GetFixedColumnCount());
    pCell->SetOptions(desc_list);
    pCell->SetStyle(CBS_DROPDOWNLIST|CBS_SORT);
//    pCell->SetStyle(CBS_SORT);

    CString file_desc = pdoc->GetXMLFileDesc();
    if (!file_desc.IsEmpty())
        pCell->SetText(file_desc);

    grid_.ExpandColsNice(FALSE);
//    grid_.ExpandColumnsToFit();
}

void CDataFormatView::OnUpdate(CView* pSender, LPARAM lHint, CObject* pHint) 
{
    if (GetDocument()->ptree_ == NULL) return;

    //if (pHint != NULL && pHint->IsKindOf(RUNTIME_CLASS(CRefreshHint)))
    //{
    //    // Document has changed so remember to rebuild the tree
    //    // xxx this should be fixed to not completely rebuild the tree?
    //    save_tree_state();
    //    InitTree();
    //    restore_tree_state();
    //}
    if (pHint != NULL && pHint->IsKindOf(RUNTIME_CLASS(CSaveStateHint)))
    {
        save_tree_state();
    }
    else if (pHint != NULL && pHint->IsKindOf(RUNTIME_CLASS(CRestoreStateHint)))
    {
        restore_tree_state();
    }
    else if (pHint != NULL && pHint->IsKindOf(RUNTIME_CLASS(CDFFDHint)))
    {
        // Selected DFFD has changed
        InitTree();

        // Note InitTree will reset the tree to be unexpanded in all views, but the current view
        // will have the tree restored again by subsequent call to restore_tree_state (CRestoreStateHint).
        ASSERT(!GetDocument()->GetXMLFileDesc().IsEmpty());
        ((CGridCellCombo*)grid_.GetCell(0, grid_.GetFixedColumnCount()))->SetText(GetDocument()->GetXMLFileDesc());
    }
    else
    {
        // Do nothing for other hints (only useful in CHexEditViews)
    }
}

void CDataFormatView::OnDestroy() 
{
    if (grid_.m_hWnd != 0)
    {
        // Save column widths
        CString strWidths;
        int curr_col = grid_.GetFixedColumnCount();
        int col_id = 0; // COL_TREE

        for (int ii = 0; ii < COL_LAST; ++ii)
        {
            if (ii == col_id)
            {
                CString ss;
                ss.Format("%ld,", long(grid_.GetUserColumnWidth(curr_col)));
                strWidths += ss;

                ++curr_col;
                if (curr_col < grid_.GetColumnCount())
                    col_id = grid_.GetItemData(0, curr_col);
                else
                    col_id = -1;   // No more columns so prevent any more matches
            }
            else
                strWidths += ",";
        }
        theApp.WriteProfileString("DataFormat", "ColumnWidths", strWidths);
    }

    CView::OnDestroy();
}

void CDataFormatView::OnSize(UINT nType, int cx, int cy) 
{
    CView::OnSize(nType, cx, cy);
    
    if (grid_.GetSafeHwnd())
    {
        CRect rect;
        GetClientRect(rect);
        grid_.MoveWindow(rect);
    }
}

BOOL CDataFormatView::OnEraseBkgnd(CDC* pDC) 
{
    return TRUE;
//  return CView::OnEraseBkgnd(pDC);
}

void CDataFormatView::OnDffdSync() 
{
    CHexEditDoc *pdoc = GetDocument();
    int frc = grid_.GetFixedRowCount();

    // Work out the currently selected line
    CCellRange sel = grid_.GetSelectedCellRange();
    int row = sel.GetMinRow();

    // Work out the file address of this element
    FILE_ADDRESS start = pdoc->df_address_[row-frc];
    FILE_ADDRESS end = pdoc->df_address_[row-frc] + mac_abs(pdoc->df_size_[row-frc]);

    if (start < 0 || start > pdoc->length())
    {
        AfxMessageBox("The address of this data element\r\n"
                      "is past the physical end of file");
        start = end = pdoc->length();
    }
    else if (end > pdoc->length())
    {
        AfxMessageBox("The end of this data element\r\n"
                      "is past the physical end of file");
        end = pdoc->length();
    }

    // Select corresp. bytes in sister view
    psis_->MoveWithDesc("Template Sync", start, end, -1, -1, FALSE, TRUE);
    psis_->SetFocus();
}

void CDataFormatView::OnUpdateDffdSync(CCmdUI* pCmdUI) 
{
    pCmdUI->Enable(!psis_->AutoSync());
}


void CDataFormatView::OnDffdWeb() 
{
    CHexEditDoc *pdoc = GetDocument();

    if (pdoc == NULL || pdoc->df_elt_.size() < 1)
        return;
    
    CString ss = pdoc->df_elt_[0].GetAttr("web_site");
    if (ss.IsEmpty())
        return;
    else if (ss.Left(7) != "http://")
        ss = "http://" + ss;

    ::ShellExecute(AfxGetMainWnd()->m_hWnd, _T("open"), ss, NULL, NULL, SW_SHOWNORMAL);
}

void CDataFormatView::OnUpdateDffdWeb(CCmdUI* pCmdUI) 
{
    CHexEditDoc *pdoc = GetDocument();

    pCmdUI->Enable(pdoc != NULL && 
                   pdoc->df_elt_.size() > 0 &&
                   !pdoc->df_elt_[0].GetAttr("web_site").IsEmpty());
}

void CDataFormatView::OnGridClick(NMHDR *pNotifyStruct, LRESULT* /*pResult*/)
{
    NM_GRIDVIEW* pItem = (NM_GRIDVIEW*) pNotifyStruct;
    TRACE("Left button click on row %d, col %d\n", pItem->iRow, pItem->iColumn);

//xxx    GetDocument()->CheckUpdate();       // Make sure tree view matches data

    if (pItem->iRow < grid_.GetFixedRowCount())
        return;                         // Don't do anything for header rows

// Moved to OnGridEndSelChanged
//    CHexEditDoc *pdoc = GetDocument();
//
//    int ii = pItem->iRow - grid_.GetFixedRowCount();
//
//    ASSERT(ii < pdoc->df_address_.size());
//    ASSERT(pdoc->df_size_.size() == pdoc->df_address_.size());
//    if (ii < pdoc->df_address_.size())
//    {
//        FILE_ADDRESS start = pdoc->df_address_[ii];
//        FILE_ADDRESS end = pdoc->df_address_[ii] + mac_abs(pdoc->df_size_[ii]);
//        if (start < 0 || start >= pdoc->length())
//        {
//            start = end = pdoc->length();
//            ((CMainFrame *)AfxGetMainWnd())->StatusBarText("Data address is past EOF");
//        }
//        else if (end > pdoc->length())
//        {
//            end = pdoc->length();
//            ((CMainFrame *)AfxGetMainWnd())->StatusBarText("End of data is past EOF");
//        }
//        if (psis_->AutoSync())
//            psis_->MoveToAddress(start, end, -1, -1, FALSE, TRUE);
//    }
}

void CDataFormatView::OnGridDoubleClick(NMHDR *pNotifyStruct, LRESULT* /*pResult*/)
{
    NM_GRIDVIEW* pItem = (NM_GRIDVIEW*) pNotifyStruct;
    TRACE("Double click on row %d, col %d\n", pItem->iRow, pItem->iColumn);

//xxx    GetDocument()->CheckUpdate();       // Make sure tree view matches data

    int fcc = grid_.GetFixedColumnCount();

    if (pItem->iColumn == fcc && pItem->iRow >= grid_.GetFixedRowCount())
    {
        int index = pItem->iRow - grid_.GetFixedRowCount();
        CHexEditDoc *pdoc = GetDocument();
        ASSERT(index >= 0 && index < (int)pdoc->df_type_.size());
        ASSERT(pdoc->df_type_.size() == pdoc->df_indent_.size());

        if (!theApp.tree_edit_ && pdoc->df_type_[index] < CHexEditDoc::DF_MORE)
        {
            // Double-click (when not in edit mode) expands all sub-nodes
            expand_all(index);
        }
        else if (!theApp.tree_edit_)
        {
            // Do nothing here (yet?)
        }
        else if (pdoc->df_type_[index] != CHexEditDoc::DF_MORE && pdoc->df_type_[index] != CHexEditDoc::DF_EXTRA)
        {
            signed char parent_type = -1;       // The type of the parent affects the behaviour of the edit dialog
            if (index > 0)
            {
                // First find the type of the parent as this determines what can be done
                int ii;
                for (ii = index-1; ii >= 0; ii--)
                {
                    if (pdoc->df_indent_[ii] < pdoc->df_indent_[index])
                    {
                        ASSERT(pdoc->df_indent_[ii] == pdoc->df_indent_[index] - 1);  // parent must be one level up
                        break;
                    }
                }
                ASSERT(ii >= 0);                // should be found

                parent_type = pdoc->df_type_[ii];
            }

            do_edit(index, parent_type);
        }
    }
}

void CDataFormatView::OnGridRClick(NMHDR *pNotifyStruct, LRESULT* /*pResult*/)
{
    NM_GRIDVIEW* pItem = (NM_GRIDVIEW*) pNotifyStruct;
    TRACE("Right button click on row %d, col %d\n", pItem->iRow, pItem->iColumn);

//xxx    GetDocument()->CheckUpdate();       // Make sure tree view matches data

    int fcc = grid_.GetFixedColumnCount();

//    // Work out where to display the popup menu
//    CRect rct;
//    grid_.GetCellRect(pItem->iRow, pItem->iColumn, &rct);
//    grid_.ClientToScreen(&rct);

    // Get mouse point so we know where to put the popup menu
    CPoint mouse_pt;
    ::GetCursorPos(&mouse_pt);

    if (pItem->iRow < grid_.GetFixedRowCount() && pItem->iColumn >= fcc)
    {
        // Right click on column headings - create menu of columns available
        CMenu mm;
        mm.CreatePopupMenu();

		// Use column headings as menu item names
        //mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+1)>0?MF_CHECKED:0), 1, "Hex Address");
        //mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+2)>0?MF_CHECKED:0), 2, "Decimal Address");
        //mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+3)>0?MF_CHECKED:0), 3, "Size");
        //mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+4)>0?MF_CHECKED:0), 4, "Type");
        //mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+5)>0?MF_CHECKED:0), 5, "Type Name");
        //mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+6)>0?MF_CHECKED:0), 6, "Data");
        //mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+7)>0?MF_CHECKED:0), 7, "Units");
        //mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+8)>0?MF_CHECKED:0), 8, "Domain");
        //mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+9)>0?MF_CHECKED:0), 9, "Flags");
        //mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+10)>0?MF_CHECKED:0),10,"Comments");
		ASSERT(sizeof(heading)/sizeof(*heading) == COL_LAST + 1);
		for (int ii = 1; ii < COL_LAST; ++ii)
			mm.AppendMenu(MF_ENABLED|(grid_.GetColumnWidth(fcc+ii)>0?MF_CHECKED:0), ii, heading[ii]);

        int item = mm.TrackPopupMenu(
                TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
//                (rct.left+rct.right)/2, (rct.top+rct.bottom)/2, this);
                mouse_pt.x, mouse_pt.y, this);
        if (item != 0)
        {
            item += fcc;
            if (grid_.GetColumnWidth(item) > 0)
                grid_.SetColumnWidth(item, 0);
            else
            {
                grid_.SetColumnWidth(item, 1);
                grid_.AutoSizeColumn(item, GVS_BOTH);
            }
            grid_.ExpandColsNice(FALSE);
        }
    }
    else if (pItem->iColumn == fcc && pItem->iRow >= grid_.GetFixedRowCount())
    {
        int item = -1;
        CHexEditDoc *pdoc = GetDocument();
        int index = pItem->iRow - grid_.GetFixedRowCount();
        ASSERT(index >= 0 && index < (int)pdoc->df_type_.size());
        ASSERT(pdoc->df_type_.size() == pdoc->df_indent_.size());
        int parent;

        // Select the row that was clicked
        grid_.SetSelectedRange(pItem->iRow, grid_.GetFixedColumnCount(), pItem->iRow, grid_.GetColumnCount()-1);

        // Now create the context menu that allows the user to change the structure
        CMenu top, *ppop;
        BOOL ok = top.LoadMenu(IDR_DFFD);
        ASSERT(ok);
        if (!ok) return;

        signed char parent_type;        // The type of parent affects the behaviour of the dialog
        if (!theApp.tree_edit_ && pdoc->df_type_[index] == CHexEditDoc::DF_FILE)
        {
            // In view mode (ie not edit mode) we only allow expand of a node
            CString strTemp;
            ASSERT((top.GetMenuString(8, strTemp, MF_BYPOSITION), strTemp == "RootNodeViewMode"));
            ppop = top.GetSubMenu(8);

            ASSERT(ppop != NULL);
            if (ppop != NULL)
                item = ppop->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
                                            mouse_pt.x, mouse_pt.y, this);
        }
        else if (!theApp.tree_edit_ && pdoc->df_type_[index] < CHexEditDoc::DF_LEAF1)
        {
            // In view mode (ie not edit mode) we only allow expand of a node
            CString strTemp;
            ASSERT((top.GetMenuString(7, strTemp, MF_BYPOSITION), strTemp == "NodeViewMode"));
            ppop = top.GetSubMenu(7);

            ASSERT(ppop != NULL);
            if (ppop != NULL)
                item = ppop->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
                                            mouse_pt.x, mouse_pt.y, this);
        }
        else if (!theApp.tree_edit_)
        {
            // Leaf nodes when not in edit mode have no options
        }
        else if (pdoc->df_type_[index] == CHexEditDoc::DF_MORE || pdoc->df_type_[index] == CHexEditDoc::DF_EXTRA)
        {
            // Do nothing for these (yet?)
        }
        else if (index == 0)
        {
            ASSERT(pdoc->df_type_[index] == CHexEditDoc::DF_FILE);

            // Only allow editing of top level (DF_FILE) element
            CString strTemp;
            ASSERT((top.GetMenuString(2, strTemp, MF_BYPOSITION), strTemp == "RootNode"));
            ppop = top.GetSubMenu(2);

            ASSERT(ppop != NULL);
            if (ppop != NULL)
                item = ppop->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
                                            mouse_pt.x, mouse_pt.y, this);
        }
        else if (pdoc->df_type_[index] == CHexEditDoc::DF_DEFINE_STRUCT)
        {
            parent = 0;
            parent_type = pdoc->df_type_[parent];
            ASSERT(parent_type == CHexEditDoc::DF_FILE);

            CString strTemp;
            ASSERT((top.GetMenuString(9, strTemp, MF_BYPOSITION), strTemp == "DefineStruct"));
            ppop = top.GetSubMenu(9);

            ASSERT(ppop != NULL);
            if (ppop != NULL)
                item = ppop->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
                                            mouse_pt.x, mouse_pt.y, this);
        }
        else
        {
            // First find the type of the parent as this determines what can be done
            for (parent = index-1; parent >= 0; parent--)
            {
                if (pdoc->df_indent_[parent] < pdoc->df_indent_[index])
                {
                    ASSERT(pdoc->df_indent_[parent] == pdoc->df_indent_[index] - 1);  // parent must be one level up
                    break;
                }
            }
            ASSERT(parent >= 0);                // should be found

            parent_type = pdoc->df_type_[parent];
            switch (parent_type)
            {
            // The parent can contain any number of sub-elements, so use menu that allows insert
            case CHexEditDoc::DF_FILE:
            case CHexEditDoc::DF_STRUCT:
            case CHexEditDoc::DF_DEFINE_STRUCT:
            case CHexEditDoc::DF_USE_STRUCT:
            case CHexEditDoc::DF_UNION:
                {
                    CString strTemp;
                    ASSERT((top.GetMenuString(0, strTemp, MF_BYPOSITION), strTemp == "Struct"));
                }
                ppop = top.GetSubMenu(0);
                ASSERT(ppop != NULL);

                if (ppop != NULL)
                {
                    // Don't allow delete if it's an only child
                    if (pdoc->df_elt_[parent].GetNumChildren() < 2)
                        ppop->EnableMenuItem(ID_DFFD_DELETE, MF_BYCOMMAND | MF_GRAYED);
                    if (pdoc->df_type_[index] >= CHexEditDoc::DF_LEAF1)
                        ppop->EnableMenuItem(ID_DFFD_EXPANDALL, MF_BYCOMMAND | MF_GRAYED);
                    item = ppop->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
//                                                (rct.left+rct.right)/2, (rct.top+rct.bottom)/2, this);
                                                mouse_pt.x, mouse_pt.y, this);
                }
                break;

            // The parent can contain only one sub-element so we cannot insert
            case CHexEditDoc::DF_FORV:
            case CHexEditDoc::DF_FORF:
            case CHexEditDoc::DF_IF:
            case CHexEditDoc::DF_SWITCH:
            case CHexEditDoc::DF_JUMP:
                {
                    CString strTemp;
                    ASSERT((top.GetMenuString(1, strTemp, MF_BYPOSITION), strTemp == "Single"));
                }
                ppop = top.GetSubMenu(1);
                ASSERT(ppop != NULL);

                if (ppop != NULL)
                {
                    if (pdoc->df_type_[index] >= CHexEditDoc::DF_LEAF1)
                        ppop->EnableMenuItem(ID_DFFD_EXPANDALL, MF_BYCOMMAND | MF_GRAYED);
                    item = ppop->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY | TPM_RETURNCMD,
//                                                (rct.left+rct.right)/2, (rct.top+rct.bottom)/2, this);
                                                mouse_pt.x, mouse_pt.y, this);
                }
                break;

            // All other types should not be parents
            default:
                ASSERT(0);
            }
        }

        top.DestroyMenu();

        switch (item)
        {
        case ID_DFFD_EXPANDALL:
            expand_all(index);
            break;
        case ID_DFFD_EDIT_MODE:
            {
                theApp.tree_edit_ = TRUE;
                GetDocument()->ScanFile();
                CDFFDHint dffdh;
                GetDocument()->UpdateAllViews(NULL, 0, &dffdh);
            }
            break;
        case ID_DFFD_VIEW_MODE:
            {
                theApp.tree_edit_ = FALSE;
                GetDocument()->ScanFile();
                CDFFDHint dffdh;
                GetDocument()->UpdateAllViews(NULL, 0, &dffdh);
            }
            break;

        case ID_DFFD_SAVE:
            if (pdoc->ptree_->GetFileName().Right(11).CompareNoCase("default.xml") != 0)
            {
                pdoc->ptree_->Save();
                break;
            }
            // fall through
        case ID_DFFD_SAVEAS:
            {
                CHexFileDialog dlgFile("TemplateFileDlg", FALSE, "xml", pdoc->ptree_->GetFileName(),
                                       OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT | OFN_SHOWHELP | OFN_NOCHANGEDIR,
                                       "Template (XML) files - *.xml)|*.xml|All Files (*.*)|*.*||");

                // Set up the title of the dialog
                dlgFile.m_ofn.lpstrTitle = "Save Template File";

                if (dlgFile.DoModal() == IDOK)
                {
                    pdoc->ptree_->SetFileName(dlgFile.GetPathName());
                    pdoc->ptree_->Save();
                    theApp.GetXMLFileList();        // rebuild the list with the new file name in it

                    // Rebuild drop down list for this view
                    CStringArray desc_list;   // Array of descriptions of format files

                    for (std::vector<CString>::const_iterator pp = theApp.xml_file_name_.begin();
                         pp != theApp.xml_file_name_.end(); ++pp)
                    {
                        desc_list.Add(*pp);
                    }
                    CGridCellCombo *pCell = (CGridCellCombo*) grid_.GetCell(0, grid_.GetFixedColumnCount());
                    pCell->SetOptions(desc_list);

                    // Make sure the doc knows the name of the template file
                    GetDocument()->xml_file_num_ = theApp.FindXMLFile(dlgFile.GetFileTitle());
                    pCell->SetText(GetDocument()->GetXMLFileDesc());
                }
            }
            break;
        case ID_DFFD_GLOBAL:
            {
                CDFFDGlobal dlg(&pdoc->df_elt_[index], this);
                if (dlg.DoModal() == IDOK && dlg.IsModified())
                {
					CSaveStateHint ssh;
					pdoc->UpdateAllViews(NULL, 0, &ssh);
                    pdoc->ScanFile();
                    // Update the display as how some things are shown has changed
                    CDFFDHint dffdh;
                    pdoc->UpdateAllViews(NULL, 0, &dffdh);
					CRestoreStateHint rsh;
					pdoc->UpdateAllViews(NULL, 0, &rsh);
                }
            }
            break;

        case ID_DFFD_INSERT_DEFINE_STRUCT:
            // This is handled differently since it is sent from context menu when root element clicked.
            // New struct definitions are always added as the first child of the root hence param 3
            // (parent) is 0 for root node and param 2 (current) is 1 to insert before the 1st child.
            do_insert(item, 1, 0, CHexEditDoc::DF_FILE);
            break;

        case ID_DFFD_INSERT_STRUCT:
        case ID_DFFD_INSERT_USE_STRUCT:
        case ID_DFFD_INSERT_FOR:
        case ID_DFFD_INSERT_IF:
        case ID_DFFD_INSERT_SWITCH:
        case ID_DFFD_INSERT_JUMP:
        case ID_DFFD_INSERT_EVAL:
        case ID_DFFD_INSERT_DATA:
            do_insert(item, index, parent, parent_type);
            break;

		case ID_DFFD_PARSE_DECLARATIONS:
			{
				TParseDlg tpd;
				tpd.m_source_code = theApp.GetProfileString("DataFormat", "ParseSource");
				if (tpd.DoModal() == IDOK)
				{
					// Save current text as default for next time
					theApp.WriteProfileString              ("DataFormat", "ParseSource", tpd.m_source_code);

					// Get parameters from dialog and parse the text
					TParser tp;
					tp.packing_default_ = "\x01\x02\x04\x08\x10"[tpd.m_pack];
 					tp.check_values_win_ = tpd.m_values_windows;
					tp.check_values_custom_ = tpd.m_values_custom;
					tp.save_values_custom_ = tpd.m_values_save;
					tp.check_std_ = tpd.m_types_std;
					tp.check_custom_ = tpd.m_types_custom;
					tp.check_win_ = tpd.m_types_windows;
					tp.check_common_ = tpd.m_types_common;
					tp.save_custom_ = tpd.m_types_save;

					CXmlTree::CFrag frag = tp.Parse(pdoc->ptree_, tpd.m_source_code);

					// If there was something generated then added it to the XML (at parent level)
					if (!frag.IsEmpty())
					{
						TRACE("%s\n", frag.DumpXML());
						frag.InsertKids(&pdoc->df_elt_[parent], &pdoc->df_elt_[index]);

						// Update the tree view to reflect the new XML
						CSaveStateHint ssh;
						pdoc->UpdateAllViews(NULL, 0, &ssh);
						pdoc->ScanFile();
						CDFFDHint dffdh;
						pdoc->UpdateAllViews(NULL, 0, &dffdh);
						CRestoreStateHint rsh;
						pdoc->UpdateAllViews(NULL, 0, &rsh);
					}
				}
			}
			break;

        case ID_DFFD_DELETE:
            if (AfxMessageBox("Are you sure you want to delete this element?", MB_YESNO) == IDYES)
            {
                if (parent_type == CHexEditDoc::DF_USE_STRUCT)
                {
                    // Find struct definition as real parent
                    CString ss = pdoc->df_elt_[parent].GetAttr("type_name");
                    CXmlTree::CElt parent_elt = pdoc->df_elt_[parent].GetOwner()->GetRoot().GetFirstChild();
                    while (parent_elt.GetAttr("type_name") != ss)
                    {
                        if (parent_elt.IsEmpty() || parent_elt.GetName() != "define_struct")
                        {
                            ASSERT(0);
                            return;
                        }
                        ++parent_elt;
                    }
                    parent_elt.DeleteChild(pdoc->df_elt_[index]);
                }
                else
                    pdoc->df_elt_[parent].DeleteChild(pdoc->df_elt_[index]);

                // Update the tree view to reflect the new XML
				CSaveStateHint ssh;
				pdoc->UpdateAllViews(NULL, 0, &ssh);
                pdoc->ScanFile();
                CDFFDHint dffdh;
                pdoc->UpdateAllViews(NULL, 0, &dffdh);
				CRestoreStateHint rsh;
				pdoc->UpdateAllViews(NULL, 0, &rsh);
            }
            break;

        case ID_DFFD_ENCLOSE_STRUCT:
        case ID_DFFD_ENCLOSE_FOR:
        case ID_DFFD_ENCLOSE_IF:
        case ID_DFFD_ENCLOSE_JUMP:
            do_enclose(item, index, parent, parent_type);
            break;

        case ID_DFFD_EDIT:
            do_edit(index, parent_type);
            break;
        }
    }
}

void CDataFormatView::OnGridEndSelChange(NMHDR *pNotifyStruct, LRESULT* /*pResult*/)
{
    NM_GRIDVIEW* pItem = (NM_GRIDVIEW*) pNotifyStruct;
    TRACE("Sel change row %d, col %d\n", pItem->iRow, pItem->iColumn);

    // Remove any custom edit cell type if we were just editing
    if (edit_row_type_changed_ != -1)
    {
        grid_.SetCellType(edit_row_type_changed_, grid_.GetFixedColumnCount()+COL_DATA, RUNTIME_CLASS(CGridCell));
        edit_row_type_changed_ = -1;
    }

    if (pItem->iRow < grid_.GetFixedRowCount())
        return;                         // Don't do anything for header rows

    CHexEditDoc *pdoc = GetDocument();

    int ii = pItem->iRow - grid_.GetFixedRowCount();

    ASSERT(ii < (int)pdoc->df_address_.size());
    ASSERT(pdoc->df_size_.size() == pdoc->df_address_.size());
    FILE_ADDRESS start;
    if (ii < (int)pdoc->df_address_.size() && (start = pdoc->df_address_[ii]) >= 0)
    {
        FILE_ADDRESS end = start + mac_abs(pdoc->df_size_[ii]);
        if (start >= pdoc->length())
        {
            start = end = pdoc->length();
            ((CMainFrame *)AfxGetMainWnd())->StatusBarText("Data address is past EOF");
        }
        else if (end > pdoc->length())
        {
            end = pdoc->length();
            ((CMainFrame *)AfxGetMainWnd())->StatusBarText("End of data is past EOF");
        }
        if (psis_->AutoSync())
            psis_->MoveWithDesc("Template Auto-sync", start, end, -1, -1, FALSE, TRUE);
    }
}

void CDataFormatView::OnGridBeginLabelEdit(NMHDR *pNotifyStruct, LRESULT* pResult)
{
    NM_GRIDVIEW* pItem = (NM_GRIDVIEW*) pNotifyStruct;
    CHexEditDoc *pdoc = GetDocument();

    if (pItem->iRow == 0 && pItem->iColumn == grid_.GetFixedColumnCount())
		return;   // This is OK - allow selection of template using drop down list

    int ii = pItem->iRow - grid_.GetFixedRowCount();   // index into arrays
    signed char df_type;
    ASSERT(ii < (int)pdoc->df_type_.size());

	// Check for editable field
	if (ii < 1 ||                                                           // < 0 means grid header row, 0 means DF_FILE 
		ii >= (int)pdoc->df_type_.size() ||                                 // past end (should not happen - bug in setting grid length?)
		pItem->iColumn != grid_.GetFixedColumnCount() + COL_DATA ||         // grid column is not the data column (only edit column)
		(df_type = mac_abs(pdoc->df_type_[ii])) < CHexEditDoc::DF_DATA ||   // row is not for data elt
		df_type == CHexEditDoc::DF_NO_TYPE ||                               // non-editable data elt (edit as hex?)
		pdoc->df_address_[ii] < 0 )                                        // data elt not present in file (so can't edit)
	{
		// Not in range of possible data rows or in data column
        *pResult = -1;
        return;
	}

	// Check if the field is read only in the template
    CString read_only_str = pdoc->df_elt_[ii].GetAttr("read_only");
    if (read_only_str.CompareNoCase("default") == 0)
        read_only_str = pdoc->df_elt_[0].GetAttr("default_read_only");
    if (read_only_str.CompareNoCase("true") == 0)
	{
		// This may be unexpected so tell the user what happened
        ::HMessageBox("This field is read only.", MB_OK);
        *pResult = -1;
        return;
	}

	// Show bytes of field as the selection.
	// This is also necessary for check_ro call (below) which uses the current selection
    FILE_ADDRESS start = pdoc->df_address_[ii];
    FILE_ADDRESS end = start + mac_abs(pdoc->df_size_[ii]);
    psis_->MoveWithDesc("Field Edit", start, end, -1, -1, FALSE, TRUE);

	// As a last check make sure that view is not read-only (and give user the option to turn off RO)
    if (psis_->check_ro("change this field"))
	{
        *pResult = -1;
        return;
	}

    CString ss;                     // Unformated string that is to be edited
#if _MSC_VER >= 1300
	CStringW sw;                    // For wide string and wide char
	wchar_t *pwc;                   // Used in wide string handling
#endif
    char disp[128];                 // Buffer used to create formatted 64 bit ints (using sprintf)
    double real_val;                // Holds temp real for analysis for special values (Inf, Nan etc)
    COleDateTime odt;               // Holds the date/time for date fields
    time_t tt;                      // Used in calcs for some date formats
    FILETIME ft;                    // Used in conversion of MSDOS date/time
    unsigned char *pp;              // Used to find string terminator
    long val32;
    __int64 val64;

    // size_t df_size = (size_t)mac_abs(pdoc->df_size_[ii]);  // causes crash in release build in VC6
    FILE_ADDRESS tmp = pdoc->df_size_[ii] < 0 ? -pdoc->df_size_[ii] : pdoc->df_size_[ii];
    size_t df_size = size_t(tmp);
    unsigned char *buf = new unsigned char[df_size + 2];   // up to 2 extra bytes may be needed for nul-term wide strings

    // xxx handle insuff memory (eg really big string)

    if (pdoc->GetData(buf, df_size, pdoc->df_address_[ii]) != df_size)
    {
        ASSERT(0);
        *pResult = -1;
        return;
    }

    if (pdoc->df_type_[ii] < 0 && df_type == CHexEditDoc::DF_WSTRING)
        flip_each_word(buf, df_size);   // Convert big-endian Unicode string to string to edit
    else if (pdoc->df_type_[ii] < 0 && df_type == CHexEditDoc::DF_DATESYSTEMTIME)
		flip_each_word(buf, df_size);
    else if (pdoc->df_type_[ii] < 0)
        flip_bytes(buf, df_size);       // Convert big-endian to little-endian

    switch (df_type)
    {
    default:
        ASSERT(0);
        *pResult = -1;
        return;
    case CHexEditDoc::DF_CHAR:  // no longer used
        ASSERT(0);
		// fall through
    case CHexEditDoc::DF_CHARA:
    case CHexEditDoc::DF_CHARO:
        if (*buf >= 32 && *buf < 127)
            ss.Format("'%c'", *buf);
        else
            ss.Format("%ld", long(*buf));
        break;
    case CHexEditDoc::DF_CHARN:
        if (isprint(*buf))
            ss.Format("'%c'", *buf);
        else
            ss.Format("%ld", long(*buf));
        break;
    case CHexEditDoc::DF_CHARE:
        if (e2a_tab[*buf] != '\0')
            ss.Format("'%c'", e2a_tab[*buf]);
        else
            ss.Format("%ld", long(*buf));
        break;
    case CHexEditDoc::DF_WCHAR:
#if _MSC_VER >= 1300
        if (iswprint(*(short *)buf))
			sw.Format(L"%c", *((short *)buf));
        else
            sw.Format(L"%ld", long(*((short *)buf)));
#else
        ss.Format("%ld", long(*((short *)buf)));
#endif
        break;

    case CHexEditDoc::DF_STRING:  // no longer used
        ASSERT(0);
		// fall through
    case CHexEditDoc::DF_STRINGO:
    case CHexEditDoc::DF_STRINGA:
    case CHexEditDoc::DF_STRINGN:
        pp = (unsigned char *)memchr(buf, pdoc->df_extra_[ii], df_size);  // search for terminator
        if (pp != NULL)
            *pp = '\0';
        else
            buf[df_size] = '\0';  // terminator not found so use all bytes as the string
        ss = buf;
        break;
    case CHexEditDoc::DF_STRINGE:
        {
            // Find end of string to display
            pp = (unsigned char *)memchr(buf, pdoc->df_extra_[ii], df_size);
            if (pp == NULL) pp = buf + df_size;

            // Convert buf to EBCDIC removing invalid characters
            unsigned char *buf2 = new unsigned char[df_size+1];
            unsigned char *po = buf2;
            for (const unsigned char *ptmp = buf; ptmp < pp; ++ptmp)
                if (e2a_tab[*ptmp] != '\0')
                    *po++ = e2a_tab[*ptmp];
            *po++ = '\0';

            ss = buf2;
            delete[] buf2;
        }
        break;
    case CHexEditDoc::DF_WSTRING:
#if _MSC_VER >= 1300
		ASSERT(df_size%2 == 0); // size must be even for a Unicode string
        pwc = wmemchr((wchar_t *)buf, (wchar_t)pdoc->df_extra_[ii], df_size/2);  // search for terminator
        if (pwc != NULL)
            *pwc = '\0';
        else
            ((wchar_t *)buf)[df_size/2] = L'\0';  // terminator not found so use all bytes as the string
        sw = (wchar_t *)buf;
        break;
#else
		return;  // TBD - Unicode strings not yet handled
#endif

    case CHexEditDoc::DF_INT8:
        ss.Format("%ld", long(*(signed char *)(buf)));
        break;
    case CHexEditDoc::DF_INT16:
        ss.Format("%ld", long(*(short *)(buf)));
        break;
    case CHexEditDoc::DF_INT32:
        ss.Format("%ld", long(*(long *)(buf)));
        break;
    case CHexEditDoc::DF_INT64:
        sprintf(disp, "%I64d", (*(__int64 *)(buf)));
        ss = disp;
        break;

    case CHexEditDoc::DF_MINT8:
        if ((val32 = long(*(signed char *)(buf))) < 0)
            val32 = -(val32&0x7F);
        ss.Format("%ld", val32);
        break;
    case CHexEditDoc::DF_MINT16:
        if ((val32 = long(*(short *)(buf))) < 0)
            val32 = -(val32&0x7FFF);
        ss.Format("%ld", val32);
        break;
    case CHexEditDoc::DF_MINT32:
        if ((val32 = *(long *)(buf)) < 0)
            val32 = -(val32&0x7fffFFFFL);
        ss.Format("%ld", val32);
        break;
    case CHexEditDoc::DF_MINT64:
        if ((val64 = (*(__int64 *)(buf))) < 0)
            val64 = -(val64&0x7fffFFFFffffFFFFi64);
        sprintf(disp, "%I64d", val64);
        ss = disp;
        break;

    case CHexEditDoc::DF_UINT8:
        ss.Format("%lu", long(*(unsigned char *)(buf)));
        break;
    case CHexEditDoc::DF_UINT16:
        ss.Format("%lu", long(*(unsigned short *)(buf)));
        break;
    case CHexEditDoc::DF_UINT32:
        ss.Format("%lu", *(unsigned long *)(buf));
        break;
    case CHexEditDoc::DF_UINT64:
        sprintf(disp, "%I64u", (*(__int64 *)(buf)));
        ss = disp;
        break;

			// xxx bitfields do not handle straddle yet
    case CHexEditDoc::DF_BITFIELD8:
		ASSERT(signed char(pdoc->df_extra_[ii]) >= 0 && (pdoc->df_extra_[ii]&0xFF)+(pdoc->df_extra_[ii]>>8) <= 8);
        ss.Format("%lu", long((*((unsigned char *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1)));
        break;
    case CHexEditDoc::DF_BITFIELD16:
		ASSERT(signed char(pdoc->df_extra_[ii]) >= 0 && (pdoc->df_extra_[ii]&0xFF)+(pdoc->df_extra_[ii]>>8) <= 16);
        ss.Format("%lu", long((*((unsigned short *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1)));
        break;
    case CHexEditDoc::DF_BITFIELD32:
		ASSERT(signed char(pdoc->df_extra_[ii]) >= 0 && (pdoc->df_extra_[ii]&0xFF)+(pdoc->df_extra_[ii]>>8) <= 32);
        ss.Format("%lu", long((*((unsigned long *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1)));
        break;
    case CHexEditDoc::DF_BITFIELD64:
		ASSERT(signed char(pdoc->df_extra_[ii]) >= 0 && (pdoc->df_extra_[ii]&0xFF)+(pdoc->df_extra_[ii]>>8) <= 64);
        sprintf(disp, "%I64u", ((*((unsigned __int64 *)buf)>>(pdoc->df_extra_[ii]&0xFF)) & ((1<<(pdoc->df_extra_[ii]>>8))-1)));
        ss = disp;
        break;

    case CHexEditDoc::DF_REAL32:
        real_val = *(float *)(buf);
        switch (_fpclass(real_val))
        {
        case _FPCLASS_SNAN:
        case _FPCLASS_QNAN:
            ss = "NaN";
            break;
        case _FPCLASS_NINF:
            ss = "-Inf";
            break;
        case _FPCLASS_PINF:
            ss = "+Inf";
            break;
        default:
            ss.Format("%.7g", real_val);
            break;
        }
        break;
    case CHexEditDoc::DF_REAL64:
        real_val = *(double *)(buf);
        switch (_fpclass(real_val))
        {
        case _FPCLASS_SNAN:
        case _FPCLASS_QNAN:
            ss = "NaN";
            break;
        case _FPCLASS_NINF:
            ss = "-Inf";
            break;
        case _FPCLASS_PINF:
            ss = "+Inf";
            break;
        default:
            ss.Format("%.15g", real_val);
            break;
        }
        break;

    case CHexEditDoc::DF_IBMREAL32:
        ss.Format("%.7g", double(ibm_fp32(buf)));
        break;
    case CHexEditDoc::DF_IBMREAL64:
        ss.Format("%.16g", double(ibm_fp64(buf)));
        break;

    case CHexEditDoc::DF_DATEC:
        odt = COleDateTime(*((time_t *)buf));
        break;
    case CHexEditDoc::DF_DATEC51:
        tt = *((long *)buf) + (365*10 + 2)*24L*60L*60L;
        odt = COleDateTime(tt);
        break;
    case CHexEditDoc::DF_DATEC7:
        tt = *((unsigned long *)buf) - (365*70 + 17 + 2)*24UL*60UL*60UL;
        odt = COleDateTime(tt);
        break;
    case CHexEditDoc::DF_DATECMIN:
        tt = *((long *)buf)*60;
        odt = COleDateTime(tt);
        break;
    case CHexEditDoc::DF_DATEOLE:
        odt = COleDateTime(*((DATE *)buf));
        break;
    case CHexEditDoc::DF_DATESYSTEMTIME:
        odt = COleDateTime(*((SYSTEMTIME *)buf));
        break;
    case CHexEditDoc::DF_DATEFILETIME:
        odt = COleDateTime(*((FILETIME *)buf));
        break;
    case CHexEditDoc::DF_DATEMSDOS:
        if (DosDateTimeToFileTime(*((LPWORD)(buf+2)), *((LPWORD)(buf)), &ft))
            odt = ft;
        else
            odt.SetStatus(COleDateTime::invalid ); 
        break;
    }
    delete[] buf;

    CString domain_str = pdoc->df_elt_[ii].GetAttr("domain");

    if (df_type >= CHexEditDoc::DF_CHAR &&
        df_type < CHexEditDoc::DF_LAST_INT &&
        domain_str.GetLength() > 0 && domain_str[0] == '{')
    {
        VERIFY(grid_.SetCellType(pItem->iRow, pItem->iColumn, RUNTIME_CLASS(CGridCellCombo)));
        edit_row_type_changed_ = pItem->iRow;
        CGridCellCombo *pcell = (CGridCellCombo*) grid_.GetCell(pItem->iRow, pItem->iColumn);

        // Make enum list for combo drop-down and also find current enum
        __int64 val;
        if (ss.GetLength() > 1 && ss[0] == '\'')
            val = ss[1];
        else if (ss.GetLength() > 1 && ss[0] == '0' && (ss[1] == 'x' || ss[1] == 'X'))
            val = ::strtoi64((const char *)ss+2, 16);   // current elt value - used to get enum string
        else
            val = ::strtoi64(ss);         // current elt value - used to get enum string
        CString estr = ss;                  // current value's enum string (or number if enum not found)
        CStringArray enum_list;             // Stores all enum strings to init combo drop down list

        CHexEditDoc::enum_t &ev = pdoc->get_enum(pdoc->df_elt_[ii]);
        CHexEditDoc::enum_t::const_iterator pe;
        for (pe = ev.begin(); pe != ev.end(); ++pe)
        {
            if (pe->first == val)
                estr = pe->second;
            enum_list.Add(pe->second);
        }
        pcell->SetOptions(enum_list);
        pcell->SetStyle(CBS_DROPDOWN);
        pcell->SetText(estr);
    }
    else if (df_type >= CHexEditDoc::DF_DATEC &&
                df_type < CHexEditDoc::DF_LAST_DATE)
    {
        CString strFormat = pdoc->df_elt_[ii].GetAttr("display");
        if (strFormat.IsEmpty())
            strFormat = theApp.default_date_format_;
        VERIFY(grid_.SetCellType(pItem->iRow, pItem->iColumn, RUNTIME_CLASS(CGridCellDateTime)));
        edit_row_type_changed_ = pItem->iRow;
        CGridCellDateTime *pcell = (CGridCellDateTime *)grid_.GetCell(pItem->iRow, pItem->iColumn);
		if (odt.GetStatus() != COleDateTime::valid)
			odt = COleDateTime();
        pcell->Init(odt, strFormat);
    }
#if _MSC_VER >= 1300
	else if (df_type == CHexEditDoc::DF_WCHAR || df_type == CHexEditDoc::DF_WSTRING)
        grid_.SetItemText(pItem->iRow, pItem->iColumn, sw);
#endif
    else
        grid_.SetItemText(pItem->iRow, pItem->iColumn, ss);
}

//#pragma function(memset)

void CDataFormatView::OnGridEndLabelEdit(NMHDR *pNotifyStruct, LRESULT* pResult)
{
    NM_GRIDVIEW* pItem = (NM_GRIDVIEW*) pNotifyStruct;

    if (pItem->iRow == 0 && pItem->iColumn == grid_.GetFixedColumnCount())
    {
        CHexEditDoc *pdoc = GetDocument();

        // Changed to different DFFD
        pdoc->OpenDataFormatFile(CString(grid_.GetCell(0, grid_.GetFixedColumnCount())->GetText()));
        pdoc->ScanFile();
        CDFFDHint dffdh;
        pdoc->UpdateAllViews(NULL, 0, &dffdh);
        // do not restore tree state here as we are using a different template

        return;
    }

    CHexEditDoc *pdoc = GetDocument();
    int ii = pItem->iRow - grid_.GetFixedRowCount();   // index into arrays
    ASSERT(ii < (int)pdoc->df_type_.size());  // make sure grid row is not past end of arrays
    signed char df_type = mac_abs(pdoc->df_type_[ii]);
    bool is_enum = false;

    // If data column get the value and put it into file
    if (pItem->iColumn == grid_.GetFixedColumnCount()+COL_DATA &&
        ii > 0 &&
        df_type >= CHexEditDoc::DF_DATA &&
        df_type != CHexEditDoc::DF_NO_TYPE &&
        pdoc->df_address_[ii] != -1)
    {
        COleDateTime odt;
        __int64 val64;

		// Need to work out time zone diff since some date formats are local time and others are std time (gmtime)
		static double tz_diff = 0.0;
		if (tz_diff == 0.0)
		{
			time_t dummy = time_t(1000000L);
			tz_diff = (1000000L - mktime(gmtime(&dummy)))/86400.0;
		}

        // First get any info from custom cell types
        CString domain_str = pdoc->df_elt_[ii].GetAttr("domain");

        if (df_type >= CHexEditDoc::DF_CHAR &&
            df_type < CHexEditDoc::DF_LAST_INT &&
            domain_str[0] == '{')
        {
            //CGridCellCombo *pcell = (CGridCellCombo*) grid_.GetCell(pItem->iRow, pItem->iColumn);
            CString ss = grid_.GetItemText(pItem->iRow, pItem->iColumn);
            ss.TrimLeft();
            if (ss.GetLength() > 0 && isalpha(ss[0]))
            {
                CHexEditDoc::enum_t &ev = pdoc->get_enum(pdoc->df_elt_[ii]);
                CHexEditDoc::enum_t::const_iterator pe;
                for (pe = ev.begin(); pe != ev.end(); ++pe)
                    if (ss == pe->second)
                    {
                        val64 = pe->first;
                        is_enum = true;
						break;
                    }
				if (pe == ev.end())
				{
					CString strTmp;
					strTmp.Format("\"%s\"\r\nis not a member of the enum list", ss);
					::HMessageBox(strTmp);
					*pResult = -1;
					return;
				}
            }
        }
        else if (df_type >= CHexEditDoc::DF_DATEC &&
                 df_type < CHexEditDoc::DF_LAST_DATE)
        {
            CGridCellDateTime *pcell = (CGridCellDateTime *)grid_.GetCell(pItem->iRow, pItem->iColumn);
            odt = pcell->GetTime();
        }

        unsigned char *pdata;           // Points to the actual bytes to change
        size_t df_size = (size_t)mac_abs(pdoc->df_size_[ii]);
        ASSERT(pdoc->df_address_[ii] + df_size <= pdoc->length());
        size_t new_size = df_size;

        CString ss;
#if _MSC_VER >= 1300   // earlier versions do not support wide CStrings (in ANSI builds)
		CStringW sw;
#endif
        signed char val8;
        short val16;
        long val32;
        float real32;
        double real64;
        SYSTEMTIME st;
        FILETIME ft;

        switch (df_type)
        {
        case CHexEditDoc::DF_STRING:  // no longer used
            ASSERT(0);
			// fall through
        case CHexEditDoc::DF_STRINGO:
        case CHexEditDoc::DF_STRINGA:
        case CHexEditDoc::DF_STRINGN:
            ss = grid_.GetItemText(pItem->iRow, pItem->iColumn);
            if (ss.GetLength() + 1 != (int)df_size && pdoc->df_elt_[ii].GetAttr("len").IsEmpty())
            {
				// Variable sized string field and new string is different length to previous string
                if (psis_->display_.overtype)
                {
                    if (::HMessageBox("You can't change the string length in overtype mode.\r"
                                      "Do you want to turn off overtype mode?",
                                      MB_OKCANCEL) == IDCANCEL)
                    {
                        theApp.mac_error_ = 10;
                        return;
                    }
                    else
                        psis_->do_insert();
                }
				ss += char(pdoc->df_extra_[ii]);     // Add terminator
                new_size = ss.GetLength();
            }
            else if (ss.GetLength() < (int)df_size)
            {
                // Fixed length field with new string shorter - terminate and fill with nulls to end of field
                size_t len = ss.GetLength();
                char *pp = ss.GetBuffer(df_size+1);
                pp[len] = (char)pdoc->df_extra_[ii];         // Terminate the string
				memset(pp + len + 1, '\0', df_size - len);   // Pad with nul bytes (else we get garbage or 0xCD in debug builds)
            }
            else if (ss.GetLength() > (int)df_size)
            {
				// String longer than field (fixed length string field)
                ss = ss.Left(df_size);
                grid_.SetItemText(pItem->iRow, pItem->iColumn, ss);
                ::HMessageBox("String is too long - truncated");
            }
			// ELSE string is length of fixed field (excluding terminator)
			// OR var length field and string is same length as before
            pdata = (unsigned char *)(const char *)ss;
            break;
        case CHexEditDoc::DF_STRINGE:
            {
                ss = grid_.GetItemText(pItem->iRow, pItem->iColumn).Left(df_size);
                char *pss = ss.GetBuffer(df_size+1);
                char *pout = pss;
                const char *pp;
                for (pp = pss; *pp != '\0' && pout < pss + df_size; ++pp)
                {
                    if (*pp > 0 && *pp < 128 && a2e_tab[*pp] != '\0')
                        *pout++ = *pp;
                }
                for ( ; pout < pss + df_size; ++pout)
                    *pout = (char)pdoc->df_extra_[ii];
                *pout = '\0';
                pdata = (unsigned char *)pss;
            }
            break;
        case CHexEditDoc::DF_WSTRING:
#if _MSC_VER >= 1300
			ASSERT(df_size % 2 == 0);    // Unicode strings must be even no of bytes
			sw = grid_.GetItemText(pItem->iRow, pItem->iColumn);
            if (2 * (sw.GetLength() + 1) != (int)df_size && pdoc->df_elt_[ii].GetAttr("len").IsEmpty())
            {
				// Variable sized string field and new string is different length to previous string
                if (psis_->display_.overtype)
                {
                    if (::HMessageBox("You can't change the string length in overtype mode.\r"
                                      "Do you want to turn off overtype mode?",
                                      MB_OKCANCEL) == IDCANCEL)
                    {
                        theApp.mac_error_ = 10;
                        return;
                    }
                    else
                        psis_->do_insert();
                }
				sw += wchar_t(pdoc->df_extra_[ii]);     // Add terminator
                new_size = 2 * (sw.GetLength());
            }
            else if (2 * (sw.GetLength()) < (int)df_size)
            {
                // Fixed length field with new string shorter - terminate and fill with nulls to end of field
                size_t len = sw.GetLength();
                wchar_t *pwc = sw.GetBuffer(df_size/2+1);
                pwc[len] = wchar_t(pdoc->df_extra_[ii]);        // Terminate the string
				wmemset(pwc + len + 1, L'\0', df_size/2 - len); // Pad with nul words else we get garbage (0xCD in debug)
            }
            else if (2 * (sw.GetLength()) > (int)df_size)
            {
				// String longer than field (fixed length string field)
                sw = sw.Left(df_size/2);
                grid_.SetItemText(pItem->iRow, pItem->iColumn, sw);
                ::HMessageBox("String is too long - truncated");
            }
			// else string is length of fixed length field OR var length field but string is same length as before
            pdata = (unsigned char *)(const wchar_t *)sw;
#endif
			break;

        case CHexEditDoc::DF_CHAR:  // no longer used
            ASSERT(0);
			// fall through
        case CHexEditDoc::DF_CHARA:
        case CHexEditDoc::DF_CHARO:
        case CHexEditDoc::DF_CHARN:
            ss = grid_.GetItemText(pItem->iRow, pItem->iColumn);
			ss.TrimLeft();
            if (is_enum)
                val8 = (signed char)val64;
            else if (ss.GetLength() > 1 && ss[0] == '\'')
                val8 = ss[1];
            else if (ss.GetLength() > 0 && (isdigit(ss[0]) || ss[0] == '-'))
                val8 = (signed char)strtol(ss, NULL, 0);
			else if (ss.GetLength() > 0)
                val8 = ss[0];
			else
				val8 = 0;
            pdata = (unsigned char *)&val8;
            ASSERT(df_size == 1);
            break;
        case CHexEditDoc::DF_CHARE:
            ss = grid_.GetItemText(pItem->iRow, pItem->iColumn);
            if (ss.GetLength() > 1 && ss[0] == '\'')
            {
                if (ss[1] > 0 && ss[1] < 128 && a2e_tab[ss[1]] != '\0')
                    val8 = a2e_tab[ss[1]];
                else
                {
                    val8 = 0;
                    AfxMessageBox("Invalid EBCDIC character");
                }
            }
            else
                val8 = (signed char)strtoul(ss, NULL, 0);
            pdata = (unsigned char *)&val8;
            ASSERT(df_size == 1);
            break;
        case CHexEditDoc::DF_WCHAR:
#if _MSC_VER >= 1300
            sw = grid_.GetItemText(pItem->iRow, pItem->iColumn);
			if (sw.GetLength() > 1)
				sw = sw.TrimLeft();
            if (is_enum)
                val16 = (short)val64;
			else if (sw.GetLength() > 0 && (iswdigit(sw[0]) || sw[0] == L'-'))
                val16 = (short)wcstol(sw, NULL, 0);
            else if (sw.GetLength() > 1 && sw[0] == L'\'')
                val16 = sw[1];
            else if (sw.GetLength() > 0)
                val16 = sw[0];
            else
				val16 = 0;
            pdata = (unsigned char *)&val16;
            ASSERT(df_size == 2);
#else
            if (is_enum)
                val16 = (short)val64;
            else
                val16 = (short)strtoul(ss, NULL, 0);
            pdata = (unsigned char *)&val16;
            ASSERT(df_size == 2);
#endif
            break;

        case CHexEditDoc::DF_INT8:
            if (is_enum)
                val8 = (signed char)val64;
            else
                val8 = (signed char)strtol(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);
            pdata = (unsigned char *)&val8;
            ASSERT(df_size == 1);
            break;
        case CHexEditDoc::DF_INT16:
            if (is_enum)
                val16 = (short)val64;
            else
                val16 = (short)strtol(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);
            pdata = (unsigned char *)&val16;
            ASSERT(df_size == 2);
            break;
        case CHexEditDoc::DF_INT32:
            if (is_enum)
                val32 = (long)val64;
            else
                val32 = strtol(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);
            pdata = (unsigned char *)&val32;
            ASSERT(df_size == 4);
            break;
        case CHexEditDoc::DF_INT64:
            if (!is_enum)
            {
                CString ss = grid_.GetItemText(pItem->iRow, pItem->iColumn);
                if (ss.GetLength() > 1 && ss[0] == '0' && (ss[1] == 'x' || ss[1] == 'X'))
                    val64 = ::strtoi64((const char *)ss+2, 16);
                else
                    val64 = ::strtoi64(ss);
            }
            pdata = (unsigned char *)&val64;
            ASSERT(df_size == 8);
            break;

        case CHexEditDoc::DF_MINT8:
            if (is_enum)
                val8 = (signed char)val64;
            else
                val8 = (signed char)strtol(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);
            if (val8 < 0)
                val8 = -val8 | 0x80;
            pdata = (unsigned char *)&val8;
            ASSERT(df_size == 1);
            break;
        case CHexEditDoc::DF_MINT16:
            if (is_enum)
                val16 = (short)val64;
            else
                val16 = (short)strtol(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);
            if (val16 < 0)
                val16 = -val16 | 0x8000;
            pdata = (unsigned char *)&val16;
            ASSERT(df_size == 2);
            break;
        case CHexEditDoc::DF_MINT32:
            if (is_enum)
                val32 = (long)val64;
            else
                val32 = strtol(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);
            if (val32 < 0)
                val32 = -val32 | 0x80000000L;
            pdata = (unsigned char *)&val32;
            ASSERT(df_size == 4);
            break;
        case CHexEditDoc::DF_MINT64:
            if (!is_enum)
            {
                CString ss = grid_.GetItemText(pItem->iRow, pItem->iColumn);
                if (ss.GetLength() > 1 && ss[0] == '0' && (ss[1] == 'x' || ss[1] == 'X'))
                    val64 = ::strtoi64((const char *)ss+2, 16);
                else
                    val64 = ::strtoi64(ss);
            }
            if (val64 < 0)
                val64 = -val64 | 0x8000000000000000i64;
            pdata = (unsigned char *)&val64;
            ASSERT(df_size == 8);
            break;

        case CHexEditDoc::DF_UINT8:     // xxx check that this preserves sign
            if (is_enum)
                val8 = (signed char)val64;
            else
                val8 = (signed char)strtoul(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);
            pdata = (unsigned char *)&val8;
            ASSERT(df_size == 1);
            break;
        case CHexEditDoc::DF_UINT16:
            if (is_enum)
                val16 = (short)val64;
            else
                val16 = (short)strtoul(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);
            pdata = (unsigned char *)&val16;
            ASSERT(df_size == 2);
            break;
        case CHexEditDoc::DF_UINT32:
            if (is_enum)
                val32 = (long)val64;
            else
                val32 = strtoul(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);
            pdata = (unsigned char *)&val32;
            ASSERT(df_size == 4);
            break;
        case CHexEditDoc::DF_UINT64:
            if (!is_enum)
            {
                CString ss = grid_.GetItemText(pItem->iRow, pItem->iColumn);
                if (ss.GetLength() > 1 && ss[0] == '0' && (ss[1] == 'x' || ss[1] == 'X'))
                    val64 = ::strtoi64((const char *)ss+2, 16);
                else
                    val64 = ::strtoi64(ss);
            }
            pdata = (unsigned char *)&val64;
            ASSERT(df_size == 8);
            break;

// xxx bitfields do not handle straddle yet
        case CHexEditDoc::DF_BITFIELD8:
            ASSERT(df_size == 1);
            pdata = (unsigned char *)&val8;
			if (pdoc->GetData(pdata, 1, pdoc->df_address_[ii]) == 1)
			{
		        signed char tmp8;
				if (is_enum)
					tmp8 = (signed char)val64;
				else
					tmp8 = (signed char)strtoul(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);

				//unsigned char xxx_mask = ~(((1<<(pdoc->df_extra_[ii]>>8))-1) << (pdoc->df_extra_[ii]&0xFF));
				// Mask out (set to zero) the existing bits
				val8 &= ~(((1<<(pdoc->df_extra_[ii]>>8))-1) << (pdoc->df_extra_[ii]&0xFF));
				// Set the new values of the bits
				val8 |= tmp8 << (pdoc->df_extra_[ii]&0xFF);
			}
			break;
        case CHexEditDoc::DF_BITFIELD16:
            ASSERT(df_size == 2);
            pdata = (unsigned char *)&val16;
			if (pdoc->GetData(pdata, 2, pdoc->df_address_[ii]) == 2)
			{
		        unsigned short tmp16;
				if (is_enum)
					tmp16 = (unsigned short)val64;
				else
					tmp16 = (unsigned short)strtoul(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);

				// Mask out (set to zero) the existing bits
				val16 &= ~(((1<<(pdoc->df_extra_[ii]>>8))-1) << (pdoc->df_extra_[ii]&0xFF));
				// Set the new values of the bits
				val16 |= tmp16 << (pdoc->df_extra_[ii]&0xFF);
			}
            break;
        case CHexEditDoc::DF_BITFIELD32:
            ASSERT(df_size == 4);
            pdata = (unsigned char *)&val32;
			if (pdoc->GetData(pdata, 4, pdoc->df_address_[ii]) == 4)
			{
		        unsigned long tmp32;
				if (is_enum)
					tmp32 = (unsigned long)val64;
				else
					tmp32 = strtoul(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);

				// Mask out (set to zero) the existing bits
				val32 &= ~(((1<<(pdoc->df_extra_[ii]>>8))-1) << (pdoc->df_extra_[ii]&0xFF));
				// Set the new values of the bits
				val32 |= tmp32 << (pdoc->df_extra_[ii]&0xFF);
			}
            break;
        case CHexEditDoc::DF_BITFIELD64:
            ASSERT(df_size == 8);
            pdata = (unsigned char *)&val64;
			if (pdoc->GetData(pdata, 8, pdoc->df_address_[ii]) == 8)
			{
		        unsigned __int64 tmp64;
				if (is_enum)
					tmp64 = (unsigned __int64)val64;
				else
					tmp64 = (unsigned __int64)strtoul(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL, 0);

				// Mask out (set to zero) the existing bits
				val64 &= ~(((__int64(1)<<(pdoc->df_extra_[ii]>>8))-1) << (pdoc->df_extra_[ii]&0xFF));
				// Set the new values of the bits
				val64 |= tmp64 << (pdoc->df_extra_[ii]&0xFF);
			}
            break;

        case CHexEditDoc::DF_REAL32:
            real32 = (float)strtod(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL);
            pdata = (unsigned char *)&real32;
            ASSERT(df_size == 4);
            break;
        case CHexEditDoc::DF_REAL64:
            real64 = strtod(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL);
            pdata = (unsigned char *)&real64;
            ASSERT(df_size == 8);
            break;

        case CHexEditDoc::DF_IBMREAL32:
            real64 = strtod(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL);
            make_ibm_fp32((unsigned char *)&val32, real64);
            pdata = (unsigned char *)&val32;
            ASSERT(df_size == 4);
            break;
        case CHexEditDoc::DF_IBMREAL64:
            real64 = strtod(CString(grid_.GetItemText(pItem->iRow, pItem->iColumn)), NULL);
            make_ibm_fp64((unsigned char *)&val64, real64);
            pdata = (unsigned char *)&val64;
            ASSERT(df_size == 8);
            break;

        case CHexEditDoc::DF_DATEC:
			if (odt.GetStatus() == COleDateTime::valid)
                val32 = time_t((odt.m_dt - (365.0*70.0 + 17 + 2) - tz_diff)*(24.0*60.0*60.0) + 0.5);
			else
				val32 = (time_t)-1;
            pdata = (unsigned char *)&val32;
			ASSERT(df_size == 4);
			break;
        case CHexEditDoc::DF_DATECMIN:
			if (odt.GetStatus() == COleDateTime::valid)
                val32 = long((odt.m_dt - (365.0*70.0 + 17 + 2) - tz_diff)*(24.0*60.0) + 0.5);
			else
				val32 = -1L;
            pdata = (unsigned char *)&val32;
			ASSERT(df_size == 4);
			break;
        case CHexEditDoc::DF_DATEC51:
			if (odt.GetStatus() == COleDateTime::valid)
                val32 = long((odt.m_dt - (365.0*80.0 + 17 + 2) - tz_diff)*(24.0*60.0) + 0.5);
			else
				val32 = -1L;
            pdata = (unsigned char *)&val32;
			ASSERT(df_size == 4);
			break;
        case CHexEditDoc::DF_DATEC7:
			if (odt.GetStatus() == COleDateTime::valid)
                val32 = (unsigned long)((odt.m_dt - tz_diff)*(24.0*60.0*60.0) + 0.5);
			else
				val32 = (time_t)-1;
            pdata = (unsigned char *)&val32;
			ASSERT(df_size == 4);
			break;
        case CHexEditDoc::DF_DATEOLE:
			pdata = (unsigned char *)&odt.m_dt;
			ASSERT(df_size == 4);
			break;
        case CHexEditDoc::DF_DATESYSTEMTIME:
			if (odt.GetStatus() == COleDateTime::valid)
            {
                st.wYear = odt.GetYear();
                st.wMonth = odt.GetMonth();
                st.wDay = odt.GetDay();
                st.wDayOfWeek = odt.GetDayOfWeek();
                st.wHour = odt.GetHour();
                st.wMinute = odt.GetMinute();
                st.wSecond = odt.GetSecond();
                st.wMilliseconds = 500;
			}
			else
                memset(&st, '\0', sizeof(st));
			pdata = (unsigned char *)&st;
			ASSERT(df_size == sizeof(st));
			break;
        case CHexEditDoc::DF_DATEFILETIME:
			if (odt.GetStatus() == COleDateTime::valid)
            {
                st.wYear = odt.GetYear();
                st.wMonth = odt.GetMonth();
                st.wDay = odt.GetDay();
                st.wDayOfWeek = odt.GetDayOfWeek();
                st.wHour = odt.GetHour();
                st.wMinute = odt.GetMinute();
                st.wSecond = odt.GetSecond();
                st.wMilliseconds = 500;
                FILETIME ft2;
                SystemTimeToFileTime(&st, &ft2);
                LocalFileTimeToFileTime(&ft2, &ft);
			}
			else
                memset(&ft, '\0', sizeof(ft));
			pdata = (unsigned char *)&ft;
			ASSERT(df_size == sizeof(ft));
			break;
        case CHexEditDoc::DF_DATEMSDOS:
			pdata = (unsigned char *)&val32;
			ASSERT(df_size == 4);
			if (odt.GetStatus() == COleDateTime::valid)
            {
                st.wYear = odt.GetYear();
                st.wMonth = odt.GetMonth();
                st.wDay = odt.GetDay();
                st.wDayOfWeek = odt.GetDayOfWeek();
                st.wHour = odt.GetHour();
                st.wMinute = odt.GetMinute();
                st.wSecond = odt.GetSecond();
                st.wMilliseconds = 500;
                FILETIME ft2;
                SystemTimeToFileTime(&st, &ft2);
                LocalFileTimeToFileTime(&ft2, &ft);
                FileTimeToDosDateTime(&ft, (LPWORD)(pdata + 2), (LPWORD)pdata);
            }
			else
				memset(pdata, 0, 4);
			break;
        case CHexEditDoc::DF_DATEC64:
			break;
        }

		// Make data big-endian (if pdoc->df_type_[ii] < 0)
        if (pdoc->df_type_[ii] < 0 && df_type == CHexEditDoc::DF_WSTRING)
		{
			// For strings we use 'new_size' since the string length may have been altered
			ASSERT(new_size%2 == 0);
            flip_each_word(pdata, new_size);     // Convert edited Unicode string to big-endian before storing
		}
        else if (pdoc->df_type_[ii] < 0 && df_type == CHexEditDoc::DF_DATESYSTEMTIME)
		{
			ASSERT(df_size == 16);
			flip_each_word(pdata, df_size);
		}
        else if (pdoc->df_type_[ii] < 0)
		{
			ASSERT(df_size == 2 || df_size == 4 || df_size == 8);  // Numeric types only have these sizes
            flip_bytes(pdata, df_size);       // Convert little-endian back to big-endian to store
		}

        if (new_size != df_size)
        {
            pdoc->Change(mod_delforw, pdoc->df_address_[ii], df_size, NULL, 0, psis_);
            pdoc->Change(mod_insert, pdoc->df_address_[ii], new_size, pdata, 0, psis_);
			pdoc->df_size_[ii] = new_size;
        }
        else
            pdoc->Change(mod_replace, pdoc->df_address_[ii], df_size, pdata, 0, psis_);

        // We only really need to do this for date (date-time ctrl) and enum (drop list) cells
//        grid_.SetCellType(pItem->iRow, pItem->iColumn, RUNTIME_CLASS(CGridCell));
//        edit_row_type_changed_ = -1;
    }
}

void CDataFormatView::OnEditCopy() 
{
	grid_.OnEditCopy();	
}

void CDataFormatView::OnUpdateEditCopyOrCut(CCmdUI* pCmdUI) 
{
	pCmdUI->Enable(grid_.GetSelectedCount() > 0);
}

// Printing
BOOL CDataFormatView::OnPreparePrinting(CPrintInfo* pInfo) 
{
    return DoPreparePrinting(pInfo);
}

void CDataFormatView::OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo) 
{
    grid_.OnBeginPrinting(pDC, pInfo);
}

void CDataFormatView::OnPrint(CDC* pDC, CPrintInfo* pInfo) 
{
    grid_.OnPrint(pDC, pInfo);
}

void CDataFormatView::OnEndPrinting(CDC* pDC, CPrintInfo* pInfo) 
{
    grid_.OnEndPrinting(pDC, pInfo);
}

void CDataFormatView::OnFilePrintPreview()
{
    BCGPrintPreview(this);
}

void CDataFormatView::OnViewtest() 
{
    CWaitCursor wc;
    int first = rand1();
    int second = rand1();
    __int64 ii;

    for (ii = 0; ii < 0xFffffFFFF; ++ii)
    {
        if (rand1() == first && rand1() == second)
            break;
    }
    if (ii == 0xFffffFFFF)
        AfxMessageBox("Cycle > 2^36");
    else
    {
        char buf[64];
        sprintf(buf, "Cycle = %I64d", ii);
        AfxMessageBox(buf);
    }
}