/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Karels at Berkeley Software Design, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)sysctl.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD$
 */

#ifndef _SYS_SYSCTL_H_
#define	_SYS_SYSCTL_H_

#ifdef _KERNEL
#include <sys/queue.h>
#endif

struct thread;
/*
 * Definitions for sysctl call.  The sysctl call uses a hierarchical name
 * for objects that can be examined or modified.  The name is expressed as
 * a sequence of integers.  Like a file path name, the meaning of each
 * component depends on its place in the hierarchy.  The top-level and kern
 * identifiers are defined here, and other identifiers are defined in the
 * respective subsystem header files.
 */

#define	CTL_MAXNAME	24	/* largest number of components supported */

/*
 * Each subsystem defined by sysctl defines a list of variables
 * for that subsystem. Each name is either a node with further
 * levels defined below it, or it is a leaf of some particular
 * type given below. Each sysctl level defines a set of name/type
 * pairs to be used by sysctl(8) in manipulating the subsystem.
 */
struct ctlname {
	char	*ctl_name;	/* subsystem name */
	int	 ctl_type;	/* type of name */
};

#define	CTLTYPE		0xf	/* mask for the type */
#define	CTLTYPE_NODE	1	/* name is a node */
#define	CTLTYPE_INT	2	/* name describes an integer */
#define	CTLTYPE_STRING	3	/* name describes a string */
#define	CTLTYPE_S64	4	/* name describes a signed 64-bit number */
#define	CTLTYPE_OPAQUE	5	/* name describes a structure */
#define	CTLTYPE_STRUCT	CTLTYPE_OPAQUE	/* name describes a structure */
#define	CTLTYPE_UINT	6	/* name describes an unsigned integer */
#define	CTLTYPE_LONG	7	/* name describes a long */
#define	CTLTYPE_ULONG	8	/* name describes an unsigned long */
#define	CTLTYPE_U64	9	/* name describes an unsigned 64-bit number */
#define	CTLTYPE_U8	0xa	/* name describes an unsigned 8-bit number */
#define	CTLTYPE_U16	0xb	/* name describes an unsigned 16-bit number */
#define	CTLTYPE_S8	0xc	/* name describes a signed 8-bit number */
#define	CTLTYPE_S16	0xd	/* name describes a signed 16-bit number */
#define	CTLTYPE_S32	0xe	/* name describes a signed 32-bit number */
#define	CTLTYPE_U32	0xf	/* name describes an unsigned 32-bit number */

#define	CTLFLAG_RD	0x80000000	/* Allow reads of variable */
#define	CTLFLAG_WR	0x40000000	/* Allow writes to the variable */
#define	CTLFLAG_RW	(CTLFLAG_RD|CTLFLAG_WR)
#define	CTLFLAG_DORMANT	0x20000000	/* This sysctl is not active yet */
#define	CTLFLAG_ANYBODY	0x10000000	/* All users can set this var */
#define	CTLFLAG_SECURE	0x08000000	/* Permit set only if securelevel<=0 */
#define	CTLFLAG_PRISON	0x04000000	/* Prisoned roots can fiddle */
#define	CTLFLAG_DYN	0x02000000	/* Dynamic oid - can be freed */
#define	CTLFLAG_SKIP	0x01000000	/* Skip this sysctl when listing */
#define	CTLMASK_SECURE	0x00F00000	/* Secure level */
#define	CTLFLAG_TUN	0x00080000	/* Default value is loaded from getenv() */
#define	CTLFLAG_RDTUN	(CTLFLAG_RD|CTLFLAG_TUN)
#define	CTLFLAG_RWTUN	(CTLFLAG_RW|CTLFLAG_TUN)
#define	CTLFLAG_MPSAFE	0x00040000	/* Handler is MP safe */
#define	CTLFLAG_VNET	0x00020000	/* Prisons with vnet can fiddle */
#define	CTLFLAG_DYING	0x00010000	/* Oid is being removed */
#define	CTLFLAG_CAPRD	0x00008000	/* Can be read in capability mode */
#define	CTLFLAG_CAPWR	0x00004000	/* Can be written in capability mode */
#define	CTLFLAG_STATS	0x00002000	/* Statistics, not a tuneable */
#define	CTLFLAG_NOFETCH	0x00001000	/* Don't fetch tunable from getenv() */
#define	CTLFLAG_CAPRW	(CTLFLAG_CAPRD|CTLFLAG_CAPWR)

/*
 * Secure level.   Note that CTLFLAG_SECURE == CTLFLAG_SECURE1.
 *
 * Secure when the securelevel is raised to at least N.
 */
#define	CTLSHIFT_SECURE	20
#define	CTLFLAG_SECURE1	(CTLFLAG_SECURE | (0 << CTLSHIFT_SECURE))
#define	CTLFLAG_SECURE2	(CTLFLAG_SECURE | (1 << CTLSHIFT_SECURE))
#define	CTLFLAG_SECURE3	(CTLFLAG_SECURE | (2 << CTLSHIFT_SECURE))

/*
 * USE THIS instead of a hardwired number from the categories below
 * to get dynamically assigned sysctl entries using the linker-set
 * technology. This is the way nearly all new sysctl variables should
 * be implemented.
 * e.g. SYSCTL_INT(_parent, OID_AUTO, name, CTLFLAG_RW, &variable, 0, "");
 */
#define	OID_AUTO	(-1)

/*
 * The starting number for dynamically-assigned entries.  WARNING!
 * ALL static sysctl entries should have numbers LESS than this!
 */
#define	CTL_AUTO_START	0x100

#ifdef _KERNEL
#include <sys/linker_set.h>

#ifdef KLD_MODULE
/* XXX allow overspecification of type in external kernel modules */
#define	SYSCTL_CT_ASSERT_MASK CTLTYPE
#else
#define	SYSCTL_CT_ASSERT_MASK 0
#endif

#define	SYSCTL_HANDLER_ARGS struct sysctl_oid *oidp, void *arg1,	\
	intmax_t arg2, struct sysctl_req *req

/* definitions for sysctl_req 'lock' member */
#define	REQ_UNWIRED	1
#define	REQ_WIRED	2

/* definitions for sysctl_req 'flags' member */
#if defined(__aarch64__) || defined(__amd64__) || defined(__powerpc64__) ||\
    (defined(__mips__) && defined(__mips_n64))
#define	SCTL_MASK32	1	/* 32 bit emulation */
#endif

