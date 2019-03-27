/*******************************************************************************
*Copyright (c) 2014 PMC-Sierra, Inc.  All rights reserved. 
*
*Redistribution and use in source and binary forms, with or without modification, are permitted provided 
*that the following conditions are met: 
*1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
*following disclaimer. 
*2. Redistributions in binary form must reproduce the above copyright notice, 
*this list of conditions and the following disclaimer in the documentation and/or other materials provided
*with the distribution. 
*
*THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED 
*WARRANTIES,INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
*FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
*NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR 
*BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
*LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
*SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
*
* $FreeBSD$
*
*******************************************************************************/
/*******************************************************************************
**
** Version Control Information:
**
**  $Revision: 114125 $
**  $Author: lindfors $
**  $Date: 2012-01-06 17:12:27 -0800 (Fri, 06 Jan 2012) $
**  $Id: encrypt_ioctl.h 112360 2012-01-07 01:12:27Z mcleanda $
**
*******************************************************************************/
#include <linux/ioctl.h>

#ifndef __ENCRYPT_IOCTL_H__
#define __ENCRYPT_IOCTL_H__

#include <dev/pms/RefTisa/tisa/api/tiapi.h>
#include <dev/pms/RefTisa/tisa/api/ostiapi.h>
#include <dev/pms/RefTisa/tisa/api/tidefs.h>
#include <dev/pms/RefTisa/tisa/api/tiglobal.h>
#include <dev/pms/RefTisa/tisa/api/titypes.h>
#include <dev/pms/RefTisa/tisa/sassata/common/tdioctl.h>
#include <dev/pms/freebsd/driver/common/osenv.h>
#include <dev/pms/freebsd/driver/common/ostypes.h>
#include <dev/pms/freebsd/driver/common/osdebug.h>

typedef struct IoctlEncryptGetInfo_s {
    tiEncryptInfo_t tisaEncryptInfo;
} __attribute__ ((packed)) IoctlEncryptGetInfo_t;

typedef struct IoctlEncryptSetMode_s {
    bit32 securityCipherMode;
} __attribute__ ((packed))  IoctlEncryptSetMode_t;

typedef struct IoctlEncryptKekAdd_s {
    bit32              kekIndex;
    bit32              wrapperKekIndex;
    bit32              blobFormat;
    tiEncryptKekBlob_t *EncryptKekBlob;
} __attribute__ ((packed)) IoctlEncryptKekAdd_t;

typedef struct IoctlEncryptDekAdd_s {
    bit32              kekIndex;
    bit32              dekTable;
    bit32              dekIndex;
	bit32              dekBlobFormat;
    bit32              dekTableKeyEntrySize;
    tiEncryptDekBlob_t *dekBlob;
} __attribute__ ((packed)) IoctlEncryptDekAdd_t;

typedef struct IoctlEncryptDekInvalidate_s {
    tiEncryptDek_t dek;
} __attribute__ ((packed)) IoctlEncryptDekInvalidate_t;

typedef struct IoctlEncryptKekNVRAM_s {
    bit32 index;
} __attribute__ ((packed)) IoctlEncryptKekNVRAM_t;

typedef struct IoctlEncryptDekTable_s {
    tiMem_t DekTable1Addr;
    tiMem_t DekTable2Addr;
} __attribute__ ((packed)) IoctlEncryptDekTable_t;

typedef struct EncryptDekMapEntry_s {
#define ENCRYPT_DEK_MAP_ENTRY_CLEAR 0x00000001UL
#define ENCRYPT_DEK_MAP_ENTRY_VALID 0x80000000UL
    bit32              flags;
    unsigned long long startLBA;
    unsigned long long endLBA;
    tiEncryptDek_t     dek;
} __attribute__ ((packed)) EncryptDekMapEntry_t;

typedef struct EncryptDeviceDekMap_s {
    bit32                host;
    bit32                channel;
    bit32                device;
    bit32                lun;
    bit32                keytag_check;
    bit32                keytag[2];
    EncryptDekMapEntry_t dekMapEntry[1];
} __attribute__ ((packed)) EncryptDeviceDekMap_t;

typedef struct IoctlEncryptDekMapTable_s {
    EncryptDeviceDekMap_t dekMap[1];
} __attribute__ ((packed)) IoctlEncryptDekMapTable_t; 

typedef struct IoctlEncryptIOError_s {
    bit64                error_id;
    bit64                timestamp;
    bit32                error_type;
    bit32                host;
    bit32                channel;
    bit32                device;
    bit32                lun;
    bit32                scsi_cmd;
    bit32                dek_index;
    bit32                dek_table;
    bit32                kek_index;
    bit32                encrypt_mode;
    bit32                keytag_check;
    bit32                keytag[2];
} __attribute__ ((packed)) IoctlEncryptIOError_t;

typedef struct __attribute__ ((packed)) IoctlEncryptErrorQuery_s {
#define ERROR_QUERY_FLAG_BLOCK 1
   bit32                 query_flag;
   bit32                 valid_mask;
   IoctlEncryptIOError_t error[32];
} __attribute__ ((packed)) IoctlEncryptErrorQuery_t;

typedef union IoctlEncryptOp_u {
    IoctlEncryptGetInfo_t       encryptGetInfo;
    IoctlEncryptSetMode_t       encryptSetMode;
    IoctlEncryptKekAdd_t        encryptKekAdd;
    IoctlEncryptDekAdd_t        encryptDekAdd;
    IoctlEncryptDekInvalidate_t encryptDekInvalidate;
    IoctlEncryptKekNVRAM_t      encryptKekNVRAM;
    IoctlEncryptDekMapTable_t   encryptDekMap;
    IoctlEncryptErrorQuery_t    encryptErrorQuery;
} __attribute__ ((packed)) IoctlEncryptOp_t;

typedef struct tiIOCTLPayloadHeader_s {
    bit32 Signature;
    bit16 MajorFunction;
    bit16 MinorFunction;
    bit16 Length;
    bit16 Status;
} __attribute__ ((packed)) tiIOCTLPayloadHeader_t;

typedef struct IoctlTISAEncrypt_s {
#define encryptGetInfo       0x00000001
#define encryptSetMode       0x00000002
#define encryptKekAdd        0x00000003
#define encryptDekAdd        0x00000004
#define encryptDekInvalidate 0x00000005
#define encryptKekStore      0x00000006
#define encryptKekLoad       0x00000007
#define encryptGetDekTable   0x00000008
#define encryptSetDekMap     0x00000009
#define encryptDekDump       0x0000000a
#define encryptErrorQuery    0x0000000c
    bit32            encryptFunction;
    bit32            status;
    bit32            subEvent;
    IoctlEncryptOp_t request;
} __attribute__ ((packed)) IoctlTISAEncrypt_t;

typedef struct IOCTLEncrypt_s {
    tiIOCTLPayloadHeader_t hdr;
    IoctlTISAEncrypt_t     body;
} __attribute__ ((packed)) IoctlEncrypt_t;

#endif
