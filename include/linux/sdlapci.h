/*****************************************************************************
* sdlapci.h	WANPIPE(tm) Multiprotocol WAN Link Driver.
*		Definitions for the SDLA PCI adapter.
*
* Author:	Gideon Hack	<ghack@sangoma.com>
*
* Copyright:	(c) 1999-2000 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Jun 02, 1999	Gideon Hack	Initial version.
*****************************************************************************/
#ifndef	_SDLAPCI_H
#define	_SDLAPCI_H

/****** Defines *************************************************************/

/* Definitions for identifying and finding S514 PCI adapters */
#define V3_VENDOR_ID		0x11B0		/* V3 vendor ID number */
#define V3_DEVICE_ID  		0x0002		/* V3 device ID number */
#define SANGOMA_SUBSYS_VENDOR 	0x4753		/* ID for Sangoma */
#define PCI_DEV_SLOT_MASK	0x1F		/* mask for slot numbering */
#define PCI_IRQ_NOT_ALLOCATED	0xFF		/* interrupt line for no IRQ */

/* Local PCI register offsets */ 
#define PCI_VENDOR_ID_WORD	0x00		/* vendor ID */
#define PCI_IO_BASE_DWORD	0x10		/* IO base */	
#define PCI_MEM_BASE0_DWORD	0x14		/* memory base - apperture 0 */
#define PCI_MEM_BASE1_DWORD     0x18		/* memory base - apperture 1 */
#define PCI_SUBSYS_VENDOR_WORD 	0x2C		/* subsystem vendor ID */
#define PCI_INT_LINE_BYTE	0x3C		/* interrupt line */
#define PCI_INT_PIN_BYTE	0x3D		/* interrupt pin */
#define PCI_MAP0_DWORD		0x40		/* PCI to local bus address 0 */
#define PCI_MAP1_DWORD          0x44		/* PCI to local bus address 1 */
#define PCI_INT_STATUS          0x48		/* interrupt status */
#define PCI_INT_CONFIG		0x4C		/* interrupt configuration */
  
/* Local PCI register usage */
#define PCI_MEMORY_ENABLE	0x00000003	/* enable PCI memory */
#define PCI_CPU_A_MEM_DISABLE	0x00000002	/* disable CPU A memory */
#define PCI_CPU_B_MEM_DISABLE  	0x00100002	/* disable CPU B memory */
#define PCI_ENABLE_IRQ_CPU_A	0x005A0004	/* enable IRQ for CPU A */
#define PCI_ENABLE_IRQ_CPU_B    0x005A0008	/* enable IRQ for CPU B */
#define PCI_DISABLE_IRQ_CPU_A   0x00000004	/* disable IRQ for CPU A */
#define PCI_DISABLE_IRQ_CPU_B   0x00000008	/* disable IRQ for CPU B */
 
/* Setting for the Interrupt Status register */  
#define IRQ_CPU_A               0x04            /* IRQ for CPU A */
#define IRQ_CPU_B               0x08		/* IRQ for CPU B */

/* The maximum size of the S514 memory */
#define MAX_SIZEOF_S514_MEMORY	(256 * 1024)

/* S514 control register offsets within the memory address space */
#define S514_CTRL_REG_BYTE	0x80000
 
/* S514 adapter control bytes */
#define S514_CPU_HALT 		0x00
#define S514_CPU_START		0x01

/* The maximum number of S514 adapters supported */
#define MAX_S514_CARDS		20	

#define PCI_CARD_TYPE		0x2E
#define S514_DUAL_CPU		0x12
#define S514_SINGLE_CPU		0x11

#endif	/* _SDLAPCI_H */

