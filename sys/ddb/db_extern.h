/*	$OpenBSD: db_extern.h,v 1.22 2024/05/13 01:15:50 jsg Exp $	*/
/*	$NetBSD: db_extern.h,v 1.1 1996/02/05 01:57:00 christos Exp $	*/

/*
 * Copyright (c) 1995 Christos Zoulas.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Christos Zoulas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _DDB_DB_EXTERN_H_
#define _DDB_DB_EXTERN_H_

/* db_sym.c */
void ddb_init(void);

/* db_examine.c */
void db_examine_cmd(db_expr_t, int, db_expr_t, char *);
void db_print_cmd(db_expr_t, int, db_expr_t, char *);
void db_search_cmd(db_expr_t, int, db_expr_t, char *);
void db_print_loc_and_inst(vaddr_t);
size_t db_strlcpy(char *, const char *, size_t);

/* db_expr.c */
int db_expression(db_expr_t *);

/* db_hangman.c */
void db_hangman(db_expr_t, int, db_expr_t, char *);

/* db_input.c */
int db_readline(char *, int);

/* db_trap.c */
void db_trap(int, int);

/* db_prof.c */
int db_prof_enable(void);
void db_prof_disable(void);

/* db_ctf.c */
void	db_ctf_init(void);

#endif /* _DDB_DB_EXTERN_H_ */
