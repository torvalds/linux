/*	$OpenBSD: db_break.c,v 1.24 2024/11/05 10:19:11 miod Exp $	*/
/*	$NetBSD: db_break.c,v 1.7 1996/03/30 22:30:03 christos Exp $	*/

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

/*
 * Breakpoints.
 */
#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>		/* type definitions */

#include <ddb/db_access.h>
#include <ddb/db_sym.h>
#include <ddb/db_break.h>
#include <ddb/db_output.h>

#define	NBREAKPOINTS	100
struct db_breakpoint	db_break_table[NBREAKPOINTS];
db_breakpoint_t		db_next_free_breakpoint = &db_break_table[0];
db_breakpoint_t		db_free_breakpoints = 0;
db_breakpoint_t		db_breakpoint_list = 0;

db_breakpoint_t db_breakpoint_alloc(void);
void db_breakpoint_free(db_breakpoint_t);
void db_set_breakpoint(vaddr_t, int);
void db_delete_breakpoint(vaddr_t);
void db_list_breakpoints(void);

db_breakpoint_t
db_breakpoint_alloc(void)
{
	db_breakpoint_t	bkpt;

	if ((bkpt = db_free_breakpoints) != 0) {
		db_free_breakpoints = bkpt->link;
		return (bkpt);
	}
	if (db_next_free_breakpoint == &db_break_table[NBREAKPOINTS]) {
		db_printf("All breakpoints used.\n");
		return (0);
	}
	bkpt = db_next_free_breakpoint;
	db_next_free_breakpoint++;

	return (bkpt);
}

void
db_breakpoint_free(db_breakpoint_t bkpt)
{
	bkpt->link = db_free_breakpoints;
	db_free_breakpoints = bkpt;
}

void
db_set_breakpoint(vaddr_t addr, int count)
{
	db_breakpoint_t	bkpt;

	if (db_find_breakpoint(addr)) {
		db_printf("Already set.\n");
		return;
	}

#ifdef DB_VALID_BREAKPOINT
	if (!DB_VALID_BREAKPOINT(addr)) {
		db_printf("Not a valid address for a breakpoint.\n");
		return;
	}
#endif

	bkpt = db_breakpoint_alloc();
	if (bkpt == 0) {
		db_printf("Too many breakpoints.\n");
		return;
	}

	bkpt->address = addr;
	bkpt->flags = 0;
	bkpt->init_count = count;
	bkpt->count = count;

	bkpt->link = db_breakpoint_list;
	db_breakpoint_list = bkpt;
}

void
db_delete_breakpoint(vaddr_t addr)
{
	db_breakpoint_t	bkpt;
	db_breakpoint_t	*prev;

	for (prev = &db_breakpoint_list; (bkpt = *prev) != 0;
	    prev = &bkpt->link) {
		if (bkpt->address == addr) {
			*prev = bkpt->link;
			break;
		}
	}
	if (bkpt == 0) {
		db_printf("Not set.\n");
		return;
	}

	db_breakpoint_free(bkpt);
}

db_breakpoint_t
db_find_breakpoint(vaddr_t addr)
{
	db_breakpoint_t	bkpt;

	for (bkpt = db_breakpoint_list; bkpt != 0; bkpt = bkpt->link)
		if (bkpt->address == addr)
			return (bkpt);

	return (0);
}

int db_breakpoints_inserted = 1;

void
db_set_breakpoints(void)
{
	db_breakpoint_t	bkpt;

	if (!db_breakpoints_inserted) {
		for (bkpt = db_breakpoint_list; bkpt != 0; bkpt = bkpt->link) {
			bkpt->bkpt_inst =
			    db_get_value(bkpt->address, BKPT_SIZE, 0);
			db_put_value(bkpt->address, BKPT_SIZE,
			    BKPT_SET(bkpt->bkpt_inst));
		}
		db_breakpoints_inserted = 1;
	}
}

void
db_clear_breakpoints(void)
{
	db_breakpoint_t	bkpt;

	if (db_breakpoints_inserted) {
		for (bkpt = db_breakpoint_list; bkpt != 0; bkpt = bkpt->link)
			db_put_value(bkpt->address, BKPT_SIZE, bkpt->bkpt_inst);
		db_breakpoints_inserted = 0;
	}
}

/*
 * Set a temporary breakpoint.
 * The instruction is changed immediately,
 * so the breakpoint does not have to be on the breakpoint list.
 */
db_breakpoint_t
db_set_temp_breakpoint(vaddr_t addr)
{
	db_breakpoint_t	bkpt;

#ifdef DB_VALID_BREAKPOINT
	if (!DB_VALID_BREAKPOINT(addr)) {
		db_printf("Not a valid address for a breakpoint.\n");
		return (0);
	}
#endif

	bkpt = db_breakpoint_alloc();
	if (bkpt == 0) {
		db_printf("Too many breakpoints.\n");
		return (0);
	}

	bkpt->address = addr;
	bkpt->flags = BKPT_TEMP;
	bkpt->init_count = 1;
	bkpt->count = 1;

	bkpt->bkpt_inst = db_get_value(bkpt->address, BKPT_SIZE, 0);
	db_put_value(bkpt->address, BKPT_SIZE, BKPT_SET(bkpt->bkpt_inst));
	return bkpt;
}

void
db_delete_temp_breakpoint(db_breakpoint_t bkpt)
{
	db_put_value(bkpt->address, BKPT_SIZE, bkpt->bkpt_inst);
	db_breakpoint_free(bkpt);
}

/*
 * List breakpoints.
 */
void
db_list_breakpoints(void)
{
	db_breakpoint_t	bkpt;

	if (db_breakpoint_list == NULL) {
		db_printf("No breakpoints set\n");
		return;
	}

	db_printf(" Count    Address\n");
	for (bkpt = db_breakpoint_list; bkpt != 0; bkpt = bkpt->link) {
		db_printf(" %5d    ", bkpt->init_count);
		db_printsym(bkpt->address, DB_STGY_PROC, db_printf);
		db_printf("\n");
	}
}

/* Delete breakpoint */
void
db_delete_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	db_delete_breakpoint((vaddr_t)addr);
}

/* Set breakpoint with skip count */
void
db_breakpoint_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	if (count < 1)
		count = 1;

	db_set_breakpoint((vaddr_t)addr, count);
}

/* list breakpoints */
void
db_listbreak_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	db_list_breakpoints();
}