/*
 * This describes the access space for a sysctl request.  This is needed
 * so that we can use the interface from the kernel or from user-space.
 */
struct sysctl_req {
	struct thread	*td;		/* used for access checking */
	int		 lock;		/* wiring state */
	void		*oldptr;
	size_t		 oldlen;
	size_t		 oldidx;
	int		(*oldfunc)(struct sysctl_req *, const void *, size_t);
	const void		*newptr;
	size_t		 newlen;
	size_t		 newidx;
	int		(*newfunc)(struct sysctl_req *, void *, size_t);
	size_t		 validlen;
	int		 flags;
};

SLIST_HEAD(sysctl_oid_list, sysctl_oid);

/*
 * This describes one "oid" in the MIB tree.  Potentially more nodes can
 * be hidden behind it, expanded by the handler.
 */
struct sysctl_oid {
	struct sysctl_oid_list oid_children;
	struct sysctl_oid_list *oid_parent;
	SLIST_ENTRY(sysctl_oid) oid_link;
	int		 oid_number;
	u_int		 oid_kind;
	void		*oid_arg1;
	intmax_t	 oid_arg2;
	const char	*oid_name;
	int		(*oid_handler)(SYSCTL_HANDLER_ARGS);
	const char	*oid_fmt;
	int		 oid_refcnt;
	u_int		 oid_running;
	const char	*oid_descr;
	const char	*oid_label;
};

#define	SYSCTL_IN(r, p, l)	(r->newfunc)(r, p, l)
#define	SYSCTL_OUT(r, p, l)	(r->oldfunc)(r, p, l)
#define	SYSCTL_OUT_STR(r, p)	(r->oldfunc)(r, p, strlen(p) + 1)

int sysctl_handle_bool(SYSCTL_HANDLER_ARGS);
int sysctl_handle_8(SYSCTL_HANDLER_ARGS);
int sysctl_handle_16(SYSCTL_HANDLER_ARGS);
int sysctl_handle_32(SYSCTL_HANDLER_ARGS);
int sysctl_handle_64(SYSCTL_HANDLER_ARGS);
int sysctl_handle_int(SYSCTL_HANDLER_ARGS);
int sysctl_msec_to_ticks(SYSCTL_HANDLER_ARGS);
int sysctl_handle_long(SYSCTL_HANDLER_ARGS);
int sysctl_handle_string(SYSCTL_HANDLER_ARGS);
int sysctl_handle_opaque(SYSCTL_HANDLER_ARGS);
int sysctl_handle_counter_u64(SYSCTL_HANDLER_ARGS);
int sysctl_handle_counter_u64_array(SYSCTL_HANDLER_ARGS);

int sysctl_handle_uma_zone_max(SYSCTL_HANDLER_ARGS);
int sysctl_handle_uma_zone_cur(SYSCTL_HANDLER_ARGS);

int sysctl_msec_to_sbintime(SYSCTL_HANDLER_ARGS);
int sysctl_usec_to_sbintime(SYSCTL_HANDLER_ARGS);

int sysctl_dpcpu_int(SYSCTL_HANDLER_ARGS);
int sysctl_dpcpu_long(SYSCTL_HANDLER_ARGS);
int sysctl_dpcpu_quad(SYSCTL_HANDLER_ARGS);

/*
 * These functions are used to add/remove an oid from the mib.
 */
void sysctl_register_oid(struct sysctl_oid *oidp);
void sysctl_register_disabled_oid(struct sysctl_oid *oidp);
void sysctl_enable_oid(struct sysctl_oid *oidp);
void sysctl_unregister_oid(struct sysctl_oid *oidp);

/* Declare a static oid to allow child oids to be added to it. */
#define	SYSCTL_DECL(name)			\
	extern struct sysctl_oid sysctl__##name

/* Hide these in macros. */
#define	SYSCTL_CHILDREN(oid_ptr)		(&(oid_ptr)->oid_children)
#define	SYSCTL_PARENT(oid_ptr)					\
    (((oid_ptr)->oid_parent != &sysctl__children) ?		\
	__containerof((oid_ptr)->oid_parent, struct sysctl_oid,	\
	oid_children) : (struct sysctl_oid *)NULL)
#define	SYSCTL_STATIC_CHILDREN(oid_name)	(&sysctl__##oid_name.oid_children)

/* === Structs and macros related to context handling. === */

/* All dynamically created sysctls can be tracked in a context list. */
struct sysctl_ctx_entry {
	struct sysctl_oid *entry;
	TAILQ_ENTRY(sysctl_ctx_entry) link;
};

TAILQ_HEAD(sysctl_ctx_list, sysctl_ctx_entry);

#define	SYSCTL_NODE_CHILDREN(parent, name) \
	sysctl__##parent##_##name.oid_children

#ifndef NO_SYSCTL_DESCR
#define	__DESCR(d) d
#else
#define	__DESCR(d) ""
#endif

/* This macro is only for internal use */
#define	SYSCTL_OID_RAW(id, parent_child_head, nbr, name, kind, a1, a2, handler, fmt, descr, label) \
	struct sysctl_oid id = {					\
		.oid_parent = (parent_child_head),			\
		.oid_children = SLIST_HEAD_INITIALIZER(&id.oid_children), \
		.oid_number = (nbr),					\
		.oid_kind = (kind),					\
		.oid_arg1 = (a1),					\
		.oid_arg2 = (a2),					\
		.oid_name = (name),					\
		.oid_handler = (handler),				\
		.oid_fmt = (fmt),					\
		.oid_descr = __DESCR(descr),				\
		.oid_label = (label),					\
	};								\
	DATA_SET(sysctl_set, id)

/* This constructs a static "raw" MIB oid. */
#define	SYSCTL_OID(parent, nbr, name, kind, a1, a2, handler, fmt, descr) \
	SYSCTL_OID_WITH_LABEL(parent, nbr, name, kind, a1, a2,		\
	    handler, fmt, descr, NULL)

