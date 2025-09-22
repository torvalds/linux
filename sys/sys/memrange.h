/* $OpenBSD: memrange.h,v 1.10 2015/08/18 20:19:32 miod Exp $ */
/*-
 * Copyright (c) 1999 Michael Smith <msmith@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Memory range attribute operations, performed on /dev/mem
 */

/* Memory range attributes */
#define MDF_UNCACHEABLE		(1<<0)	/* region not cached */
#define MDF_WRITECOMBINE	(1<<1)	/* region supports "write combine" action */
#define MDF_WRITETHROUGH	(1<<2)	/* write-through cached */
#define MDF_WRITEBACK		(1<<3)	/* write-back cached */
#define MDF_WRITEPROTECT	(1<<4)	/* read-only region */
#define MDF_UNKNOWN		(1<<5)	/* any state we don't understand */
#define MDF_ATTRMASK		(0x00ffffff)

#define MDF_FIXBASE		(1<<24)	/* fixed base */
#define MDF_FIXLEN		(1<<25)	/* fixed length */
#define MDF_FIRMWARE		(1<<26)	/* set by firmware (XXX not useful?) */
#define MDF_ACTIVE		(1<<27)	/* currently active */
#define MDF_BOGUS		(1<<28)	/* we don't like it */
#define MDF_FIXACTIVE		(1<<29)	/* can't be turned off */
#define MDF_FORCE		(1<<31)	/* force risky changes */

struct mem_range_desc {
	u_int64_t	mr_base;
	u_int64_t	mr_len;
	int		mr_flags;
	char		mr_owner[8];
};

struct mem_range_op {
	struct mem_range_desc	*mo_desc;
	int			mo_arg[2];
#define MEMRANGE_SET_UPDATE	0
#define MEMRANGE_SET_REMOVE	1
	/* XXX want a flag that says "set and undo when I exit" */
};

#define MEMRANGE_GET	_IOWR('m', 50, struct mem_range_op)
#define MEMRANGE_SET	_IOW('m', 51, struct mem_range_op)

/* Offset indicating a write combining mapping is requested.  */
#define MEMRANGE_WC_RANGE	0x4000000000000000ULL

#ifdef _KERNEL

struct mem_range_softc;
struct mem_range_ops {
	void	(*init)(struct mem_range_softc *sc);
	int	(*set)(struct mem_range_softc *sc,
		    struct mem_range_desc *mrd, int *arg);
	void	(*initAP)(struct mem_range_softc *sc);
	void	(*reload)(struct mem_range_softc *sc);
};

struct mem_range_softc {
	struct mem_range_ops	*mr_op;
	int			mr_cap;
	int			mr_ndesc;
	struct mem_range_desc 	*mr_desc;
};

extern struct mem_range_softc mem_range_softc;

__BEGIN_DECLS
extern void mem_range_attach(void);
extern int mem_range_attr_get(struct mem_range_desc *mrd, int *arg);
extern int mem_range_attr_set(struct mem_range_desc *mrd, int *arg);
extern void mem_range_AP_init(void);
extern void mem_range_reload(void);
__END_DECLS
#endif /* _KERNEL */

