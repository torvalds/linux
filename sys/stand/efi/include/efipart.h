/* $FreeBSD: head/sys/boot/efi/include/efipart.h 163898 2006-11-02 02:42:48Z marcel $ */
#ifndef _EFI_PART_H
#define _EFI_PART_H

/*++

Copyright (c)  1999 - 2002 Intel Corporation. All rights reserved
This software and associated documentation (if any) is furnished
under a license and may only be used or copied in accordance
with the terms of the license. Except as permitted by such
license, no part of this software or documentation may be
reproduced, stored in a retrieval system, or transmitted in any
form or by any means without the express written consent of
Intel Corporation.

Module Name:

    efipart.h

Abstract:
    Info about disk partitions and Master Boot Records




Revision History

--*/

//
//
//

#define EFI_PARTITION   0xef
#define MBR_SIZE        512

#pragma pack(1)

typedef struct {
    UINT8       BootIndicator;
    UINT8       StartHead;
    UINT8       StartSector;
    UINT8       StartTrack;
    UINT8       OSIndicator;
    UINT8       EndHead;
    UINT8       EndSector;
    UINT8       EndTrack;
    UINT8       StartingLBA[4];
    UINT8       SizeInLBA[4];
} MBR_PARTITION_RECORD;

#define EXTRACT_UINT32(D) (UINT32)(D[0] | (D[1] << 8) | (D[2] << 16) | (D[3] << 24))

#define MBR_SIGNATURE           0xaa55
#define MIN_MBR_DEVICE_SIZE     0x80000
#define MBR_ERRATA_PAD          0x40000 // 128 MB

#define MAX_MBR_PARTITIONS  4
typedef struct {
    UINT8                   BootStrapCode[440];
    UINT8                   UniqueMbrSignature[4];
    UINT8                   Unknown[2];
    MBR_PARTITION_RECORD    Partition[MAX_MBR_PARTITIONS];
    UINT16                  Signature;
} MASTER_BOOT_RECORD;
#pragma pack()


#endif
