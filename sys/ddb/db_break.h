/*	$OpenBSD: db_break.h,v 1.12 2019/11/07 13:16:25 mpi Exp $	*/
/*	$NetBSD: db_break.h,v 1.8 1996/02/05 01:56:52 christos Exp $	*/

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

#ifndef	_DDB_DB_BREAK_H_
#define	_DDB_DB_BREAK_H_

#include <uvm/uvm_extern.h>

/*
 * Breakpoints.
 */
typedef struct db_breakpoint {
	vaddr_t address;		/* set here */
	int	init_count;		/* number of times to skip bkpt */
	int	count;			/* current count */
	int	flags;			/* flags: */
#define	BKPT_SINGLE_STEP	0x2	    /* to simulate single step */
#define	BKPT_TEMP		0x4	    /* temporary */
	int	bkpt_inst;		/* saved instruction at bkpt */
	struct db_breakpoint *link;	/* link in in-use or free chain */
} *db_breakpoint_t;

db_breakpoint_t db_find_breakpoint(vaddr_t);
void db_set_breakpoints(void);
void db_clear_breakpoints(void);
db_breakpoint_t db_set_temp_breakpoint(vaddr_t);
void db_delete_temp_breakpoint(db_breakpoint_t);
void db_delete_cmd(db_expr_t, int, db_expr_t, char *);
void db_breakpoint_cmd(db_expr_t, int, db_expr_t, char *);
void db_listbreak_cmd(db_expr_t, int, db_expr_t, char *);

#endif	/* _DDB_DB_BREAK_H_ */
