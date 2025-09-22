/*	$OpenBSD: db_interface.h,v 1.28 2024/09/05 08:52:27 bluhm Exp $	*/
/*	$NetBSD: db_interface.h,v 1.1 1996/02/05 01:57:03 christos Exp $	*/

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
#ifndef _DDB_DB_INTERFACE_H_
#define _DDB_DB_INTERFACE_H_

/* arch/<arch>/<arch>/db_trace.c */
void db_stack_trace_print(db_expr_t, int, db_expr_t, char *,
    int (*)(const char *, ...));

/* arch/<arch>/<arch>/db_disasm.c */
vaddr_t db_disasm(vaddr_t, int);

/* kern/kern_proc.c */
void db_kill_cmd(db_expr_t, int, db_expr_t, char *);
void db_show_all_procs(db_expr_t, int, db_expr_t, char *);

/* kern/kern_clockintr.c */
void db_show_all_clockintr(db_expr_t, int, db_expr_t, char *);

/* kern/kern_timeout.c */
void db_show_callout(db_expr_t, int, db_expr_t, char *);

struct mount;

/* kern/vfs_subr.c */
void vfs_buf_print(void *, int, int (*)(const char *, ...));
void vfs_vnode_print(void *, int, int (*)(const char *, ...));
void vfs_mount_print(struct mount *, int, int (*)(const char *, ...));

/* kern/subr_pool.c */
void db_show_all_pools(db_expr_t, int, db_expr_t, char *);

/* kern/uipc_mbuf.c */
void m_print(void *, int (*)(const char *, ...));
void m_print_chain(void *, int, int (*)(const char *, ...));
void m_print_packet(void *, int, int (*)(const char *, ...));

/* kern/uipc_socket.c */
void so_print(void *, int (*)(const char *, ...));

struct rtentry;
int db_show_rtentry(struct rtentry *, void *, unsigned int);
int db_show_rtable(int, unsigned int);

/* nfs/nfs_debug.c */
void db_show_all_nfsreqs(db_expr_t, int, db_expr_t, char *);
void nfs_request_print(void *, int, int (*)(const char *, ...));
void db_show_all_nfsnodes(db_expr_t, int, db_expr_t, char *);
void nfs_node_print(void *, int, int (*)(const char *, ...));

/* uvm/uvm_swap.c */
void swap_print_all(int (*)(const char *, ...));

/* arch/<arch>/<arch>/db_interface.c */
void db_machine_init(void);

#endif /* _DDB_DB_INTERFACE_H_ */
