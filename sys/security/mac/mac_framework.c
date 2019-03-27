/*-
 * Copyright (c) 1999-2002, 2006, 2009 Robert N. M. Watson
 * Copyright (c) 2001 Ilmar S. Habibulin
 * Copyright (c) 2001-2005 Networks Associates Technology, Inc.
 * Copyright (c) 2005-2006 SPARTA, Inc.
 * Copyright (c) 2008-2009 Apple Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson and Ilmar Habibulin for the
 * TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract 
 * N66001-04-C-6019 ("SEFOS").
 *
 * This software was developed at the University of Cambridge Computer
 * Laboratory with support from a grant from Google, Inc.
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

/*-
 * Framework for extensible kernel access control.  This file contains core
 * kernel infrastructure for the TrustedBSD MAC Framework, including policy
 * registration, versioning, locking, error composition operator, and system
 * calls.
 *
 * The MAC Framework implements three programming interfaces:
 *
 * - The kernel MAC interface, defined in mac_framework.h, and invoked
 *   throughout the kernel to request security decisions, notify of security
 *   related events, etc.
 *
 * - The MAC policy module interface, defined in mac_policy.h, which is
 *   implemented by MAC policy modules and invoked by the MAC Framework to
 *   forward kernel security requests and notifications to policy modules.
 *
 * - The user MAC API, defined in mac.h, which allows user programs to query
 *   and set label state on objects.
 *
 * The majority of the MAC Framework implementation may be found in
 * src/sys/security/mac.  Sample policy modules may be found in
 * src/sys/security/mac_*.
 */

#include "opt_mac.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/condvar.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mac.h>
#include <sys/module.h>
#include <sys/rmlock.h>
#include <sys/sdt.h>
#include <sys/sx.h>
#include <sys/sysctl.h>

#include <security/mac/mac_framework.h>
#include <security/mac/mac_internal.h>
#include <security/mac/mac_policy.h>

/*
 * DTrace SDT providers for MAC.
 */
SDT_PROVIDER_DEFINE(mac);
SDT_PROVIDER_DEFINE(mac_framework);

SDT_PROBE_DEFINE2(mac, , policy, modevent, "int",
    "struct mac_policy_conf *");
SDT_PROBE_DEFINE1(mac, , policy, register,
    "struct mac_policy_conf *");
SDT_PROBE_DEFINE1(mac, , policy, unregister,
    "struct mac_policy_conf *");

/*
 * Root sysctl node for all MAC and MAC policy controls.
 */
SYSCTL_NODE(_security, OID_AUTO, mac, CTLFLAG_RW, 0,
    "TrustedBSD MAC policy controls");

/*
 * Declare that the kernel provides MAC support, version 3 (FreeBSD 7.x).
 * This permits modules to refuse to be loaded if the necessary support isn't
 * present, even if it's pre-boot.
 */
MODULE_VERSION(kernel_mac_support, MAC_VERSION);

static unsigned int	mac_version = MAC_VERSION;
SYSCTL_UINT(_security_mac, OID_AUTO, version, CTLFLAG_RD, &mac_version, 0,
    "");

/*
 * Labels consist of a indexed set of "slots", which are allocated policies
 * as required.  The MAC Framework maintains a bitmask of slots allocated so
 * far to prevent reuse.  Slots cannot be reused, as the MAC Framework
 * guarantees that newly allocated slots in labels will be NULL unless
 * otherwise initialized, and because we do not have a mechanism to garbage
 * collect slots on policy unload.  As labeled policies tend to be statically
 * loaded during boot, and not frequently unloaded and reloaded, this is not
 * generally an issue.
 */
#if MAC_MAX_SLOTS > 32
#error "MAC_MAX_SLOTS too large"
#endif

static unsigned int mac_max_slots = MAC_MAX_SLOTS;
static unsigned int mac_slot_offsets_free = (1 << MAC_MAX_SLOTS) - 1;
SYSCTL_UINT(_security_mac, OID_AUTO, max_slots, CTLFLAG_RD, &mac_max_slots,
    0, "");

/*
 * Has the kernel started generating labeled objects yet?  All read/write
 * access to this variable is serialized during the boot process.  Following
 * the end of serialization, we don't update this flag; no locking.
 */
static int	mac_late = 0;

/*
 * Each policy declares a mask of object types requiring labels to be
 * allocated for them.  For convenience, we combine and cache the bitwise or
 * of the per-policy object flags to track whether we will allocate a label
 * for an object type at run-time.
 */
uint64_t	mac_labeled;
SYSCTL_UQUAD(_security_mac, OID_AUTO, labeled, CTLFLAG_RD, &mac_labeled, 0,
    "Mask of object types being labeled");

