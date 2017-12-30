/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *	Swansea University Computer Society	NET3
 *	
 *	This file declares the constants of special use with the SLIP/CSLIP/
 *	KISS TNC driver.
 */
 
#ifndef __LINUX_SLIP_H
#define __LINUX_SLIP_H

#define		SL_MODE_SLIP		0
#define		SL_MODE_CSLIP		1
#define 	SL_MODE_KISS		4

#define		SL_OPT_SIXBIT		2
#define		SL_OPT_ADAPTIVE		8

/*
 *	VSV = ioctl for keepalive & outfill in SLIP driver 
 */
 
#define SIOCSKEEPALIVE	(SIOCDEVPRIVATE)		/* Set keepalive timeout in sec */
#define SIOCGKEEPALIVE	(SIOCDEVPRIVATE+1)		/* Get keepalive timeout */
#define SIOCSOUTFILL	(SIOCDEVPRIVATE+2)		/* Set outfill timeout */
#define	SIOCGOUTFILL	(SIOCDEVPRIVATE+3)		/* Get outfill timeout */
#define SIOCSLEASE	(SIOCDEVPRIVATE+4)		/* Set "leased" line type */
#define	SIOCGLEASE	(SIOCDEVPRIVATE+5)		/* Get line type */


#endif