#define	SYSCTL_OID_WITH_LABEL(parent, nbr, name, kind, a1, a2, handler, fmt, descr, label) \
    static SYSCTL_OID_RAW(sysctl__##parent##_##name,			\
	SYSCTL_CHILDREN(&sysctl__##parent),				\
	nbr, #name, kind, a1, a2, handler, fmt, descr, label)

/* This constructs a global "raw" MIB oid. */
#define	SYSCTL_OID_GLOBAL(parent, nbr, name, kind, a1, a2, handler, fmt, descr, label) \
    SYSCTL_OID_RAW(sysctl__##parent##_##name, \
	SYSCTL_CHILDREN(&sysctl__##parent),	\
	nbr, #name, kind, a1, a2, handler, fmt, descr, label)

#define	SYSCTL_ADD_OID(ctx, parent, nbr, name, kind, a1, a2, handler, fmt, descr) \
	sysctl_add_oid(ctx, parent, nbr, name, kind, a1, a2, handler, fmt, __DESCR(descr), NULL)

/* This constructs a root node from which other nodes can hang. */
#define	SYSCTL_ROOT_NODE(nbr, name, access, handler, descr)	\
	SYSCTL_OID_RAW(sysctl___##name, &sysctl__children,	\
	    nbr, #name, CTLTYPE_NODE|(access), NULL, 0,		\
	    handler, "N", descr, NULL);				\
	CTASSERT(((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_NODE)

/* This constructs a node from which other oids can hang. */
#define	SYSCTL_NODE(parent, nbr, name, access, handler, descr) \
	SYSCTL_NODE_WITH_LABEL(parent, nbr, name, access, handler, descr, NULL)

#define	SYSCTL_NODE_WITH_LABEL(parent, nbr, name, access, handler, descr, label) \
	SYSCTL_OID_GLOBAL(parent, nbr, name, CTLTYPE_NODE|(access),	\
	    NULL, 0, handler, "N", descr, label);			\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_NODE)

#define	SYSCTL_ADD_NODE(ctx, parent, nbr, name, access, handler, descr)	\
	SYSCTL_ADD_NODE_WITH_LABEL(ctx, parent, nbr, name, access, \
	    handler, descr, NULL)

#define	SYSCTL_ADD_NODE_WITH_LABEL(ctx, parent, nbr, name, access, handler, descr, label) \
({									\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_NODE);	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_NODE|(access),	\
	    NULL, 0, handler, "N", __DESCR(descr), label);		\
})

#define	SYSCTL_ADD_ROOT_NODE(ctx, nbr, name, access, handler, descr)	\
({									\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_NODE);	\
	sysctl_add_oid(ctx, &sysctl__children, nbr, name,		\
	    CTLTYPE_NODE|(access),					\
	    NULL, 0, handler, "N", __DESCR(descr), NULL);		\
})

/* Oid for a string.  len can be 0 to indicate '\0' termination. */
#define	SYSCTL_STRING(parent, nbr, name, access, arg, len, descr)	\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_STRING|(access),		\
	    arg, len, sysctl_handle_string, "A", descr);		\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_STRING)

#define	SYSCTL_ADD_STRING(ctx, parent, nbr, name, access, arg, len, descr) \
({									\
	char *__arg = (arg);						\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_STRING);	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_STRING|(access),	\
	    __arg, len, sysctl_handle_string, "A", __DESCR(descr),	\
	    NULL); \
})

/* Oid for a constant '\0' terminated string. */
#define	SYSCTL_CONST_STRING(parent, nbr, name, access, arg, descr)	\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_STRING|(access),		\
	    __DECONST(char *, arg), 0, sysctl_handle_string, "A", descr); \
	CTASSERT(!(access & CTLFLAG_WR));				\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_STRING)

#define	SYSCTL_ADD_CONST_STRING(ctx, parent, nbr, name, access, arg, descr) \
({									\
	char *__arg = __DECONST(char *, arg);				\
	CTASSERT(!(access & CTLFLAG_WR));				\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_STRING);	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_STRING|(access),	\
	    __arg, 0, sysctl_handle_string, "A", __DESCR(descr),	\
	    NULL); \
})

/* Oid for a bool.  If ptr is NULL, val is returned. */
#define	SYSCTL_NULL_BOOL_PTR ((bool *)NULL)
#define	SYSCTL_BOOL(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_U8 | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_bool, "CU", descr);		\
	CTASSERT(((access) & CTLTYPE) == 0 &&			\
	    sizeof(bool) == sizeof(*(ptr)))

#define	SYSCTL_ADD_BOOL(ctx, parent, nbr, name, access, ptr, val, descr) \
({									\
	bool *__ptr = (ptr);						\
	CTASSERT(((access) & CTLTYPE) == 0);				\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_U8 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, val, sysctl_handle_bool, "CU", __DESCR(descr),	\
	    NULL);							\
})

/* Oid for a signed 8-bit int.  If ptr is NULL, val is returned. */
#define	SYSCTL_NULL_S8_PTR ((int8_t *)NULL)
#define	SYSCTL_S8(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_S8 | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_8, "C", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S8) && \
	    sizeof(int8_t) == sizeof(*(ptr)))

#define	SYSCTL_ADD_S8(ctx, parent, nbr, name, access, ptr, val, descr)	\
({									\
	int8_t *__ptr = (ptr);						\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S8);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_S8 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, val, sysctl_handle_8, "C", __DESCR(descr), NULL);	\
})

/* Oid for an unsigned 8-bit int.  If ptr is NULL, val is returned. */
#define	SYSCTL_NULL_U8_PTR ((uint8_t *)NULL)
#define	SYSCTL_U8(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_U8 | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_8, "CU", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U8) && \
	    sizeof(uint8_t) == sizeof(*(ptr)))

#define	SYSCTL_ADD_U8(ctx, parent, nbr, name, access, ptr, val, descr)	\
({									\
	uint8_t *__ptr = (ptr);						\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U8);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_U8 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, val, sysctl_handle_8, "CU", __DESCR(descr), NULL);	\
})

/* Oid for a signed 16-bit int.  If ptr is NULL, val is returned. */
#define	SYSCTL_NULL_S16_PTR ((int16_t *)NULL)
#define	SYSCTL_S16(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_S16 | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_16, "S", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S16) && \
	    sizeof(int16_t) == sizeof(*(ptr)))

#define	SYSCTL_ADD_S16(ctx, parent, nbr, name, access, ptr, val, descr)	\
({									\
	int16_t *__ptr = (ptr);						\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S16);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_S16 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, val, sysctl_handle_16, "S", __DESCR(descr), NULL);	\
})

/* Oid for an unsigned 16-bit int.  If ptr is NULL, val is returned. */
#define	SYSCTL_NULL_U16_PTR ((uint16_t *)NULL)
#define	SYSCTL_U16(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_U16 | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_16, "SU", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U16) && \
	    sizeof(uint16_t) == sizeof(*(ptr)))

