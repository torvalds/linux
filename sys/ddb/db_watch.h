/*	$OpenBSD: db_watch.h,v 1.11 2019/11/07 13:16:25 mpi Exp $	*/
/*	$NetBSD: db_watch.h,v 1.9 1996/02/05 01:57:24 christos Exp $	*/

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
 *	Date:	10/90
 */

#ifndef	_DDB_DB_WATCH_
#define	_DDB_DB_WATCH_

/*
 * Watchpoint.
 */
typedef struct db_watchpoint {
	vaddr_t loaddr;			/* from this address */
	vaddr_t hiaddr;			/* to this address */
	struct db_watchpoint *link;	/* link in in-use or free chain */
} *db_watchpoint_t;

db_watchpoint_t db_watchpoint_alloc(void);
void db_watchpoint_free(db_watchpoint_t);
void db_set_watchpoint(vaddr_t, vsize_t);
void db_delete_watchpoint(vaddr_t);
void db_list_watchpoints(void);
void db_deletewatch_cmd(db_expr_t, int, db_expr_t, char *);
void db_watchpoint_cmd(db_expr_t, int, db_expr_t, char *);
void db_listwatch_cmd(db_expr_t, int, db_expr_t, char *);
void db_set_watchpoints(void);
void db_clear_watchpoints(void);

#endif	/* _DDB_DB_WATCH_ */
