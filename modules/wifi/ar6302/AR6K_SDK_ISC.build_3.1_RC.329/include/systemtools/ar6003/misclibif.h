// Copyright (c) 2010 Atheros Communications Inc.
// All rights reserved.
// $ATH_LICENSE_TARGET_C$

#ifndef __INCmisclibifh
#define __INCmisclibifh

#include "wlantype.h"
#include "dk_cmds.h"

A_INT32 m_loadAndRunCode
(
	A_UINT32 loadFlag,
	A_UINT32 pPhyAddr,
	A_UINT32 length,
 	A_UCHAR  *pBuffer,
	A_UINT32 devIndex
);

A_INT32 m_loadAndProgramCode
(
	A_UINT32 loadFlag,
	A_UINT32 csAddr,
	A_UINT32 pPhyAddr,
	A_UINT32 length,
 	A_UCHAR  *pBuffer,
	A_UINT32 devIndex
);

void m_writeNewProdData
(
	A_UINT32 devNum,
	A_INT32  argList[16],
	A_UINT32 numArgs
);

void m_writeProdData
(
	A_UINT32 devNum,
	A_UCHAR wlan0Mac[6],
	A_UCHAR wlan1Mac[6],
	A_UCHAR enet0Mac[6], 
	A_UCHAR enet1Mac[6]
);

A_INT32 m_ftpDownloadFile
(
 A_CHAR *hostname,
 A_CHAR *user,
 A_CHAR *passwd,
 A_CHAR *remotefile,
 A_CHAR *localfile
);

A_INT32 m_runScreeningTest
(	
	A_UINT32 testId
);

#endif

