/*-
 * Copyright (c) 1999-2002, 2006, 2009 Robert N. M. Watson
 * Copyright (c) 2001 Ilmar S. Habibulin
 * Copyright (c) 2001-2004 Networks Associates Technology, Inc.
 * Copyright (c) 2006 nCircle Network Security, Inc.
 * Copyright (c) 2006 SPARTA, Inc.
 * Copyright (c) 2009 Apple, Inc.
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
 * This software was developed by Robert N. M. Watson for the TrustedBSD
 * Project under contract to nCircle Network Security, Inc.
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
 *
 * $FreeBSD$
 */

#ifndef _SECURITY_MAC_MAC_INTERNAL_H_
#define	_SECURITY_MAC_MAC_INTERNAL_H_

#ifndef _KERNEL
#error "no user-serviceable parts inside"
#endif

#include <sys/lock.h>
#include <sys/rmlock.h>

/*
 * MAC Framework sysctl namespace.
 */
#ifdef SYSCTL_DECL
SYSCTL_DECL(_security_mac);
#endif /* SYSCTL_DECL */

/*
 * MAC Framework SDT DTrace probe namespace, macros for declaring entry
 * point probes, macros for invoking them.
 */
#ifdef SDT_PROVIDER_DECLARE
SDT_PROVIDER_DECLARE(mac);		/* MAC Framework-level events. */
SDT_PROVIDER_DECLARE(mac_framework);	/* Entry points to MAC. */

#define	MAC_CHECK_PROBE_DEFINE4(name, arg0, arg1, arg2, arg3)		\
	SDT_PROBE_DEFINE5(mac_framework, , name, mac__check__err,	\
	    "int", arg0, arg1, arg2, arg3);				\
	SDT_PROBE_DEFINE5(mac_framework, , name, mac__check__ok,	\
	    "int", arg0, arg1, arg2, arg3);

#define	MAC_CHECK_PROBE_DEFINE3(name, arg0, arg1, arg2)			\
	SDT_PROBE_DEFINE4(mac_framework, , name, mac__check__err,	\
	    "int", arg0, arg1, arg2);					\
	SDT_PROBE_DEFINE4(mac_framework, , name, mac__check__ok,	\
	    "int", arg0, arg1, arg2);

#define	MAC_CHECK_PROBE_DEFINE2(name, arg0, arg1)			\
	SDT_PROBE_DEFINE3(mac_framework, , name, mac__check__err,	\
	    "int", arg0, arg1);						\
	SDT_PROBE_DEFINE3(mac_framework, , name, mac__check__ok,	\
	    "int", arg0, arg1);

#define	MAC_CHECK_PROBE_DEFINE1(name, arg0)				\
	SDT_PROBE_DEFINE2(mac_framework, , name, mac__check__err,	\
	    "int", arg0);						\
	SDT_PROBE_DEFINE2(mac_framework, , name, mac__check__ok,	\
	    "int", arg0);

#define	MAC_CHECK_PROBE4(name, error, arg0, arg1, arg2, arg3)	do {	\
	if (SDT_PROBES_ENABLED()) {					\
		if (error) {						\
			SDT_PROBE5(mac_framework, , name, mac__check__err,\
			    error, arg0, arg1, arg2, arg3);		\
		} else {						\
			SDT_PROBE5(mac_framework, , name, mac__check__ok,\
			    0, arg0, arg1, arg2, arg3);			\
		}							\
	}								\
} while (0)

#define	MAC_CHECK_PROBE3(name, error, arg0, arg1, arg2)			\
	MAC_CHECK_PROBE4(name, error, arg0, arg1, arg2, 0)
#define	MAC_CHECK_PROBE2(name, error, arg0, arg1)			\
	MAC_CHECK_PROBE3(name, error, arg0, arg1, 0)
#define	MAC_CHECK_PROBE1(name, error, arg0)				\
	MAC_CHECK_PROBE2(name, error, arg0, 0)
#endif

#define	MAC_GRANT_PROBE_DEFINE2(name, arg0, arg1)			\
	SDT_PROBE_DEFINE3(mac_framework, , name, mac__grant__err,	\
	    "int", arg0, arg1);						\
	SDT_PROBE_DEFINE3(mac_framework, , name, mac__grant__ok,	\
	    "int", arg0, arg1);

