/*	$OpenBSD: db_watch.c,v 1.19 2023/03/08 04:43:07 guenther Exp $ */
/*	$NetBSD: db_watch.c,v 1.9 1996/03/30 22:30:12 christos Exp $	*/

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
 * 	Author: Richard P. Draves, Carnegie Mellon University
 *	Date:	10/90
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/db_machdep.h>

#include <ddb/db_break.h>
#include <ddb/db_watch.h>
#include <ddb/db_lex.h>
#include <ddb/db_run.h>
#include <ddb/db_sym.h>
#include <ddb/db_output.h>
#include <ddb/db_command.h>
#include <ddb/db_extern.h>

/*
 * Watchpoints.
 */

int db_watchpoints_inserted = 1;

#define	NWATCHPOINTS	100
struct db_watchpoint	db_watch_table[NWATCHPOINTS];
db_watchpoint_t		db_next_free_watchpoint = &db_watch_table[0];
db_watchpoint_t		db_free_watchpoints = 0;
db_watchpoint_t		db_watchpoint_list = 0;

db_watchpoint_t
db_watchpoint_alloc(void)
{
	db_watchpoint_t	watch;

	if ((watch = db_free_watchpoints) != 0) {
		db_free_watchpoints = watch->link;
		return (watch);
	}
	if (db_next_free_watchpoint == &db_watch_table[NWATCHPOINTS]) {
		db_printf("All watchpoints used.\n");
		return (0);
	}
	watch = db_next_free_watchpoint;
	db_next_free_watchpoint++;

	return (watch);
}

void
db_watchpoint_free(db_watchpoint_t watch)
{
	watch->link = db_free_watchpoints;
	db_free_watchpoints = watch;
}

void
db_set_watchpoint(vaddr_t addr, vsize_t size)
{
	db_watchpoint_t	watch;

	/*
	 *	Should we do anything fancy with overlapping regions?
	 */

	for (watch = db_watchpoint_list; watch != 0; watch = watch->link)
		if (watch->loaddr == addr && watch->hiaddr == addr + size) {
			db_printf("Already set.\n");
			return;
		}

	watch = db_watchpoint_alloc();
	if (watch == 0) {
		db_printf("Too many watchpoints.\n");
		return;
	}

	watch->loaddr = addr;
	watch->hiaddr = addr+size;

	watch->link = db_watchpoint_list;
	db_watchpoint_list = watch;

	db_watchpoints_inserted = 0;
}

void
db_delete_watchpoint(vaddr_t addr)
{
	db_watchpoint_t	watch;
	db_watchpoint_t	*prev;

	for (prev = &db_watchpoint_list; (watch = *prev) != 0;
	    prev = &watch->link)
		if (watch->loaddr <= addr && addr < watch->hiaddr) {
			*prev = watch->link;
			db_watchpoint_free(watch);
			return;
		}

	db_printf("Not set.\n");
}

void
db_list_watchpoints(void)
{
	db_watchpoint_t	watch;

	if (db_watchpoint_list == 0) {
		db_printf("No watchpoints set\n");
		return;
	}

	db_printf("  Address  Size\n");
	for (watch = db_watchpoint_list; watch != 0; watch = watch->link)
		db_printf("%8lx  %lx\n",
		    watch->loaddr, watch->hiaddr - watch->loaddr);
}

/* Delete watchpoint */
void
db_deletewatch_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	db_delete_watchpoint(addr);
}

/* Set watchpoint */
void
db_watchpoint_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	vsize_t		size;
	db_expr_t	value;

	if (db_expression(&value))
		size = (vsize_t) value;
	else
		size = 4;
	db_skip_to_eol();

	db_set_watchpoint(addr, size);
}

/* list watchpoints */
void
db_listwatch_cmd(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	db_list_watchpoints();
}

void
db_set_watchpoints(void)
{
	db_watchpoint_t	watch;

	if (!db_watchpoints_inserted && db_watchpoint_list != NULL) {
		for (watch = db_watchpoint_list; watch != 0;
		    watch = watch->link)
			pmap_protect(pmap_kernel(), trunc_page(watch->loaddr),
			    round_page(watch->hiaddr), PROT_READ);
		pmap_update(pmap_kernel());
		db_watchpoints_inserted = 1;
	}
}

void
db_clear_watchpoints(void)
{
	db_watchpoints_inserted = 0;
}
