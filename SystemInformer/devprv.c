/*
 * Copyright (c) 2022 Winsider Seminars & Solutions, Inc.  All rights reserved.
 *
 * This file is part of System Informer.
 *
 * Authors:
 *
 *     jxy-s   2022-2023
 *
 */

#include <phapp.h>
#include <phplug.h>
#include <settings.h>

#include <devprv.h>

#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <wdmguid.h>
#include <bthdef.h>
#include <devguid.h>
#include <usbiodef.h>

#undef DEFINE_GUID
#include <pciprop.h>
#include <ntddstor.h>

static PH_STRINGREF RootInstanceId = PH_STRINGREF_INIT(L"HTREE\\ROOT\\0");
PPH_OBJECT_TYPE PhDeviceTreeType = NULL;
PPH_OBJECT_TYPE PhDeviceItemType = NULL;
PPH_OBJECT_TYPE PhDeviceNotifyType = NULL;
static PPH_OBJECT_TYPE PhpDeviceInfoType = NULL;
static PPH_DEVICE_TREE PhpDeviceTree = NULL;
static PH_FAST_LOCK PhpDeviceTreeLock = PH_FAST_LOCK_INIT;
static HCMNOTIFICATION PhpDeviceNotification = NULL;
static HCMNOTIFICATION PhpDeviceInterfaceNotification = NULL;
static PH_FAST_LOCK PhpDeviceNotifyLock = PH_FAST_LOCK_INIT;
static HANDLE PhpDeviceNotifyEvent = NULL;
static LIST_ENTRY PhpDeviceNotifyList = { 0 };

#if !defined(NTDDI_WIN10_NI) || (NTDDI_VERSION < NTDDI_WIN10_NI)
// Note: This propkey is required for building with 22H1 and older Windows SDK (dmex)
DEFINE_DEVPROPKEY(DEVPKEY_Device_FirmwareVendor, 0x540b947e, 0x8b40, 0x45bc, 0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2, 26);   // DEVPROP_TYPE_STRING
#endif

#define DEVPROP_FILL_FLAG_CLASS_INTERFACE 0x00000001
#define DEVPROP_FILL_FLAG_CLASS_INSTALLER 0x00000002

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
typedef
VOID
NTAPI
PH_DEVICE_PROPERTY_FILL_CALLBACK(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    );
typedef PH_DEVICE_PROPERTY_FILL_CALLBACK* PPH_DEVICE_PROPERTY_FILL_CALLBACK;

typedef struct _PH_DEVICE_PROPERTY_TABLE_ENTRY
{
    PH_DEVICE_PROPERTY_CLASS PropClass;
    const DEVPROPKEY* PropKey;
    PPH_DEVICE_PROPERTY_FILL_CALLBACK Callback;
    ULONG CallbackFlags;
} PH_DEVICE_PROPERTY_TABLE_ENTRY, *PPH_DEVICE_PROPERTY_TABLE_ENTRY;

BOOLEAN PhpGetDevicePropertyGuid(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* DeviceProperty,
    _Out_ PGUID Guid
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = sizeof(GUID);

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)Guid,
        sizeof(GUID),
        &requiredLength,
        0
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_GUID))
    {
        return TRUE;
    }

    RtlZeroMemory(Guid, sizeof(GUID));

    return FALSE;
}

BOOLEAN PhpGetClassPropertyGuid(
    _In_ const GUID* ClassGuid,
    _In_ const DEVPROPKEY* DeviceProperty,
    _In_ ULONG Flags,
    _Out_ PGUID Guid
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = sizeof(GUID);

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)Guid,
        sizeof(GUID),
        &requiredLength,
        Flags
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_GUID))
    {
        return TRUE;
    }

    RtlZeroMemory(Guid, sizeof(GUID));

    return FALSE;
}

BOOLEAN PhpGetDevicePropertyUInt64(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* DeviceProperty,
    _Out_ PULONG64 Value
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = sizeof(ULONG64);

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)Value,
        sizeof(ULONG64),
        &requiredLength,
        0
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_UINT64))
    {
        return TRUE;
    }

    *Value = 0;

    return FALSE;
}

BOOLEAN PhpGetClassPropertyUInt64(
    _In_ const GUID* ClassGuid,
    _In_ const DEVPROPKEY* DeviceProperty,
    _In_ ULONG Flags,
    _Out_ PULONG64 Value
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = sizeof(ULONG64);

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)Value,
        sizeof(ULONG64),
        &requiredLength,
        Flags
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_UINT64))
    {
        return TRUE;
    }

    *Value = 0;

    return FALSE;
}

BOOLEAN PhpGetDevicePropertyUInt32(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* DeviceProperty,
    _Out_ PULONG Value
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = sizeof(ULONG);

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)Value,
        sizeof(ULONG),
        &requiredLength,
        0
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_UINT32))
    {
        return TRUE;
    }

    *Value = 0;

    return FALSE;
}

BOOLEAN PhpGetClassPropertyUInt32(
    _In_ const GUID* ClassGuid,
    _In_ const DEVPROPKEY* DeviceProperty,
    _In_ ULONG Flags,
    _Out_ PULONG Value
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = sizeof(ULONG);

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)Value,
        sizeof(ULONG),
        &requiredLength,
        Flags
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_UINT32))
    {
        return TRUE;
    }

    *Value = 0;

    return FALSE;
}

BOOLEAN PhpGetDevicePropertyInt32(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* DeviceProperty,
    _Out_ PLONG Value
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = sizeof(LONG);

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)Value,
        sizeof(LONG),
        &requiredLength,
        0
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_INT32))
    {
        return TRUE;
    }

    *Value = 0;

    return FALSE;
}

BOOLEAN PhpGetClassPropertyInt32(
    _In_ const GUID* ClassGuid,
    _In_ const DEVPROPKEY* DeviceProperty,
    _In_ ULONG Flags,
    _Out_ PLONG Value
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = sizeof(LONG);

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)Value,
        sizeof(LONG),
        &requiredLength,
        Flags
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_INT32))
    {
        return TRUE;
    }

    *Value = 0;

    return FALSE;
}

BOOLEAN PhpGetDevicePropertyNTSTATUS(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* DeviceProperty,
    _Out_ PNTSTATUS Status
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = sizeof(NTSTATUS);

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)Status,
        sizeof(NTSTATUS),
        &requiredLength,
        0
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_NTSTATUS))
    {
        return TRUE;
    }

    *Status = 0;

    return FALSE;
}

BOOLEAN PhpGetClassPropertyNTSTATUS(
    _In_ const GUID* ClassGuid,
    _In_ const DEVPROPKEY* DeviceProperty,
    _In_ ULONG Flags,
    _Out_ PNTSTATUS Status
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = sizeof(NTSTATUS);

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)Status,
        sizeof(NTSTATUS),
        &requiredLength,
        Flags
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_NTSTATUS))
    {
        return TRUE;
    }

    *Status = 0;

    return FALSE;
}

BOOLEAN PhpGetDevicePropertyBoolean(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* DeviceProperty,
    _Out_ PBOOLEAN Boolean
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    DEVPROP_BOOLEAN boolean;
    ULONG requiredLength = sizeof(DEVPROP_BOOLEAN);

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)&boolean,
        sizeof(DEVPROP_BOOLEAN),
        &requiredLength,
        0
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_BOOLEAN))
    {
        *Boolean = boolean == DEVPROP_TRUE;

        return TRUE;
    }

    *Boolean = FALSE;

    return FALSE;
}

BOOLEAN PhpGetClassPropertyBoolean(
    _In_ const GUID* ClassGuid,
    _In_ const DEVPROPKEY* DeviceProperty,
    _In_ ULONG Flags,
    _Out_ PBOOLEAN Boolean
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    DEVPROP_BOOLEAN boolean;
    ULONG requiredLength = sizeof(DEVPROP_BOOLEAN);

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)&boolean,
        sizeof(DEVPROP_BOOLEAN),
        &requiredLength,
        Flags
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_BOOLEAN))
    {
        *Boolean = boolean == DEVPROP_TRUE;

        return TRUE;
    }

    *Boolean = FALSE;

    return FALSE;
}

BOOLEAN PhpGetDevicePropertyTimeStamp(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* DeviceProperty,
    _Out_ PLARGE_INTEGER TimeStamp
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    FILETIME fileTime;
    ULONG requiredLength = sizeof(FILETIME);

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)&fileTime,
        sizeof(FILETIME),
        &requiredLength,
        0
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_FILETIME))
    {
        TimeStamp->HighPart = fileTime.dwHighDateTime;
        TimeStamp->LowPart = fileTime.dwLowDateTime;

        return TRUE;
    }

    TimeStamp->QuadPart = 0;

    return FALSE;
}

BOOLEAN PhpGetClassPropertyTimeStamp(
    _In_ const GUID* ClassGuid,
    _In_ const DEVPROPKEY* DeviceProperty,
    _In_ ULONG Flags,
    _Out_ PLARGE_INTEGER TimeStamp
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    FILETIME fileTime;
    ULONG requiredLength = sizeof(FILETIME);

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        (PBYTE)&fileTime,
        sizeof(FILETIME),
        &requiredLength,
        Flags
        );
    if (result && (devicePropertyType == DEVPROP_TYPE_FILETIME))
    {
        TimeStamp->HighPart = fileTime.dwHighDateTime;
        TimeStamp->LowPart = fileTime.dwLowDateTime;

        return TRUE;
    }

    TimeStamp->QuadPart = 0;

    return FALSE;
}

BOOLEAN PhpGetDevicePropertyString(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* DeviceProperty,
    _Out_ PPH_STRING* String
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = 0;
    PVOID buffer = NULL;

    *String = NULL;

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        NULL,
        0,
        &requiredLength,
        0
        );
    if (result ||
        (requiredLength == 0) ||
        (GetLastError() != ERROR_INSUFFICIENT_BUFFER) ||
        ((devicePropertyType != DEVPROP_TYPE_STRING) &&
         (devicePropertyType != DEVPROP_TYPE_SECURITY_DESCRIPTOR_STRING)))
    {
        goto Exit;
    }

    buffer = PhAllocate(requiredLength);

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        buffer,
        requiredLength,
        &requiredLength,
        0
        );
    if (!result)
    {
        goto Exit;
    }

    *String = PhCreateString(buffer);

Exit:

    if (buffer)
        PhFree(buffer);

    return !!result;
}

BOOLEAN PhpGetClassPropertyString(
    _In_ const GUID* ClassGuid,
    _In_ const DEVPROPKEY* DeviceProperty,
    _In_ ULONG Flags,
    _Out_ PPH_STRING* String
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = 0;
    PVOID buffer = NULL;

    *String = NULL;

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        NULL,
        0,
        &requiredLength,
        Flags
        );
    if (result ||
        (requiredLength == 0) ||
        (GetLastError() != ERROR_INSUFFICIENT_BUFFER) ||
        ((devicePropertyType != DEVPROP_TYPE_STRING) &&
         (devicePropertyType != DEVPROP_TYPE_SECURITY_DESCRIPTOR_STRING)))
    {
        goto Exit;
    }

    buffer = PhAllocate(requiredLength);

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        buffer,
        requiredLength,
        &requiredLength,
        Flags
        );
    if (!result)
    {
        goto Exit;
    }

    *String = PhCreateString(buffer);

Exit:

    if (buffer)
        PhFree(buffer);

    return !!result;
}

