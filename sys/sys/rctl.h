/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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

/*
 * Resource Limits.
 */

#ifndef _RCTL_H_
#define	_RCTL_H_

#include <sys/cdefs.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/_task.h>

struct proc;
struct uidinfo;
struct loginclass;
struct prison_racct;
struct ucred;
struct rctl_rule_link;

#ifdef _KERNEL

/*
 * Rules describe an action to be taken when conditions defined
 * in the rule are met.  There is no global list of rules; instead,
 * rules are linked to by the racct structures for all the subjects
 * they apply to - for example, a rule of type "user" is linked to the
 * appropriate struct uidinfo, and to all the processes of that user.
 *
 * 'rr_refcount' is equal to the number of rctl_rule_link structures
 * pointing to the rule.
 *
 * This structure must never change after being added, via rctl_rule_link
 * structures, to subjects.  In order to change a rule, add a new rule
 * and remove the previous one.
 */
struct rctl_rule {
	int		rr_subject_type;
	union {
		struct proc		*rs_proc;
		struct uidinfo		*rs_uip;
		struct loginclass	*rs_loginclass;
		struct prison_racct	*rs_prison_racct;
	} rr_subject;
	int		rr_per;
	int		rr_resource;
	int		rr_action;
	int64_t		rr_amount;
	u_int		rr_refcount;
	struct task	rr_task;
};

/*
 * Allowed values for rr_subject_type and rr_per fields.
 */
#define	RCTL_SUBJECT_TYPE_UNDEFINED	-1
#define	RCTL_SUBJECT_TYPE_PROCESS	0x0000
#define	RCTL_SUBJECT_TYPE_USER		0x0001
#define	RCTL_SUBJECT_TYPE_LOGINCLASS	0x0003
#define	RCTL_SUBJECT_TYPE_JAIL		0x0004
#define	RCTL_SUBJECT_TYPE_MAX		RCTL_SUBJECT_TYPE_JAIL

/*
 * Allowed values for rr_action field.
 */
#define	RCTL_ACTION_UNDEFINED		-1
#define	RCTL_ACTION_SIGHUP		SIGHUP
#define	RCTL_ACTION_SIGINT		SIGINT
#define	RCTL_ACTION_SIGQUIT		SIGQUIT
#define	RCTL_ACTION_SIGILL		SIGILL
#define	RCTL_ACTION_SIGTRAP		SIGTRAP
#define	RCTL_ACTION_SIGABRT		SIGABRT
#define	RCTL_ACTION_SIGEMT		SIGEMT
#define	RCTL_ACTION_SIGFPE		SIGFPE
#define	RCTL_ACTION_SIGKILL		SIGKILL
#define	RCTL_ACTION_SIGBUS		SIGBUS
#define	RCTL_ACTION_SIGSEGV		SIGSEGV
#define	RCTL_ACTION_SIGSYS		SIGSYS
#define	RCTL_ACTION_SIGPIPE		SIGPIPE
#define	RCTL_ACTION_SIGALRM		SIGALRM
#define	RCTL_ACTION_SIGTERM		SIGTERM
#define	RCTL_ACTION_SIGURG		SIGURG
#define	RCTL_ACTION_SIGSTOP		SIGSTOP
#define	RCTL_ACTION_SIGTSTP		SIGTSTP
#define	RCTL_ACTION_SIGCHLD		SIGCHLD
#define	RCTL_ACTION_SIGTTIN		SIGTTIN
#define	RCTL_ACTION_SIGTTOU		SIGTTOU
#define	RCTL_ACTION_SIGIO		SIGIO
#define	RCTL_ACTION_SIGXCPU		SIGXCPU
#define	RCTL_ACTION_SIGXFSZ		SIGXFSZ
#define	RCTL_ACTION_SIGVTALRM		SIGVTALRM
#define	RCTL_ACTION_SIGPROF		SIGPROF
#define	RCTL_ACTION_SIGWINCH		SIGWINCH
#define	RCTL_ACTION_SIGINFO		SIGINFO
#define	RCTL_ACTION_SIGUSR1		SIGUSR1
#define	RCTL_ACTION_SIGUSR2		SIGUSR2
#define	RCTL_ACTION_SIGTHR		SIGTHR
#define	RCTL_ACTION_SIGNAL_MAX		RCTL_ACTION_SIGTHR
#define	RCTL_ACTION_DENY		(RCTL_ACTION_SIGNAL_MAX + 1)
#define	RCTL_ACTION_LOG			(RCTL_ACTION_SIGNAL_MAX + 2)
#define	RCTL_ACTION_DEVCTL		(RCTL_ACTION_SIGNAL_MAX + 3)
#define	RCTL_ACTION_THROTTLE		(RCTL_ACTION_SIGNAL_MAX + 4)
#define	RCTL_ACTION_MAX			RCTL_ACTION_THROTTLE

#define	RCTL_AMOUNT_UNDEFINED		-1

struct rctl_rule *rctl_rule_alloc(int flags);
struct rctl_rule *rctl_rule_duplicate(const struct rctl_rule *rule, int flags);
void	rctl_rule_acquire(struct rctl_rule *rule);
void	rctl_rule_release(struct rctl_rule *rule);
int	rctl_rule_add(struct rctl_rule *rule);
int	rctl_rule_remove(struct rctl_rule *filter);
int	rctl_enforce(struct proc *p, int resource, uint64_t amount);
void	rctl_throttle_decay(struct racct *racct, int resource);
int64_t	rctl_pcpu_available(const struct proc *p);
uint64_t rctl_get_limit(struct proc *p, int resource);
uint64_t rctl_get_available(struct proc *p, int resource);
const char *rctl_resource_name(int resource);
void	rctl_proc_ucred_changed(struct proc *p, struct ucred *newcred);
int	rctl_proc_fork(struct proc *parent, struct proc *child);
void	rctl_racct_release(struct racct *racct);
#else /* !_KERNEL */

/*
 * Syscall interface.
 */
__BEGIN_DECLS
int	rctl_get_racct(const char *inbufp, size_t inbuflen, char *outbufp,
	    size_t outbuflen);
int	rctl_get_rules(const char *inbufp, size_t inbuflen, char *outbufp,
	    size_t outbuflen);
int	rctl_get_limits(const char *inbufp, size_t inbuflen, char *outbufp,
	    size_t outbuflen);
int	rctl_add_rule(const char *inbufp, size_t inbuflen, char *outbufp,
	    size_t outbuflen);
int	rctl_remove_rule(const char *inbufp, size_t inbuflen, char *outbufp,
	    size_t outbuflen);
__END_DECLS

#endif /* !_KERNEL */

#endif /* !_RCTL_H_ */
