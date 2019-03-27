/*	$FreeBSD$ */
/*	$NetBSD: pfil.h,v 1.22 2003/06/23 12:57:08 martin Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2019 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 1996 Matthew R. Green
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _NET_PFIL_H_
#define _NET_PFIL_H_

#include <sys/ioccom.h>

enum pfil_types {
	PFIL_TYPE_IP4,
	PFIL_TYPE_IP6,
	PFIL_TYPE_ETHERNET,
};

#define	MAXPFILNAME	64

struct pfilioc_head {
	char		pio_name[MAXPFILNAME];
	int		pio_nhooksin;
	int		pio_nhooksout;
	enum pfil_types	pio_type;
};

struct pfilioc_hook {
	char		pio_module[MAXPFILNAME];
	char		pio_ruleset[MAXPFILNAME];
	int		pio_flags;
	enum pfil_types pio_type;
};

struct pfilioc_list {
	u_int			 pio_nheads;
	u_int			 pio_nhooks;
	struct pfilioc_head	*pio_heads;
	struct pfilioc_hook	*pio_hooks;
};

struct pfilioc_link {
	char		pio_name[MAXPFILNAME];
	char		pio_module[MAXPFILNAME];
	char		pio_ruleset[MAXPFILNAME];
	int		pio_flags;
};

#define	PFILDEV			"pfil"
#define	PFILIOC_LISTHEADS	_IOWR('P', 1, struct pfilioc_list)
#define	PFILIOC_LISTHOOKS	_IOWR('P', 2, struct pfilioc_list)
#define	PFILIOC_LINK		_IOW('P', 3, struct pfilioc_link)

#define	PFIL_IN		0x00010000
#define	PFIL_OUT	0x00020000
#define	PFIL_FWD	0x00040000
#define	PFIL_DIR(f)	((f) & (PFIL_IN|PFIL_OUT))
#define	PFIL_MEMPTR	0x00080000
#define	PFIL_HEADPTR	0x00100000
#define	PFIL_HOOKPTR	0x00200000
#define	PFIL_APPEND	0x00400000
#define	PFIL_UNLINK	0x00800000
#define	PFIL_LENMASK	0x0000ffff
#define	PFIL_LENGTH(f)	((f) & PFIL_LENMASK)

#ifdef _KERNEL
struct mbuf;
struct ifnet;
struct inpcb;

typedef union {
	struct mbuf	**m;
	void		*mem;
	uintptr_t	__ui;
} pfil_packet_t __attribute__((__transparent_union__));

static inline pfil_packet_t
pfil_packet_align(pfil_packet_t p)
{

	return ((pfil_packet_t ) (((uintptr_t)(p).mem +
	    (_Alignof(void *) - 1)) & - _Alignof(void *)));
}

static inline struct mbuf *
pfil_mem2mbuf(void *v)
{

	return (*(struct mbuf **) (((uintptr_t)(v) +
	    (_Alignof(void *) - 1)) & - _Alignof(void *)));
}

typedef enum {
	PFIL_PASS = 0,
	PFIL_DROPPED,
	PFIL_CONSUMED,
	PFIL_REALLOCED,
} pfil_return_t;

typedef	pfil_return_t	(*pfil_func_t)(pfil_packet_t, struct ifnet *, int,
			    void *, struct inpcb *);
/*
 * A pfil head is created by a packet intercept point.
 *
 * A pfil hook is created by a packet filter.
 *
 * Hooks are chained on heads.  Historically some hooking happens
 * automatically, e.g. ipfw(4), pf(4) and ipfilter(4) would register
 * theirselves on IPv4 and IPv6 input/output.
 */

typedef struct pfil_hook *	pfil_hook_t;
typedef struct pfil_head *	pfil_head_t;

/*
 * Give us a chance to modify pfil_xxx_args structures in future.
 */
#define	PFIL_VERSION	1

/* Argument structure used by packet filters to register themselves. */
struct pfil_hook_args {
	int		 pa_version;
	int		 pa_flags;
	enum pfil_types	 pa_type;
	pfil_func_t	 pa_func;
	void		*pa_ruleset;
	const char	*pa_modname;
	const char	*pa_rulname;
};

/* Public functions for pfil hook management by packet filters. */
pfil_hook_t	pfil_add_hook(struct pfil_hook_args *);
void		pfil_remove_hook(pfil_hook_t);

/* Argument structure used by ioctl() and packet filters to set filters. */
struct pfil_link_args {
	int		pa_version;
	int		pa_flags;
	union {
		const char	*pa_headname;
		pfil_head_t	 pa_head;
	};
	union {
		struct {
			const char	*pa_modname;
			const char	*pa_rulname;
		};
		pfil_hook_t	 pa_hook;
	};
};

/* Public function to configure filter chains.  Used by ioctl() and filters. */
int	pfil_link(struct pfil_link_args *);

/* Argument structure used by inspection points to register themselves. */
struct pfil_head_args {
	int		 pa_version;
	int		 pa_flags;
	enum pfil_types	 pa_type;
	const char	*pa_headname;
};

/* Public functions for pfil head management by inspection points. */
pfil_head_t	pfil_head_register(struct pfil_head_args *);
void		pfil_head_unregister(pfil_head_t);

/* Public functions to run the packet inspection by inspection points. */
int	pfil_run_hooks(struct pfil_head *, pfil_packet_t, struct ifnet *, int,
    struct inpcb *inp);
/*
 * Minimally exposed structure to avoid function call in case of absence
 * of any filters by protocols and macros to do the check.
 */
struct _pfil_head {
	int	head_nhooksin;
	int	head_nhooksout;
};
#define	PFIL_HOOKED_IN(p) (((struct _pfil_head *)(p))->head_nhooksin > 0)
#define	PFIL_HOOKED_OUT(p) (((struct _pfil_head *)(p))->head_nhooksout > 0)

/*
 * Alloc mbuf to be used instead of memory pointer.
 */
int	pfil_realloc(pfil_packet_t *, int, struct ifnet *);

#endif /* _KERNEL */
#endif /* _NET_PFIL_H_ */