BOOLEAN PhpGetDevicePropertyStringList(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* DeviceProperty,
    _Out_ PPH_LIST* StringList
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = 0;
    PVOID buffer = NULL;
    PPH_LIST stringList;

    *StringList = NULL;

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        NULL,
        0,
        &requiredLength,
        0
        );
    if (result ||
        (requiredLength == 0) ||
        (GetLastError() != ERROR_INSUFFICIENT_BUFFER) ||
        (devicePropertyType != DEVPROP_TYPE_STRING_LIST))
    {
        goto Exit;
    }

    buffer = PhAllocate(requiredLength);

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        buffer,
        requiredLength,
        &requiredLength,
        0
        );
    if (!result)
    {
        goto Exit;
    }

    stringList = PhCreateList(1);

    for (PZZWSTR item = buffer;;)
    {
        UNICODE_STRING string;

        RtlInitUnicodeString(&string, item);

        if (string.Length == 0)
        {
            break;
        }

        PhAddItemList(stringList, PhCreateStringFromUnicodeString(&string));

        item = PTR_ADD_OFFSET(item, string.MaximumLength);
    }

    *StringList = stringList;

Exit:

    if (buffer)
        PhFree(buffer);

    return !!result;
}

BOOLEAN PhpGetClassPropertyStringList(
    _In_ const GUID* ClassGuid,
    _In_ const DEVPROPKEY* DeviceProperty,
    _In_ ULONG Flags,
    _Out_ PPH_LIST* StringList
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = 0;
    PVOID buffer = NULL;
    PPH_LIST stringList;

    *StringList = NULL;

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        NULL,
        0,
        &requiredLength,
        Flags
        );
    if (result ||
        (requiredLength == 0) ||
        (GetLastError() != ERROR_INSUFFICIENT_BUFFER) ||
        (devicePropertyType != DEVPROP_TYPE_STRING_LIST))
    {
        goto Exit;
    }

    buffer = PhAllocate(requiredLength);

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        buffer,
        requiredLength,
        &requiredLength,
        Flags
        );
    if (!result)
    {
        goto Exit;
    }

    stringList = PhCreateList(1);

    for (PZZWSTR item = buffer;;)
    {
        PH_STRINGREF string;

        PhInitializeStringRefLongHint(&string, item);

        if (string.Length == 0)
        {
            break;
        }

        PhAddItemList(stringList, PhCreateString2(&string));

        item = PTR_ADD_OFFSET(item, string.Length + sizeof(UNICODE_NULL));
    }

    *StringList = stringList;

Exit:

    if (buffer)
        PhFree(buffer);

    return !!result;
}

BOOLEAN PhpGetDevicePropertyBinary(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* DeviceProperty,
    _Out_ PBYTE* Buffer,
    _Out_ PULONG Size
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = 0;
    PVOID buffer = NULL;

    *Buffer = NULL;
    *Size = 0;

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        NULL,
        0,
        &requiredLength,
        0
        );
    if (result ||
        (requiredLength == 0) ||
        (GetLastError() != ERROR_INSUFFICIENT_BUFFER) ||
        ((devicePropertyType != DEVPROP_TYPE_BINARY) &&
         (devicePropertyType != DEVPROP_TYPE_SECURITY_DESCRIPTOR)))
    {
        goto Exit;
    }

    buffer = PhAllocate(requiredLength);

    result = SetupDiGetDevicePropertyW(
        DeviceInfoSet,
        DeviceInfoData,
        DeviceProperty,
        &devicePropertyType,
        buffer,
        requiredLength,
        &requiredLength,
        0
        );
    if (!result)
    {
        goto Exit;
    }

    *Size = requiredLength;
    *Buffer = buffer;
    buffer = NULL;

Exit:

    if (buffer)
        PhFree(buffer);

    return !!result;
}

BOOLEAN PhpGetClassPropertyBinary(
    _In_ const GUID* ClassGuid,
    _In_ const DEVPROPKEY* DeviceProperty,
    _In_ ULONG Flags,
    _Out_ PBYTE* Buffer,
    _Out_ PULONG Size
    )
{
    BOOL result;
    DEVPROPTYPE devicePropertyType = DEVPROP_TYPE_EMPTY;
    ULONG requiredLength = 0;
    PVOID buffer = NULL;

    *Buffer = NULL;
    *Size = 0;

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        NULL,
        0,
        &requiredLength,
        Flags
        );
    if (result ||
        (requiredLength == 0) ||
        (GetLastError() != ERROR_INSUFFICIENT_BUFFER) ||
        ((devicePropertyType != DEVPROP_TYPE_BINARY) &&
         (devicePropertyType != DEVPROP_TYPE_SECURITY_DESCRIPTOR)))
    {
        goto Exit;
    }

    buffer = PhAllocate(requiredLength);

    result = SetupDiGetClassPropertyW(
        ClassGuid,
        DeviceProperty,
        &devicePropertyType,
        buffer,
        requiredLength,
        &requiredLength,
        Flags
        );
    if (!result)
    {
        goto Exit;
    }

    *Size = requiredLength;
    *Buffer = buffer;
    buffer = NULL;

Exit:

    if (buffer)
        PhFree(buffer);

    return !!result;
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillString(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeString;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyString(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->String
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyString(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->String
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyString(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->String
            );
    }

    if (Property->Valid)
    {
        Property->AsString = Property->String;
        PhReferenceObject(Property->AsString);
    }
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillUInt64(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeUInt64;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyUInt64(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->UInt64
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyUInt64(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->UInt64
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyUInt64(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->UInt64
            );
    }

    if (Property->Valid)
    {
        PH_FORMAT format[1];

        PhInitFormatI64U(&format[0], Property->UInt64);

        Property->AsString = PhFormat(format, ARRAYSIZE(format), 1);
    }
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillUInt64Hex(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeUInt64;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyUInt64(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->UInt64
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyUInt64(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->UInt64
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyUInt64(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->UInt64
            );
    }

    if (Property->Valid)
    {
        PH_FORMAT format[2];

        PhInitFormatI64X(&format[1], Property->UInt64);

        Property->AsString = PhFormat(format, ARRAYSIZE(format), 10);
    }
}


_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillUInt32(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeUInt32;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyUInt32(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->UInt32
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyUInt32(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->UInt32
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyUInt32(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->UInt32
            );
    }

    if (Property->Valid)
    {
        PH_FORMAT format[1];

        PhInitFormatU(&format[0], Property->UInt32);

        Property->AsString = PhFormat(format, ARRAYSIZE(format), 1);
    }
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillUInt32Hex(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeUInt32;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyUInt32(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->UInt32
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyUInt32(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->UInt32
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyUInt32(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->UInt32
            );
    }

    if (Property->Valid)
    {
        PH_FORMAT format[2];

        PhInitFormatS(&format[0], L"0x");
        PhInitFormatIX(&format[1], Property->UInt32);

        Property->AsString = PhFormat(format, ARRAYSIZE(format), 10);
    }
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillInt32(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeUInt32;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyInt32(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->Int32
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyInt32(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->Int32
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyInt32(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->Int32
            );
    }

    if (Property->Valid)
    {
        PH_FORMAT format[1];

        PhInitFormatD(&format[0], Property->Int32);

        Property->AsString = PhFormat(format, ARRAYSIZE(format), 1);
    }
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillNTSTATUS(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeNTSTATUS;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyNTSTATUS(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->Status
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyNTSTATUS(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->Status
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyNTSTATUS(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->Status
            );
    }

    if (Property->Valid && Property->Status != STATUS_SUCCESS)
    {
        Property->AsString = PhGetStatusMessage(Property->Status, 0);
    }
}

typedef struct _PH_WELL_KNOWN_GUID
{
    const GUID* Guid;
    PH_STRINGREF Symbol;
} PH_WELL_KNOWN_GUID, *PPH_WELL_KNOWN_GUID;
#define PH_DEFINE_WELL_KNOWN_GUID(guid) const PH_WELL_KNOWN_GUID PH_WELL_KNOWN_##guid = { &guid, PH_STRINGREF_INIT(TEXT(#guid)) }

PH_DEFINE_WELL_KNOWN_GUID(GUID_HWPROFILE_QUERY_CHANGE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_HWPROFILE_CHANGE_CANCELLED);
PH_DEFINE_WELL_KNOWN_GUID(GUID_HWPROFILE_CHANGE_COMPLETE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVICE_INTERFACE_ARRIVAL);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVICE_INTERFACE_REMOVAL);
PH_DEFINE_WELL_KNOWN_GUID(GUID_TARGET_DEVICE_QUERY_REMOVE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_TARGET_DEVICE_REMOVE_CANCELLED);
PH_DEFINE_WELL_KNOWN_GUID(GUID_TARGET_DEVICE_REMOVE_COMPLETE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PNP_CUSTOM_NOTIFICATION);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PNP_POWER_NOTIFICATION);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PNP_POWER_SETTING_CHANGE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_TARGET_DEVICE_TRANSPORT_RELATIONS_CHANGED);
PH_DEFINE_WELL_KNOWN_GUID(GUID_KERNEL_SOFT_RESTART_PREPARE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_KERNEL_SOFT_RESTART_CANCEL);
PH_DEFINE_WELL_KNOWN_GUID(GUID_RECOVERY_PCI_PREPARE_SHUTDOWN);
PH_DEFINE_WELL_KNOWN_GUID(GUID_RECOVERY_NVMED_PREPARE_SHUTDOWN);
PH_DEFINE_WELL_KNOWN_GUID(GUID_KERNEL_SOFT_RESTART_FINALIZE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCI_BUS_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCI_BUS_INTERFACE_STANDARD2);
PH_DEFINE_WELL_KNOWN_GUID(GUID_ARBITER_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_TRANSLATOR_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_ACPI_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_INT_ROUTE_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCMCIA_BUS_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_ACPI_REGS_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_LEGACY_DEVICE_DETECTION_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCI_DEVICE_PRESENT_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_MF_ENUMERATION_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_REENUMERATE_SELF_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_AGP_TARGET_BUS_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_ACPI_CMOS_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_ACPI_PORT_RANGES_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_ACPI_INTERFACE_STANDARD2);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PNP_LOCATION_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCI_EXPRESS_LINK_QUIESCENT_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCI_EXPRESS_ROOT_PORT_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_MSIX_TABLE_CONFIG_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_D3COLD_SUPPORT_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PROCESSOR_PCC_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCI_VIRTUALIZATION_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCC_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCC_INTERFACE_INTERNAL);
PH_DEFINE_WELL_KNOWN_GUID(GUID_THERMAL_COOLING_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DMA_CACHE_COHERENCY_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVICE_RESET_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_IOMMU_BUS_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCI_SECURITY_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_SCM_BUS_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_SECURE_DRIVER_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_SDEV_IDENTIFIER_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_SCM_BUS_NVD_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_SCM_BUS_LD_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_SCM_PHYSICAL_NVDIMM_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PNP_EXTENDED_ADDRESS_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_D3COLD_AUX_POWER_AND_TIMING_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCI_FPGA_CONTROL_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCI_PTM_CONTROL_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_RESOURCE_UPDATE_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_NPEM_CONTROL_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PCI_ATS_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_INTERNAL);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_PCMCIA);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_PCI);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_ISAPNP);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_EISA);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_MCA);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_SERENUM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_USB);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_LPTENUM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_USBPRINT);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_DOT4PRT);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_1394);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_HID);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_AVC);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_IRDA);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_SD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_ACPI);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_SW_DEVICE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BUS_TYPE_SCM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_POWER_DEVICE_ENABLE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_POWER_DEVICE_TIMEOUTS);
PH_DEFINE_WELL_KNOWN_GUID(GUID_POWER_DEVICE_WAKE_ENABLE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_WUDF_DEVICE_HOST_PROBLEM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_PARTITION_UNIT_INTERFACE_STANDARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_QUERY_CRASHDUMP_FUNCTIONS);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_DISK);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_CDROM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_PARTITION);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_TAPE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_WRITEONCEDISK);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_VOLUME);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_MEDIUMCHANGER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_FLOPPY);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_CDCHANGER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_STORAGEPORT);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_VMLUN);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_SES);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_ZNSDISK);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_SERVICE_VOLUME);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_HIDDEN_VOLUME);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_UNIFIED_ACCESS_RPMB);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_SCM_PHYSICAL_DEVICE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_SCM_PD_HEALTH_NOTIFICATION);
PH_DEFINE_WELL_KNOWN_GUID(GUID_SCM_PD_PASSTHROUGH_INVDIMM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_COMPORT);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_SERENUM_BUS_ENUMERATOR);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BTHPORT_DEVICE_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_BTH_RFCOMM_SERVICE_DEVICE_INTERFACE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_1394);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_1394DEBUG);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_61883);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_ADAPTER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_APMSUPPORT);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_AVC);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_BATTERY);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_BIOMETRIC);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_BLUETOOTH);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_CAMERA);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_CDROM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_COMPUTEACCELERATOR);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_COMPUTER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_DECODER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_DISKDRIVE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_DISPLAY);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_DOT4);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_DOT4PRINT);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_EHSTORAGESILO);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_ENUM1394);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_EXTENSION);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FDC);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FIRMWARE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FLOPPYDISK);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_GENERIC);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_GPS);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_HDC);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_HIDCLASS);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_HOLOGRAPHIC);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_IMAGE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_INFINIBAND);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_INFRARED);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_KEYBOARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_LEGACYDRIVER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_MEDIA);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_MEDIUM_CHANGER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_MEMORY);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_MODEM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_MONITOR);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_MOUSE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_MTD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_MULTIFUNCTION);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_MULTIPORTSERIAL);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_NET);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_NETCLIENT);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_NETDRIVER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_NETSERVICE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_NETTRANS);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_NETUIO);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_NODRIVER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_PCMCIA);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_PNPPRINTERS);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_PORTS);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_PRIMITIVE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_PRINTER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_PRINTERUPGRADE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_PRINTQUEUE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_PROCESSOR);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SBP2);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SCMDISK);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SCMVOLUME);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SCSIADAPTER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SECURITYACCELERATOR);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SENSOR);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SIDESHOW);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SMARTCARDREADER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SMRDISK);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SMRVOLUME);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SOFTWARECOMPONENT);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SOUND);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_SYSTEM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_TAPEDRIVE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_UNKNOWN);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_UCM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_USB);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_VOLUME);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_VOLUMESNAPSHOT);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_WCEUSBS);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_WPD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_TOP);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_ACTIVITYMONITOR);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_UNDELETE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_ANTIVIRUS);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_REPLICATION);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_CONTINUOUSBACKUP);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_CONTENTSCREENER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_QUOTAMANAGEMENT);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_SYSTEMRECOVERY);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_CFSMETADATASERVER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_HSM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_COMPRESSION);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_ENCRYPTION);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_VIRTUALIZATION);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_PHYSICALQUOTAMANAGEMENT);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_OPENFILEBACKUP);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_SECURITYENHANCER);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_COPYPROTECTION);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_BOTTOM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_SYSTEM);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVCLASS_FSFILTER_INFRASTRUCTURE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_USB_HUB);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_USB_BILLBOARD);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_USB_DEVICE);
PH_DEFINE_WELL_KNOWN_GUID(GUID_DEVINTERFACE_USB_HOST_CONTROLLER);

