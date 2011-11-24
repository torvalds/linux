#ifndef	__INCdesc_routinesh
#define	__INCdesc_routinesh

/*
#include "wlantype.h"
#include "athreg.h"
#include "mdata.h"
#ifdef THIN_CLIENT_BUILD
#include "hwext.h"
#else
#include "errno.h"
#include "common_defs.h"
#include "mDevtbl.h"
#endif
#include "mConfig.h"

extern void zeroDescriptorStatus ( A_UINT32	devNumIndex, MDK_ATHEROS_DESC *pDesc, A_UINT32 swDevID );
extern void writeDescriptors ( A_UINT32	devNumIndex, A_UINT32	descAddress, MDK_ATHEROS_DESC *pDesc, A_UINT32   numDescriptors);
extern void writeDescriptor ( A_UINT32	devNum, A_UINT32	descAddress, MDK_ATHEROS_DESC *pDesc);

*/

void createDescriptors(A_UINT32 devNumIndex, A_UINT32 descBaseAddress,  A_UINT32 descInfo, A_UINT32 bufAddrIncrement, A_UINT32 descOp, A_UINT32 *descWords);
       
#endif

