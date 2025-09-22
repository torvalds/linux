/*	$OpenBSD: db_variables.h,v 1.8 2016/01/25 14:30:30 mpi Exp $	*/
/*	$NetBSD: db_variables.h,v 1.5 1996/02/05 01:57:21 christos Exp $	*/

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
 *	Date:	7/90
 */

#ifndef	_DB_VARIABLES_H_
#define	_DB_VARIABLES_H_

/*
 * Debugger variables.
 */
struct db_variable {
	char	*name;		/* Name of variable */
	long	*valuep;	/* value of variable */
				/* function to call when reading/writing */
	int	(*fcn)(struct db_variable *, db_expr_t *, int);
#define DB_VAR_GET	0
#define DB_VAR_SET	1
};
#define	FCN_NULL ((int (*)(struct db_variable *, db_expr_t *, int))0)

extern struct db_variable	db_vars[];	/* debugger variables */
extern struct db_variable	*db_evars;
extern struct db_variable	db_regs[];	/* machine registers */
extern struct db_variable	*db_eregs;

int	db_find_variable(struct db_variable **);
int	db_get_variable(db_expr_t *);
int	db_set_variable(db_expr_t);
void	db_read_variable(struct db_variable *, db_expr_t *);
void	db_write_variable(struct db_variable *, db_expr_t *);
void	db_set_cmd(db_expr_t, int, db_expr_t, char *);
int	db_var_rw_int(struct db_variable *, db_expr_t *, int);

#endif	/* _DB_VARIABLES_H_ */
