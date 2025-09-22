/*	$OpenBSD: db_rint.c,v 1.1 2022/07/12 17:12:31 jca Exp $	*/
/*	$NetBSD: db_machdep.c,v 1.17 1999/06/20 00:58:23 ragge Exp $	*/

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
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
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

#include <sys/types.h>
#include <sys/systm.h>

#include <ddb/db_var.h>

/*
 * db_rint: enters ddb(4) if the "ESC D" sequence is matched.
 * Returns:
 * - 0 if the character isn't part of the escape sequence
 * - 1 if the character is part of the escape sequence and should be skipped
 * - 2 if the character isn't the end of an escape sequence, and an ESC
 *   character should be inserted before it.
 */
int
db_rint(int c)
{
	static int ddbescape = 0;

	if (ddbescape && ((c & 0x7f) == 'D')) {
		if (db_console)
			db_enter();
		ddbescape = 0;
		return 1;
	}

	if ((ddbescape == 0) && ((c & 0x7f) == 27)) {
		ddbescape = 1;
		return 1;
	}

	if (ddbescape) {
		ddbescape = 0;
		return 2;
	}

	ddbescape = 0;
	return 0;
}
