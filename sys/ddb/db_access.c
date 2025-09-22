/*	$OpenBSD: db_access.c,v 1.16 2019/11/07 13:16:25 mpi Exp $	*/
/*	$NetBSD: db_access.c,v 1.8 1994/10/09 08:37:35 mycroft Exp $	*/

/*
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * the rights to redistribute these changes.
 *
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

#include <sys/param.h>
#include <sys/endian.h>

#include <machine/db_machdep.h>		/* type definitions */

#include <ddb/db_access.h>

/*
 * Access unaligned data items on aligned (longword)
 * boundaries.
 */
db_expr_t
db_get_value(vaddr_t addr, size_t size, int is_signed)
{
	char data[sizeof(db_expr_t)];
	db_expr_t value, extend;
	int i;

#ifdef DIAGNOSTIC
	if (size > sizeof data)
		size = sizeof data;
#endif

	db_read_bytes(addr, size, data);

	value = 0;
	extend = (~(db_expr_t)0) << (size * 8 - 1);
#if BYTE_ORDER == LITTLE_ENDIAN
	for (i = size - 1; i >= 0; i--)
#else /* BYTE_ORDER == BIG_ENDIAN */
	for (i = 0; i < size; i++)
#endif /* BYTE_ORDER */
		value = (value << 8) + (data[i] & 0xFF);

	if (size < sizeof(db_expr_t) && is_signed && (value & extend))
		value |= extend;
	return (value);
}

void
db_put_value(vaddr_t addr, size_t size, db_expr_t value)
{
	char data[sizeof(db_expr_t)];
	int i;

#ifdef DIAGNOSTIC
	if (size > sizeof data)
		size = sizeof data;
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
	for (i = 0; i < size; i++)
#else /* BYTE_ORDER == BIG_ENDIAN */
	for (i = size - 1; i >= 0; i--)
#endif /* BYTE_ORDER */
	{
		data[i] = value & 0xff;
		value >>= 8;
	}

	db_write_bytes(addr, size, data);
}
