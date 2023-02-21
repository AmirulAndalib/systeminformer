/*
 * Copyright (c) 2022 Winsider Seminars & Solutions, Inc.  All rights reserved.
 *
 * This file is part of System Informer.
 *
 * Authors:
 *
 *     jxy-s    2023
 *
 */

#include <peview.h>

typedef struct _PVP_PE_DYNAMIC_RELOCATION_CONTEXT
{
    HWND WindowHandle;
    HWND ListViewHandle;
    PH_LAYOUT_MANAGER LayoutManager;
    PPV_PROPPAGECONTEXT PropSheetContext;
} PVP_PE_DYNAMIC_RELOCATION_CONTEXT, *PPVP_PE_DYNAMIC_RELOCATION_CONTEXT;

VOID PvEnumerateDynamicRelocationEntries(
    _In_ HWND ListViewHandle
    )
{
    ULONG count = 0;
    PH_MAPPED_IMAGE_DYNAMIC_RELOC relocations;

    ExtendedListView_SetRedraw(ListViewHandle, FALSE);
    ListView_DeleteAllItems(ListViewHandle);

    if (NT_SUCCESS(PhGetMappedImageDynamicRelocations(&PvMappedImage, &relocations)))
    {
        for (ULONG i = 0; i < relocations.NumberOfEntries; i++)
        {
            PPH_IMAGE_DYNAMIC_RELOC_ENTRY entry = &relocations.RelocationEntries[i];
            INT lvItemIndex;
            WCHAR value[PH_INT64_STR_LEN_1];

            PhPrintUInt64(value, ++count);
            lvItemIndex = PhAddListViewItem(ListViewHandle, MAXINT, value, NULL);

            if (entry->Symbol == IMAGE_DYNAMIC_RELOCATION_ARM64X)
            {
                PhPrintPointer(value, PTR_ADD_OFFSET(entry->ARM64X.BlockRva, entry->ARM64X.RecordFixup.Offset));
                PhSetListViewSubItem(ListViewHandle, lvItemIndex, 1, value);
                PhSetListViewSubItem(ListViewHandle, lvItemIndex, 2, L"ARM64X");

                switch (entry->ARM64X.RecordFixup.Type)
                {
                case IMAGE_DVRT_ARM64X_FIXUP_TYPE_ZEROFILL:
                    switch (entry->ARM64X.RecordFixup.Size)
                    {
                    case IMAGE_DVRT_ARM64X_FIXUP_SIZE_2BYTES:
                        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, L"0x0000");
                        break;
                    case IMAGE_DVRT_ARM64X_FIXUP_SIZE_4BYTES:
                        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, L"0x00000000");
                        break;
                    case IMAGE_DVRT_ARM64X_FIXUP_SIZE_8BYTES:
                        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, L"0x0000000000000000");
                        break;
                    default:
                        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, L"UNKNOWN");
                        break;
                    }
                    break;
                case IMAGE_DVRT_ARM64X_FIXUP_TYPE_VALUE:
                    switch (entry->ARM64X.RecordFixup.Size)
                    {
                    case IMAGE_DVRT_ARM64X_FIXUP_SIZE_2BYTES:
                        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, PhFormatString(L"0x%04x", entry->ARM64X.Value2)->Buffer);
                        break;
                    case IMAGE_DVRT_ARM64X_FIXUP_SIZE_4BYTES:
                        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, PhFormatString(L"0x%08lx", entry->ARM64X.Value4)->Buffer);
                        break;
                    case IMAGE_DVRT_ARM64X_FIXUP_SIZE_8BYTES:
                        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, PhFormatString(L"0x%016llx", entry->ARM64X.Value8)->Buffer);
                        break;
                    default:
                        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, L"UNKNOWN");
                        break;
                    }
                    break;
                case IMAGE_DVRT_ARM64X_FIXUP_TYPE_DELTA:
                    {
                    if (entry->ARM64X.RecordDelta.Sign)
                        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, PhFormatString(L"-0x%llx", entry->ARM64X.Delta)->Buffer);
                    else
                        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, PhFormatString(L"+0x%llx", entry->ARM64X.Delta)->Buffer);
                    }
                    break;
                default:
                    PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, L"UNKNOWN");
                    break;
                }

                if (entry->MappedImageVa)
                {
                    PIMAGE_SECTION_HEADER section;
                    PPH_STRING symbol;
                    
                    section = PhMappedImageRvaToSection(
                        &PvMappedImage,
                        PtrToUlong(PTR_SUB_OFFSET(entry->MappedImageVa, PvMappedImage.ViewBase))
                        );
                    if (section)
                    {
                        WCHAR sectionName[IMAGE_SIZEOF_SHORT_NAME + 1];

                        if (PhGetMappedImageSectionName(
                            section,
                            sectionName,
                            RTL_NUMBER_OF(sectionName),
                            NULL
                            ))
                        {
                            PhSetListViewSubItem(ListViewHandle, lvItemIndex, 4, sectionName);
                        }
                    }

                    symbol = PhGetSymbolFromAddress(
                        PvSymbolProvider,
                        (ULONG64)entry->ImageBaseVa,
                        NULL,
                        NULL,
                        NULL,
                        NULL
                        );
                    if (symbol)
                    {
                        PhSetListViewSubItem(ListViewHandle, lvItemIndex, 5, symbol->Buffer);
                        PhDereferenceObject(symbol);
                    }
                }
            }
            else
            {
                // TODO(jxy-s) not implemented, make it obvious
                PhSetListViewSubItem(ListViewHandle, lvItemIndex, 3, L"NOT IMPLEMENTED");
                continue;
            }
        }
    }

    //ExtendedListView_SortItems(ListViewHandle);
    ExtendedListView_SetRedraw(ListViewHandle, TRUE);
}