#define	SYSCTL_ADD_U16(ctx, parent, nbr, name, access, ptr, val, descr)	\
({									\
	uint16_t *__ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U16);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_U16 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, val, sysctl_handle_16, "SU", __DESCR(descr), NULL);	\
})

/* Oid for a signed 32-bit int.  If ptr is NULL, val is returned. */
#define	SYSCTL_NULL_S32_PTR ((int32_t *)NULL)
#define	SYSCTL_S32(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_S32 | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_32, "I", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S32) && \
	    sizeof(int32_t) == sizeof(*(ptr)))

#define	SYSCTL_ADD_S32(ctx, parent, nbr, name, access, ptr, val, descr)	\
({									\
	int32_t *__ptr = (ptr);						\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S32);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_S32 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, val, sysctl_handle_32, "I", __DESCR(descr), NULL);	\
})

/* Oid for an unsigned 32-bit int.  If ptr is NULL, val is returned. */
#define	SYSCTL_NULL_U32_PTR ((uint32_t *)NULL)
#define	SYSCTL_U32(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_U32 | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_32, "IU", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U32) && \
	    sizeof(uint32_t) == sizeof(*(ptr)))

#define	SYSCTL_ADD_U32(ctx, parent, nbr, name, access, ptr, val, descr)	\
({									\
	uint32_t *__ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U32);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_U32 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, val, sysctl_handle_32, "IU", __DESCR(descr), NULL);	\
})

/* Oid for a signed 64-bit int.  If ptr is NULL, val is returned. */
#define	SYSCTL_NULL_S64_PTR ((int64_t *)NULL)
#define	SYSCTL_S64(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_S64 | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_64, "Q", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S64) && \
	    sizeof(int64_t) == sizeof(*(ptr)))

#define	SYSCTL_ADD_S64(ctx, parent, nbr, name, access, ptr, val, descr)	\
({									\
	int64_t *__ptr = (ptr);						\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S64);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_S64 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, val, sysctl_handle_64, "Q", __DESCR(descr), NULL);	\
})

/* Oid for an unsigned 64-bit int.  If ptr is NULL, val is returned. */
#define	SYSCTL_NULL_U64_PTR ((uint64_t *)NULL)
#define	SYSCTL_U64(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_U64 | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_64, "QU", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U64) && \
	    sizeof(uint64_t) == sizeof(*(ptr)))

#define	SYSCTL_ADD_U64(ctx, parent, nbr, name, access, ptr, val, descr)	\
({									\
	uint64_t *__ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U64);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_U64 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, val, sysctl_handle_64, "QU", __DESCR(descr), NULL);	\
})

/* Oid for an int.  If ptr is SYSCTL_NULL_INT_PTR, val is returned. */
#define	SYSCTL_NULL_INT_PTR ((int *)NULL)
#define	SYSCTL_INT(parent, nbr, name, access, ptr, val, descr) \
	SYSCTL_INT_WITH_LABEL(parent, nbr, name, access, ptr, val, descr, NULL)

#define	SYSCTL_INT_WITH_LABEL(parent, nbr, name, access, ptr, val, descr, label) \
	SYSCTL_OID_WITH_LABEL(parent, nbr, name,			\
	    CTLTYPE_INT | CTLFLAG_MPSAFE | (access),			\
	    ptr, val, sysctl_handle_int, "I", descr, label);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_INT) && \
	    sizeof(int) == sizeof(*(ptr)))

#define	SYSCTL_ADD_INT(ctx, parent, nbr, name, access, ptr, val, descr)	\
({									\
	int *__ptr = (ptr);						\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_INT);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_INT | CTLFLAG_MPSAFE | (access),			\
	    __ptr, val, sysctl_handle_int, "I", __DESCR(descr), NULL);	\
})

/* Oid for an unsigned int.  If ptr is NULL, val is returned. */
#define	SYSCTL_NULL_UINT_PTR ((unsigned *)NULL)
#define	SYSCTL_UINT(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_UINT | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_int, "IU", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_UINT) && \
	    sizeof(unsigned) == sizeof(*(ptr)))

#define	SYSCTL_ADD_UINT(ctx, parent, nbr, name, access, ptr, val, descr) \
({									\
	unsigned *__ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_UINT);	\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_UINT | CTLFLAG_MPSAFE | (access),			\
	    __ptr, val, sysctl_handle_int, "IU", __DESCR(descr), NULL);	\
})

/* Oid for a long.  The pointer must be non NULL. */
#define	SYSCTL_NULL_LONG_PTR ((long *)NULL)
#define	SYSCTL_LONG(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_LONG | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_long, "L", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_LONG) && \
	    sizeof(long) == sizeof(*(ptr)))

#define	SYSCTL_ADD_LONG(ctx, parent, nbr, name, access, ptr, descr)	\
({									\
	long *__ptr = (ptr);						\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_LONG);	\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_LONG | CTLFLAG_MPSAFE | (access),			\
	    __ptr, 0, sysctl_handle_long, "L", __DESCR(descr), NULL);	\
})

/* Oid for an unsigned long.  The pointer must be non NULL. */
#define	SYSCTL_NULL_ULONG_PTR ((unsigned long *)NULL)
#define	SYSCTL_ULONG(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,					\
	    CTLTYPE_ULONG | CTLFLAG_MPSAFE | (access),			\
	    ptr, val, sysctl_handle_long, "LU", descr);			\
	CTASSERT((((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_ULONG) &&	\
	    sizeof(unsigned long) == sizeof(*(ptr)))

#define	SYSCTL_ADD_ULONG(ctx, parent, nbr, name, access, ptr, descr)	\
({									\
	unsigned long *__ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_ULONG);	\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_ULONG | CTLFLAG_MPSAFE | (access),			\
	    __ptr, 0, sysctl_handle_long, "LU", __DESCR(descr), NULL);	\
})

/* Oid for a quad.  The pointer must be non NULL. */
#define	SYSCTL_NULL_QUAD_PTR ((int64_t *)NULL)
#define	SYSCTL_QUAD(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_S64 | CTLFLAG_MPSAFE | (access),		\
	    ptr, val, sysctl_handle_64, "Q", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S64) && \
	    sizeof(int64_t) == sizeof(*(ptr)))