static PH_INITONCE PhpWellKnownGuidsInitOnce = PH_INITONCE_INIT;
static const PH_WELL_KNOWN_GUID* PhpWellKnownGuids[] =
{
    &PH_WELL_KNOWN_GUID_HWPROFILE_QUERY_CHANGE,
    &PH_WELL_KNOWN_GUID_HWPROFILE_CHANGE_CANCELLED,
    &PH_WELL_KNOWN_GUID_HWPROFILE_CHANGE_COMPLETE,
    &PH_WELL_KNOWN_GUID_DEVICE_INTERFACE_ARRIVAL,
    &PH_WELL_KNOWN_GUID_DEVICE_INTERFACE_REMOVAL,
    &PH_WELL_KNOWN_GUID_TARGET_DEVICE_QUERY_REMOVE,
    &PH_WELL_KNOWN_GUID_TARGET_DEVICE_REMOVE_CANCELLED,
    &PH_WELL_KNOWN_GUID_TARGET_DEVICE_REMOVE_COMPLETE,
    &PH_WELL_KNOWN_GUID_PNP_CUSTOM_NOTIFICATION,
    &PH_WELL_KNOWN_GUID_PNP_POWER_NOTIFICATION,
    &PH_WELL_KNOWN_GUID_PNP_POWER_SETTING_CHANGE,
    &PH_WELL_KNOWN_GUID_TARGET_DEVICE_TRANSPORT_RELATIONS_CHANGED,
    &PH_WELL_KNOWN_GUID_KERNEL_SOFT_RESTART_PREPARE,
    &PH_WELL_KNOWN_GUID_KERNEL_SOFT_RESTART_CANCEL,
    &PH_WELL_KNOWN_GUID_RECOVERY_PCI_PREPARE_SHUTDOWN,
    &PH_WELL_KNOWN_GUID_RECOVERY_NVMED_PREPARE_SHUTDOWN,
    &PH_WELL_KNOWN_GUID_KERNEL_SOFT_RESTART_FINALIZE,
    &PH_WELL_KNOWN_GUID_BUS_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_PCI_BUS_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_PCI_BUS_INTERFACE_STANDARD2,
    &PH_WELL_KNOWN_GUID_ARBITER_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_TRANSLATOR_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_ACPI_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_INT_ROUTE_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_PCMCIA_BUS_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_ACPI_REGS_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_LEGACY_DEVICE_DETECTION_STANDARD,
    &PH_WELL_KNOWN_GUID_PCI_DEVICE_PRESENT_INTERFACE,
    &PH_WELL_KNOWN_GUID_MF_ENUMERATION_INTERFACE,
    &PH_WELL_KNOWN_GUID_REENUMERATE_SELF_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_AGP_TARGET_BUS_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_ACPI_CMOS_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_ACPI_PORT_RANGES_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_ACPI_INTERFACE_STANDARD2,
    &PH_WELL_KNOWN_GUID_PNP_LOCATION_INTERFACE,
    &PH_WELL_KNOWN_GUID_PCI_EXPRESS_LINK_QUIESCENT_INTERFACE,
    &PH_WELL_KNOWN_GUID_PCI_EXPRESS_ROOT_PORT_INTERFACE,
    &PH_WELL_KNOWN_GUID_MSIX_TABLE_CONFIG_INTERFACE,
    &PH_WELL_KNOWN_GUID_D3COLD_SUPPORT_INTERFACE,
    &PH_WELL_KNOWN_GUID_PROCESSOR_PCC_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_PCI_VIRTUALIZATION_INTERFACE,
    &PH_WELL_KNOWN_GUID_PCC_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_PCC_INTERFACE_INTERNAL,
    &PH_WELL_KNOWN_GUID_THERMAL_COOLING_INTERFACE,
    &PH_WELL_KNOWN_GUID_DMA_CACHE_COHERENCY_INTERFACE,
    &PH_WELL_KNOWN_GUID_DEVICE_RESET_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_IOMMU_BUS_INTERFACE,
    &PH_WELL_KNOWN_GUID_PCI_SECURITY_INTERFACE,
    &PH_WELL_KNOWN_GUID_SCM_BUS_INTERFACE,
    &PH_WELL_KNOWN_GUID_SECURE_DRIVER_INTERFACE,
    &PH_WELL_KNOWN_GUID_SDEV_IDENTIFIER_INTERFACE,
    &PH_WELL_KNOWN_GUID_SCM_BUS_NVD_INTERFACE,
    &PH_WELL_KNOWN_GUID_SCM_BUS_LD_INTERFACE,
    &PH_WELL_KNOWN_GUID_SCM_PHYSICAL_NVDIMM_INTERFACE,
    &PH_WELL_KNOWN_GUID_PNP_EXTENDED_ADDRESS_INTERFACE,
    &PH_WELL_KNOWN_GUID_D3COLD_AUX_POWER_AND_TIMING_INTERFACE,
    &PH_WELL_KNOWN_GUID_PCI_FPGA_CONTROL_INTERFACE,
    &PH_WELL_KNOWN_GUID_PCI_PTM_CONTROL_INTERFACE,
    &PH_WELL_KNOWN_GUID_BUS_RESOURCE_UPDATE_INTERFACE,
    &PH_WELL_KNOWN_GUID_NPEM_CONTROL_INTERFACE,
    &PH_WELL_KNOWN_GUID_PCI_ATS_INTERFACE,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_INTERNAL,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_PCMCIA,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_PCI,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_ISAPNP,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_EISA,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_MCA,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_SERENUM,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_USB,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_LPTENUM,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_USBPRINT,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_DOT4PRT,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_1394,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_HID,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_AVC,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_IRDA,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_SD,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_ACPI,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_SW_DEVICE,
    &PH_WELL_KNOWN_GUID_BUS_TYPE_SCM,
    &PH_WELL_KNOWN_GUID_POWER_DEVICE_ENABLE,
    &PH_WELL_KNOWN_GUID_POWER_DEVICE_TIMEOUTS,
    &PH_WELL_KNOWN_GUID_POWER_DEVICE_WAKE_ENABLE,
    &PH_WELL_KNOWN_GUID_WUDF_DEVICE_HOST_PROBLEM,
    &PH_WELL_KNOWN_GUID_PARTITION_UNIT_INTERFACE_STANDARD,
    &PH_WELL_KNOWN_GUID_QUERY_CRASHDUMP_FUNCTIONS,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_DISK,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_CDROM,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_PARTITION,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_TAPE,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_WRITEONCEDISK,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_VOLUME,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_MEDIUMCHANGER,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_FLOPPY,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_CDCHANGER,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_STORAGEPORT,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_VMLUN,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_SES,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_ZNSDISK,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_SERVICE_VOLUME,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_HIDDEN_VOLUME,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_UNIFIED_ACCESS_RPMB,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_SCM_PHYSICAL_DEVICE,
    &PH_WELL_KNOWN_GUID_SCM_PD_HEALTH_NOTIFICATION,
    &PH_WELL_KNOWN_GUID_SCM_PD_PASSTHROUGH_INVDIMM,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_COMPORT,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_SERENUM_BUS_ENUMERATOR,
    &PH_WELL_KNOWN_GUID_BTHPORT_DEVICE_INTERFACE,
    &PH_WELL_KNOWN_GUID_BTH_RFCOMM_SERVICE_DEVICE_INTERFACE,
    &PH_WELL_KNOWN_GUID_DEVCLASS_1394,
    &PH_WELL_KNOWN_GUID_DEVCLASS_1394DEBUG,
    &PH_WELL_KNOWN_GUID_DEVCLASS_61883,
    &PH_WELL_KNOWN_GUID_DEVCLASS_ADAPTER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_APMSUPPORT,
    &PH_WELL_KNOWN_GUID_DEVCLASS_AVC,
    &PH_WELL_KNOWN_GUID_DEVCLASS_BATTERY,
    &PH_WELL_KNOWN_GUID_DEVCLASS_BIOMETRIC,
    &PH_WELL_KNOWN_GUID_DEVCLASS_BLUETOOTH,
    &PH_WELL_KNOWN_GUID_DEVCLASS_CAMERA,
    &PH_WELL_KNOWN_GUID_DEVCLASS_CDROM,
    &PH_WELL_KNOWN_GUID_DEVCLASS_COMPUTEACCELERATOR,
    &PH_WELL_KNOWN_GUID_DEVCLASS_COMPUTER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_DECODER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_DISKDRIVE,
    &PH_WELL_KNOWN_GUID_DEVCLASS_DISPLAY,
    &PH_WELL_KNOWN_GUID_DEVCLASS_DOT4,
    &PH_WELL_KNOWN_GUID_DEVCLASS_DOT4PRINT,
    &PH_WELL_KNOWN_GUID_DEVCLASS_EHSTORAGESILO,
    &PH_WELL_KNOWN_GUID_DEVCLASS_ENUM1394,
    &PH_WELL_KNOWN_GUID_DEVCLASS_EXTENSION,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FDC,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FIRMWARE,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FLOPPYDISK,
    &PH_WELL_KNOWN_GUID_DEVCLASS_GENERIC,
    &PH_WELL_KNOWN_GUID_DEVCLASS_GPS,
    &PH_WELL_KNOWN_GUID_DEVCLASS_HDC,
    &PH_WELL_KNOWN_GUID_DEVCLASS_HIDCLASS,
    &PH_WELL_KNOWN_GUID_DEVCLASS_HOLOGRAPHIC,
    &PH_WELL_KNOWN_GUID_DEVCLASS_IMAGE,
    &PH_WELL_KNOWN_GUID_DEVCLASS_INFINIBAND,
    &PH_WELL_KNOWN_GUID_DEVCLASS_INFRARED,
    &PH_WELL_KNOWN_GUID_DEVCLASS_KEYBOARD,
    &PH_WELL_KNOWN_GUID_DEVCLASS_LEGACYDRIVER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_MEDIA,
    &PH_WELL_KNOWN_GUID_DEVCLASS_MEDIUM_CHANGER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_MEMORY,
    &PH_WELL_KNOWN_GUID_DEVCLASS_MODEM,
    &PH_WELL_KNOWN_GUID_DEVCLASS_MONITOR,
    &PH_WELL_KNOWN_GUID_DEVCLASS_MOUSE,
    &PH_WELL_KNOWN_GUID_DEVCLASS_MTD,
    &PH_WELL_KNOWN_GUID_DEVCLASS_MULTIFUNCTION,
    &PH_WELL_KNOWN_GUID_DEVCLASS_MULTIPORTSERIAL,
    &PH_WELL_KNOWN_GUID_DEVCLASS_NET,
    &PH_WELL_KNOWN_GUID_DEVCLASS_NETCLIENT,
    &PH_WELL_KNOWN_GUID_DEVCLASS_NETDRIVER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_NETSERVICE,
    &PH_WELL_KNOWN_GUID_DEVCLASS_NETTRANS,
    &PH_WELL_KNOWN_GUID_DEVCLASS_NETUIO,
    &PH_WELL_KNOWN_GUID_DEVCLASS_NODRIVER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_PCMCIA,
    &PH_WELL_KNOWN_GUID_DEVCLASS_PNPPRINTERS,
    &PH_WELL_KNOWN_GUID_DEVCLASS_PORTS,
    &PH_WELL_KNOWN_GUID_DEVCLASS_PRIMITIVE,
    &PH_WELL_KNOWN_GUID_DEVCLASS_PRINTER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_PRINTERUPGRADE,
    &PH_WELL_KNOWN_GUID_DEVCLASS_PRINTQUEUE,
    &PH_WELL_KNOWN_GUID_DEVCLASS_PROCESSOR,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SBP2,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SCMDISK,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SCMVOLUME,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SCSIADAPTER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SECURITYACCELERATOR,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SENSOR,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SIDESHOW,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SMARTCARDREADER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SMRDISK,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SMRVOLUME,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SOFTWARECOMPONENT,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SOUND,
    &PH_WELL_KNOWN_GUID_DEVCLASS_SYSTEM,
    &PH_WELL_KNOWN_GUID_DEVCLASS_TAPEDRIVE,
    &PH_WELL_KNOWN_GUID_DEVCLASS_UNKNOWN,
    &PH_WELL_KNOWN_GUID_DEVCLASS_UCM,
    &PH_WELL_KNOWN_GUID_DEVCLASS_USB,
    &PH_WELL_KNOWN_GUID_DEVCLASS_VOLUME,
    &PH_WELL_KNOWN_GUID_DEVCLASS_VOLUMESNAPSHOT,
    &PH_WELL_KNOWN_GUID_DEVCLASS_WCEUSBS,
    &PH_WELL_KNOWN_GUID_DEVCLASS_WPD,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_TOP,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_ACTIVITYMONITOR,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_UNDELETE,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_ANTIVIRUS,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_REPLICATION,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_CONTINUOUSBACKUP,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_CONTENTSCREENER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_QUOTAMANAGEMENT,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_SYSTEMRECOVERY,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_CFSMETADATASERVER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_HSM,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_COMPRESSION,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_ENCRYPTION,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_VIRTUALIZATION,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_PHYSICALQUOTAMANAGEMENT,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_OPENFILEBACKUP,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_SECURITYENHANCER,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_COPYPROTECTION,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_BOTTOM,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_SYSTEM,
    &PH_WELL_KNOWN_GUID_DEVCLASS_FSFILTER_INFRASTRUCTURE,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_USB_HUB,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_USB_BILLBOARD,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_USB_DEVICE,
    &PH_WELL_KNOWN_GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
};

