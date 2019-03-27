/*-
 * Copyright (c) 2000-2015 Mark R V Murray
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *
 * $FreeBSD$
 */

#ifndef SYS_DEV_RANDOM_RANDOMDEV_H_INCLUDED
#define	SYS_DEV_RANDOM_RANDOMDEV_H_INCLUDED

#ifdef _KERNEL

/* This header contains only those definitions that are global
 * and non algorithm-specific for the entropy processor
 */

#ifdef SYSCTL_DECL	/* from sysctl.h */
SYSCTL_DECL(_kern_random);

#define	RANDOM_CHECK_UINT(name, min, max)				\
static int								\
random_check_uint_##name(SYSCTL_HANDLER_ARGS)				\
{									\
	if (oidp->oid_arg1 != NULL) {					\
		if (*(u_int *)(oidp->oid_arg1) <= (min))		\
			*(u_int *)(oidp->oid_arg1) = (min);		\
		else if (*(u_int *)(oidp->oid_arg1) > (max))		\
			*(u_int *)(oidp->oid_arg1) = (max);		\
	}								\
	return (sysctl_handle_int(oidp, oidp->oid_arg1, oidp->oid_arg2,	\
		req));							\
}
#endif /* SYSCTL_DECL */

MALLOC_DECLARE(M_ENTROPY);

#endif /* _KERNEL */

struct harvest_event;

typedef void random_alg_init_t(void *);
typedef void random_alg_deinit_t(void *);
typedef void random_alg_pre_read_t(void);
typedef void random_alg_read_t(uint8_t *, u_int);
typedef bool random_alg_seeded_t(void);
typedef void random_alg_reseed_t(void);
typedef void random_alg_eventprocessor_t(struct harvest_event *);

typedef u_int random_source_read_t(void *, u_int);

/*
 * Random Algorithm is a processor of randomness for the kernel
 * and for userland.
 */
struct random_algorithm {
	const char			*ra_ident;
	u_int				 ra_poolcount;
	void				(*ra_init_alg)(void *);
	void				(*ra_deinit_alg)(void *);
	random_alg_pre_read_t		*ra_pre_read;
	random_alg_read_t		*ra_read;
	random_alg_seeded_t		*ra_seeded;
	random_alg_eventprocessor_t	*ra_event_processor;
};

extern struct random_algorithm random_alg_context, *p_random_alg_context;

#ifdef _KERNEL

/*
 * Random Source is a source of entropy that can provide
 * specified or approximate amount of entropy immediately
 * upon request.
 */
struct random_source {
	const char			*rs_ident;
	enum random_entropy_source	 rs_source;
	random_source_read_t		*rs_read;
};

struct random_sources {
	LIST_ENTRY(random_sources)	 rrs_entries;
	struct random_source		*rrs_source;
};

LIST_HEAD(sources_head, random_sources);
extern struct sources_head source_list;

void random_source_register(struct random_source *);
void random_source_deregister(struct random_source *);

#if defined(RANDOM_LOADABLE)
extern struct sx randomdev_config_lock;
#define	RANDOM_CONFIG_INIT_LOCK(x)	sx_init(&randomdev_config_lock, "configuration change lock")
#define	RANDOM_CONFIG_X_LOCK(x)		sx_xlock(&randomdev_config_lock)
#define	RANDOM_CONFIG_X_UNLOCK(x)	sx_xunlock(&randomdev_config_lock)
#define	RANDOM_CONFIG_S_LOCK(x)		sx_slock(&randomdev_config_lock)
#define	RANDOM_CONFIG_S_UNLOCK(x)	sx_sunlock(&randomdev_config_lock)
#define	RANDOM_CONFIG_DEINIT_LOCK(x)	sx_destroy(&randomdev_config_lock)
void random_infra_init(int (*)(struct uio *, bool), u_int (*)(void *, u_int));
void random_infra_uninit(void);
#endif

#endif /* _KERNEL */

void randomdev_unblock(void);

#endif /* SYS_DEV_RANDOM_RANDOMDEV_H_INCLUDED */