#define	SYSCTL_ADD_QUAD(ctx, parent, nbr, name, access, ptr, descr)	\
({									\
	int64_t *__ptr = (ptr);						\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S64);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_S64 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, 0, sysctl_handle_64, "Q", __DESCR(descr), NULL);	\
})

#define	SYSCTL_NULL_UQUAD_PTR ((uint64_t *)NULL)
#define	SYSCTL_UQUAD(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_OID(parent, nbr, name,					\
	    CTLTYPE_U64 | CTLFLAG_MPSAFE | (access),			\
	     ptr, val, sysctl_handle_64, "QU", descr);			\
	CTASSERT((((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U64) &&	\
	    sizeof(uint64_t) == sizeof(*(ptr)))

#define	SYSCTL_ADD_UQUAD(ctx, parent, nbr, name, access, ptr, descr)	\
({									\
	uint64_t *__ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U64);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_U64 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, 0, sysctl_handle_64, "QU", __DESCR(descr), NULL);	\
})

/* Oid for a CPU dependent variable */
#define	SYSCTL_ADD_UAUTO(ctx, parent, nbr, name, access, ptr, descr)	\
({									\
	struct sysctl_oid *__ret;					\
	CTASSERT((sizeof(uint64_t) == sizeof(*(ptr)) ||			\
	    sizeof(unsigned) == sizeof(*(ptr))) &&			\
	    ((access) & CTLTYPE) == 0);					\
	if (sizeof(uint64_t) == sizeof(*(ptr))) {			\
		__ret = sysctl_add_oid(ctx, parent, nbr, name,		\
		    CTLTYPE_U64 | CTLFLAG_MPSAFE | (access),		\
		    (ptr), 0, sysctl_handle_64, "QU",			\
		    __DESCR(descr), NULL);				\
	} else {							\
		__ret = sysctl_add_oid(ctx, parent, nbr, name,		\
		    CTLTYPE_UINT | CTLFLAG_MPSAFE | (access),		\
		    (ptr), 0, sysctl_handle_int, "IU",			\
		    __DESCR(descr), NULL);				\
	}								\
	__ret;								\
})

/* Oid for a 64-bit unsigned counter(9).  The pointer must be non NULL. */
#define	SYSCTL_COUNTER_U64(parent, nbr, name, access, ptr, descr)	\
	SYSCTL_OID(parent, nbr, name,					\
	    CTLTYPE_U64 | CTLFLAG_MPSAFE | (access),			\
	    (ptr), 0, sysctl_handle_counter_u64, "QU", descr);		\
	CTASSERT((((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U64) &&	\
	    sizeof(counter_u64_t) == sizeof(*(ptr)) &&			\
	    sizeof(uint64_t) == sizeof(**(ptr)))

#define	SYSCTL_ADD_COUNTER_U64(ctx, parent, nbr, name, access, ptr, descr) \
({									\
	counter_u64_t *__ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_U64);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_U64 | CTLFLAG_MPSAFE | (access),			\
	    __ptr, 0, sysctl_handle_counter_u64, "QU", __DESCR(descr),	\
	    NULL);							\
})

/* Oid for an array of counter(9)s.  The pointer and length must be non zero. */
#define	SYSCTL_COUNTER_U64_ARRAY(parent, nbr, name, access, ptr, len, descr) \
	SYSCTL_OID(parent, nbr, name,					\
	    CTLTYPE_OPAQUE | CTLFLAG_MPSAFE | (access),			\
	    (ptr), (len), sysctl_handle_counter_u64_array, "S", descr);	\
	CTASSERT((((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_OPAQUE) &&	\
	    sizeof(counter_u64_t) == sizeof(*(ptr)) &&			\
	    sizeof(uint64_t) == sizeof(**(ptr)))

#define	SYSCTL_ADD_COUNTER_U64_ARRAY(ctx, parent, nbr, name, access,	\
    ptr, len, descr)							\
({									\
	counter_u64_t *__ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_OPAQUE);	\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_OPAQUE | CTLFLAG_MPSAFE | (access),			\
	    __ptr, len, sysctl_handle_counter_u64_array, "S",		\
	    __DESCR(descr), NULL);					\
})

/* Oid for an opaque object.  Specified by a pointer and a length. */
#define	SYSCTL_OPAQUE(parent, nbr, name, access, ptr, len, fmt, descr)	\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_OPAQUE|(access),		\
	    ptr, len, sysctl_handle_opaque, fmt, descr);		\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_OPAQUE)

#define	SYSCTL_ADD_OPAQUE(ctx, parent, nbr, name, access, ptr, len, fmt, descr)	\
({									\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_OPAQUE);	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_OPAQUE|(access),	\
	    ptr, len, sysctl_handle_opaque, fmt, __DESCR(descr), NULL);	\
})

/* Oid for a struct.  Specified by a pointer and a type. */
#define	SYSCTL_STRUCT(parent, nbr, name, access, ptr, type, descr)	\
	SYSCTL_OID(parent, nbr, name, CTLTYPE_OPAQUE|(access),		\
	    ptr, sizeof(struct type), sysctl_handle_opaque,		\
	    "S," #type, descr);						\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_OPAQUE)

#define	SYSCTL_ADD_STRUCT(ctx, parent, nbr, name, access, ptr, type, descr) \
({									\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_OPAQUE);	\
	sysctl_add_oid(ctx, parent, nbr, name, CTLTYPE_OPAQUE|(access),	\
	    (ptr), sizeof(struct type),					\
	    sysctl_handle_opaque, "S," #type, __DESCR(descr), NULL);	\
})

/* Oid for a procedure.  Specified by a pointer and an arg. */
#define	SYSCTL_PROC(parent, nbr, name, access, ptr, arg, handler, fmt, descr) \
	SYSCTL_OID(parent, nbr, name, (access),				\
	    ptr, arg, handler, fmt, descr);				\
	CTASSERT(((access) & CTLTYPE) != 0)

#define	SYSCTL_ADD_PROC(ctx, parent, nbr, name, access, ptr, arg, handler, fmt, descr) \
({									\
	CTASSERT(((access) & CTLTYPE) != 0);				\
	sysctl_add_oid(ctx, parent, nbr, name, (access),		\
	    (ptr), (arg), (handler), (fmt), __DESCR(descr), NULL);	\
})

/* Oid to handle limits on uma(9) zone specified by pointer. */
#define	SYSCTL_UMA_MAX(parent, nbr, name, access, ptr, descr)	\
	SYSCTL_OID(parent, nbr, name,				\
	    CTLTYPE_INT | CTLFLAG_MPSAFE | (access),		\
	    (ptr), 0, sysctl_handle_uma_zone_max, "I", descr);	\
	CTASSERT(((access) & CTLTYPE) == 0 ||			\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_INT)

#define	SYSCTL_ADD_UMA_MAX(ctx, parent, nbr, name, access, ptr, descr)	\
({									\
	uma_zone_t __ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_INT);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_INT | CTLFLAG_MPSAFE | (access),			\
	    __ptr, 0, sysctl_handle_uma_zone_max, "I", __DESCR(descr),	\
	    NULL);							\
})