static int __cdecl PhpWellKnownGuidSortFunction(
    const void* Left,
    const void* Right
    )
{
    PPH_WELL_KNOWN_GUID lhsItem;
    PPH_WELL_KNOWN_GUID rhsItem;

    lhsItem = *(PPH_WELL_KNOWN_GUID*)Left;
    rhsItem = *(PPH_WELL_KNOWN_GUID*)Right;

    return memcmp(lhsItem->Guid, rhsItem->Guid, sizeof(GUID));
}

static int __cdecl PhpWellKnownGuidSearchFunction(
    const GUID* Guid,
    const void* Item 
    )
{
    PPH_WELL_KNOWN_GUID item;

    item = *(PPH_WELL_KNOWN_GUID*)Item;

    return memcmp(Guid, item->Guid, sizeof(GUID));
}

PPH_STRING PhpDevPropWellKnownGuidToString(
    _In_ PGUID Guid 
    )
{
    const PH_WELL_KNOWN_GUID** entry;

    if (PhBeginInitOnce(&PhpWellKnownGuidsInitOnce))
    {
        qsort(
            (void*)PhpWellKnownGuids,
            RTL_NUMBER_OF(PhpWellKnownGuids),
            sizeof(PPH_WELL_KNOWN_GUID),
            PhpWellKnownGuidSortFunction
            );

#ifdef DEBUG
        // check for collisions 
        for (ULONG i = 0; i < RTL_NUMBER_OF(PhpWellKnownGuids) - 1; i++)
        {
            const PH_WELL_KNOWN_GUID* lhs = PhpWellKnownGuids[i];
            const PH_WELL_KNOWN_GUID* rhs = PhpWellKnownGuids[i + 1];
            assert(!IsEqualGUID(&lhs->Guid, &rhs->Guid));
        }
#endif

        PhEndInitOnce(&PhpWellKnownGuidsInitOnce);
    }

    entry = bsearch(
        Guid,
        PhpWellKnownGuids,
        RTL_NUMBER_OF(PhpWellKnownGuids),
        sizeof(PPH_WELL_KNOWN_GUID),
        PhpWellKnownGuidSearchFunction
        );
    if (!entry)
        return NULL;

    return PhCreateString2((PPH_STRINGREF)&(*entry)->Symbol);
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillGuid(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeGUID;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyGuid(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->Guid
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyGuid(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->Guid
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyGuid(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->Guid
            );
    }

    if (Property->Valid)
    {
        Property->AsString = PhpDevPropWellKnownGuidToString(&Property->Guid);

        if (!Property->AsString)
            Property->AsString = PhFormatGuid(&Property->Guid);
    }
}

PPH_STRING PhpDevPropPciDeviceInterruptSupportToString(
    _In_ ULONG Flags
    )
{
    PH_STRING_BUILDER stringBuilder;
    WCHAR pointer[PH_PTR_STR_LEN_1];

    PhInitializeStringBuilder(&stringBuilder, 10);

    if (BooleanFlagOn(Flags, DevProp_PciDevice_InterruptType_LineBased))
        PhAppendStringBuilder2(&stringBuilder, L"Line based, ");
    if (BooleanFlagOn(Flags, DevProp_PciDevice_InterruptType_Msi))
        PhAppendStringBuilder2(&stringBuilder, L"Msi, ");
    if (BooleanFlagOn(Flags, DevProp_PciDevice_InterruptType_MsiX))
        PhAppendStringBuilder2(&stringBuilder, L"MsiX, ");

    if (PhEndsWithString2(stringBuilder.String, L", ", FALSE))
        PhRemoveEndStringBuilder(&stringBuilder, 2);

    PhPrintPointer(pointer, UlongToPtr(Flags));
    PhAppendFormatStringBuilder(&stringBuilder, L" (%s)", pointer);

    return PhFinalStringBuilderString(&stringBuilder);
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillPciDeviceInterruptSupport(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeUInt32;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyUInt32(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->UInt32
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyUInt32(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->UInt32
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyUInt32(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->UInt32
            );
    }

    if (Property->Valid)
    {
        Property->AsString = PhpDevPropPciDeviceInterruptSupportToString(Property->UInt32);
    }
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillBoolean(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeBoolean;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyBoolean(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->Boolean
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyBoolean(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->Boolean
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyBoolean(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->Boolean
            );
    }

    if (Property->Valid)
    {
        if (Property->Boolean)
            Property->AsString = PhCreateString(L"true");
        else
            Property->AsString = PhCreateString(L"false");
    }
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillTimeStamp(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeTimeStamp;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyTimeStamp(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->TimeStamp
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyTimeStamp(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->TimeStamp
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyTimeStamp(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->TimeStamp
            );
    }

    if (Property->Valid)
    {
        SYSTEMTIME systemTime;

        PhLargeIntegerToLocalSystemTime(&systemTime, &Property->TimeStamp);

        Property->AsString = PhFormatDateTime(&systemTime);
    }
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillStringList(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeStringList;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyStringList(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->StringList
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyStringList(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->StringList
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyStringList(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->StringList
            );
    }

    if (Property->Valid && Property->StringList->Count > 0)
    {
        PH_STRING_BUILDER stringBuilder;
        PPH_STRING string;

        PhInitializeStringBuilder(&stringBuilder, Property->StringList->Count);

        for (ULONG i = 0; i < Property->StringList->Count - 1; i++)
        {
            string = Property->StringList->Items[i];

            PhAppendStringBuilder(&stringBuilder, &string->sr);
            PhAppendStringBuilder2(&stringBuilder, L", ");
        }

        string = Property->StringList->Items[Property->StringList->Count - 1];

        PhAppendStringBuilder(&stringBuilder, &string->sr);

        Property->AsString = PhFinalStringBuilderString(&stringBuilder);
        PhReferenceObject(Property->AsString);

        PhDeleteStringBuilder(&stringBuilder);
    }
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillStringOrStringList(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    PhpDevPropFillString(
        DeviceInfoSet,
        DeviceInfoData,
        PropertyKey,
        Property,
        Flags
        );
    if (!Property->Valid)
    {
        PhpDevPropFillStringList(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            Property,
            Flags
            );
    }
}

_Function_class_(PH_DEVICE_PROPERTY_FILL_CALLBACK)
VOID NTAPI PhpDevPropFillBinary(
    _In_ HDEVINFO DeviceInfoSet,
    _In_ PSP_DEVINFO_DATA DeviceInfoData,
    _In_ const DEVPROPKEY* PropertyKey,
    _Out_ PPH_DEVICE_PROPERTY Property,
    _In_ ULONG Flags
    )
{
    Property->Type = PhDevicePropertyTypeBinary;

    if (!(Flags & (DEVPROP_FILL_FLAG_CLASS_INSTALLER | DEVPROP_FILL_FLAG_CLASS_INTERFACE)))
    {
        Property->Valid = PhpGetDevicePropertyBinary(
            DeviceInfoSet,
            DeviceInfoData,
            PropertyKey,
            &Property->Binary.Buffer,
            &Property->Binary.Size
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INTERFACE))
    {
        Property->Valid = PhpGetClassPropertyBinary(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INTERFACE,
            &Property->Binary.Buffer,
            &Property->Binary.Size
            );
    }

    if (!Property->Valid && (Flags & DEVPROP_FILL_FLAG_CLASS_INSTALLER))
    {
        Property->Valid = PhpGetClassPropertyBinary(
            &DeviceInfoData->ClassGuid,
            PropertyKey,
            DICLASSPROP_INSTALLER,
            &Property->Binary.Buffer,
            &Property->Binary.Size
            );
    }

    if (Property->Valid)
    {
        Property->AsString = PhBufferToHexString(Property->Binary.Buffer, Property->Binary.Size);
    }
}

static const PH_DEVICE_PROPERTY_TABLE_ENTRY PhpDeviceItemPropertyTable[] =
{
    { PhDevicePropertyName, &DEVPKEY_NAME, PhpDevPropFillString, 0 },
    { PhDevicePropertyManufacturer, &DEVPKEY_Device_Manufacturer, PhpDevPropFillString, 0 },
    { PhDevicePropertyService, &DEVPKEY_Device_Service, PhpDevPropFillString, 0 },
    { PhDevicePropertyClass, &DEVPKEY_Device_Class, PhpDevPropFillString, 0 },
    { PhDevicePropertyEnumeratorName, &DEVPKEY_Device_EnumeratorName, PhpDevPropFillString, 0 },
    { PhDevicePropertyInstallDate, &DEVPKEY_Device_InstallDate, PhpDevPropFillTimeStamp, 0 },

    { PhDevicePropertyFirstInstallDate, &DEVPKEY_Device_FirstInstallDate, PhpDevPropFillTimeStamp, 0 },
    { PhDevicePropertyLastArrivalDate, &DEVPKEY_Device_LastArrivalDate, PhpDevPropFillTimeStamp, 0 },
    { PhDevicePropertyLastRemovalDate, &DEVPKEY_Device_LastRemovalDate, PhpDevPropFillTimeStamp, 0 },
    { PhDevicePropertyDeviceDesc, &DEVPKEY_Device_DeviceDesc, PhpDevPropFillString, 0 },
    { PhDevicePropertyFriendlyName, &DEVPKEY_Device_FriendlyName, PhpDevPropFillString, 0 },
    { PhDevicePropertyInstanceId, &DEVPKEY_Device_InstanceId, PhpDevPropFillString, 0 },
    { PhDevicePropertyParentInstanceId, &DEVPKEY_Device_Parent, PhpDevPropFillString, 0 },
    { PhDevicePropertyPDOName, &DEVPKEY_Device_PDOName, PhpDevPropFillString, 0 },
    { PhDevicePropertyLocationInfo, &DEVPKEY_Device_LocationInfo, PhpDevPropFillString, 0 },
    { PhDevicePropertyClassGuid, &DEVPKEY_Device_ClassGuid, PhpDevPropFillGuid, 0 },
    { PhDevicePropertyDriver, &DEVPKEY_Device_Driver, PhpDevPropFillString, 0 },
    { PhDevicePropertyDriverVersion, &DEVPKEY_Device_DriverVersion, PhpDevPropFillString, 0 },
    { PhDevicePropertyDriverDate, &DEVPKEY_Device_DriverDate, PhpDevPropFillTimeStamp, 0 },
    { PhDevicePropertyFirmwareDate, &DEVPKEY_Device_FirmwareDate, PhpDevPropFillTimeStamp, 0 },
    { PhDevicePropertyFirmwareVersion, &DEVPKEY_Device_FirmwareVersion, PhpDevPropFillString, 0 },
    { PhDevicePropertyFirmwareRevision, &DEVPKEY_Device_FirmwareRevision, PhpDevPropFillString, 0 },
    { PhDevicePropertyHasProblem, &DEVPKEY_Device_HasProblem, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyProblemCode, &DEVPKEY_Device_ProblemCode, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyProblemStatus, &DEVPKEY_Device_ProblemStatus, PhpDevPropFillNTSTATUS, 0 },
    { PhDevicePropertyDevNodeStatus, &DEVPKEY_Device_DevNodeStatus, PhpDevPropFillUInt32Hex, 0 },
    { PhDevicePropertyDevCapabilities, &DEVPKEY_Device_Capabilities, PhpDevPropFillUInt32Hex, 0 },
    { PhDevicePropertyUpperFilters, &DEVPKEY_Device_UpperFilters, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyLowerFilters, &DEVPKEY_Device_LowerFilters, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyHardwareIds, &DEVPKEY_Device_HardwareIds, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyCompatibleIds, &DEVPKEY_Device_CompatibleIds, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyConfigFlags, &DEVPKEY_Device_ConfigFlags, PhpDevPropFillUInt32Hex, 0 },
    { PhDevicePropertyUINumber, &DEVPKEY_Device_UINumber, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyBusTypeGuid, &DEVPKEY_Device_BusTypeGuid, PhpDevPropFillGuid, 0 },
    { PhDevicePropertyLegacyBusType, &DEVPKEY_Device_LegacyBusType, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyBusNumber, &DEVPKEY_Device_BusNumber, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertySecurity, &DEVPKEY_Device_Security, PhpDevPropFillBinary, 0 }, // DEVPROP_TYPE_SECURITY_DESCRIPTOR as binary, PhDevicePropertySecuritySDS for string
    { PhDevicePropertySecuritySDS, &DEVPKEY_Device_SecuritySDS, PhpDevPropFillString, 0 },
    { PhDevicePropertyDevType, &DEVPKEY_Device_DevType, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyExclusive, &DEVPKEY_Device_Exclusive, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyCharacteristics, &DEVPKEY_Device_Characteristics, PhpDevPropFillUInt32Hex, 0 },
    { PhDevicePropertyAddress, &DEVPKEY_Device_Address, PhpDevPropFillUInt32Hex, 0 },
    { PhDevicePropertyPowerData, &DEVPKEY_Device_PowerData, PhpDevPropFillBinary, 0 }, // TODO(jxy-s) CM_POWER_DATA could be formatted AsString nicer
    { PhDevicePropertyRemovalPolicy, &DEVPKEY_Device_RemovalPolicy, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyRemovalPolicyDefault, &DEVPKEY_Device_RemovalPolicyDefault, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyRemovalPolicyOverride, &DEVPKEY_Device_RemovalPolicyOverride, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyInstallState, &DEVPKEY_Device_InstallState, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyLocationPaths, &DEVPKEY_Device_LocationPaths, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyBaseContainerId, &DEVPKEY_Device_BaseContainerId, PhpDevPropFillGuid, 0 },
    { PhDevicePropertyEjectionRelations, &DEVPKEY_Device_EjectionRelations, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyRemovalRelations, &DEVPKEY_Device_RemovalRelations, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyPowerRelations, &DEVPKEY_Device_PowerRelations, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyBusRelations, &DEVPKEY_Device_BusRelations, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyChildren, &DEVPKEY_Device_Children, PhpDevPropFillStringList, 0 },
    { PhDevicePropertySiblings, &DEVPKEY_Device_Siblings, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyTransportRelations, &DEVPKEY_Device_TransportRelations, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyReported, &DEVPKEY_Device_Reported, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyLegacy, &DEVPKEY_Device_Legacy, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerId, &DEVPKEY_Device_ContainerId, PhpDevPropFillGuid, 0 },
    { PhDevicePropertyInLocalMachineContainer, &DEVPKEY_Device_InLocalMachineContainer, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyModel, &DEVPKEY_Device_Model, PhpDevPropFillString, 0 },
    { PhDevicePropertyModelId, &DEVPKEY_Device_ModelId, PhpDevPropFillGuid, 0 },
    { PhDevicePropertyFriendlyNameAttributes, &DEVPKEY_Device_FriendlyNameAttributes, PhpDevPropFillUInt32Hex, 0 },
    { PhDevicePropertyManufacturerAttributes, &DEVPKEY_Device_ManufacturerAttributes, PhpDevPropFillUInt32Hex, 0 },
    { PhDevicePropertyPresenceNotForDevice, &DEVPKEY_Device_PresenceNotForDevice, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertySignalStrength, &DEVPKEY_Device_SignalStrength, PhpDevPropFillInt32, 0 },
    { PhDevicePropertyIsAssociateableByUserAction, &DEVPKEY_Device_IsAssociateableByUserAction, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyShowInUninstallUI, &DEVPKEY_Device_ShowInUninstallUI, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyNumaProximityDomain, &DEVPKEY_Device_Numa_Proximity_Domain, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyDHPRebalancePolicy, &DEVPKEY_Device_DHP_Rebalance_Policy, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyNumaNode, &DEVPKEY_Device_Numa_Node, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyBusReportedDeviceDesc, &DEVPKEY_Device_BusReportedDeviceDesc, PhpDevPropFillString, 0 },
    { PhDevicePropertyIsPresent, &DEVPKEY_Device_IsPresent, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyConfigurationId, &DEVPKEY_Device_ConfigurationId, PhpDevPropFillString, 0 },
    { PhDevicePropertyReportedDeviceIdsHash, &DEVPKEY_Device_ReportedDeviceIdsHash, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyPhysicalDeviceLocation, &DEVPKEY_Device_PhysicalDeviceLocation, PhpDevPropFillBinary, 0 }, // TODO(jxy-s) look into ACPI 4.0a Specification, section 6.1.6 for AsString formatting 
    { PhDevicePropertyBiosDeviceName, &DEVPKEY_Device_BiosDeviceName, PhpDevPropFillString, 0 },
    { PhDevicePropertyDriverProblemDesc, &DEVPKEY_Device_DriverProblemDesc, PhpDevPropFillString, 0 },
    { PhDevicePropertyDebuggerSafe, &DEVPKEY_Device_DebuggerSafe, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyPostInstallInProgress, &DEVPKEY_Device_PostInstallInProgress, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyStack, &DEVPKEY_Device_Stack, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyExtendedConfigurationIds, &DEVPKEY_Device_ExtendedConfigurationIds, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyIsRebootRequired, &DEVPKEY_Device_IsRebootRequired, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyDependencyProviders, &DEVPKEY_Device_DependencyProviders, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyDependencyDependents, &DEVPKEY_Device_DependencyDependents, PhpDevPropFillStringList, 0 },
    { PhDevicePropertySoftRestartSupported, &DEVPKEY_Device_SoftRestartSupported, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyExtendedAddress, &DEVPKEY_Device_ExtendedAddress, PhpDevPropFillUInt64Hex, 0 },
    { PhDevicePropertyAssignedToGuest, &DEVPKEY_Device_AssignedToGuest, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyCreatorProcessId, &DEVPKEY_Device_CreatorProcessId, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyFirmwareVendor, &DEVPKEY_Device_FirmwareVendor, PhpDevPropFillString, 0 },
    { PhDevicePropertySessionId, &DEVPKEY_Device_SessionId, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyDriverDesc, &DEVPKEY_Device_DriverDesc, PhpDevPropFillString, 0 },
    { PhDevicePropertyDriverInfPath, &DEVPKEY_Device_DriverInfPath, PhpDevPropFillString, 0 },
    { PhDevicePropertyDriverInfSection, &DEVPKEY_Device_DriverInfSection, PhpDevPropFillString, 0 },
    { PhDevicePropertyDriverInfSectionExt, &DEVPKEY_Device_DriverInfSectionExt, PhpDevPropFillString, 0 },
    { PhDevicePropertyMatchingDeviceId, &DEVPKEY_Device_MatchingDeviceId, PhpDevPropFillString, 0 },
    { PhDevicePropertyDriverProvider, &DEVPKEY_Device_DriverProvider, PhpDevPropFillString, 0 },
    { PhDevicePropertyDriverPropPageProvider, &DEVPKEY_Device_DriverPropPageProvider, PhpDevPropFillString, 0 },
    { PhDevicePropertyDriverCoInstallers, &DEVPKEY_Device_DriverCoInstallers, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyResourcePickerTags, &DEVPKEY_Device_ResourcePickerTags, PhpDevPropFillString, 0 },
    { PhDevicePropertyResourcePickerExceptions, &DEVPKEY_Device_ResourcePickerExceptions, PhpDevPropFillString, 0 },
    { PhDevicePropertyDriverRank, &DEVPKEY_Device_DriverRank, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyDriverLogoLevel, &DEVPKEY_Device_DriverLogoLevel, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyNoConnectSound, &DEVPKEY_Device_NoConnectSound, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyGenericDriverInstalled, &DEVPKEY_Device_GenericDriverInstalled, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyAdditionalSoftwareRequested, &DEVPKEY_Device_AdditionalSoftwareRequested, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertySafeRemovalRequired, &DEVPKEY_Device_SafeRemovalRequired, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertySafeRemovalRequiredOverride, &DEVPKEY_Device_SafeRemovalRequiredOverride, PhpDevPropFillBoolean, 0 },

    { PhDevicePropertyPkgModel, &DEVPKEY_DrvPkg_Model, PhpDevPropFillString, 0 },
    { PhDevicePropertyPkgVendorWebSite, &DEVPKEY_DrvPkg_VendorWebSite, PhpDevPropFillString, 0 },
    { PhDevicePropertyPkgDetailedDescription, &DEVPKEY_DrvPkg_DetailedDescription, PhpDevPropFillString, 0 },
    { PhDevicePropertyPkgDocumentationLink, &DEVPKEY_DrvPkg_DocumentationLink, PhpDevPropFillString, 0 },
    { PhDevicePropertyPkgIcon, &DEVPKEY_DrvPkg_Icon, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyPkgBrandingIcon, &DEVPKEY_DrvPkg_BrandingIcon, PhpDevPropFillStringList, 0 },

    { PhDevicePropertyClassUpperFilters, &DEVPKEY_DeviceClass_UpperFilters, PhpDevPropFillStringList, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassLowerFilters, &DEVPKEY_DeviceClass_LowerFilters, PhpDevPropFillStringList, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassSecurity, &DEVPKEY_DeviceClass_Security, PhpDevPropFillBinary, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER }, // DEVPROP_TYPE_SECURITY_DESCRIPTOR as binary, PhDevicePropertyClassSecuritySDS for string
    { PhDevicePropertyClassSecuritySDS, &DEVPKEY_DeviceClass_SecuritySDS, PhpDevPropFillString, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassDevType, &DEVPKEY_DeviceClass_DevType, PhpDevPropFillUInt32, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassExclusive, &DEVPKEY_DeviceClass_Exclusive, PhpDevPropFillBoolean, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassCharacteristics, &DEVPKEY_DeviceClass_Characteristics, PhpDevPropFillUInt32Hex, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassName, &DEVPKEY_DeviceClass_Name, PhpDevPropFillString, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassClassName, &DEVPKEY_DeviceClass_ClassName, PhpDevPropFillString, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassIcon, &DEVPKEY_DeviceClass_Icon, PhpDevPropFillString, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassClassInstaller, &DEVPKEY_DeviceClass_ClassInstaller, PhpDevPropFillString, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassPropPageProvider, &DEVPKEY_DeviceClass_PropPageProvider, PhpDevPropFillString, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassNoInstallClass, &DEVPKEY_DeviceClass_NoInstallClass, PhpDevPropFillBoolean, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassNoDisplayClass, &DEVPKEY_DeviceClass_NoDisplayClass, PhpDevPropFillBoolean, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassSilentInstall, &DEVPKEY_DeviceClass_SilentInstall, PhpDevPropFillBoolean, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassNoUseClass, &DEVPKEY_DeviceClass_NoUseClass, PhpDevPropFillBoolean, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassDefaultService, &DEVPKEY_DeviceClass_DefaultService, PhpDevPropFillString, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassIconPath, &DEVPKEY_DeviceClass_IconPath, PhpDevPropFillStringList, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassDHPRebalanceOptOut, &DEVPKEY_DeviceClass_DHPRebalanceOptOut, PhpDevPropFillBoolean, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyClassClassCoInstallers, &DEVPKEY_DeviceClass_ClassCoInstallers, PhpDevPropFillStringList, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },

    { PhDevicePropertyInterfaceFriendlyName, &DEVPKEY_DeviceInterface_FriendlyName, PhpDevPropFillString, 0 },
    { PhDevicePropertyInterfaceEnabled, &DEVPKEY_DeviceInterface_Enabled, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyInterfaceClassGuid, &DEVPKEY_DeviceInterface_ClassGuid, PhpDevPropFillGuid, 0 },
    { PhDevicePropertyInterfaceReferenceString, &DEVPKEY_DeviceInterface_ReferenceString, PhpDevPropFillString, 0 },
    { PhDevicePropertyInterfaceRestricted, &DEVPKEY_DeviceInterface_Restricted, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyInterfaceUnrestrictedAppCapabilities, &DEVPKEY_DeviceInterface_UnrestrictedAppCapabilities, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyInterfaceSchematicName, &DEVPKEY_DeviceInterface_SchematicName, PhpDevPropFillString, 0 },

    { PhDevicePropertyInterfaceClassDefaultInterface, &DEVPKEY_DeviceInterfaceClass_DefaultInterface, PhpDevPropFillString, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },
    { PhDevicePropertyInterfaceClassName, &DEVPKEY_DeviceInterfaceClass_Name, PhpDevPropFillString, DEVPROP_FILL_FLAG_CLASS_INTERFACE | DEVPROP_FILL_FLAG_CLASS_INSTALLER },

    { PhDevicePropertyContainerAddress, &DEVPKEY_DeviceContainer_Address, PhpDevPropFillStringOrStringList, 0 },
    { PhDevicePropertyContainerDiscoveryMethod, &DEVPKEY_DeviceContainer_DiscoveryMethod, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyContainerIsEncrypted, &DEVPKEY_DeviceContainer_IsEncrypted, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerIsAuthenticated, &DEVPKEY_DeviceContainer_IsAuthenticated, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerIsConnected, &DEVPKEY_DeviceContainer_IsConnected, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerIsPaired, &DEVPKEY_DeviceContainer_IsPaired, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerIcon, &DEVPKEY_DeviceContainer_Icon, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerVersion, &DEVPKEY_DeviceContainer_Version, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerLastSeen, &DEVPKEY_DeviceContainer_Last_Seen, PhpDevPropFillTimeStamp, 0 },
    { PhDevicePropertyContainerLastConnected, &DEVPKEY_DeviceContainer_Last_Connected, PhpDevPropFillTimeStamp, 0 },
    { PhDevicePropertyContainerIsShowInDisconnectedState, &DEVPKEY_DeviceContainer_IsShowInDisconnectedState, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerIsLocalMachine, &DEVPKEY_DeviceContainer_IsLocalMachine, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerMetadataPath, &DEVPKEY_DeviceContainer_MetadataPath, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerIsMetadataSearchInProgress, &DEVPKEY_DeviceContainer_IsMetadataSearchInProgress, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerIsMetadataChecksum, &DEVPKEY_DeviceContainer_MetadataChecksum, PhpDevPropFillBinary, 0 },
    { PhDevicePropertyContainerIsNotInterestingForDisplay, &DEVPKEY_DeviceContainer_IsNotInterestingForDisplay, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerLaunchDeviceStageOnDeviceConnect, &DEVPKEY_DeviceContainer_LaunchDeviceStageOnDeviceConnect, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerLaunchDeviceStageFromExplorer, &DEVPKEY_DeviceContainer_LaunchDeviceStageFromExplorer, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerBaselineExperienceId, &DEVPKEY_DeviceContainer_BaselineExperienceId, PhpDevPropFillGuid, 0 },
    { PhDevicePropertyContainerIsDeviceUniquelyIdentifiable, &DEVPKEY_DeviceContainer_IsDeviceUniquelyIdentifiable, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerAssociationArray, &DEVPKEY_DeviceContainer_AssociationArray, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyContainerDeviceDescription1, &DEVPKEY_DeviceContainer_DeviceDescription1, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerDeviceDescription2, &DEVPKEY_DeviceContainer_DeviceDescription2, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerHasProblem, &DEVPKEY_DeviceContainer_HasProblem, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerIsSharedDevice, &DEVPKEY_DeviceContainer_IsSharedDevice, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerIsNetworkDevice, &DEVPKEY_DeviceContainer_IsNetworkDevice, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerIsDefaultDevice, &DEVPKEY_DeviceContainer_IsDefaultDevice, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerMetadataCabinet, &DEVPKEY_DeviceContainer_MetadataCabinet, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerRequiresPairingElevation, &DEVPKEY_DeviceContainer_RequiresPairingElevation, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerExperienceId, &DEVPKEY_DeviceContainer_ExperienceId, PhpDevPropFillGuid, 0 },
    { PhDevicePropertyContainerCategory, &DEVPKEY_DeviceContainer_Category, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyContainerCategoryDescSingular, &DEVPKEY_DeviceContainer_Category_Desc_Singular, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyContainerCategoryDescPlural, &DEVPKEY_DeviceContainer_Category_Desc_Plural, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyContainerCategoryIcon, &DEVPKEY_DeviceContainer_Category_Icon, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerCategoryGroupDesc, &DEVPKEY_DeviceContainer_CategoryGroup_Desc, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyContainerCategoryGroupIcon, &DEVPKEY_DeviceContainer_CategoryGroup_Icon, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerPrimaryCategory, &DEVPKEY_DeviceContainer_PrimaryCategory, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerUnpairUninstall, &DEVPKEY_DeviceContainer_UnpairUninstall, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerRequiresUninstallElevation, &DEVPKEY_DeviceContainer_RequiresUninstallElevation, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerDeviceFunctionSubRank, &DEVPKEY_DeviceContainer_DeviceFunctionSubRank, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyContainerAlwaysShowDeviceAsConnected, &DEVPKEY_DeviceContainer_AlwaysShowDeviceAsConnected, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerConfigFlags, &DEVPKEY_DeviceContainer_ConfigFlags, PhpDevPropFillUInt32Hex, 0 },
    { PhDevicePropertyContainerPrivilegedPackageFamilyNames, &DEVPKEY_DeviceContainer_PrivilegedPackageFamilyNames, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyContainerCustomPrivilegedPackageFamilyNames, &DEVPKEY_DeviceContainer_CustomPrivilegedPackageFamilyNames, PhpDevPropFillStringList, 0 },
    { PhDevicePropertyContainerIsRebootRequired, &DEVPKEY_DeviceContainer_IsRebootRequired, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyContainerFriendlyName, &DEVPKEY_DeviceContainer_FriendlyName, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerManufacturer, &DEVPKEY_DeviceContainer_Manufacturer, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerModelName, &DEVPKEY_DeviceContainer_ModelName, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerModelNumber, &DEVPKEY_DeviceContainer_ModelNumber, PhpDevPropFillString, 0 },
    { PhDevicePropertyContainerInstallInProgress, &DEVPKEY_DeviceContainer_InstallInProgress, PhpDevPropFillBoolean, 0 },

    { PhDevicePropertyObjectType, &DEVPKEY_DevQuery_ObjectType, PhpDevPropFillUInt32, 0 },

    { PhDevicePropertyPciInterruptSupport, &DEVPKEY_PciDevice_InterruptSupport, PhpDevPropFillPciDeviceInterruptSupport, 0 },
    { PhDevicePropertyPciExpressCapabilityControl, &DEVPKEY_PciRootBus_PCIExpressCapabilityControl, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyPciNativeExpressControl, &DEVPKEY_PciRootBus_NativePciExpressControl, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyPciSystemMsiSupport, &DEVPKEY_PciRootBus_SystemMsiSupport, PhpDevPropFillBoolean, 0 },

    { PhDevicePropertyStoragePortable, &DEVPKEY_Storage_Portable, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyStorageRemovableMedia, &DEVPKEY_Storage_Removable_Media, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyStorageSystemCritical, &DEVPKEY_Storage_System_Critical, PhpDevPropFillBoolean, 0 },
    { PhDevicePropertyStorageDiskNumber, &DEVPKEY_Storage_Disk_Number, PhpDevPropFillUInt32, 0 },
    { PhDevicePropertyStoragePartitionNumber, &DEVPKEY_Storage_Partition_Number, PhpDevPropFillUInt32, 0 },
};
C_ASSERT(RTL_NUMBER_OF(PhpDeviceItemPropertyTable) == PhMaxDeviceProperty);

#ifdef DEBUG
static BOOLEAN PhpBreakOnDeviceProperty = FALSE;
static PH_DEVICE_PROPERTY_CLASS PhpBreakOnDevicePropertyClass = 0;
#endif

PPH_DEVICE_PROPERTY PhGetDeviceProperty(
    _In_ PPH_DEVICE_ITEM Item,
    _In_ PH_DEVICE_PROPERTY_CLASS Class
    )
{
    PPH_DEVICE_PROPERTY prop;

    prop = &Item->Properties[Class];
    if (!prop->Initialized)
    {
        const PH_DEVICE_PROPERTY_TABLE_ENTRY* entry;

        entry = &PhpDeviceItemPropertyTable[Class];

#ifdef DEBUG
        if (PhpBreakOnDeviceProperty && (PhpBreakOnDevicePropertyClass == Class))
            __debugbreak();
#endif

        entry->Callback(
            Item->DeviceInfo->Handle,
            &Item->DeviceInfoData,
            entry->PropKey,
            prop,
            entry->CallbackFlags
            );

        prop->Initialized = TRUE;
    }

    return prop;
}

HICON PhGetDeviceIcon(
    _In_ PPH_DEVICE_ITEM Item,
    _In_ PPH_INTEGER_PAIR IconSize
    )
{
    HICON iconHandle;

    if (!SetupDiLoadDeviceIcon(
        Item->DeviceInfo->Handle,
        &Item->DeviceInfoData,
        IconSize->X,
        IconSize->Y,
        0,
        &iconHandle
        ))
    {
        iconHandle = NULL;
    }

    return iconHandle;
}

PPH_DEVICE_TREE PhReferenceDeviceTree(
    VOID
    )
{
    PPH_DEVICE_TREE deviceTree;

    PhAcquireFastLockShared(&PhpDeviceTreeLock);
    PhSetReference(&deviceTree, PhpDeviceTree);
    PhReleaseFastLockShared(&PhpDeviceTreeLock);

    return deviceTree;
}

ULONG PhpGenerateInstanceIdHash(
    _In_ PPH_STRINGREF InstanceId
    )
{
    return PhHashStringRefEx(InstanceId, TRUE, PH_STRING_HASH_X65599);
}

static int __cdecl PhpDeviceItemSortFunction(
    const void* Left,
    const void* Right
    )
{
    PPH_DEVICE_ITEM lhsItem;
    PPH_DEVICE_ITEM rhsItem;

    lhsItem = *(PPH_DEVICE_ITEM*)Left;
    rhsItem = *(PPH_DEVICE_ITEM*)Right;

    if (lhsItem->InstanceIdHash < rhsItem->InstanceIdHash)
        return -1;
    else if (lhsItem->InstanceIdHash > rhsItem->InstanceIdHash)
        return 1;
    else
        return 0;
}

static int __cdecl PhpDeviceItemSearchFunction(
    const void* Hash,
    const void* Item 
    )
{
    PPH_DEVICE_ITEM item;

    item = *(PPH_DEVICE_ITEM*)Item;

    if (PtrToUlong(Hash) < item->InstanceIdHash)
        return -1;
    else if (PtrToUlong(Hash) > item->InstanceIdHash)
        return 1;
    else
        return 0;
}

_Success_(return != NULL)
_Must_inspect_result_
PPH_DEVICE_ITEM PhLookupDeviceItem(
    _In_ PPH_DEVICE_TREE Tree,
    _In_ PPH_STRINGREF InstanceId
    )
{
    ULONG hash;
    PPH_DEVICE_ITEM* deviceItem;

    hash = PhpGenerateInstanceIdHash(InstanceId);

    deviceItem = bsearch(
        UlongToPtr(hash),
        Tree->DeviceList->Items,
        Tree->DeviceList->Count,
        sizeof(PVOID),
        PhpDeviceItemSearchFunction
        );

    return deviceItem ? *deviceItem : NULL;
}

_Success_(return != NULL)
_Must_inspect_result_
PPH_DEVICE_ITEM PhReferenceDeviceItem(
    _In_ PPH_DEVICE_TREE Tree,
    _In_ PPH_STRINGREF InstanceId
    )
{
    PPH_DEVICE_ITEM deviceItem;

    PhSetReference(&deviceItem, PhLookupDeviceItem(Tree, InstanceId));

    return deviceItem;
}

_Success_(return != NULL)
_Must_inspect_result_
PPH_DEVICE_ITEM PhReferenceDeviceItem2(
    _In_ PPH_STRINGREF InstanceId
    )
{
    PPH_DEVICE_TREE deviceTree;
    PPH_DEVICE_ITEM deviceItem;

    deviceTree = PhReferenceDeviceTree();
    PhSetReference(&deviceItem, PhLookupDeviceItem(deviceTree, InstanceId));
    PhDereferenceObject(deviceTree);

    return deviceItem;
}

VOID PhpDeviceInfoDeleteProcedure(
    _In_ PVOID Object,
    _In_ ULONG Flags
    )
{
    PPH_DEVINFO info = Object;

    if (info->Handle != INVALID_HANDLE_VALUE)
    {
        SetupDiDestroyDeviceInfoList(info->Handle);
        info->Handle = INVALID_HANDLE_VALUE;
    }
}

VOID PhpDeviceItemDeleteProcedure(
    _In_ PVOID Object,
    _In_ ULONG Flags
    )
{
    PPH_DEVICE_ITEM item = Object;

    PhDereferenceObject(item->Children);

    for (ULONG i = 0; i < ARRAYSIZE(item->Properties); i++)
    {
        PPH_DEVICE_PROPERTY prop;

        prop = &item->Properties[i];

        if (prop->Valid)
        {
            if (prop->Type == PhDevicePropertyTypeString)
            {
                PhDereferenceObject(prop->String);
            }
            else if (prop->Type == PhDevicePropertyTypeStringList)
            {
                for (ULONG j = 0; j < prop->StringList->Count; j++)
                {
                    PhDereferenceObject(prop->StringList->Items[j]);
                }

                PhDereferenceObject(prop->StringList);
            }
            else if (prop->Type == PhDevicePropertyTypeBinary)
            {
                PhFree(prop->Binary.Buffer);
            }
        }

        PhClearReference(&prop->AsString);
    }

    PhClearReference(&item->InstanceId);
    PhClearReference(&item->ParentInstanceId);
}

VOID PhpDeviceTreeDeleteProcedure(
    _In_ PVOID Object,
    _In_ ULONG Flags
    )
{
    PPH_DEVICE_TREE tree = Object;

    for (ULONG i = 0; i < tree->DeviceList->Count; i++)
    {
        PPH_DEVICE_ITEM item;

        item = tree->DeviceList->Items[i];

        PhDereferenceObject(item);
    }

    PhDereferenceObject(tree->DeviceList);
    PhDereferenceObject(tree->DeviceInfo);
}

VOID PhpDeviceNotifyDeleteProcedure(
    _In_ PVOID Object,
    _In_ ULONG Flags
    )
{
    PPH_DEVICE_NOTIFY notify = Object;

    if (notify->Action == PhDeviceNotifyInstanceEnumerated ||
        notify->Action == PhDeviceNotifyInstanceStarted ||
        notify->Action == PhDeviceNotifyInstanceRemoved)
    {
        PhDereferenceObject(notify->DeviceInstance.InstanceId);
    }
}

PPH_DEVICE_ITEM NTAPI PhpAddDeviceItem(
    _In_ PPH_DEVICE_TREE Tree,
    _In_ PSP_DEVINFO_DATA DeviceInfoData
    )
{
    PPH_DEVICE_ITEM item;

    item = PhCreateObjectZero(sizeof(PH_DEVICE_ITEM), PhDeviceItemType);

    item->DeviceInfo = PhReferenceObject(Tree->DeviceInfo);
    RtlCopyMemory(&item->DeviceInfoData, DeviceInfoData, sizeof(SP_DEVINFO_DATA));
    RtlCopyMemory(&item->ClassGuid, &DeviceInfoData->ClassGuid, sizeof(GUID));

    item->Children = PhCreateList(1);

    //
    // Only get the minimum here, the rest will be retrieved later if necessary.
    // For convenience later, other frequently referenced items are gotten here too.
    //

    item->InstanceId = PhGetDeviceProperty(item, PhDevicePropertyInstanceId)->AsString;
    if (item->InstanceId)
    {
        item->InstanceIdHash = PhpGenerateInstanceIdHash(&item->InstanceId->sr);

        //
        // If this is the root enumerator override some properties.
        //
        if (PhEqualStringRef(&item->InstanceId->sr, &RootInstanceId, TRUE))
        {
            PPH_DEVICE_PROPERTY prop;

            prop = &item->Properties[PhDevicePropertyName];
            assert(!prop->Initialized);

            prop->AsString = PhGetActiveComputerName();
            prop->Initialized = TRUE;
        }

        PhReferenceObject(item->InstanceId);
    }

    item->ParentInstanceId = PhGetDeviceProperty(item, PhDevicePropertyParentInstanceId)->AsString;
    if (item->ParentInstanceId)
        PhReferenceObject(item->ParentInstanceId);

    PhGetDeviceProperty(item, PhDevicePropertyProblemCode);
    if (item->Properties[PhDevicePropertyProblemCode].Valid)
        item->ProblemCode = item->Properties[PhDevicePropertyProblemCode].UInt32;
    else
        item->ProblemCode = CM_PROB_PHANTOM;

    PhGetDeviceProperty(item, PhDevicePropertyDevNodeStatus);
    if (item->Properties[PhDevicePropertyDevNodeStatus].Valid)
        item->DevNodeStatus = item->Properties[PhDevicePropertyDevNodeStatus].UInt32;
    else
        item->DevNodeStatus = 0;

    PhGetDeviceProperty(item, PhDevicePropertyDevCapabilities);
    if (item->Properties[PhDevicePropertyDevCapabilities].Valid)
        item->Capabilities = item->Properties[PhDevicePropertyDevCapabilities].UInt32;
    else
        item->Capabilities = 0;

    PhGetDeviceProperty(item, PhDevicePropertyUpperFilters);
    PhGetDeviceProperty(item, PhDevicePropertyClassUpperFilters);
    if ((item->Properties[PhDevicePropertyUpperFilters].Valid &&
         (item->Properties[PhDevicePropertyUpperFilters].StringList->Count > 0)) ||
        (item->Properties[PhDevicePropertyClassUpperFilters].Valid &&
         (item->Properties[PhDevicePropertyClassUpperFilters].StringList->Count > 0)))
    {
        item->HasUpperFilters = TRUE;
    }

    PhGetDeviceProperty(item, PhDevicePropertyLowerFilters);
    PhGetDeviceProperty(item, PhDevicePropertyClassLowerFilters);
    if ((item->Properties[PhDevicePropertyLowerFilters].Valid &&
         (item->Properties[PhDevicePropertyLowerFilters].StringList->Count > 0)) ||
        (item->Properties[PhDevicePropertyClassLowerFilters].Valid &&
         (item->Properties[PhDevicePropertyClassLowerFilters].StringList->Count > 0)))
    {
        item->HasLowerFilters = TRUE;
    }

    PhAddItemList(Tree->DeviceList, item);

    return item;
}

PPH_DEVICE_TREE PhpCreateDeviceTree(
    VOID
    )
{
    PPH_DEVICE_TREE tree;
    PPH_DEVICE_ITEM root;

    tree = PhCreateObjectZero(sizeof(PH_DEVICE_TREE), PhDeviceTreeType);

    tree->DeviceList = PhCreateList(40);
    tree->DeviceInfo = PhCreateObject(sizeof(PH_DEVINFO), PhpDeviceInfoType);

    tree->DeviceInfo->Handle = SetupDiGetClassDevsW(
        NULL,
        NULL,
        NULL,
        DIGCF_ALLCLASSES
        );
    if (tree->DeviceInfo->Handle == INVALID_HANDLE_VALUE)
    {
        return tree;
    }

    for (ULONG i = 0;; i++)
    {
        SP_DEVINFO_DATA deviceInfoData;

        memset(&deviceInfoData, 0, sizeof(SP_DEVINFO_DATA));
        deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

        if (!SetupDiEnumDeviceInfo(tree->DeviceInfo->Handle, i, &deviceInfoData))
            break;

        PhpAddDeviceItem(tree, &deviceInfoData);
    }

    // Link the device relations.
    root = NULL;

    for (ULONG i = 0; i < tree->DeviceList->Count; i++)
    {
        BOOLEAN found;
        PPH_DEVICE_ITEM item;
        PPH_DEVICE_ITEM other;

        found = FALSE;

        item = tree->DeviceList->Items[i];

        for (ULONG j = 0; j < tree->DeviceList->Count; j++)
        {
            other = tree->DeviceList->Items[j];

            if (item == other)
            {
                continue;
            }

            if (!other->InstanceId || !item->ParentInstanceId)
            {
                continue;
            }

            if (!PhEqualString(other->InstanceId, item->ParentInstanceId, TRUE))
            {
                continue;
            }

            found = TRUE;

            item->Parent = other;

            if (!other->Child)
            {
                other->Child = item;
                break;
            }

            other = other->Child;
            while (other->Sibling)
            {
                other = other->Sibling;
            }

            other->Sibling = item;
            break;
        }

        if (found)
        {
            continue;
        }

        other = root;
        if (!other)
        {
            root = item;
            continue;
        }

        while (other->Sibling)
        {
            other = other->Sibling;
        }

        other->Sibling = item;
    }

    for (ULONG i = 0; i < tree->DeviceList->Count; i++)
    {
        PPH_DEVICE_ITEM item;
        PPH_DEVICE_ITEM child;

        item = tree->DeviceList->Items[i];

        child = item->Child;
        while (child)
        {
            PhAddItemList(item->Children, child);
            child = child->Sibling;
        }
    }

    assert(root && !root->Sibling);
    tree->Root = root;

    // sort the list for faster lookups later
    qsort(
        tree->DeviceList->Items,
        tree->DeviceList->Count,
        sizeof(PVOID),
        PhpDeviceItemSortFunction
        );

    return tree;
}

VOID PhpDeviceNotify(
    _In_ PLIST_ENTRY List
    )
{
    PPH_DEVICE_TREE newTree;
    PPH_DEVICE_TREE oldTree;

    // We process device notifications in blocks so that bursts of device changes
    // don't each trigger a new tree each time.

    newTree = PhpCreateDeviceTree();
    PhAcquireFastLockExclusive(&PhpDeviceTreeLock);
    oldTree = PhpDeviceTree;
    PhpDeviceTree = newTree;
    PhReleaseFastLockExclusive(&PhpDeviceTreeLock);

    while (!IsListEmpty(List))
    {
        PPH_DEVICE_NOTIFY entry;

        entry = CONTAINING_RECORD(RemoveHeadList(List), PH_DEVICE_NOTIFY, ListEntry);

        PhInvokeCallback(PhGetGeneralCallback(GeneralCallbackDeviceNotificationEvent), entry);

        PhDereferenceObject(entry);
    }

    PhDereferenceObject(oldTree);
}

_Function_class_(PUSER_THREAD_START_ROUTINE)
NTSTATUS NTAPI PhpDeviceNotifyWorker(
    _In_ PVOID ThreadParameter
    )
{
    PhSetThreadName(NtCurrentThread(), L"DeviceNotifyWorker");

    for (;;)
    {
        LIST_ENTRY list;

        // delay to process events in blocks
        PhDelayExecution(1000);

        PhAcquireFastLockExclusive(&PhpDeviceNotifyLock);

        if (IsListEmpty(&PhpDeviceNotifyList))
        {
            NtResetEvent(PhpDeviceNotifyEvent, NULL);
            PhReleaseFastLockExclusive(&PhpDeviceNotifyLock);
            NtWaitForSingleObject(PhpDeviceNotifyEvent, FALSE, NULL);
            continue;
        }

        // drain the list
        list = PhpDeviceNotifyList;
        list.Flink->Blink = &list;
        list.Blink->Flink = &list;
        InitializeListHead(&PhpDeviceNotifyList);

        PhReleaseFastLockExclusive(&PhpDeviceNotifyLock);

        PhpDeviceNotify(&list);
    }

    return STATUS_SUCCESS;
}

_Function_class_(PCM_NOTIFY_CALLBACK)
ULONG CALLBACK PhpCmNotifyCallback(
    _In_ HCMNOTIFICATION hNotify,
    _In_opt_ PVOID Context,
    _In_ CM_NOTIFY_ACTION Action,
    _In_reads_bytes_(EventDataSize) PCM_NOTIFY_EVENT_DATA EventData,
    _In_ ULONG EventDataSize
    )
{
    PPH_DEVICE_NOTIFY entry = PhCreateObjectZero(sizeof(PH_DEVICE_NOTIFY), PhDeviceNotifyType);

    switch (Action)
    {
    case CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL:
        {
            entry->Action = PhDeviceNotifyInterfaceArrival;
            RtlCopyMemory(&entry->DeviceInterface.ClassGuid, &EventData->u.DeviceInterface.ClassGuid, sizeof(GUID));
        }
        break;
    case CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL:
        {
            entry->Action = PhDeviceNotifyInterfaceRemoval;
            RtlCopyMemory(&entry->DeviceInterface.ClassGuid, &EventData->u.DeviceInterface.ClassGuid, sizeof(GUID));
        }
        break;
    case CM_NOTIFY_ACTION_DEVICEINSTANCEENUMERATED:
        {
            entry->Action = PhDeviceNotifyInstanceEnumerated;
            entry->DeviceInstance.InstanceId = PhCreateString(EventData->u.DeviceInstance.InstanceId);
        }
        break;
    case CM_NOTIFY_ACTION_DEVICEINSTANCESTARTED:
        {
            entry->Action = PhDeviceNotifyInstanceStarted;
            entry->DeviceInstance.InstanceId = PhCreateString(EventData->u.DeviceInstance.InstanceId);
        }
        break;
    case CM_NOTIFY_ACTION_DEVICEINSTANCEREMOVED:
        {
            entry->Action = PhDeviceNotifyInstanceRemoved;
            entry->DeviceInstance.InstanceId = PhCreateString(EventData->u.DeviceInstance.InstanceId);
        }
        break;
    default:
        return ERROR_SUCCESS;
    }

    PhAcquireFastLockExclusive(&PhpDeviceNotifyLock);
    InsertTailList(&PhpDeviceNotifyList, &entry->ListEntry);
    NtSetEvent(PhpDeviceNotifyEvent, NULL);
    PhReleaseFastLockExclusive(&PhpDeviceNotifyLock);

    return ERROR_SUCCESS;
}

BOOLEAN PhDeviceProviderInitialization(
    VOID
    )
{
    NTSTATUS status;
    CM_NOTIFY_FILTER cmFilter;

    if (WindowsVersion < WINDOWS_10 || !PhGetIntegerSetting(L"EnableDeviceSupport"))
        return TRUE;

    PhDeviceItemType = PhCreateObjectType(L"DeviceItem", 0, PhpDeviceItemDeleteProcedure);
    PhDeviceTreeType = PhCreateObjectType(L"DeviceTree", 0, PhpDeviceTreeDeleteProcedure);
    PhDeviceNotifyType = PhCreateObjectType(L"DeviceNotify", 0, PhpDeviceNotifyDeleteProcedure);
    PhpDeviceInfoType = PhCreateObjectType(L"DeviceInfo", 0, PhpDeviceInfoDeleteProcedure);

    PhpDeviceTree = PhpCreateDeviceTree();

    InitializeListHead(&PhpDeviceNotifyList);
    if (!NT_SUCCESS(status = NtCreateEvent(&PhpDeviceNotifyEvent, EVENT_ALL_ACCESS, NULL, NotificationEvent, FALSE)))
        return FALSE;
    if (!NT_SUCCESS(status = PhCreateThread2(PhpDeviceNotifyWorker, NULL)))
        return FALSE;

    RtlZeroMemory(&cmFilter, sizeof(CM_NOTIFY_FILTER));
    cmFilter.cbSize = sizeof(CM_NOTIFY_FILTER);

    cmFilter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINSTANCE;
    cmFilter.Flags = CM_NOTIFY_FILTER_FLAG_ALL_DEVICE_INSTANCES;
    if (CM_Register_Notification(
        &cmFilter,
        NULL,
        PhpCmNotifyCallback,
        &PhpDeviceNotification
        ) != CR_SUCCESS)
    {
        return FALSE;
    }

    cmFilter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    cmFilter.Flags = CM_NOTIFY_FILTER_FLAG_ALL_INTERFACE_CLASSES;
    if (CM_Register_Notification(
        &cmFilter,
        NULL,
        PhpCmNotifyCallback,
        &PhpDeviceInterfaceNotification
        ) != CR_SUCCESS)
    {
        return FALSE;
    }

    return TRUE;
}
