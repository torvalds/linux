/*-
 * SPDX-License-Identifier: MIT-CMU
 *
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
 * rights to redistribute these changes.
 */
/*
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kdb.h>
#include <sys/endian.h>

#include <ddb/ddb.h>
#include <ddb/db_access.h>

/*
 * Access unaligned data items on aligned (longword)
 * boundaries.
 */

static unsigned db_extend[] = {	/* table for sign-extending */
	0,
	0xFFFFFF80U,
	0xFFFF8000U,
	0xFF800000U
};

db_expr_t
db_get_value(db_addr_t addr, int size, bool is_signed)
{
	char		data[sizeof(u_int64_t)];
	db_expr_t	value;
	int		i;

	if (db_read_bytes(addr, size, data) != 0) {
		db_printf("*** error reading from address %llx ***\n",
		    (long long)addr);
		kdb_reenter();
	}

	value = 0;
#if _BYTE_ORDER == _BIG_ENDIAN
	for (i = 0; i < size; i++)
#else	/* _LITTLE_ENDIAN */
	for (i = size - 1; i >= 0; i--)
#endif
	{
	    value = (value << 8) + (data[i] & 0xFF);
	}

	if (size < 4) {
	    if (is_signed && (value & db_extend[size]) != 0)
		value |= db_extend[size];
	}
	return (value);
}

void
db_put_value(db_addr_t addr, int size, db_expr_t value)
{
	char		data[sizeof(int)];
	int		i;

#if _BYTE_ORDER == _BIG_ENDIAN
	for (i = size - 1; i >= 0; i--)
#else	/* _LITTLE_ENDIAN */
	for (i = 0; i < size; i++)
#endif
	{
	    data[i] = value & 0xFF;
	    value >>= 8;
	}

	if (db_write_bytes(addr, size, data) != 0) {
		db_printf("*** error writing to address %llx ***\n",
		    (long long)addr);
		kdb_reenter();
	}
}