MALLOC_DEFINE(M_MACTEMP, "mactemp", "MAC temporary label storage");

/*
 * MAC policy modules are placed in one of two lists: mac_static_policy_list,
 * for policies that are loaded early and cannot be unloaded, and
 * mac_policy_list, which holds policies either loaded later in the boot
 * cycle or that may be unloaded.  The static policy list does not require
 * locks to iterate over, but the dynamic list requires synchronization.
 * Support for dynamic policy loading can be compiled out using the
 * MAC_STATIC kernel option.
 *
 * The dynamic policy list is protected by two locks: modifying the list
 * requires both locks to be held exclusively.  One of the locks,
 * mac_policy_rm, is acquired over policy entry points that will never sleep;
 * the other, mac_policy_sx, is acquire over policy entry points that may
 * sleep.  The former category will be used when kernel locks may be held
 * over calls to the MAC Framework, during network processing in ithreads,
 * etc.  The latter will tend to involve potentially blocking memory
 * allocations, extended attribute I/O, etc.
 */
#ifndef MAC_STATIC
static struct rmlock mac_policy_rm;	/* Non-sleeping entry points. */
static struct sx mac_policy_sx;		/* Sleeping entry points. */
#endif

struct mac_policy_list_head mac_policy_list;
struct mac_policy_list_head mac_static_policy_list;
u_int mac_policy_count;			/* Registered policy count. */

static void	mac_policy_xlock(void);
static void	mac_policy_xlock_assert(void);
static void	mac_policy_xunlock(void);

void
mac_policy_slock_nosleep(struct rm_priotracker *tracker)
{

#ifndef MAC_STATIC
	if (!mac_late)
		return;

	rm_rlock(&mac_policy_rm, tracker);
#endif
}

void
mac_policy_slock_sleep(void)
{

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
 	    "mac_policy_slock_sleep");

#ifndef MAC_STATIC
	if (!mac_late)
		return;

	sx_slock(&mac_policy_sx);
#endif
}

void
mac_policy_sunlock_nosleep(struct rm_priotracker *tracker)
{

#ifndef MAC_STATIC
	if (!mac_late)
		return;

	rm_runlock(&mac_policy_rm, tracker);
#endif
}

void
mac_policy_sunlock_sleep(void)
{

#ifndef MAC_STATIC
	if (!mac_late)
		return;

	sx_sunlock(&mac_policy_sx);
#endif
}

static void
mac_policy_xlock(void)
{

	WITNESS_WARN(WARN_GIANTOK | WARN_SLEEPOK, NULL,
 	    "mac_policy_xlock()");

#ifndef MAC_STATIC
	if (!mac_late)
		return;

	sx_xlock(&mac_policy_sx);
	rm_wlock(&mac_policy_rm);
#endif
}

static void
mac_policy_xunlock(void)
{

#ifndef MAC_STATIC
	if (!mac_late)
		return;

	rm_wunlock(&mac_policy_rm);
	sx_xunlock(&mac_policy_sx);
#endif
}

static void
mac_policy_xlock_assert(void)
{

#ifndef MAC_STATIC
	if (!mac_late)
		return;

	/* XXXRW: rm_assert(&mac_policy_rm, RA_WLOCKED); */
	sx_assert(&mac_policy_sx, SA_XLOCKED);
#endif
}

/*
 * Initialize the MAC subsystem, including appropriate SMP locks.
 */
static void
mac_init(void)
{

	LIST_INIT(&mac_static_policy_list);
	LIST_INIT(&mac_policy_list);
	mac_labelzone_init();

#ifndef MAC_STATIC
	rm_init_flags(&mac_policy_rm, "mac_policy_rm", RM_NOWITNESS |
	    RM_RECURSE);
	sx_init_flags(&mac_policy_sx, "mac_policy_sx", SX_NOWITNESS);
#endif
}

/*
 * For the purposes of modules that want to know if they were loaded "early",
 * set the mac_late flag once we've processed modules either linked into the
 * kernel, or loaded before the kernel startup.
 */
static void
mac_late_init(void)
{

	mac_late = 1;
}

/*
 * Given a policy, derive from its set of non-NULL label init methods what
 * object types the policy is interested in.
 */
