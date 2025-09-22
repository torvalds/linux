/*	$OpenBSD: bbinfo.h,v 1.5 1997/05/05 06:01:45 millert Exp $	*/
/*	$NetBSD: bbinfo.h,v 1.2 1997/04/06 08:40:57 cgd Exp $	*/

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

struct bbinfoloc {
	u_int64_t	magic1;
	u_int64_t	start;
	u_int64_t	end;
	u_int64_t	pad1[4];
	u_int64_t	magic2;
};

struct bbinfo {
	int32_t		cksum;
	int32_t		nblocks;
	int32_t		bsize;
	int32_t		pad1[8];
	int32_t		blocks[1];
};

struct netbbinfo {
	u_int64_t	magic1;
	u_int8_t	set;
	u_int8_t	ether_addr[6];
	u_int8_t	force;
	u_int64_t	pad1[4];
	u_int64_t	cksum;
	u_int64_t	magic2;
};
