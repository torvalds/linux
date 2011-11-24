// Copyright (c) 2010 Atheros Communications Inc.
// All rights reserved.
// $ATH_LICENSE_TARGET_C$

#ifndef __ANWIIO_H
#define __ANWIIO_H

#include "anwi.h"
#include "anwiclient.h"

ULONG32 anwiRegRead(ULONG32 offset,pAnwiAddrDesc pRegMap);
VOID anwiRegWrite(ULONG32 offset,ULONG32 data,pAnwiAddrDesc pRegMap);

ULONG32 anwiCfgRead(ULONG32 offset,ULONG32 length,pAnwiClientInfo pClient);
VOID anwiCfgWrite(ULONG32 offset,ULONG32 length,ULONG32 data,pAnwiClientInfo pClient);

USHORT anwiIORead ( ULONG32 offset, ULONG32 length);
VOID anwiIOWrite ( ULONG32 offset, ULONG32 length, USHORT data);

#endif
