// Copyright (c) 2010 Atheros Communications Inc.
// All rights reserved.
// $ATH_LICENSE_TARGET_C$

/*
 * anwiIoctls.h - IOCTL definitions for anwi wdm interface.
 *
 * Versions:
 * 	Created / Karthik / Date
 *
 */

#ifndef __ANWI_IOCTL_H__
#define __ANWI_IOCTL_H__

#define MAX_BARS 6

// This IOCTL is sent to get the version number of the driver
 
#define IOCTL_ANWI_GET_VERSION CTL_CODE(\
		FILE_DEVICE_UNKNOWN,	\
		0X801,			\
		METHOD_IN_DIRECT,	\
		FILE_ANY_ACCESS)

// This IOCTL is sent to get the client info of the client 
#define IOCTL_ANWI_GET_CLIENT_INFO CTL_CODE(\
		FILE_DEVICE_UNKNOWN,	\
		0X802,			\
		METHOD_IN_DIRECT,	\
		FILE_ANY_ACCESS)

// This IOCTL is sent to the WDM for device I/O Access 
#define IOCTL_ANWI_DEV_OP CTL_CODE(\
		FILE_DEVICE_UNKNOWN,	\
		0X803,			\
		METHOD_IN_DIRECT,	\
		FILE_ANY_ACCESS)

// This IOCTL is sent to the WDM for Event Handling 
#define IOCTL_ANWI_EVENT_OP CTL_CODE(\
		FILE_DEVICE_UNKNOWN,	\
		0X804,			\
		METHOD_IN_DIRECT,	\
		FILE_ANY_ACCESS)

// Return codes from the wdm driver to the client application
typedef enum anwiCCC_ {
	ANWI_OK, // Call completed ok
	ANWI_PARAMETER_ERROR, // Wrong no of parameters
	ANWI_DEVICE_NOT_FOUND,
	ANWI_CREATE_CLIENT_ERROR,
	ANWI_MAX_CLIENTS_REG,	//  already serves max clients
	ANWI_INVALID_CLIENT_ID,
	ANWI_CLAIM_RESOURCE_ERROR,
	ANWI_INVALID_DEVOP_TYPE,
	ANWI_INVALID_EVENTOP_TYPE,
	ANWI_CREATE_EVENT_ERROR
} anwiCCC;

typedef enum anwiDevOpType_ {
	ANWI_CFG_READ,
	ANWI_CFG_WRITE,
	ANWI_IO_READ,
	ANWI_IO_WRITE
} anwiDevOpType;

typedef enum anwiEventOpType_ {
	ANWI_CREATE_EVENT,
	ANWI_GET_NEXT_EVENT
} anwiEventOpType;

// A generic structure is used to return the IOCTL data to the application
typedef struct anwiReturnContext_ {
	anwiCCC returnCode;	// Quick lookup code
	ULONG contextLen; // Length of the context returned.
	UCHAR context[1024]; // Optional return data from  driver
}anwiReturnContext, *pAnwiReturnContext;

typedef struct anwiInClientInfo_{
	ULONG32 baseAddress;
	ULONG32 irqLevel;
}anwiInClientInfo, *pAnwiInClientInfo;

typedef struct anwiOutClientInfo_{
	ULONG32 regPhyAddr; // retain this for backward compatibility
	ULONG32 regVirAddr; // retain this for backward compatibility
	ULONG32	memPhyAddr;
	ULONG32 memVirAddr;
	ULONG32 irqLevel;
	ULONG32	regRange; // retain this for backward compatibility
	ULONG32 memSize;
	ULONG32 aregPhyAddr[MAX_BARS];	
	ULONG32 aregVirAddr[MAX_BARS];
	ULONG32	aregRange[MAX_BARS];
    ULONG32 numBars;
    ULONG32 res_type[MAX_BARS];
}anwiOutClientInfo, *pAnwiOutClientInfo;

typedef struct anwiVersionInfo_ {
	ULONG32 majorVersion;	
	ULONG32 minorVersion;	
} anwiVersionInfo, *pAnwiVersionInfo;

typedef struct anwiDevOpStruct_ {
	anwiDevOpType opType;
	ULONG32 param1;
	ULONG32 param2;
	ULONG32 param3;
} anwiDevOpStruct,*pAnwiDevOpStruct;

typedef struct anwiEventOpStruct_ {
	anwiEventOpType opType;
	ULONG32 valid;
	ULONG32 param[16];
} anwiEventOpStruct, *pAnwiEventOpStruct;

#endif /* __ANWI_IOCTLS_H__*/
