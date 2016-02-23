/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Global definitions for the Frame relay interface.
 *
 * Version:	@(#)if_ifrad.h	0.20	13 Apr 96
 *
 * Author:	Mike McLagan <mike.mclagan@linux.org>
 *
 * Changes:
 *		0.15	Mike McLagan	Structure packing
 *
 *		0.20	Mike McLagan	New flags for S508 buffer handling
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#ifndef _UAPISDLA_H
#define _UAPISDLA_H

/* adapter type */
#define SDLA_TYPES
#define SDLA_S502A			5020
#define SDLA_S502E			5021
#define SDLA_S503			5030
#define SDLA_S507			5070
#define SDLA_S508			5080
#define SDLA_S509			5090
#define SDLA_UNKNOWN			-1

/* port selection flags for the S508 */
#define SDLA_S508_PORT_V35		0x00
#define SDLA_S508_PORT_RS232		0x02

/* Z80 CPU speeds */
#define SDLA_CPU_3M			0x00
#define SDLA_CPU_5M			0x01
#define SDLA_CPU_7M			0x02
#define SDLA_CPU_8M			0x03
#define SDLA_CPU_10M			0x04
#define SDLA_CPU_16M			0x05
#define SDLA_CPU_12M			0x06

/* some private IOCTLs */
#define SDLA_IDENTIFY			(FRAD_LAST_IOCTL + 1)
#define SDLA_CPUSPEED			(FRAD_LAST_IOCTL + 2)
#define SDLA_PROTOCOL			(FRAD_LAST_IOCTL + 3)

#define SDLA_CLEARMEM			(FRAD_LAST_IOCTL + 4)
#define SDLA_WRITEMEM			(FRAD_LAST_IOCTL + 5)
#define SDLA_READMEM			(FRAD_LAST_IOCTL + 6)

struct sdla_mem {
   int  addr;
   int  len;
   void __user *data;
};

#define SDLA_START			(FRAD_LAST_IOCTL + 7)
#define SDLA_STOP			(FRAD_LAST_IOCTL + 8)

/* some offsets in the Z80's memory space */
#define SDLA_NMIADDR			0x0000
#define SDLA_CONF_ADDR			0x0010
#define SDLA_S502A_NMIADDR		0x0066
#define SDLA_CODE_BASEADDR		0x0100
#define SDLA_WINDOW_SIZE		0x2000
#define SDLA_ADDR_MASK			0x1FFF

/* largest handleable block of data */
#define SDLA_MAX_DATA			4080
#define SDLA_MAX_MTU			4072	/* MAX_DATA - sizeof(fradhdr) */
#define SDLA_MAX_DLCI			24

/* this should be the same as frad_conf */
struct sdla_conf {
   short station;
   short config;
   short kbaud;
   short clocking;
   short max_frm;
   short T391;
   short T392;
   short N391;
   short N392;
   short N393;
   short CIR_fwd;
   short Bc_fwd;
   short Be_fwd;
   short CIR_bwd;
   short Bc_bwd;
   short Be_bwd;
};

/* this should be the same as dlci_conf */
struct sdla_dlci_conf {
   short config;
   short CIR_fwd;
   short Bc_fwd;
   short Be_fwd;
   short CIR_bwd;
   short Bc_bwd;
   short Be_bwd; 
   short Tc_fwd;
   short Tc_bwd;
   short Tf_max;
   short Tb_max;
};


#endif /* _UAPISDLA_H */
