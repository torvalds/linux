// dk_mem.h - macros and definitions for memory management

// Copyright © 2000-2001 Atheros Communications, Inc.,  All Rights Reserved.
// $ATH_LICENSE_TARGET_C$

// modification history
// --------------------
// 01Dec01 	sharmat		created (copied from windows client)

#ifndef __INCdk_memh
#define __INCdk_memh


#ifdef __cplusplus
extern "C" {
#endif // __cplusplus 
#include "wlantype.h"

#if defined(VXWORKS) 
#include "hw.h"
#else
#include "common_hw.h"
#endif

// conflicting with a function declared in mAlloc.c using the suffix #2
A_STATUS memGetIndexForBlock2 
(
	MDK_WLAN_DEV_INFO *pdevInfo, // pointer to device info structure 
	A_UCHAR *mapBytes, // pointer to the map bits to reference 
	A_UINT16 numBlocks, // number of blocks want to allocate
	A_UINT16 *pIndex // gets updated with index
);

// conflicting with a function declared in mAlloc.c using the suffix #2
void memMarkIndexesFree2
(
	MDK_WLAN_DEV_INFO *pdevInfo, // pointer to device info structure 
	A_UINT16 index, // the index to free 
	A_UCHAR *mapBytes // pointer to the map bits to reference 
);

#ifdef __cplusplus
}
#endif // __cplusplus 

#endif // __INCdk_memh
