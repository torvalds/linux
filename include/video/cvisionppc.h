/*
 * Phase5 CybervisionPPC (TVP4020) definitions for the Permedia2 framebuffer
 * driver.
 *
 * Copyright (c) 1998-1999 Ilario Nardinocchi (nardinoc@CS.UniBO.IT)
 * --------------------------------------------------------------------------
 * $Id: cvisionppc.h,v 1.8 1999/01/28 13:18:07 illo Exp $
 * --------------------------------------------------------------------------
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef CVISIONPPC_H
#define CVISIONPPC_H

#ifndef PM2FB_H
#include "pm2fb.h"
#endif

struct cvppc_par {
	unsigned char* pci_config;
	unsigned char* pci_bridge;
	u32 user_flags;
};

#define CSPPC_PCI_BRIDGE		0xfffe0000
#define CSPPC_BRIDGE_ENDIAN		0x0000
#define CSPPC_BRIDGE_INT		0x0010

#define	CVPPC_PCI_CONFIG		0xfffc0000
#define CVPPC_ROM_ADDRESS		0xe2000001
#define CVPPC_REGS_REGION		0xef000000
#define CVPPC_FB_APERTURE_ONE		0xe0000000
#define CVPPC_FB_APERTURE_TWO		0xe1000000
#define CVPPC_FB_SIZE			0x00800000
#define CVPPC_MEM_CONFIG_OLD		0xed61fcaa	/* FIXME Fujitsu?? */
#define CVPPC_MEM_CONFIG_NEW		0xed41c532	/* FIXME USA?? */
#define CVPPC_MEMCLOCK			83000		/* in KHz */

/* CVPPC_BRIDGE_ENDIAN */
#define CSPPCF_BRIDGE_BIG_ENDIAN	0x02

/* CVPPC_BRIDGE_INT */
#define CSPPCF_BRIDGE_ACTIVE_INT2	0x01

#endif	/* CVISIONPPC_H */

/*****************************************************************************
 * That's all folks!
 *****************************************************************************/
