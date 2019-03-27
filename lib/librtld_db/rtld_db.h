/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Rui Paulo under sponsorship from the
 * FreeBSD Foundation.
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
 *
 * $FreeBSD$
 */

#ifndef _RTLD_DB_H_
#define _RTLD_DB_H_

#include <sys/param.h>

#define	RD_VERSION	1

typedef enum {
	RD_OK,
	RD_ERR,
	RD_DBERR,
	RD_NOCAPAB,
	RD_NODYNAM,
	RD_NOBASE,
	RD_NOMAPS
} rd_err_e;

/* XXX struct rd_agent should be private. */
struct procstat;

typedef struct rd_agent {
	struct proc_handle *rda_php;

	uintptr_t rda_dlactivity_addr;
	uintptr_t rda_preinit_addr;
	uintptr_t rda_postinit_addr;

	struct procstat *rda_procstat;
} rd_agent_t;

typedef struct rd_loadobj {
	uintptr_t	rdl_saddr;		/* start address */
	uintptr_t	rdl_eaddr;		/* end address */
	uint32_t	rdl_offset;
	uint8_t		rdl_prot;
#define RD_RDL_R	0x01
#define RD_RDL_W	0x02
#define RD_RDL_X	0x04
	enum {
		RDL_TYPE_NONE	= 0,
		RDL_TYPE_DEF,
		RDL_TYPE_VNODE,
		RDL_TYPE_SWAP,
		RDL_TYPE_DEV,
		/* XXX some types missing */
		RDL_TYPE_UNKNOWN = 255
	} rdl_type;
	unsigned char	rdl_path[PATH_MAX];
} rd_loadobj_t;

typedef enum {
	RD_NONE = 0,
	RD_PREINIT,
	RD_POSTINIT,
	RD_DLACTIVITY
} rd_event_e;

typedef enum {
	RD_NOTIFY_BPT,
	RD_NOTIFY_AUTOBPT,
	RD_NOTIFY_SYSCALL
} rd_notify_e;

typedef struct rd_notify {
	rd_notify_e type;
	union {
		uintptr_t bptaddr;
		long      syscallno;
	} u;
} rd_notify_t;

typedef enum {
	RD_NOSTATE = 0,
	RD_CONSISTENT,
	RD_ADD,
	RD_DELETE
} rd_state_e;

typedef struct rd_event_msg {
	rd_event_e type;
	union {
		rd_state_e state;
	} u;
} rd_event_msg_t;

typedef enum {
	RD_RESOLVE_NONE,
	RD_RESOLVE_STEP,
	RD_RESOLVE_TARGET,
	RD_RESOLVE_TARGET_STEP
} rd_skip_e;

typedef struct rd_plt_info {
	rd_skip_e pi_skip_method;
	long	  pi_nstep;
	uintptr_t pi_target;
	uintptr_t pi_baddr;
	unsigned int pi_flags;
} rd_plt_info_t;

#define RD_FLG_PI_PLTBOUND	0x0001

__BEGIN_DECLS

struct proc_handle;
void		rd_delete(rd_agent_t *);
const char 	*rd_errstr(rd_err_e);
rd_err_e	rd_event_addr(rd_agent_t *, rd_event_e, rd_notify_t *);
rd_err_e	rd_event_enable(rd_agent_t *, int);
rd_err_e	rd_event_getmsg(rd_agent_t *, rd_event_msg_t *);
rd_err_e	rd_init(int);
typedef int rl_iter_f(const rd_loadobj_t *, void *);
rd_err_e	rd_loadobj_iter(rd_agent_t *, rl_iter_f *, void *);
void		rd_log(const int);
rd_agent_t 	*rd_new(struct proc_handle *);
rd_err_e	rd_objpad_enable(rd_agent_t *, size_t);
struct proc;
rd_err_e	rd_plt_resolution(rd_agent_t *, uintptr_t, struct proc *,
		    uintptr_t, rd_plt_info_t *);
rd_err_e	rd_reset(rd_agent_t *);

__END_DECLS

#endif /* _RTLD_DB_H_ */
