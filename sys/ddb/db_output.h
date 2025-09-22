/*	$OpenBSD: db_output.h,v 1.17 2021/02/09 14:37:13 jcs Exp $ */
/*	$NetBSD: db_output.h,v 1.9 1996/04/04 05:13:50 cgd Exp $	*/

/* 
 * Mach Operating System
 * Copyright (c) 1993,1992,1991,1990 Carnegie Mellon University
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
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 *
 * 	Author: David B. Golub, Carnegie Mellon University
 *	Date:	8/90
 */

/*
 * Printing routines for kernel debugger.
 */
void db_force_whitespace(void);
void db_putchar(int);
int db_print_position(void);
int db_printf(const char *, ...)
    __attribute__((__format__(__kprintf__,1,2)));
int db_vprintf(const char *, va_list)
    __attribute__((__format__(__kprintf__,1,0)));
void db_end_line(int);
void db_resize(int, int);

/*
 * This is a replacement for the non-standard %z, %n and %r printf formats
 * in db_printf.
 *
 * db_format(buf, bufsize, val, format, alt, width)
 *
 * val is the value we want printed.
 * format is one of DB_FORMAT_[ZRN]
 * alt specifies if we should provide an "alternate" format (# in the printf
 *   format).
 * width is the field width. 0 is the same as no width specifier.
 */
#define DB_FORMAT_Z	1
#define DB_FORMAT_R	2
#define DB_FORMAT_N	3
#define DB_FORMAT_BUF_SIZE	64	/* should be plenty for all formats */
char *db_format(char *, size_t, long, int, int, int);

/* XXX - this is the wrong place, but we have no better. */
void db_stack_dump(void);
