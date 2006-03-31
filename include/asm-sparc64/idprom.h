/* $Id: idprom.h,v 1.2 1997/04/04 00:50:16 davem Exp $
 * idprom.h: Macros and defines for idprom routines
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_IDPROM_H
#define _SPARC64_IDPROM_H

#include <linux/types.h>

struct idprom {
	u8		id_format;	/* Format identifier (always 0x01) */
	u8		id_machtype;	/* Machine type */
	u8		id_ethaddr[6];	/* Hardware ethernet address */
	s32		id_date;	/* Date of manufacture */
	u32		id_sernum:24;	/* Unique serial number */
	u8		id_cksum;	/* Checksum - xor of the data bytes */
	u8		reserved[16];
};

extern struct idprom *idprom;
extern void idprom_init(void);

#endif /* !(_SPARC_IDPROM_H) */
