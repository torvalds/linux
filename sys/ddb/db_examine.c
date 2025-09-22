/*	$OpenBSD: db_examine.c,v 1.28 2023/03/08 04:43:07 guenther Exp $	*/
/*	$NetBSD: db_examine.c,v 1.11 1996/03/30 22:30:07 christos Exp $	*/

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
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	7/90
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>		/* type definitions */

#include <ddb/db_lex.h>
#include <ddb/db_output.h>
#include <ddb/db_command.h>
#include <ddb/db_sym.h>
#include <ddb/db_access.h>
#include <ddb/db_extern.h>
#include <ddb/db_interface.h>

char	db_examine_format[TOK_STRING_SIZE] = "x";

void db_examine(vaddr_t, char *, int);
void db_search(vaddr_t, int, db_expr_t, db_expr_t, db_expr_t);

/*
 * Examine (print) data.  Syntax is:
 *		x/[bhlq][cdiorsuxz]*
 * For example, the command:
 *  	x/bxxxx
 * should print:
 *  	address:  01  23  45  67
 */
void
db_examine_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	if (modif[0] != '\0')
		db_strlcpy(db_examine_format, modif, sizeof(db_examine_format));

	if (count == -1)
		count = 1;

	db_examine((vaddr_t)addr, db_examine_format, count);
}

void
db_examine(vaddr_t addr, char *fmt, int count)
{
	int		i, c;
	db_expr_t	value;
	int		size;
	int		width;
	int		bytes;
	char *		fp;
	vaddr_t		incr;
	int		dis;
	char		tmpfmt[28];

	while (--count >= 0) {
		fp = fmt;

		/* defaults */
		size = 4;
		width = 12;
		incr = 0;
		dis = 0;

		while ((c = *fp++) != 0) {
			if (db_print_position() == 0) {
				/* Always print the address. */
				db_printsym(addr, DB_STGY_ANY, db_printf);
				db_printf(":\t");
				db_prev = addr;
			}
			incr = size;
			switch (c) {
			case 'b':	/* byte */
				size = 1;
				width = 4;
				break;
			case 'h':	/* half-word */
				size = 2;
				width = 8;
				break;
			case 'l':	/* long-word */
				size = 4;
				width = 12;
				break;
#ifdef __LP64__
			case 'q':	/* quad-word */
				size = 8;
				width = 20;
				break;
#endif
			case 'a':	/* address */
				db_printf("= 0x%lx\n", (long)addr);
				incr = 0;
				break;
			case 'r':	/* signed, current radix */
				value = db_get_value(addr, size, 1);
				db_format(tmpfmt, sizeof tmpfmt,
				    (long)value, DB_FORMAT_R, 0, width);
				db_printf("%-*s", width, tmpfmt);
				break;
			case 'x':	/* unsigned hex */
				value = db_get_value(addr, size, 0);
				db_printf("%*lx", width, (long)value);
				break;
			case 'm':	/* hex dump */
				/*
				 * Print off in chunks of size. Try to print 16
				 * bytes at a time into 4 columns. This
				 * loops modify's count extra times in order
				 * to get the nicely formatted lines.
				 */
				incr = 0;
				bytes = 0;
				do {
					for (i = 0; i < size; i++) {
						value =
						    db_get_value(addr+bytes, 1,
							0);
						db_printf("%02lx",
						    (long)value);
						bytes++;
						if (!(bytes % 4))
							db_printf(" ");
					}
				} while ((bytes != 16) && count--);
				/* True up the columns before continuing */
				db_printf("%-*s",
			            (16-bytes)*2 + (4 - bytes/4) + 1, " ");
				/* Print chars, use . for non-printables */
				while (bytes--) {
					value = db_get_value(addr + incr, 1, 0);
					incr++;
					if (value >= ' ' && value <= '~')
						db_printf("%c", (int)value);
					else
						db_printf(".");
				}
				db_printf("\n");
				break;
			case 'z':	/* signed hex */
				value = db_get_value(addr, size, 1);
				db_format(tmpfmt, sizeof tmpfmt,
				    (long)value, DB_FORMAT_Z, 0, width);
				db_printf("%-*s", width, tmpfmt);
				break;
			case 'd':	/* signed decimal */
				value = db_get_value(addr, size, 1);
				db_printf("%-*ld", width, (long)value);
				break;
			case 'u':	/* unsigned decimal */
				value = db_get_value(addr, size, 0);
				db_printf("%-*lu", width, (long)value);
				break;
			case 'o':	/* unsigned octal */
				value = db_get_value(addr, size, 0);
				db_printf("%-*lo", width, value);
				break;
			case 'c':	/* character */
				value = db_get_value(addr, 1, 0);
				incr = 1;
				if (value >= ' ' && value <= '~')
					db_printf("%c", (int)value);
				else
					db_printf("\\%03o", (int)value);
				break;
			case 's':	/* null-terminated string */
				incr = 0;
				for (;;) {
					value = db_get_value(addr + incr, 1,
					    0);
					incr++;
					if (value == 0)
						break;
					if (value >= ' ' && value <= '~')
						db_printf("%c", (int)value);
					else
						db_printf("\\%03o", (int)value);
				}
				break;
			case 'i':	/* instruction */
			case 'I':	/* instruction, alternate form */
				dis = c;
				break;
			default:
				incr = 0;
				break;
			}
		}
		/* if we had a disassembly modifier, do it last */
		switch (dis) {
		case 'i':	/* instruction */
			addr = db_disasm(addr, 0);
			break;
		case 'I':	/* instruction, alternate form */
			addr = db_disasm(addr, 1);
			break;
		default:
			addr += incr;
			break;
		}
		if (db_print_position() != 0)
			db_printf("\n");
	}
	db_next = addr;
}