#define	MAC_GRANT_PROBE2(name, error, arg0, arg1)	do {		\
	if (SDT_PROBES_ENABLED()) {					\
		if (error) {						\
			SDT_PROBE3(mac_framework, , name, mac__grant__err,\
			    error, arg0, arg1);				\
		} else {						\
			SDT_PROBE3(mac_framework, , name, mac__grant__ok,\
			    error, arg0, arg1);				\
		}							\
	}								\
} while (0)

/*
 * MAC Framework global types and typedefs.
 */
LIST_HEAD(mac_policy_list_head, mac_policy_conf);
#ifdef MALLOC_DECLARE
MALLOC_DECLARE(M_MACTEMP);
#endif

/*
 * MAC labels -- in-kernel storage format.
 *
 * In general, struct label pointers are embedded in kernel data structures
 * representing objects that may be labeled (and protected).  Struct label is
 * opaque to both kernel services that invoke the MAC Framework and MAC
 * policy modules.  In particular, we do not wish to encode the layout of the
 * label structure into any ABIs.  Historically, the slot array contained
 * unions of {long, void} but now contains uintptr_t.
 */
#define	MAC_MAX_SLOTS	4
#define	MAC_FLAG_INITIALIZED	0x0000001	/* Is initialized for use. */
struct label {
	int		l_flags;
	intptr_t	l_perpolicy[MAC_MAX_SLOTS];
};


/*
 * Flags for mac_labeled, a bitmask of object types need across the union of
 * all policies currently registered with the MAC Framework, used to key
 * whether or not labels are allocated and constructors for the type are
 * invoked.
 */
#define	MPC_OBJECT_CRED			0x0000000000000001
#define	MPC_OBJECT_PROC			0x0000000000000002
#define	MPC_OBJECT_VNODE		0x0000000000000004
#define	MPC_OBJECT_INPCB		0x0000000000000008
#define	MPC_OBJECT_SOCKET		0x0000000000000010
#define	MPC_OBJECT_DEVFS		0x0000000000000020
#define	MPC_OBJECT_MBUF			0x0000000000000040
#define	MPC_OBJECT_IPQ			0x0000000000000080
#define	MPC_OBJECT_IFNET		0x0000000000000100
#define	MPC_OBJECT_BPFDESC		0x0000000000000200
#define	MPC_OBJECT_PIPE			0x0000000000000400
#define	MPC_OBJECT_MOUNT		0x0000000000000800
#define	MPC_OBJECT_POSIXSEM		0x0000000000001000
#define	MPC_OBJECT_POSIXSHM		0x0000000000002000
#define	MPC_OBJECT_SYSVMSG		0x0000000000004000
#define	MPC_OBJECT_SYSVMSQ		0x0000000000008000
#define	MPC_OBJECT_SYSVSEM		0x0000000000010000
#define	MPC_OBJECT_SYSVSHM		0x0000000000020000
#define	MPC_OBJECT_SYNCACHE		0x0000000000040000
#define	MPC_OBJECT_IP6Q			0x0000000000080000

/*
 * MAC Framework global variables.
 */
extern struct mac_policy_list_head	mac_policy_list;
extern struct mac_policy_list_head	mac_static_policy_list;
extern u_int				mac_policy_count;
extern uint64_t				mac_labeled;
extern struct mtx			mac_ifnet_mtx;

/*
 * MAC Framework infrastructure functions.
 */
int	mac_error_select(int error1, int error2);

void	mac_policy_slock_nosleep(struct rm_priotracker *tracker);
void	mac_policy_slock_sleep(void);
void	mac_policy_sunlock_nosleep(struct rm_priotracker *tracker);
void	mac_policy_sunlock_sleep(void);

struct label	*mac_labelzone_alloc(int flags);
void		 mac_labelzone_free(struct label *label);
void		 mac_labelzone_init(void);

void	mac_init_label(struct label *label);
void	mac_destroy_label(struct label *label);
int	mac_check_structmac_consistent(struct mac *mac);
int	mac_allocate_slot(void);

#define MAC_IFNET_LOCK(ifp)	mtx_lock(&mac_ifnet_mtx)
#define MAC_IFNET_UNLOCK(ifp)	mtx_unlock(&mac_ifnet_mtx)