/* Oid to obtain current use of uma(9) zone specified by pointer. */
#define	SYSCTL_UMA_CUR(parent, nbr, name, access, ptr, descr)		\
	SYSCTL_OID(parent, nbr, name,					\
	    CTLTYPE_INT | CTLFLAG_MPSAFE | CTLFLAG_RD | (access),	\
	    (ptr), 0, sysctl_handle_uma_zone_cur, "I", descr);		\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_INT)

#define	SYSCTL_ADD_UMA_CUR(ctx, parent, nbr, name, access, ptr, descr)	\
({									\
	uma_zone_t __ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_INT);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_INT | CTLFLAG_MPSAFE | CTLFLAG_RD | (access),	\
	    __ptr, 0, sysctl_handle_uma_zone_cur, "I", __DESCR(descr),	\
	    NULL);							\
})

/* OID expressing a sbintime_t as microseconds */
#define	SYSCTL_SBINTIME_USEC(parent, nbr, name, access, ptr, descr)	\
	SYSCTL_OID(parent, nbr, name,					\
	    CTLTYPE_INT | CTLFLAG_MPSAFE | CTLFLAG_RD | (access),	\
	    (ptr), 0, sysctl_usec_to_sbintime, "Q", descr);		\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S64)
#define	SYSCTL_ADD_SBINTIME_USEC(ctx, parent, nbr, name, access, ptr, descr) \
({									\
	sbintime_t *__ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S64);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_INT | CTLFLAG_MPSAFE | CTLFLAG_RD | (access),	\
	    __ptr, 0, sysctl_usec_to_sbintime, "Q", __DESCR(descr),	\
	    NULL);							\
})

/* OID expressing a sbintime_t as milliseconds */
#define	SYSCTL_SBINTIME_MSEC(parent, nbr, name, access, ptr, descr)	\
	SYSCTL_OID(parent, nbr, name,					\
	    CTLTYPE_INT | CTLFLAG_MPSAFE | CTLFLAG_RD | (access),	\
	    (ptr), 0, sysctl_msec_to_sbintime, "Q", descr);		\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S64)
#define	SYSCTL_ADD_SBINTIME_MSEC(ctx, parent, nbr, name, access, ptr, descr) \
({									\
	sbintime_t *__ptr = (ptr);					\
	CTASSERT(((access) & CTLTYPE) == 0 ||				\
	    ((access) & SYSCTL_CT_ASSERT_MASK) == CTLTYPE_S64);		\
	sysctl_add_oid(ctx, parent, nbr, name,				\
	    CTLTYPE_INT | CTLFLAG_MPSAFE | CTLFLAG_RD | (access),	\
	    __ptr, 0, sysctl_msec_to_sbintime, "Q", __DESCR(descr),	\
	    NULL);							\
})

/*
 * A macro to generate a read-only sysctl to indicate the presence of optional
 * kernel features.
 */
#define	FEATURE(name, desc)						\
	SYSCTL_INT_WITH_LABEL(_kern_features, OID_AUTO, name,		\
	    CTLFLAG_RD | CTLFLAG_CAPRD, SYSCTL_NULL_INT_PTR, 1, desc, "feature")

#endif /* _KERNEL */

/*
 * Top-level identifiers
 */
#define	CTL_UNSPEC	0		/* unused */
#define	CTL_KERN	1		/* "high kernel": proc, limits */
#define	CTL_VM		2		/* virtual memory */
#define	CTL_VFS		3		/* filesystem, mount type is next */
#define	CTL_NET		4		/* network, see socket.h */
#define	CTL_DEBUG	5		/* debugging parameters */
#define	CTL_HW		6		/* generic cpu/io */
#define	CTL_MACHDEP	7		/* machine dependent */
#define	CTL_USER	8		/* user-level */
#define	CTL_P1003_1B	9		/* POSIX 1003.1B */

/*
 * CTL_KERN identifiers
 */
#define	KERN_OSTYPE		 1	/* string: system version */
#define	KERN_OSRELEASE		 2	/* string: system release */
#define	KERN_OSREV		 3	/* int: system revision */
#define	KERN_VERSION		 4	/* string: compile time info */
#define	KERN_MAXVNODES		 5	/* int: max vnodes */
#define	KERN_MAXPROC		 6	/* int: max processes */
#define	KERN_MAXFILES		 7	/* int: max open files */
#define	KERN_ARGMAX		 8	/* int: max arguments to exec */
#define	KERN_SECURELVL		 9	/* int: system security level */
#define	KERN_HOSTNAME		10	/* string: hostname */
#define	KERN_HOSTID		11	/* int: host identifier */
#define	KERN_CLOCKRATE		12	/* struct: struct clockrate */
#define	KERN_VNODE		13	/* struct: vnode structures */
#define	KERN_PROC		14	/* struct: process entries */
#define	KERN_FILE		15	/* struct: file entries */
#define	KERN_PROF		16	/* node: kernel profiling info */
#define	KERN_POSIX1		17	/* int: POSIX.1 version */
#define	KERN_NGROUPS		18	/* int: # of supplemental group ids */
#define	KERN_JOB_CONTROL	19	/* int: is job control available */
#define	KERN_SAVED_IDS		20	/* int: saved set-user/group-ID */
#define	KERN_BOOTTIME		21	/* struct: time kernel was booted */
#define	KERN_NISDOMAINNAME	22	/* string: YP domain name */
#define	KERN_UPDATEINTERVAL	23	/* int: update process sleep time */
#define	KERN_OSRELDATE		24	/* int: kernel release date */
#define	KERN_NTP_PLL		25	/* node: NTP PLL control */
#define	KERN_BOOTFILE		26	/* string: name of booted kernel */
#define	KERN_MAXFILESPERPROC	27	/* int: max open files per proc */
#define	KERN_MAXPROCPERUID	28	/* int: max processes per uid */
#define	KERN_DUMPDEV		29	/* struct cdev *: device to dump on */
#define	KERN_IPC		30	/* node: anything related to IPC */
#define	KERN_DUMMY		31	/* unused */
#define	KERN_PS_STRINGS		32	/* int: address of PS_STRINGS */
#define	KERN_USRSTACK		33	/* int: address of USRSTACK */
#define	KERN_LOGSIGEXIT		34	/* int: do we log sigexit procs? */
#define	KERN_IOV_MAX		35	/* int: value of UIO_MAXIOV */
#define	KERN_HOSTUUID		36	/* string: host UUID identifier */
#define	KERN_ARND		37	/* int: from arc4rand() */
#define	KERN_MAXPHYS		38	/* int: MAXPHYS value */
/*
 * KERN_PROC subtypes
 */
