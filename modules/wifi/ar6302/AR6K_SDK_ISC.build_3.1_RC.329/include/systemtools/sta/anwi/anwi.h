// Copyright (c) 2010 Atheros Communications Inc.
// All rights reserved.
// $ATH_LICENSE_TARGET_C$

/*
 * The main communication interface structures definition
 * for the anwi interface. The IOCTL'definition are in the
 * anwiIoctls.h. 
 *  
 *  Revisions:
 */

#ifndef __ANWI_H__
#define __ANWI_H__

#include <ntddk.h>

// comment undef ANWI_DEBUG to enable debug messages
#define ANWI_DEBUG 1
//#undef ANWI_DEBUG

// comment undef ANWI_NO_UNLOAD to prevent unloading the driver
// once it is loaded.
#define ANWI_NO_UNLOAD 1
//#undef ANWI_NO_UNLOAD

#define ANWI_MAJOR_VERSION	1
#define ANWI_MINOR_VERSION	3
#define COPY_ZERO_BYTES	0

// MACRO PCI_BUS has to be defined for PCI based cards
#define ATHEROS_VENDOR_ID	0x168c
#define PCI_BUS		1
#define MAX_BARS	6

// 2 MB memory for frames and descriptors
#define MEM_SIZE	(2048*1024) 

#define MAX_REG_OFFSET (64 * 1024)

typedef enum resourceType_ 
{ 
	RES_NONE=0,
	RES_INTERRUPT=1, 
	RES_MEMORY=2, 
	RES_IO=3, 
} resourceType, *pResourceType;

/*
 * This is the structure used to query the anwi driver regarding
 * PCI resources of the device, while use the value of 
 * WhichPCIInfo selects the structure in use inside the union
 */
typedef struct anwiPartialResourceInfo_ {
	resourceType res_type; // ITEM_TYPE
	union {
		struct { // ITEM_MEMORY
			PULONG32 mappedAddress; // Mapped Address
			ULONG32 nBytes;  // address range
		} Mem;
        
		struct { // ITEM_IO
			PULONG32 bAddr;  // beginning of io address
			ULONG32 nBytes; // IO range
		} IO;

		struct { // ITEM_INTERRUPT
			ULONG32 irql; // Interrupt Number.
			ULONG32 vector; // Interrupt Vector
		} Int;

		struct {
			ULONG32 dw1;
			ULONG32 dw2;
		} Val;
	} I;
} anwiPartialResourceInfo, *pAnwiPartialResourceInfo;


typedef struct anwiFullResourceInfo_ {
	ULONG32 numItems;
	anwiPartialResourceInfo Item[MAX_BARS]; //Array of information items.
} anwiFullResourceInfo, *pAnwiFullResourceInfo;

typedef struct anwiAddrDesc_ {
	ULONG32 valid;
	PVOID physAddr;// physical address of the allocated memory
	PVOID kerVirAddr; // Virtual address of the allocated memory in kernel space
	PVOID usrVirAddr; // Virtual address of the allocated memory in user space 
    		  	// if mapped to user space
	PMDL pMdl;
    	ULONG32 range;	// size of the allocation
        resourceType res_type;
} anwiAddrDesc, *pAnwiAddrDesc; 

typedef struct anwiIntrDesc_ {
	PKINTERRUPT pIntObj; // Pointer to the Interrupt object of this device
	KIRQL	irql;
	KIRQL	syncIrql;
	KAFFINITY affinity;
	KINTERRUPT_MODE intMode;
	ULONG vector;
} anwiIntrDesc, *pAnwiIntrDesc;

#endif /* __ANWI_H__ */
