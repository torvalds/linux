/*	$OpenBSD: nfs_debug.c,v 1.7 2024/05/01 13:15:59 jsg Exp $ */
/*
 * Copyright (c) 2009 Thordur I. Bjornsson. <thib@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/pool.h>
#include <sys/vnode.h>

#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsnode.h>

#include <machine/db_machdep.h>
#include <ddb/db_interface.h>
#include <ddb/db_output.h>

void
db_show_all_nfsreqs(db_expr_t expr, int haddr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	pool_walk(&nfsreqpl, full, db_printf, nfs_request_print);
}

void
nfs_request_print(void *v, int full, int (*pr)(const char *, ...))
{
	struct nfsreq	*rep = v;

	(*pr)("xid 0x%x flags 0x%x rexmit %i procnum %i proc %p\n",
	    rep->r_xid, rep->r_flags, rep->r_rexmit, rep->r_procnum,
	    rep->r_procp);

	if (full) {
		(*pr)("mreq %p mrep %p md %p nfsmount %p vnode %p timer %i",
		    " rtt %i\n",
		    rep->r_mreq, rep->r_mrep, rep->r_md, rep->r_nmp,
		    rep->r_vp, rep->r_timer, rep->r_rtt);
	}
}

void
db_show_all_nfsnodes(db_expr_t expr, int haddr, db_expr_t count, char *modif)
{
	int full = 0;

	if (modif[0] == 'f')
		full = 1;

	pool_walk(&nfs_node_pool, full, db_printf, nfs_node_print);
}



void
nfs_node_print(void *v, int full, int (*pr)(const char *, ...))
{
	struct nfsnode	*np = v;

	(*pr)("size %llu flag %i vnode %p accstamp %lld\n",
	    np->n_size, np->n_flag, np->n_vnode, (long long)np->n_accstamp);

	if (full) {
		(*pr)("pushedlo %llu pushedhi %llu pushlo %llu pushhi %llu\n",
		    np->n_pushedlo, np->n_pushedhi, np->n_pushlo,
		    np->n_pushhi);
		(*pr)("commitflags %i\n", np->n_commitflags);
	}
}