/*
 * Print value.
 */
char	db_print_format = 'x';

void
db_print_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	db_expr_t	value;
	char		tmpfmt[28];

	if (modif[0] != '\0')
		db_print_format = modif[0];

	switch (db_print_format) {
	case 'a':
		db_printsym((vaddr_t)addr, DB_STGY_ANY, db_printf);
		break;
	case 'r':
		db_printf("%s", db_format(tmpfmt, sizeof tmpfmt, addr,
		    DB_FORMAT_R, 0, sizeof(db_expr_t) * 2 * 6 / 5));
		break;
	case 'x':
		db_printf("%*lx", (uint)sizeof(db_expr_t) * 2, addr);
		break;
	case 'z':
		db_printf("%s", db_format(tmpfmt, sizeof tmpfmt, addr,
		    DB_FORMAT_Z, 0, sizeof(db_expr_t) * 2));
		break;
	case 'd':
		db_printf("%*ld", (uint)sizeof(db_expr_t) * 2 * 6 / 5, addr);
		break;
	case 'u':
		db_printf("%*lu", (uint)sizeof(db_expr_t) * 2 * 6 / 5, addr);
		break;
	case 'o':
		db_printf("%*lo", (uint)sizeof(db_expr_t) * 2 * 4 / 3, addr);
		break;
	case 'c':
		value = addr & 0xFF;
		if (value >= ' ' && value <= '~')
			db_printf("%c", (int)value);
		else
			db_printf("\\%03o", (int)value);
		break;
	}
	db_printf("\n");
}

void
db_print_loc_and_inst(vaddr_t loc)
{
	db_printsym(loc, DB_STGY_PROC, db_printf);
	if (loc != 0) {
		db_printf(":\t");
		db_disasm(loc, 0);
	}
}

/* local copy is needed here so that we can trace strlcpy() in libkern */
size_t
db_strlcpy(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0 && --n != 0) {
		do {
			if ((*d++ = *s++) == 0)
				break;
		} while (--n != 0);
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			continue;
	}

	return(s - src - 1);	/* count does not include NUL */
}

/*
 * Search for a value in memory.
 * Syntax: search [/bhl] addr value [mask] [,count]
 */
void
db_search_cmd(db_expr_t daddr, int have_addr, db_expr_t dcount, char *modif)
{
	int		t;
	vaddr_t		addr;
	int		size;
	db_expr_t	value;
	db_expr_t	mask;
	db_expr_t	count;

	t = db_read_token();
	if (t == tSLASH) {
		t = db_read_token();
		if (t != tIDENT) {
			bad_modifier:
			db_printf("Bad modifier\n");
			db_flush_lex();
			return;
		}

		if (!strcmp(db_tok_string, "b"))
			size = 1;
		else if (!strcmp(db_tok_string, "h"))
			size = 2;
		else if (!strcmp(db_tok_string, "l"))
			size = 4;
		else
			goto bad_modifier;
	} else {
		db_unread_token(t);
		size = 4;
	}

	if (!db_expression(&value)) {
		db_printf("Address missing\n");
		db_flush_lex();
		return;
	}
	addr = (vaddr_t) value;

	if (!db_expression(&value)) {
		db_printf("Value missing\n");
		db_flush_lex();
		return;
	}

	if (!db_expression(&mask))
		mask = (int) ~0;

	t = db_read_token();
	if (t == tCOMMA) {
		if (!db_expression(&count)) {
			db_printf("Count missing\n");
			db_flush_lex();
			return;
		}
	} else {
		db_unread_token(t);
		count = -1;		/* forever */
	}
	db_skip_to_eol();

	db_search(addr, size, value, mask, count);
}

void
db_search(vaddr_t addr, int size, db_expr_t value, db_expr_t mask,
    db_expr_t count)
{
	/* Negative counts means forever.  */
	while (count < 0 || count-- != 0) {
		db_prev = addr;
		if ((db_get_value(addr, size, 0) & mask) == value)
			break;
		addr += size;
	}
	db_next = addr;
}