INT_PTR CALLBACK PvpPeDynamicRelocationDlgProc(
    _In_ HWND hwndDlg,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam
    )
{
    PPVP_PE_DYNAMIC_RELOCATION_CONTEXT context;

    if (uMsg == WM_INITDIALOG)
    {
        context = PhAllocateZero(sizeof(PVP_PE_DYNAMIC_RELOCATION_CONTEXT));
        PhSetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT, context);

        if (lParam)
        {
            LPPROPSHEETPAGE propSheetPage = (LPPROPSHEETPAGE)lParam;
            context->PropSheetContext = (PPV_PROPPAGECONTEXT)propSheetPage->lParam;
        }
    }
    else
    {
        context = PhGetWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
    }

    if (!context)
        return FALSE;

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            context->WindowHandle = hwndDlg;
            context->ListViewHandle = GetDlgItem(hwndDlg, IDC_LIST);

            PhSetListViewStyle(context->ListViewHandle, TRUE, TRUE);
            PhSetControlTheme(context->ListViewHandle, L"explorer");
            PhAddListViewColumn(context->ListViewHandle, 0, 0, 0, LVCFMT_LEFT, 50, L"#");
            PhAddListViewColumn(context->ListViewHandle, 1, 1, 1, LVCFMT_LEFT, 100, L"RVA");
            PhAddListViewColumn(context->ListViewHandle, 2, 2, 2, LVCFMT_LEFT, 100, L"Type");
            PhAddListViewColumn(context->ListViewHandle, 3, 3, 3, LVCFMT_LEFT, 100, L"Info");
            PhAddListViewColumn(context->ListViewHandle, 4, 4, 4, LVCFMT_LEFT, 100, L"Section");
            PhAddListViewColumn(context->ListViewHandle, 5, 5, 5, LVCFMT_LEFT, 300, L"Symbol");
            PhSetExtendedListView(context->ListViewHandle);
            //PhLoadListViewColumnsFromSetting(L"ImageDynamicRelocationsListViewColumns", context->ListViewHandle);
            PvConfigTreeBorders(context->ListViewHandle);

            PhInitializeLayoutManager(&context->LayoutManager, hwndDlg);
            PhAddLayoutItem(&context->LayoutManager, context->ListViewHandle, NULL, PH_ANCHOR_ALL);

            PvEnumerateDynamicRelocationEntries(context->ListViewHandle);

            PhInitializeWindowTheme(hwndDlg, PeEnableThemeSupport);
        }
        break;
    case WM_SHOWWINDOW:
        {
            if (context->PropSheetContext && !context->PropSheetContext->LayoutInitialized)
            {
                PvAddPropPageLayoutItem(hwndDlg, hwndDlg, PH_PROP_PAGE_TAB_CONTROL_PARENT, PH_ANCHOR_ALL);
                PvDoPropPageLayout(hwndDlg);

                context->PropSheetContext->LayoutInitialized = TRUE;
            }
        }
        break;
    case WM_DESTROY:
        {
            PhSaveListViewColumnsToSetting(L"ImageDynamicRelocationsListViewColumns", context->ListViewHandle);

            PhDeleteLayoutManager(&context->LayoutManager);

            PhRemoveWindowContext(hwndDlg, PH_WINDOW_CONTEXT_DEFAULT);
            PhFree(context);
        }
        break;
    case WM_SIZE:
        {
            PhLayoutManagerLayout(&context->LayoutManager);
        }
        break;
    case WM_NOTIFY:
        {
            PvHandleListViewNotifyForCopy(lParam, context->ListViewHandle);
        }
        break;
    case WM_CONTEXTMENU:
        {
            PvHandleListViewCommandCopy(hwndDlg, lParam, wParam, context->ListViewHandle);
        }
        break;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
        {
            SetBkMode((HDC)wParam, TRANSPARENT);
            SetTextColor((HDC)wParam, RGB(0, 0, 0));
            SetDCBrushColor((HDC)wParam, RGB(255, 255, 255));
            return (INT_PTR)GetStockBrush(DC_BRUSH);
        }
        break;
    }

    return FALSE;
}