/*
 * MAC Framework per-object type functions.  It's not yet clear how the
 * namespaces, etc, should work for these, so for now, sort by object type.
 */
struct label	*mac_cred_label_alloc(void);
void		 mac_cred_label_free(struct label *label);
struct label	*mac_pipe_label_alloc(void);
void		 mac_pipe_label_free(struct label *label);
struct label	*mac_socket_label_alloc(int flag);
void		 mac_socket_label_free(struct label *label);
struct label	*mac_vnode_label_alloc(void);
void		 mac_vnode_label_free(struct label *label);

int	mac_cred_check_relabel(struct ucred *cred, struct label *newlabel);
int	mac_cred_externalize_label(struct label *label, char *elements,
	    char *outbuf, size_t outbuflen);
int	mac_cred_internalize_label(struct label *label, char *string);
void	mac_cred_relabel(struct ucred *cred, struct label *newlabel);

struct label	*mac_mbuf_to_label(struct mbuf *m);

void	mac_pipe_copy_label(struct label *src, struct label *dest);
int	mac_pipe_externalize_label(struct label *label, char *elements,
	    char *outbuf, size_t outbuflen);
int	mac_pipe_internalize_label(struct label *label, char *string);

int	mac_socket_label_set(struct ucred *cred, struct socket *so,
	    struct label *label);
void	mac_socket_copy_label(struct label *src, struct label *dest);
int	mac_socket_externalize_label(struct label *label, char *elements,
	    char *outbuf, size_t outbuflen);
int	mac_socket_internalize_label(struct label *label, char *string);

int	mac_vnode_externalize_label(struct label *label, char *elements,
	    char *outbuf, size_t outbuflen);
int	mac_vnode_internalize_label(struct label *label, char *string);
void	mac_vnode_check_mmap_downgrade(struct ucred *cred, struct vnode *vp,
	    int *prot);
int	vn_setlabel(struct vnode *vp, struct label *intlabel,
	    struct ucred *cred);

/*
 * MAC Framework composition macros invoke all registered MAC policies for a
 * specific entry point.  They come in two forms: one which permits policies
 * to sleep/block, and another that does not.
 *
 * MAC_POLICY_CHECK performs the designated check by walking the policy
 * module list and checking with each as to how it feels about the request.
 * Note that it returns its value via 'error' in the scope of the caller.
 */
