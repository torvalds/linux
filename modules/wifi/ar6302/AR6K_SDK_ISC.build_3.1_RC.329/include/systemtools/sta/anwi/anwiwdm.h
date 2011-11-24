// Copyright (c) 2010 Atheros Communications Inc.
// All rights reserved.
// $ATH_LICENSE_TARGET_C$

/*
 * anwiWdm.h - this header file contains all the
 * declarations for the anwi WDM driver. 
 *
 * Revisions
 *
 */

#ifndef __ANWI_WDM__
#define __ANWI_WDM__

#include "ntddk.h"
#include "anwi.h"

// The device Extension for the anwi wdm dummy device
typedef struct DEVICE_EXTENSION_ {
	PDEVICE_OBJECT pDevice; // Back pointer to device object
	PDRIVER_OBJECT pDriver; // Pointer to the driver object
	PDEVICE_OBJECT pLowerDevice; // Pointer to the next lower decice in the stack
	ULONG  deviceNumber;
	UNICODE_STRING symLinkName;
	UNICODE_STRING symLinkName_uart;
	ULONG	dummy;
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

#endif /* __ANWI_WDM__ */
