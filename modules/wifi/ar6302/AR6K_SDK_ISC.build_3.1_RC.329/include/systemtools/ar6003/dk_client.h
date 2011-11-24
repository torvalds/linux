// dk_client.h - contains definitions of dk structures  

// Copyright (c) 2000 Atheros Communications, Inc., All Rights Reserved
// $ATH_LICENSE_TARGET_C$
 
// modification history
// --------------------
// 000	00jan02 	sharmat		created (copied from windows client)

// DESCRIPTION
// -----------
// Contains the definitions of main dk strucutres.

#ifndef	__INCdk_clienth
#define	__INCdk_clienth


#if !defined(ECOS) && !defined(ATHOS)
#include "wlantype.h"
#endif

#ifdef WIN32
#ifdef JUNGO
#include "windrvr.h"
#endif
#endif

#define DK_CLIENT_EXE ("mdk_client.exe")
#define MDK_EXE ("mdk.exe")

#define MDK_MAIN ("mdk_main")
#define MDK_CLIENT ("mdk_client")
#define MDK_MAIN_PRIO 95
#define MDK_CLIENT_PRIO 100


#if !defined(VXWORKS) && !defined(ECOS) && !defined(ATHOS)
#include <dk_structures.h>
#endif

#define MAX_BARS  6
#define WLAN_MAX_DEV	8		/* Number of maximum supported devices */

typedef enum {
    CLIENT_WLAN_MODE_NOHT40 = 0,
    CLIENT_WLAN_MODE_HT40 = 1,
} CLIENT_WLAN_MODE;

#if defined(VXWORKS) || defined(ECOS) || defined (ATHOS)
/* holds all the dk specific information within DEV_INFO structure */
typedef struct dkDevInfo {
  A_UINT32    devIndex;   /* used to track which F2 within system this is */
  A_UINT32    f2Mapped;           /* true if the f2 registers are mapped */
  A_UINT32    devMapped;          /* true if the f2 registers are mapped */
  A_UINT32    f2MapAddress;
  A_UINT32    regVirAddr;
  A_UINT32    regMapRange;
  A_UINT32    memPhyAddr;
  A_UINT32    memVirAddr;
  A_UINT32    memSize;
  A_UINT32      haveEvent;
  A_UINT32    aregPhyAddr[MAX_BARS];
  A_UINT32    aregVirAddr[MAX_BARS];
  A_UINT32    aregRange[MAX_BARS];
  A_UINT32 res_type[MAX_BARS];
  A_UINT32    bar_select;
  A_UINT32    numBars;
  A_UINT32    device_fn;
  A_UINT32		printPciWrites; //set to true when want to print pci reg writes
  A_UINT32	  version;
} DK_DEV_INFO;
#endif



#ifdef __cplusplus
extern "C" {
#endif //__cplusplus 


extern A_INT32 mdk_main(A_INT32 debugMode, A_UINT16 cport);

#ifdef __cplusplus
}
#endif // __cplusplus 

#endif //__INCdk_clienth 