#define	KERN_PROC_ALL		0	/* everything */
#define	KERN_PROC_PID		1	/* by process id */
#define	KERN_PROC_PGRP		2	/* by process group id */
#define	KERN_PROC_SESSION	3	/* by session of pid */
#define	KERN_PROC_TTY		4	/* by controlling tty */
#define	KERN_PROC_UID		5	/* by effective uid */
#define	KERN_PROC_RUID		6	/* by real uid */
#define	KERN_PROC_ARGS		7	/* get/set arguments/proctitle */
#define	KERN_PROC_PROC		8	/* only return procs */
#define	KERN_PROC_SV_NAME	9	/* get syscall vector name */
#define	KERN_PROC_RGID		10	/* by real group id */
#define	KERN_PROC_GID		11	/* by effective group id */
#define	KERN_PROC_PATHNAME	12	/* path to executable */
#define	KERN_PROC_OVMMAP	13	/* Old VM map entries for process */
#define	KERN_PROC_OFILEDESC	14	/* Old file descriptors for process */
#define	KERN_PROC_KSTACK	15	/* Kernel stacks for process */
#define	KERN_PROC_INC_THREAD	0x10	/*
					 * modifier for pid, pgrp, tty,
					 * uid, ruid, gid, rgid and proc
					 * This effectively uses 16-31
					 */
#define	KERN_PROC_VMMAP		32	/* VM map entries for process */
#define	KERN_PROC_FILEDESC	33	/* File descriptors for process */
#define	KERN_PROC_GROUPS	34	/* process groups */
#define	KERN_PROC_ENV		35	/* get environment */
#define	KERN_PROC_AUXV		36	/* get ELF auxiliary vector */
#define	KERN_PROC_RLIMIT	37	/* process resource limits */
#define	KERN_PROC_PS_STRINGS	38	/* get ps_strings location */
#define	KERN_PROC_UMASK		39	/* process umask */
#define	KERN_PROC_OSREL		40	/* osreldate for process binary */
#define	KERN_PROC_SIGTRAMP	41	/* signal trampoline location */
#define	KERN_PROC_CWD		42	/* process current working directory */
#define	KERN_PROC_NFDS		43	/* number of open file descriptors */

/*
 * KERN_IPC identifiers
 */
#define	KIPC_MAXSOCKBUF		1	/* int: max size of a socket buffer */
#define	KIPC_SOCKBUF_WASTE	2	/* int: wastage factor in sockbuf */
#define	KIPC_SOMAXCONN		3	/* int: max length of connection q */
#define	KIPC_MAX_LINKHDR	4	/* int: max length of link header */
#define	KIPC_MAX_PROTOHDR	5	/* int: max length of network header */
#define	KIPC_MAX_HDR		6	/* int: max total length of headers */
#define	KIPC_MAX_DATALEN	7	/* int: max length of data? */

/*
 * CTL_HW identifiers
 */
#define	HW_MACHINE	 1		/* string: machine class */
#define	HW_MODEL	 2		/* string: specific machine model */
#define	HW_NCPU		 3		/* int: number of cpus */
#define	HW_BYTEORDER	 4		/* int: machine byte order */
#define	HW_PHYSMEM	 5		/* int: total memory */
#define	HW_USERMEM	 6		/* int: non-kernel memory */
#define	HW_PAGESIZE	 7		/* int: software page size */
#define	HW_DISKNAMES	 8		/* strings: disk drive names */
#define	HW_DISKSTATS	 9		/* struct: diskstats[] */
#define	HW_FLOATINGPT	10		/* int: has HW floating point? */
#define	HW_MACHINE_ARCH	11		/* string: machine architecture */
#define	HW_REALMEM	12		/* int: 'real' memory */

/*
 * CTL_USER definitions
 */
#define	USER_CS_PATH		 1	/* string: _CS_PATH */
#define	USER_BC_BASE_MAX	 2	/* int: BC_BASE_MAX */
#define	USER_BC_DIM_MAX		 3	/* int: BC_DIM_MAX */
#define	USER_BC_SCALE_MAX	 4	/* int: BC_SCALE_MAX */
#define	USER_BC_STRING_MAX	 5	/* int: BC_STRING_MAX */
#define	USER_COLL_WEIGHTS_MAX	 6	/* int: COLL_WEIGHTS_MAX */
#define	USER_EXPR_NEST_MAX	 7	/* int: EXPR_NEST_MAX */
#define	USER_LINE_MAX		 8	/* int: LINE_MAX */
#define	USER_RE_DUP_MAX		 9	/* int: RE_DUP_MAX */
#define	USER_POSIX2_VERSION	10	/* int: POSIX2_VERSION */
#define	USER_POSIX2_C_BIND	11	/* int: POSIX2_C_BIND */
#define	USER_POSIX2_C_DEV	12	/* int: POSIX2_C_DEV */
#define	USER_POSIX2_CHAR_TERM	13	/* int: POSIX2_CHAR_TERM */
#define	USER_POSIX2_FORT_DEV	14	/* int: POSIX2_FORT_DEV */
#define	USER_POSIX2_FORT_RUN	15	/* int: POSIX2_FORT_RUN */
#define	USER_POSIX2_LOCALEDEF	16	/* int: POSIX2_LOCALEDEF */
#define	USER_POSIX2_SW_DEV	17	/* int: POSIX2_SW_DEV */
#define	USER_POSIX2_UPE		18	/* int: POSIX2_UPE */
#define	USER_STREAM_MAX		19	/* int: POSIX2_STREAM_MAX */
#define	USER_TZNAME_MAX		20	/* int: POSIX2_TZNAME_MAX */