static uint64_t
mac_policy_getlabeled(struct mac_policy_conf *mpc)
{
	uint64_t labeled;

#define	MPC_FLAG(method, flag)					\
	if (mpc->mpc_ops->mpo_ ## method != NULL)			\
		labeled |= (flag);					\

	labeled = 0;
	MPC_FLAG(cred_init_label, MPC_OBJECT_CRED);
	MPC_FLAG(proc_init_label, MPC_OBJECT_PROC);
	MPC_FLAG(vnode_init_label, MPC_OBJECT_VNODE);
	MPC_FLAG(inpcb_init_label, MPC_OBJECT_INPCB);
	MPC_FLAG(socket_init_label, MPC_OBJECT_SOCKET);
	MPC_FLAG(devfs_init_label, MPC_OBJECT_DEVFS);
	MPC_FLAG(mbuf_init_label, MPC_OBJECT_MBUF);
	MPC_FLAG(ipq_init_label, MPC_OBJECT_IPQ);
	MPC_FLAG(ifnet_init_label, MPC_OBJECT_IFNET);
	MPC_FLAG(bpfdesc_init_label, MPC_OBJECT_BPFDESC);
	MPC_FLAG(pipe_init_label, MPC_OBJECT_PIPE);
	MPC_FLAG(mount_init_label, MPC_OBJECT_MOUNT);
	MPC_FLAG(posixsem_init_label, MPC_OBJECT_POSIXSEM);
	MPC_FLAG(posixshm_init_label, MPC_OBJECT_POSIXSHM);
	MPC_FLAG(sysvmsg_init_label, MPC_OBJECT_SYSVMSG);
	MPC_FLAG(sysvmsq_init_label, MPC_OBJECT_SYSVMSQ);
	MPC_FLAG(sysvsem_init_label, MPC_OBJECT_SYSVSEM);
	MPC_FLAG(sysvshm_init_label, MPC_OBJECT_SYSVSHM);
	MPC_FLAG(syncache_init_label, MPC_OBJECT_SYNCACHE);
	MPC_FLAG(ip6q_init_label, MPC_OBJECT_IP6Q);

#undef MPC_FLAG
	return (labeled);
}

/*
 * When policies are loaded or unloaded, walk the list of registered policies
 * and built mac_labeled, a bitmask representing the union of all objects
 * requiring labels across all policies.
 */
static void
mac_policy_update(void)
{
	struct mac_policy_conf *mpc;

	mac_policy_xlock_assert();

	mac_labeled = 0;
	mac_policy_count = 0;
	LIST_FOREACH(mpc, &mac_static_policy_list, mpc_list) {
		mac_labeled |= mac_policy_getlabeled(mpc);
		mac_policy_count++;
	}
	LIST_FOREACH(mpc, &mac_policy_list, mpc_list) {
		mac_labeled |= mac_policy_getlabeled(mpc);
		mac_policy_count++;
	}
}

static int
mac_policy_register(struct mac_policy_conf *mpc)
{
	struct mac_policy_conf *tmpc;
	int error, slot, static_entry;

	error = 0;

	/*
	 * We don't technically need exclusive access while !mac_late, but
	 * hold it for assertion consistency.
	 */
	mac_policy_xlock();

	/*
	 * If the module can potentially be unloaded, or we're loading late,
	 * we have to stick it in the non-static list and pay an extra
	 * performance overhead.  Otherwise, we can pay a light locking cost
	 * and stick it in the static list.
	 */
	static_entry = (!mac_late &&
	    !(mpc->mpc_loadtime_flags & MPC_LOADTIME_FLAG_UNLOADOK));

	if (static_entry) {
		LIST_FOREACH(tmpc, &mac_static_policy_list, mpc_list) {
			if (strcmp(tmpc->mpc_name, mpc->mpc_name) == 0) {
				error = EEXIST;
				goto out;
			}
		}
	} else {
		LIST_FOREACH(tmpc, &mac_policy_list, mpc_list) {
			if (strcmp(tmpc->mpc_name, mpc->mpc_name) == 0) {
				error = EEXIST;
				goto out;
			}
		}
	}
	if (mpc->mpc_field_off != NULL) {
		slot = ffs(mac_slot_offsets_free);
		if (slot == 0) {
			error = ENOMEM;
			goto out;
		}
		slot--;
		mac_slot_offsets_free &= ~(1 << slot);
		*mpc->mpc_field_off = slot;
	}
	mpc->mpc_runtime_flags |= MPC_RUNTIME_FLAG_REGISTERED;

	/*
	 * If we're loading a MAC module after the framework has initialized,
	 * it has to go into the dynamic list.  If we're loading it before
	 * we've finished initializing, it can go into the static list with
	 * weaker locker requirements.
	 */
	if (static_entry)
		LIST_INSERT_HEAD(&mac_static_policy_list, mpc, mpc_list);
	else
		LIST_INSERT_HEAD(&mac_policy_list, mpc, mpc_list);

	/*
	 * Per-policy initialization.  Currently, this takes place under the
	 * exclusive lock, so policies must not sleep in their init method.
	 * In the future, we may want to separate "init" from "start", with
	 * "init" occurring without the lock held.  Likewise, on tear-down,
	 * breaking out "stop" from "destroy".
	 */
	if (mpc->mpc_ops->mpo_init != NULL)
		(*(mpc->mpc_ops->mpo_init))(mpc);
	mac_policy_update();

	SDT_PROBE1(mac, , policy, register, mpc);
	printf("Security policy loaded: %s (%s)\n", mpc->mpc_fullname,
	    mpc->mpc_name);

out:
	mac_policy_xunlock();
	return (error);
}

static int
mac_policy_unregister(struct mac_policy_conf *mpc)
{

	/*
	 * If we fail the load, we may get a request to unload.  Check to see
	 * if we did the run-time registration, and if not, silently succeed.
	 */
	mac_policy_xlock();
	if ((mpc->mpc_runtime_flags & MPC_RUNTIME_FLAG_REGISTERED) == 0) {
		mac_policy_xunlock();
		return (0);
	}
#if 0
	/*
	 * Don't allow unloading modules with private data.
	 */
	if (mpc->mpc_field_off != NULL) {
		mac_policy_xunlock();
		return (EBUSY);
	}
#endif
	/*
	 * Only allow the unload to proceed if the module is unloadable by
	 * its own definition.
	 */
	if ((mpc->mpc_loadtime_flags & MPC_LOADTIME_FLAG_UNLOADOK) == 0) {
		mac_policy_xunlock();
		return (EBUSY);
	}
	if (mpc->mpc_ops->mpo_destroy != NULL)
		(*(mpc->mpc_ops->mpo_destroy))(mpc);

	LIST_REMOVE(mpc, mpc_list);
	mpc->mpc_runtime_flags &= ~MPC_RUNTIME_FLAG_REGISTERED;
	mac_policy_update();
	mac_policy_xunlock();

	SDT_PROBE1(mac, , policy, unregister, mpc);
	printf("Security policy unload: %s (%s)\n", mpc->mpc_fullname,
	    mpc->mpc_name);

	return (0);
}

/*
 * Allow MAC policy modules to register during boot, etc.
 */
int
mac_policy_modevent(module_t mod, int type, void *data)
{
	struct mac_policy_conf *mpc;
	int error;

	error = 0;
	mpc = (struct mac_policy_conf *) data;

#ifdef MAC_STATIC
	if (mac_late) {
		printf("mac_policy_modevent: MAC_STATIC and late\n");
		return (EBUSY);
	}
#endif

	SDT_PROBE2(mac, , policy, modevent, type, mpc);
	switch (type) {
	case MOD_LOAD:
		if (mpc->mpc_loadtime_flags & MPC_LOADTIME_FLAG_NOTLATE &&
		    mac_late) {
			printf("mac_policy_modevent: can't load %s policy "
			    "after booting\n", mpc->mpc_name);
			error = EBUSY;
			break;
		}
		error = mac_policy_register(mpc);
		break;
	case MOD_UNLOAD:
		/* Don't unregister the module if it was never registered. */
		if ((mpc->mpc_runtime_flags & MPC_RUNTIME_FLAG_REGISTERED)
		    != 0)
			error = mac_policy_unregister(mpc);
		else
			error = 0;
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

/*
 * Define an error value precedence, and given two arguments, selects the
 * value with the higher precedence.
 */
int
mac_error_select(int error1, int error2)
{

	/* Certain decision-making errors take top priority. */
	if (error1 == EDEADLK || error2 == EDEADLK)
		return (EDEADLK);

	/* Invalid arguments should be reported where possible. */
	if (error1 == EINVAL || error2 == EINVAL)
		return (EINVAL);

	/* Precedence goes to "visibility", with both process and file. */
	if (error1 == ESRCH || error2 == ESRCH)
		return (ESRCH);

	if (error1 == ENOENT || error2 == ENOENT)
		return (ENOENT);

	/* Precedence goes to DAC/MAC protections. */
	if (error1 == EACCES || error2 == EACCES)
		return (EACCES);

	/* Precedence goes to privilege. */
	if (error1 == EPERM || error2 == EPERM)
		return (EPERM);

	/* Precedence goes to error over success; otherwise, arbitrary. */
	if (error1 != 0)
		return (error1);
	return (error2);
}

int
mac_check_structmac_consistent(struct mac *mac)
{

	/* Require that labels have a non-zero length. */
	if (mac->m_buflen > MAC_MAX_LABEL_BUF_LEN ||
	    mac->m_buflen <= sizeof(""))
		return (EINVAL);

	return (0);
}

SYSINIT(mac, SI_SUB_MAC, SI_ORDER_FIRST, mac_init, NULL);
SYSINIT(mac_late, SI_SUB_MAC_LATE, SI_ORDER_FIRST, mac_late_init, NULL);
