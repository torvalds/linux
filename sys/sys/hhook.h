/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010,2013 Lawrence Stewart <lstewart@freebsd.org>
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Lawrence Stewart while studying at the Centre
 * for Advanced Internet Architectures, Swinburne University of Technology, made
 * possible in part by grants from the FreeBSD Foundation and Cisco University
 * Research Program Fund at Community Foundation Silicon Valley.
 *
 * Portions of this software were developed at the Centre for Advanced
 * Internet Architectures, Swinburne University of Technology, Melbourne,
 * Australia by Lawrence Stewart under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * A KPI modelled on the pfil framework for instantiating helper hook points
 * within the kernel for use by Khelp modules. Originally released as part of
 * the NewTCP research project at Swinburne University of Technology's Centre
 * for Advanced Internet Architectures, Melbourne, Australia, which was made
 * possible in part by a grant from the Cisco University Research Program Fund
 * at Community Foundation Silicon Valley. More details are available at:
 *   http://caia.swin.edu.au/urp/newtcp/
 */

#ifndef _SYS_HHOOK_H_
#define _SYS_HHOOK_H_

/* XXXLAS: Is there a way around this? */
#include <sys/lock.h>
#include <sys/rmlock.h>

/* hhook_head flags. */
#define	HHH_ISINVNET		0x00000001 /* Is the hook point in a vnet? */

/* Flags common to  all register functions. */
#define	HHOOK_WAITOK		0x00000001 /* Sleeping allowed. */
#define	HHOOK_NOWAIT		0x00000002 /* Sleeping disallowed. */
/* Flags only relevant to hhook_head_register() and hhook_head_is_virtual(). */
#define	HHOOK_HEADISINVNET	0x00000100 /* Public proxy for HHH_ISINVNET. */

/* Helper hook types. */
#define	HHOOK_TYPE_TCP		1
#define	HHOOK_TYPE_SOCKET	2
#define	HHOOK_TYPE_IPSEC_IN	3
#define	HHOOK_TYPE_IPSEC_OUT	4

struct helper;
struct osd;

/* Signature for helper hook functions. */
typedef int (*hhook_func_t)(int32_t hhook_type, int32_t hhook_id, void *udata,
    void *ctx_data, void *hdata, struct osd *hosd);

/*
 * Information required to add/remove a helper hook function to/from a helper
 * hook point.
 */
struct hookinfo {
	hhook_func_t	hook_func;
	struct helper	*hook_helper;
	void		*hook_udata;
	int32_t		hook_id;
	int32_t		hook_type;
};

/*
 * Ideally this would be private but we need access to the hhh_nhooks member
 * variable in order to make the HHOOKS_RUN_IF() macro low impact.
 */
struct hhook_head {
	STAILQ_HEAD(hhook_list, hhook)	hhh_hooks;
	struct rmlock			hhh_lock;
	uintptr_t			hhh_vid;
	int32_t				hhh_id;
	int32_t				hhh_nhooks;
	int32_t				hhh_type;
	uint32_t			hhh_flags;
	volatile uint32_t		hhh_refcount;
	LIST_ENTRY(hhook_head)		hhh_next;
	LIST_ENTRY(hhook_head)		hhh_vnext;
};

/* Public KPI functions. */
void	hhook_run_hooks(struct hhook_head *hhh, void *ctx_data, struct osd *hosd);

int	hhook_add_hook(struct hhook_head *hhh, struct hookinfo *hki,
    uint32_t flags);

int	hhook_add_hook_lookup(struct hookinfo *hki, uint32_t flags);

int	hhook_remove_hook(struct hhook_head *hhh, struct hookinfo *hki);

int	hhook_remove_hook_lookup(struct hookinfo *hki);

int	hhook_head_register(int32_t hhook_type, int32_t hhook_id,
    struct hhook_head **hhh, uint32_t flags);

int	hhook_head_deregister(struct hhook_head *hhh);

int	hhook_head_deregister_lookup(int32_t hhook_type, int32_t hhook_id);

struct hhook_head * hhook_head_get(int32_t hhook_type, int32_t hhook_id);

void	hhook_head_release(struct hhook_head *hhh);

uint32_t hhook_head_is_virtualised(struct hhook_head *hhh);

uint32_t hhook_head_is_virtualised_lookup(int32_t hook_type, int32_t hook_id);

/*
 * A wrapper around hhook_run_hooks() that only calls the function if at least
 * one helper hook function is registered for the specified helper hook point.
 */
#define	HHOOKS_RUN_IF(hhh, ctx_data, hosd) do {				\
	if (hhh != NULL && hhh->hhh_nhooks > 0)				\
		hhook_run_hooks(hhh, ctx_data, hosd);			\
} while (0)

/*
 * WARNING: This macro should only be used in code paths that execute
 * infrequently, otherwise the refcounting overhead would be excessive.
 *
 * A similar wrapper to HHOOKS_RUN_IF() for situations where the caller prefers
 * not to lookup and store the appropriate hhook_head pointer themselves.
 */
#define	HHOOKS_RUN_LOOKUP_IF(hhook_type, hhook_id, ctx_data, hosd) do {	\
	struct hhook_head *_hhh;					\
									\
	_hhh = hhook_head_get(hhook_type, hhook_id);			\
	if (_hhh != NULL) {						\
		if (_hhh->hhh_nhooks > 0)				\
			hhook_run_hooks(_hhh, ctx_data, hosd);		\
		hhook_head_release(_hhh);				\
	}								\
} while (0)

#endif /* _SYS_HHOOK_H_ */
