/*
 * Process Hacker Online Checks -
 *   Main Headers
 *
 * Copyright (C) 2010-2013 wj32
 * Copyright (C) 2012-2016 dmex
 *
 * This file is part of Process Hacker.
 *
 * Process Hacker is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Process Hacker is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Process Hacker.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ONLNCHK_H
#define ONLNCHK_H

#define CINTERFACE
#define COBJMACROS
#include <phdk.h>
#include <phappresource.h>
#include <settings.h>
#include <mxml.h>
#include <commonutil.h>
#include <workqueue.h>
#include <shlobj.h>
#include <windowsx.h>
#include <winhttp.h>
#include "resource.h"
#include "db.h"

#define PLUGIN_NAME L"ProcessHacker.OnlineChecks"
#define SETTING_NAME_VIRUSTOTAL_SCAN_ENABLED (PLUGIN_NAME L".EnableVirusTotalScanning")
#define SETTING_NAME_VIRUSTOTAL_HIGHLIGHT_DETECTIONS (PLUGIN_NAME L".VirusTotalHighlightDetection")

#define UM_UPLOAD (WM_APP + 1)
#define UM_EXISTS (WM_APP + 2)
#define UM_LAUNCH (WM_APP + 3)
#define UM_ERROR (WM_APP + 4)
#define UM_SHOWDIALOG (WM_APP + 5)

extern PPH_PLUGIN PluginInstance;

typedef struct _SERVICE_INFO
{
    ULONG Id;
    PWSTR HostName;
    INTERNET_PORT HostPort;
    ULONG HostFlags;
    PWSTR UploadObjectName;
    PWSTR FileNameFieldName;
} SERVICE_INFO, *PSERVICE_INFO;

typedef struct _PROCESS_EXTENSION
{
    LIST_ENTRY ListEntry;

    union
    {
        BOOLEAN Flags;
        struct
        {
            BOOLEAN Stage1 : 1;
            BOOLEAN ResultValid : 1;
            BOOLEAN Spare : 6;
        };
    };

    INT64 Retries;
    INT64 Positives;
    PPH_STRING VirusTotalResult;
    PPH_STRING FilePath;

    PPH_PROCESS_ITEM ProcessItem;
    PPH_MODULE_ITEM ModuleItem;
    PPH_SERVICE_ITEM ServiceItem;
} PROCESS_EXTENSION, *PPROCESS_EXTENSION;

typedef struct _VIRUSTOTAL_FILE_HASH_ENTRY
{
    union
    {
        BOOLEAN Flags;
        struct
        {
            BOOLEAN Stage1 : 1;
            BOOLEAN Processing : 1;
            BOOLEAN Processed : 1;
            BOOLEAN Found : 1;
            BOOLEAN Spare : 5;
        };
    };

    PPROCESS_EXTENSION Extension;

    INT64 Positives;
    INT64 Total;
    PPH_STRING FileName;
    PPH_STRING FileHash;
    PPH_BYTES FileNameAnsi;
    PPH_BYTES FileHashAnsi;
    PPH_BYTES CreationTime;
} VIRUSTOTAL_FILE_HASH_ENTRY, *PVIRUSTOTAL_FILE_HASH_ENTRY;

typedef struct _UPLOAD_CONTEXT
{
    BOOLEAN VtApiUpload;
    BOOLEAN FileExists;
    ULONG Service;
    ULONG ErrorCode;
    ULONG TotalFileLength;
    
    HWND DialogHandle;
    HANDLE UploadThreadHandle;
    HICON IconLargeHandle;
    HICON IconSmallHandle;
    HINTERNET HttpHandle;
    ITaskbarList3* TaskbarListClass;

    PPH_STRING FileSize;
    PPH_STRING ErrorString;
    PPH_STRING FileName;
    PPH_STRING BaseFileName;
    PPH_STRING WindowFileName;
    PPH_STRING LaunchCommand;
    PPH_STRING Detected;
    PPH_STRING MaxDetected;
    PPH_STRING UploadUrl;
    PPH_STRING ReAnalyseUrl;
    PPH_STRING FirstAnalysisDate;
    PPH_STRING LastAnalysisDate;
    PPH_STRING LastAnalysisUrl;
    PPH_STRING LastAnalysisAgo;
} UPLOAD_CONTEXT, *PUPLOAD_CONTEXT;

VOID ShowOptionsDialog(
    _In_opt_ HWND Parent
    );

NTSTATUS UploadFileThreadStart(
    _In_ PVOID Parameter
    );

NTSTATUS UploadCheckThreadStart(
    _In_ PVOID Parameter
    );

VOID ShowVirusTotalUploadDialog(
    _In_ PUPLOAD_CONTEXT Context
    );

VOID ShowFileFoundDialog(
    _In_ PUPLOAD_CONTEXT Context
    );

VOID ShowVirusTotalProgressDialog(
    _In_ PUPLOAD_CONTEXT Context
    );

VOID VirusTotalShowErrorDialog(
    _In_ PUPLOAD_CONTEXT Context
    );

typedef struct _VIRUSTOTAL_FILE_REPORT_RESULT
{
    PPH_STRING FileName;
    PPH_STRING BaseFileName;

    PPH_STRING Total;
    PPH_STRING Positives;
    PPH_STRING Resource;
    PPH_STRING ScanId;
    PPH_STRING Md5;
    PPH_STRING Sha1;
    PPH_STRING Sha256;
    PPH_STRING ScanDate;
    PPH_STRING Permalink;
    PPH_STRING StatusMessage;
    PPH_LIST ScanResults;
} VIRUSTOTAL_FILE_REPORT_RESULT, *PVIRUSTOTAL_FILE_REPORT_RESULT;

PPH_STRING VirusTotalStringToTime(
    _In_ PPH_STRING Time
    );

PVIRUSTOTAL_FILE_REPORT_RESULT VirusTotalSendHttpFileReportRequest(
    _In_ PPH_STRING FileHash
    );

// upload
#define ENABLE_SERVICE_VIRUSTOTAL 100
#define MENUITEM_VIRUSTOTAL_QUEUE 101
#define MENUITEM_VIRUSTOTAL_UPLOAD 102
#define MENUITEM_VIRUSTOTAL_UPLOAD_FILE 103
#define MENUITEM_JOTTI_UPLOAD 104

VOID UploadToOnlineService(
    _In_ PPH_STRING FileName,
    _In_ ULONG Service
    );

typedef enum _NETWORK_COLUMN_ID
{
    COLUMN_ID_VIUSTOTAL_PROCESS = 1,
    COLUMN_ID_VIUSTOTAL_MODULE = 2,
    COLUMN_ID_VIUSTOTAL_SERVICE = 3
} NETWORK_COLUMN_ID;

NTSTATUS HashFileAndResetPosition(
    _In_ HANDLE FileHandle,
    _In_ PLARGE_INTEGER FileSize,
    _In_ PH_HASH_ALGORITHM Algorithm,
    _Out_ PPH_STRING *HashString
    );

typedef struct _VIRUSTOTAL_API_RESULT
{
    BOOLEAN Found;
    INT64 Positives;
    INT64 Total;
    PPH_STRING FileHash;
} VIRUSTOTAL_API_RESULT, *PVIRUSTOTAL_API_RESULT;

extern PPH_LIST VirusTotalList;
extern BOOLEAN VirusTotalScanningEnabled;

PVIRUSTOTAL_FILE_HASH_ENTRY VirusTotalAddCacheResult(
    _In_ PPH_STRING FileName,
    _In_ PPROCESS_EXTENSION Extension
    );

PVIRUSTOTAL_FILE_HASH_ENTRY VirusTotalGetCachedResult(
    _In_ PPH_STRING FileName
    );

PPH_BYTES VirusTotalGetCachedDbHash(
    VOID
    );

VOID InitializeVirusTotalProcessMonitor(
    VOID
    );

VOID CleanupVirusTotalProcessMonitor(
    VOID
    );

NTSTATUS QueryServiceFileName(
    _In_ PPH_STRINGREF ServiceName,
    _Out_ PPH_STRING *ServiceFileName,
    _Out_ PPH_STRING *ServiceBinaryPath
    );

#endif
