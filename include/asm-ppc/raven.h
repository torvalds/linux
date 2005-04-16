/*
 *  include/asm-ppc/raven.h -- Raven MPIC chip.
 *
 *  Copyright (C) 1998 Johnnie Peters
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifdef __KERNEL__
#ifndef _ASMPPC_RAVEN_H
#define _ASMPPC_RAVEN_H

#define MVME2600_INT_SIO		0
#define MVME2600_INT_FALCN_ECC_ERR	1
#define MVME2600_INT_PCI_ETHERNET	2
#define MVME2600_INT_PCI_SCSI		3
#define MVME2600_INT_PCI_GRAPHICS	4
#define MVME2600_INT_PCI_VME0		5
#define MVME2600_INT_PCI_VME1		6
#define MVME2600_INT_PCI_VME2		7
#define MVME2600_INT_PCI_VME3		8
#define MVME2600_INT_PCI_INTA		9
#define MVME2600_INT_PCI_INTB		10
#define MVME2600_INT_PCI_INTC 		11
#define MVME2600_INT_PCI_INTD 		12
#define MVME2600_INT_LM_SIG0		13
#define MVME2600_INT_LM_SIG1		14

extern struct hw_interrupt_type raven_pic;

extern int raven_init(void);
#endif /* _ASMPPC_RAVEN_H */
#endif /* __KERNEL__ */
