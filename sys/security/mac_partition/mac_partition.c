/*-
 * Copyright (c) 1999-2002, 2007-2008 Robert N. M. Watson
 * Copyright (c) 2001-2002 Networks Associates Technology, Inc.
 * Copyright (c) 2006 SPARTA, Inc.
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
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
 * Developed by the TrustedBSD Project.
 *
 * Experiment with a partition-like model.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_pcb.h>

#include <security/mac/mac_policy.h>
#include <security/mac_partition/mac_partition.h>

SYSCTL_DECL(_security_mac);

static SYSCTL_NODE(_security_mac, OID_AUTO, partition, CTLFLAG_RW, 0,
    "TrustedBSD mac_partition policy controls");

static int	partition_enabled = 1;
SYSCTL_INT(_security_mac_partition, OID_AUTO, enabled, CTLFLAG_RW,
    &partition_enabled, 0, "Enforce partition policy");

static int	partition_slot;
#define	SLOT(l)	mac_label_get((l), partition_slot)
#define	SLOT_SET(l, v)	mac_label_set((l), partition_slot, (v))

static int
partition_check(struct label *subject, struct label *object)
{

	if (partition_enabled == 0)
		return (0);

	if (subject == NULL)
		return (0);

	if (SLOT(subject) == 0)
		return (0);

	/*
	 * If the object label hasn't been allocated, then it's effectively
	 * not in a partition, and we know the subject is as it has a label
	 * and it's not 0, so reject.
	 */
	if (object == NULL)
		return (EPERM);

	if (SLOT(subject) == SLOT(object))
		return (0);

	return (EPERM);
}

/*
 * Object-specific entry points are sorted alphabetically by object type name
 * and then by operation.
 */
static int
partition_cred_check_relabel(struct ucred *cred, struct label *newlabel)
{
	int error;

	error = 0;

	/*
	 * Treat "0" as a no-op request because it reflects an unset
	 * partition label.  If we ever want to support switching back to an
	 * unpartitioned state for a process, we'll need to differentiate the
	 * "not in a partition" and "no partition defined during internalize"
	 * conditions.
	 */
	if (SLOT(newlabel) != 0) {
		/*
		 * Require BSD privilege in order to change the partition.
		 * Originally we also required that the process not be in a
		 * partition in the first place, but this didn't interact
		 * well with sendmail.
		 */
		error = priv_check_cred(cred, PRIV_MAC_PARTITION);
	}

	return (error);
}

static int
partition_cred_check_visible(struct ucred *cr1, struct ucred *cr2)
{
	int error;

	error = partition_check(cr1->cr_label, cr2->cr_label);

	return (error == 0 ? 0 : ESRCH);
}

static void
partition_cred_copy_label(struct label *src, struct label *dest)
{

	if (src != NULL && dest != NULL)
		SLOT_SET(dest, SLOT(src));
	else if (dest != NULL)
		SLOT_SET(dest, 0);
}

static void
partition_cred_create_init(struct ucred *cred)
{

	SLOT_SET(cred->cr_label, 0);
}

static void
partition_cred_create_swapper(struct ucred *cred)
{

	SLOT_SET(cred->cr_label, 0);
}

static void
partition_cred_destroy_label(struct label *label)
{

	SLOT_SET(label, 0);
}

static int
partition_cred_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{

	if (strcmp(MAC_PARTITION_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;

	if (label != NULL) {
		if (sbuf_printf(sb, "%jd", (intmax_t)SLOT(label)) == -1)
			return (EINVAL);
	} else {
		if (sbuf_printf(sb, "0") == -1)
			return (EINVAL);
	}
	return (0);
}

static void
partition_cred_init_label(struct label *label)
{

	SLOT_SET(label, 0);
}

static int
partition_cred_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{

	if (strcmp(MAC_PARTITION_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;
	SLOT_SET(label, strtol(element_data, NULL, 10));
	return (0);
}

static void
partition_cred_relabel(struct ucred *cred, struct label *newlabel)
{

	if (newlabel != NULL && SLOT(newlabel) != 0)
		SLOT_SET(cred->cr_label, SLOT(newlabel));
}

static int
partition_inpcb_check_visible(struct ucred *cred, struct inpcb *inp,
    struct label *inplabel)
{
	int error;

	error = partition_check(cred->cr_label, inp->inp_cred->cr_label);

	return (error ? ENOENT : 0);
}

static int
partition_proc_check_debug(struct ucred *cred, struct proc *p)
{
	int error;

	error = partition_check(cred->cr_label, p->p_ucred->cr_label);

	return (error ? ESRCH : 0);
}

static int
partition_proc_check_sched(struct ucred *cred, struct proc *p)
{
	int error;

	error = partition_check(cred->cr_label, p->p_ucred->cr_label);

	return (error ? ESRCH : 0);
}

static int
partition_proc_check_signal(struct ucred *cred, struct proc *p,
    int signum)
{
	int error;

	error = partition_check(cred->cr_label, p->p_ucred->cr_label);

	return (error ? ESRCH : 0);
}

static int
partition_socket_check_visible(struct ucred *cred, struct socket *so,
    struct label *solabel)
{
	int error;

	error = partition_check(cred->cr_label, so->so_cred->cr_label);

	return (error ? ENOENT : 0);
}

static int
partition_vnode_check_exec(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct image_params *imgp,
    struct label *execlabel)
{

	if (execlabel != NULL) {
		/*
		 * We currently don't permit labels to be changed at
		 * exec-time as part of the partition model, so disallow
		 * non-NULL partition label changes in execlabel.
		 */
		if (SLOT(execlabel) != 0)
			return (EINVAL);
	}

	return (0);
}

static struct mac_policy_ops partition_ops =
{
	.mpo_cred_check_relabel = partition_cred_check_relabel,
	.mpo_cred_check_visible = partition_cred_check_visible,
	.mpo_cred_copy_label = partition_cred_copy_label,
	.mpo_cred_create_init = partition_cred_create_init,
	.mpo_cred_create_swapper = partition_cred_create_swapper,
	.mpo_cred_destroy_label = partition_cred_destroy_label,
	.mpo_cred_externalize_label = partition_cred_externalize_label,
	.mpo_cred_init_label = partition_cred_init_label,
	.mpo_cred_internalize_label = partition_cred_internalize_label,
	.mpo_cred_relabel = partition_cred_relabel,
	.mpo_inpcb_check_visible = partition_inpcb_check_visible,
	.mpo_proc_check_debug = partition_proc_check_debug,
	.mpo_proc_check_sched = partition_proc_check_sched,
	.mpo_proc_check_signal = partition_proc_check_signal,
	.mpo_socket_check_visible = partition_socket_check_visible,
	.mpo_vnode_check_exec = partition_vnode_check_exec,
};

MAC_POLICY_SET(&partition_ops, mac_partition, "TrustedBSD MAC/Partition",
    MPC_LOADTIME_FLAG_UNLOADOK, &partition_slot);
