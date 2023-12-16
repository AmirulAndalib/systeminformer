/*
 * Copyright (c) 2022 Winsider Seminars & Solutions, Inc.  All rights reserved.
 *
 * This file is part of System Informer.
 *
 * sistatus.h IS AN AUTOGENERATED FILE, DO NOT MODIFY
 *
 * Changes should be made in sistatus.mc
 *
 */

#ifndef SI_STATUS_H
#define SI_STATUS_H
//
//  Values are 32 bit values laid out as follows:
//
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +---+-+-+-----------------------+-------------------------------+
//  |Sev|C|R|     Facility          |               Code            |
//  +---+-+-+-----------------------+-------------------------------+
//
//  where
//
//      Sev - is the severity code
//
//          00 - Success
//          01 - Informational
//          10 - Warning
//          11 - Error
//
//      C - is the Customer code flag
//
//      R - is a reserved bit
//
//      Facility - is the facility code
//
//      Code - is the facility's status code
//
//
// Define the facility codes
//
#define FACILITY_SI                      0x1
#define FACILITY_SI_DYNDATA              0x2
#define FACILITY_KSI                     0x3


//
// Define the severity codes
//
#define STATUS_SEVERITY_SUCCESS          0x0
#define STATUS_SEVERITY_INFORMATIONAL    0x1
#define STATUS_SEVERITY_WARNING          0x2
#define STATUS_SEVERITY_ERROR            0x3


//
// MessageId: STATUS_SI_DYNDATA_UNSUPPORTED_KERNEL
//
// MessageText:
//
// System Informer dynamic data is not yet supported on this kernel version.
//
#define STATUS_SI_DYNDATA_UNSUPPORTED_KERNEL ((NTSTATUS)0xE0020001L)

//
// MessageId: STATUS_SI_DYNDATA_VERSION_MISMATCH
//
// MessageText:
//
// System Informer dynamic data version is incompatible.
//
#define STATUS_SI_DYNDATA_VERSION_MISMATCH ((NTSTATUS)0xE0020002L)

//
// MessageId: STATUS_SI_DYNDATA_INVALID_LENGTH
//
// MessageText:
//
// System Informer dynamic data is an invalid length.
//
#define STATUS_SI_DYNDATA_INVALID_LENGTH ((NTSTATUS)0xE0020003L)

//
// MessageId: STATUS_SI_DYNDATA_INVALID_SIGNATURE
//
// MessageText:
//
// System Informer dynamic data signature is invalid.
//
#define STATUS_SI_DYNDATA_INVALID_SIGNATURE ((NTSTATUS)0xE0020004L)

#endif