#define	MAC_POLICY_CHECK(check, args...) do {				\
	struct mac_policy_conf *mpc;					\
									\
	error = 0;							\
	LIST_FOREACH(mpc, &mac_static_policy_list, mpc_list) {		\
		if (mpc->mpc_ops->mpo_ ## check != NULL)		\
			error = mac_error_select(			\
			    mpc->mpc_ops->mpo_ ## check (args),		\
			    error);					\
	}								\
	if (!LIST_EMPTY(&mac_policy_list)) {				\
		mac_policy_slock_sleep();				\
		LIST_FOREACH(mpc, &mac_policy_list, mpc_list) {		\
			if (mpc->mpc_ops->mpo_ ## check != NULL)	\
				error = mac_error_select(		\
				    mpc->mpc_ops->mpo_ ## check (args),	\
				    error);				\
		}							\
		mac_policy_sunlock_sleep();				\
	}								\
} while (0)

#define	MAC_POLICY_CHECK_NOSLEEP(check, args...) do {			\
	struct mac_policy_conf *mpc;					\
									\
	error = 0;							\
	LIST_FOREACH(mpc, &mac_static_policy_list, mpc_list) {		\
		if (mpc->mpc_ops->mpo_ ## check != NULL)		\
			error = mac_error_select(			\
			    mpc->mpc_ops->mpo_ ## check (args),		\
			    error);					\
	}								\
	if (!LIST_EMPTY(&mac_policy_list)) {				\
		struct rm_priotracker tracker;				\
									\
		mac_policy_slock_nosleep(&tracker);			\
		LIST_FOREACH(mpc, &mac_policy_list, mpc_list) {		\
			if (mpc->mpc_ops->mpo_ ## check != NULL)	\
				error = mac_error_select(		\
				    mpc->mpc_ops->mpo_ ## check (args),	\
				    error);				\
		}							\
		mac_policy_sunlock_nosleep(&tracker);			\
	}								\
} while (0)

/*
 * MAC_POLICY_GRANT performs the designated check by walking the policy
 * module list and checking with each as to how it feels about the request.
 * Unlike MAC_POLICY_CHECK, it grants if any policies return '0', and
 * otherwise returns EPERM.  Note that it returns its value via 'error' in
 * the scope of the caller.
 */
#define	MAC_POLICY_GRANT_NOSLEEP(check, args...) do {			\
	struct mac_policy_conf *mpc;					\
									\
	error = EPERM;							\
	LIST_FOREACH(mpc, &mac_static_policy_list, mpc_list) {		\
		if (mpc->mpc_ops->mpo_ ## check != NULL) {		\
			if (mpc->mpc_ops->mpo_ ## check(args) == 0)	\
				error = 0;				\
		}							\
	}								\
	if (!LIST_EMPTY(&mac_policy_list)) {				\
		struct rm_priotracker tracker;				\
									\
		mac_policy_slock_nosleep(&tracker);			\
		LIST_FOREACH(mpc, &mac_policy_list, mpc_list) {		\
			if (mpc->mpc_ops->mpo_ ## check != NULL) {	\
				if (mpc->mpc_ops->mpo_ ## check (args)	\
				    == 0)				\
					error = 0;			\
			}						\
		}							\
		mac_policy_sunlock_nosleep(&tracker);			\
	}								\
} while (0)

/*
 * MAC_POLICY_BOOLEAN performs the designated boolean composition by walking
 * the module list, invoking each instance of the operation, and combining
 * the results using the passed C operator.  Note that it returns its value
 * via 'result' in the scope of the caller, which should be initialized by
 * the caller in a meaningful way to get a meaningful result.
 */
#define	MAC_POLICY_BOOLEAN(operation, composition, args...) do {	\
	struct mac_policy_conf *mpc;					\
									\
	LIST_FOREACH(mpc, &mac_static_policy_list, mpc_list) {		\
		if (mpc->mpc_ops->mpo_ ## operation != NULL)		\
			result = result composition			\
			    mpc->mpc_ops->mpo_ ## operation (args);	\
	}								\
	if (!LIST_EMPTY(&mac_policy_list)) {				\
		mac_policy_slock_sleep();				\
		LIST_FOREACH(mpc, &mac_policy_list, mpc_list) {		\
			if (mpc->mpc_ops->mpo_ ## operation != NULL)	\
				result = result composition		\
				    mpc->mpc_ops->mpo_ ## operation	\
				    (args);				\
		}							\
		mac_policy_sunlock_sleep();				\
	}								\
} while (0)

#define	MAC_POLICY_BOOLEAN_NOSLEEP(operation, composition, args...) do {\
	struct mac_policy_conf *mpc;					\
									\
	LIST_FOREACH(mpc, &mac_static_policy_list, mpc_list) {		\
		if (mpc->mpc_ops->mpo_ ## operation != NULL)		\
			result = result composition			\
			    mpc->mpc_ops->mpo_ ## operation (args);	\
	}								\
	if (!LIST_EMPTY(&mac_policy_list)) {				\
		struct rm_priotracker tracker;				\
									\
		mac_policy_slock_nosleep(&tracker);			\
		LIST_FOREACH(mpc, &mac_policy_list, mpc_list) {		\
			if (mpc->mpc_ops->mpo_ ## operation != NULL)	\
				result = result composition		\
				    mpc->mpc_ops->mpo_ ## operation	\
				    (args);				\
		}							\
		mac_policy_sunlock_nosleep(&tracker);			\
	}								\
} while (0)

/*
 * MAC_POLICY_EXTERNALIZE queries each policy to see if it can generate an
 * externalized version of a label element by name.  Policies declare whether
 * they have matched a particular element name, parsed from the string by
 * MAC_POLICY_EXTERNALIZE, and an error is returned if any element is matched
 * by no policy.
 */
#define	MAC_POLICY_EXTERNALIZE(type, label, elementlist, outbuf, 	\
    outbuflen) do {							\
	int claimed, first, ignorenotfound, savedlen;			\
	char *element_name, *element_temp;				\
	struct sbuf sb;							\
									\
	error = 0;							\
	first = 1;							\
	sbuf_new(&sb, outbuf, outbuflen, SBUF_FIXEDLEN);		\
	element_temp = elementlist;					\
	while ((element_name = strsep(&element_temp, ",")) != NULL) {	\
		if (element_name[0] == '?') {				\
			element_name++;					\
			ignorenotfound = 1;				\
		 } else							\
			ignorenotfound = 0;				\
		savedlen = sbuf_len(&sb);				\
		if (first)						\
			error = sbuf_printf(&sb, "%s/", element_name);	\
		else							\
			error = sbuf_printf(&sb, ",%s/", element_name);	\
		if (error == -1) {					\
			error = EINVAL;	/* XXX: E2BIG? */		\
			break;						\
		}							\
		claimed = 0;						\
		MAC_POLICY_CHECK(type ## _externalize_label, label,	\
		    element_name, &sb, &claimed);			\
		if (error)						\
			break;						\
		if (claimed == 0 && ignorenotfound) {			\
			/* Revert last label name. */			\
			sbuf_setpos(&sb, savedlen);			\
		} else if (claimed != 1) {				\
			error = EINVAL;	/* XXX: ENOLABEL? */		\
			break;						\
		} else {						\
			first = 0;					\
		}							\
	}								\
	sbuf_finish(&sb);						\
} while (0)

/*
 * MAC_POLICY_INTERNALIZE presents parsed element names and data to each
 * policy to see if any is willing to claim it and internalize the label
 * data.  If no policies match, an error is returned.
 */
#define	MAC_POLICY_INTERNALIZE(type, label, instring) do {		\
	char *element, *element_name, *element_data;			\
	int claimed;							\
									\
	error = 0;							\
	element = instring;						\
	while ((element_name = strsep(&element, ",")) != NULL) {	\
		element_data = element_name;				\
		element_name = strsep(&element_data, "/");		\
		if (element_data == NULL) {				\
			error = EINVAL;					\
			break;						\
		}							\
		claimed = 0;						\
		MAC_POLICY_CHECK(type ## _internalize_label, label,	\
		    element_name, element_data, &claimed);		\
		if (error)						\
			break;						\
		if (claimed != 1) {					\
			/* XXXMAC: Another error here? */		\
			error = EINVAL;					\
			break;						\
		}							\
	}								\
} while (0)

/*
 * MAC_POLICY_PERFORM performs the designated operation by walking the policy
 * module list and invoking that operation for each policy.
 */
#define	MAC_POLICY_PERFORM(operation, args...) do {			\
	struct mac_policy_conf *mpc;					\
									\
	LIST_FOREACH(mpc, &mac_static_policy_list, mpc_list) {		\
		if (mpc->mpc_ops->mpo_ ## operation != NULL)		\
			mpc->mpc_ops->mpo_ ## operation (args);		\
	}								\
	if (!LIST_EMPTY(&mac_policy_list)) {				\
		mac_policy_slock_sleep();				\
		LIST_FOREACH(mpc, &mac_policy_list, mpc_list) {		\
			if (mpc->mpc_ops->mpo_ ## operation != NULL)	\
				mpc->mpc_ops->mpo_ ## operation (args);	\
		}							\
		mac_policy_sunlock_sleep();				\
	}								\
} while (0)

#define	MAC_POLICY_PERFORM_NOSLEEP(operation, args...) do {		\
	struct mac_policy_conf *mpc;					\
									\
	LIST_FOREACH(mpc, &mac_static_policy_list, mpc_list) {		\
		if (mpc->mpc_ops->mpo_ ## operation != NULL)		\
			mpc->mpc_ops->mpo_ ## operation (args);		\
	}								\
	if (!LIST_EMPTY(&mac_policy_list)) {				\
		struct rm_priotracker tracker;				\
									\
		mac_policy_slock_nosleep(&tracker);			\
		LIST_FOREACH(mpc, &mac_policy_list, mpc_list) {		\
			if (mpc->mpc_ops->mpo_ ## operation != NULL)	\
				mpc->mpc_ops->mpo_ ## operation (args);	\
		}							\
		mac_policy_sunlock_nosleep(&tracker);			\
	}								\
} while (0)

#endif /* !_SECURITY_MAC_MAC_INTERNAL_H_ */