#define	CTL_P1003_1B_ASYNCHRONOUS_IO		1	/* boolean */
#define	CTL_P1003_1B_MAPPED_FILES		2	/* boolean */
#define	CTL_P1003_1B_MEMLOCK			3	/* boolean */
#define	CTL_P1003_1B_MEMLOCK_RANGE		4	/* boolean */
#define	CTL_P1003_1B_MEMORY_PROTECTION		5	/* boolean */
#define	CTL_P1003_1B_MESSAGE_PASSING		6	/* boolean */
#define	CTL_P1003_1B_PRIORITIZED_IO		7	/* boolean */
#define	CTL_P1003_1B_PRIORITY_SCHEDULING	8	/* boolean */
#define	CTL_P1003_1B_REALTIME_SIGNALS		9	/* boolean */
#define	CTL_P1003_1B_SEMAPHORES			10	/* boolean */
#define	CTL_P1003_1B_FSYNC			11	/* boolean */
#define	CTL_P1003_1B_SHARED_MEMORY_OBJECTS	12	/* boolean */
#define	CTL_P1003_1B_SYNCHRONIZED_IO		13	/* boolean */
#define	CTL_P1003_1B_TIMERS			14	/* boolean */
#define	CTL_P1003_1B_AIO_LISTIO_MAX		15	/* int */
#define	CTL_P1003_1B_AIO_MAX			16	/* int */
#define	CTL_P1003_1B_AIO_PRIO_DELTA_MAX		17	/* int */
#define	CTL_P1003_1B_DELAYTIMER_MAX		18	/* int */
#define	CTL_P1003_1B_MQ_OPEN_MAX		19	/* int */
#define	CTL_P1003_1B_PAGESIZE			20	/* int */
#define	CTL_P1003_1B_RTSIG_MAX			21	/* int */
#define	CTL_P1003_1B_SEM_NSEMS_MAX		22	/* int */
#define	CTL_P1003_1B_SEM_VALUE_MAX		23	/* int */
#define	CTL_P1003_1B_SIGQUEUE_MAX		24	/* int */
#define	CTL_P1003_1B_TIMER_MAX			25	/* int */

#define	CTL_P1003_1B_MAXID		26

#ifdef _KERNEL

/*
 * Declare some common oids.
 */
extern struct sysctl_oid_list sysctl__children;
SYSCTL_DECL(_kern);
SYSCTL_DECL(_kern_features);
SYSCTL_DECL(_kern_ipc);
SYSCTL_DECL(_kern_proc);
SYSCTL_DECL(_kern_sched);
SYSCTL_DECL(_kern_sched_stats);
SYSCTL_DECL(_sysctl);
SYSCTL_DECL(_vm);
SYSCTL_DECL(_vm_stats);
SYSCTL_DECL(_vm_stats_misc);
SYSCTL_DECL(_vfs);
SYSCTL_DECL(_net);
SYSCTL_DECL(_debug);
SYSCTL_DECL(_debug_sizeof);
SYSCTL_DECL(_dev);
SYSCTL_DECL(_hw);
SYSCTL_DECL(_hw_bus);
SYSCTL_DECL(_hw_bus_devices);
SYSCTL_DECL(_hw_bus_info);
SYSCTL_DECL(_machdep);
SYSCTL_DECL(_user);
SYSCTL_DECL(_compat);
SYSCTL_DECL(_regression);
SYSCTL_DECL(_security);
SYSCTL_DECL(_security_bsd);

extern char	machine[];
extern char	osrelease[];
extern char	ostype[];
extern char	kern_ident[];

/* Dynamic oid handling */
struct sysctl_oid *sysctl_add_oid(struct sysctl_ctx_list *clist,
	    struct sysctl_oid_list *parent, int nbr, const char *name, int kind,
	    void *arg1, intmax_t arg2, int (*handler)(SYSCTL_HANDLER_ARGS),
	    const char *fmt, const char *descr, const char *label);
int	sysctl_remove_name(struct sysctl_oid *parent, const char *name, int del,
	    int recurse);
void	sysctl_rename_oid(struct sysctl_oid *oidp, const char *name);
int	sysctl_move_oid(struct sysctl_oid *oidp,
	    struct sysctl_oid_list *parent);
int	sysctl_remove_oid(struct sysctl_oid *oidp, int del, int recurse);
int	sysctl_ctx_init(struct sysctl_ctx_list *clist);
int	sysctl_ctx_free(struct sysctl_ctx_list *clist);
struct	sysctl_ctx_entry *sysctl_ctx_entry_add(struct sysctl_ctx_list *clist,
	    struct sysctl_oid *oidp);
struct	sysctl_ctx_entry *sysctl_ctx_entry_find(struct sysctl_ctx_list *clist,
	    struct sysctl_oid *oidp);
int	sysctl_ctx_entry_del(struct sysctl_ctx_list *clist,
	    struct sysctl_oid *oidp);

int	kernel_sysctl(struct thread *td, int *name, u_int namelen, void *old,
	    size_t *oldlenp, void *new, size_t newlen, size_t *retval,
	    int flags);
int	kernel_sysctlbyname(struct thread *td, char *name, void *old,
	    size_t *oldlenp, void *new, size_t newlen, size_t *retval,
	    int flags);
int	userland_sysctl(struct thread *td, int *name, u_int namelen, void *old,
	    size_t *oldlenp, int inkernel, const void *new, size_t newlen,
	    size_t *retval, int flags);
int	sysctl_find_oid(int *name, u_int namelen, struct sysctl_oid **noid,
	    int *nindx, struct sysctl_req *req);
void	sysctl_wlock(void);
void	sysctl_wunlock(void);
int	sysctl_wire_old_buffer(struct sysctl_req *req, size_t len);

struct sbuf;
struct sbuf *sbuf_new_for_sysctl(struct sbuf *, char *, int,
	    struct sysctl_req *);
#else	/* !_KERNEL */
#include <sys/cdefs.h>

__BEGIN_DECLS
int	sysctl(const int *, u_int, void *, size_t *, const void *, size_t);
int	sysctlbyname(const char *, void *, size_t *, const void *, size_t);
int	sysctlnametomib(const char *, int *, size_t *);
__END_DECLS
#endif	/* _KERNEL */

#endif	/* !_SYS_SYSCTL_H_ */
