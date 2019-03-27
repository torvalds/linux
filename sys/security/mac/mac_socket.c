/*-
 * Copyright (c) 1999-2002, 2009 Robert N. M. Watson
 * Copyright (c) 2001 Ilmar S. Habibulin
 * Copyright (c) 2001-2005 Networks Associates Technology, Inc.
 * Copyright (c) 2005-2006 SPARTA, Inc.
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson and Ilmar Habibulin for the
 * TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by McAfee
 * Research, the Technology Research Division of Network Associates, Inc.
 * under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
 * DARPA CHATS research program.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_mac.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/mac.h>
#include <sys/sbuf.h>
#include <sys/sdt.h>
#include <sys/systm.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/namei.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>

#include <net/bpfdesc.h>
#include <net/if.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>

#include <security/mac/mac_framework.h>
#include <security/mac/mac_internal.h>
#include <security/mac/mac_policy.h>

/*
 * Currently, sockets hold two labels: the label of the socket itself, and a
 * peer label, which may be used by policies to hold a copy of the label of
 * any remote endpoint.
 *
 * Possibly, this peer label should be maintained at the protocol layer
 * (inpcb, unpcb, etc), as this would allow protocol-aware code to maintain
 * the label consistently.  For example, it might be copied live from a
 * remote socket for UNIX domain sockets rather than keeping a local copy on
 * this endpoint, but be cached and updated based on packets received for
 * TCP/IP.
 *
 * Unlike with many other object types, the lock protecting MAC labels on
 * sockets (the socket lock) is not frequently held at the points in code
 * where socket-related checks are called.  The MAC Framework acquires the
 * lock over some entry points in order to enforce atomicity (such as label
 * copies) but in other cases the policy modules will have to acquire the
 * lock themselves if they use labels.  This approach (a) avoids lock
 * acquisitions when policies don't require labels and (b) solves a number of
 * potential lock order issues when multiple sockets are used in the same
 * entry point.
 */

struct label *
mac_socket_label_alloc(int flag)
{
	struct label *label;
	int error;

	label = mac_labelzone_alloc(flag);
	if (label == NULL)
		return (NULL);

	if (flag & M_WAITOK)
		MAC_POLICY_CHECK(socket_init_label, label, flag);
	else
		MAC_POLICY_CHECK_NOSLEEP(socket_init_label, label, flag);
	if (error) {
		MAC_POLICY_PERFORM_NOSLEEP(socket_destroy_label, label);
		mac_labelzone_free(label);
		return (NULL);
	}
	return (label);
}

static struct label *
mac_socketpeer_label_alloc(int flag)
{
	struct label *label;
	int error;

	label = mac_labelzone_alloc(flag);
	if (label == NULL)
		return (NULL);

	if (flag & M_WAITOK)
		MAC_POLICY_CHECK(socketpeer_init_label, label, flag);
	else
		MAC_POLICY_CHECK_NOSLEEP(socketpeer_init_label, label, flag);
	if (error) {
		MAC_POLICY_PERFORM_NOSLEEP(socketpeer_destroy_label, label);
		mac_labelzone_free(label);
		return (NULL);
	}
	return (label);
}

int
mac_socket_init(struct socket *so, int flag)
{

	if (mac_labeled & MPC_OBJECT_SOCKET) {
		so->so_label = mac_socket_label_alloc(flag);
		if (so->so_label == NULL)
			return (ENOMEM);
		so->so_peerlabel = mac_socketpeer_label_alloc(flag);
		if (so->so_peerlabel == NULL) {
			mac_socket_label_free(so->so_label);
			so->so_label = NULL;
			return (ENOMEM);
		}
	} else {
		so->so_label = NULL;
		so->so_peerlabel = NULL;
	}
	return (0);
}

void
mac_socket_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(socket_destroy_label, label);
	mac_labelzone_free(label);
}

static void
mac_socketpeer_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(socketpeer_destroy_label, label);
	mac_labelzone_free(label);
}

void
mac_socket_destroy(struct socket *so)
{

	if (so->so_label != NULL) {
		mac_socket_label_free(so->so_label);
		so->so_label = NULL;
		mac_socketpeer_label_free(so->so_peerlabel);
		so->so_peerlabel = NULL;
	}
}

void
mac_socket_copy_label(struct label *src, struct label *dest)
{

	MAC_POLICY_PERFORM_NOSLEEP(socket_copy_label, src, dest);
}

int
mac_socket_externalize_label(struct label *label, char *elements,
    char *outbuf, size_t outbuflen)
{
	int error;

	MAC_POLICY_EXTERNALIZE(socket, label, elements, outbuf, outbuflen);

	return (error);
}

static int
mac_socketpeer_externalize_label(struct label *label, char *elements,
    char *outbuf, size_t outbuflen)
{
	int error;

	MAC_POLICY_EXTERNALIZE(socketpeer, label, elements, outbuf,
	    outbuflen);

	return (error);
}

int
mac_socket_internalize_label(struct label *label, char *string)
{
	int error;

	MAC_POLICY_INTERNALIZE(socket, label, string);

	return (error);
}

void
mac_socket_create(struct ucred *cred, struct socket *so)
{

	MAC_POLICY_PERFORM_NOSLEEP(socket_create, cred, so, so->so_label);
}

void
mac_socket_newconn(struct socket *oldso, struct socket *newso)
{

	MAC_POLICY_PERFORM_NOSLEEP(socket_newconn, oldso, oldso->so_label,
	    newso, newso->so_label);
}

static void
mac_socket_relabel(struct ucred *cred, struct socket *so,
    struct label *newlabel)
{

	SOCK_LOCK_ASSERT(so);

	MAC_POLICY_PERFORM_NOSLEEP(socket_relabel, cred, so, so->so_label,
	    newlabel);
}

void
mac_socketpeer_set_from_mbuf(struct mbuf *m, struct socket *so)
{
	struct label *label;

	if (mac_policy_count == 0)
		return;

	label = mac_mbuf_to_label(m);

	MAC_POLICY_PERFORM_NOSLEEP(socketpeer_set_from_mbuf, m, label, so,
	    so->so_peerlabel);
}

void
mac_socketpeer_set_from_socket(struct socket *oldso, struct socket *newso)
{
	
	if (mac_policy_count == 0)
		return;

	MAC_POLICY_PERFORM_NOSLEEP(socketpeer_set_from_socket, oldso,
	    oldso->so_label, newso, newso->so_peerlabel);
}

void
mac_socket_create_mbuf(struct socket *so, struct mbuf *m)
{
	struct label *label;

	if (mac_policy_count == 0)
		return;

	label = mac_mbuf_to_label(m);

	MAC_POLICY_PERFORM_NOSLEEP(socket_create_mbuf, so, so->so_label, m,
	    label);
}

MAC_CHECK_PROBE_DEFINE2(socket_check_accept, "struct ucred *",
    "struct socket *");

int
mac_socket_check_accept(struct ucred *cred, struct socket *so)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(socket_check_accept, cred, so,
	    so->so_label);
	MAC_CHECK_PROBE2(socket_check_accept, error, cred, so);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(socket_check_bind, "struct ucred *",
    "struct socket *", "struct sockaddr *");

int
mac_socket_check_bind(struct ucred *cred, struct socket *so,
    struct sockaddr *sa)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(socket_check_bind, cred, so, so->so_label,
	    sa);
	MAC_CHECK_PROBE3(socket_check_bind, error, cred, so, sa);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(socket_check_connect, "struct ucred *",
    "struct socket *", "struct sockaddr *");

int
mac_socket_check_connect(struct ucred *cred, struct socket *so,
    struct sockaddr *sa)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(socket_check_connect, cred, so,
	    so->so_label, sa);
	MAC_CHECK_PROBE3(socket_check_connect, error, cred, so, sa);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(socket_check_create, "struct ucred *", "int", "int",
    "int");

int
mac_socket_check_create(struct ucred *cred, int domain, int type, int proto)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(socket_check_create, cred, domain, type,
	    proto);
	MAC_CHECK_PROBE4(socket_check_create, error, cred, domain, type,
	    proto);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(socket_check_deliver, "struct socket *",
    "struct mbuf *");

int
mac_socket_check_deliver(struct socket *so, struct mbuf *m)
{
	struct label *label;
	int error;

	if (mac_policy_count == 0)
		return (0);

	label = mac_mbuf_to_label(m);

	MAC_POLICY_CHECK_NOSLEEP(socket_check_deliver, so, so->so_label, m,
	    label);
	MAC_CHECK_PROBE2(socket_check_deliver, error, so, m);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(socket_check_listen, "struct ucred *",
    "struct socket *");

int
mac_socket_check_listen(struct ucred *cred, struct socket *so)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(socket_check_listen, cred, so,
	    so->so_label);
	MAC_CHECK_PROBE2(socket_check_listen, error, cred, so);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(socket_check_poll, "struct ucred *",
    "struct socket *");

int
mac_socket_check_poll(struct ucred *cred, struct socket *so)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(socket_check_poll, cred, so, so->so_label);
	MAC_CHECK_PROBE2(socket_check_poll, error, cred, so);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(socket_check_receive, "struct ucred *",
    "struct socket *");

int
mac_socket_check_receive(struct ucred *cred, struct socket *so)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(socket_check_receive, cred, so,
	    so->so_label);
	MAC_CHECK_PROBE2(socket_check_receive, error, cred, so);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(socket_check_relabel, "struct ucred *",
    "struct socket *", "struct label *");

static int
mac_socket_check_relabel(struct ucred *cred, struct socket *so,
    struct label *newlabel)
{
	int error;

	SOCK_LOCK_ASSERT(so);

	MAC_POLICY_CHECK_NOSLEEP(socket_check_relabel, cred, so,
	    so->so_label, newlabel);
	MAC_CHECK_PROBE3(socket_check_relabel, error, cred, so, newlabel);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(socket_check_send, "struct ucred *",
    "struct socket *");

int
mac_socket_check_send(struct ucred *cred, struct socket *so)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(socket_check_send, cred, so, so->so_label);
	MAC_CHECK_PROBE2(socket_check_send, error, cred, so);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(socket_check_stat, "struct ucred *",
    "struct socket *");

int
mac_socket_check_stat(struct ucred *cred, struct socket *so)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(socket_check_stat, cred, so, so->so_label);
	MAC_CHECK_PROBE2(socket_check_stat, error, cred, so);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(socket_check_visible, "struct ucred *",
    "struct socket *");

int
mac_socket_check_visible(struct ucred *cred, struct socket *so)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(socket_check_visible, cred, so,
	    so->so_label);
	MAC_CHECK_PROBE2(socket_check_visible, error, cred, so);

	return (error);
}

int
mac_socket_label_set(struct ucred *cred, struct socket *so,
    struct label *label)
{
	int error;

	/*
	 * We acquire the socket lock when we perform the test and set, but
	 * have to release it as the pcb code needs to acquire the pcb lock,
	 * which will precede the socket lock in the lock order.  However,
	 * this is fine, as any race will simply result in the inpcb being
	 * refreshed twice, but still consistently, as the inpcb code will
	 * acquire the socket lock before refreshing, holding both locks.
	 */
	SOCK_LOCK(so);
	error = mac_socket_check_relabel(cred, so, label);
	if (error) {
		SOCK_UNLOCK(so);
		return (error);
	}

	mac_socket_relabel(cred, so, label);
	SOCK_UNLOCK(so);

	/*
	 * If the protocol has expressed interest in socket layer changes,
	 * such as if it needs to propagate changes to a cached pcb label
	 * from the socket, notify it of the label change while holding the
	 * socket lock.
	 */
	if (so->so_proto->pr_usrreqs->pru_sosetlabel != NULL)
		(so->so_proto->pr_usrreqs->pru_sosetlabel)(so);

	return (0);
}

int
mac_setsockopt_label(struct ucred *cred, struct socket *so, struct mac *mac)
{
	struct label *intlabel;
	char *buffer;
	int error;

	if (!(mac_labeled & MPC_OBJECT_SOCKET))
		return (EINVAL);

	error = mac_check_structmac_consistent(mac);
	if (error)
		return (error);

	buffer = malloc(mac->m_buflen, M_MACTEMP, M_WAITOK);
	error = copyinstr(mac->m_string, buffer, mac->m_buflen, NULL);
	if (error) {
		free(buffer, M_MACTEMP);
		return (error);
	}

	intlabel = mac_socket_label_alloc(M_WAITOK);
	error = mac_socket_internalize_label(intlabel, buffer);
	free(buffer, M_MACTEMP);
	if (error)
		goto out;

	error = mac_socket_label_set(cred, so, intlabel);
out:
	mac_socket_label_free(intlabel);
	return (error);
}

int
mac_getsockopt_label(struct ucred *cred, struct socket *so, struct mac *mac)
{
	char *buffer, *elements;
	struct label *intlabel;
	int error;

	if (!(mac_labeled & MPC_OBJECT_SOCKET))
		return (EINVAL);

	error = mac_check_structmac_consistent(mac);
	if (error)
		return (error);

	elements = malloc(mac->m_buflen, M_MACTEMP, M_WAITOK);
	error = copyinstr(mac->m_string, elements, mac->m_buflen, NULL);
	if (error) {
		free(elements, M_MACTEMP);
		return (error);
	}

	buffer = malloc(mac->m_buflen, M_MACTEMP, M_WAITOK | M_ZERO);
	intlabel = mac_socket_label_alloc(M_WAITOK);
	SOCK_LOCK(so);
	mac_socket_copy_label(so->so_label, intlabel);
	SOCK_UNLOCK(so);
	error = mac_socket_externalize_label(intlabel, elements, buffer,
	    mac->m_buflen);
	mac_socket_label_free(intlabel);
	if (error == 0)
		error = copyout(buffer, mac->m_string, strlen(buffer)+1);

	free(buffer, M_MACTEMP);
	free(elements, M_MACTEMP);

	return (error);
}

int
mac_getsockopt_peerlabel(struct ucred *cred, struct socket *so,
    struct mac *mac)
{
	char *elements, *buffer;
	struct label *intlabel;
	int error;

	if (!(mac_labeled & MPC_OBJECT_SOCKET))
		return (EINVAL);

	error = mac_check_structmac_consistent(mac);
	if (error)
		return (error);

	elements = malloc(mac->m_buflen, M_MACTEMP, M_WAITOK);
	error = copyinstr(mac->m_string, elements, mac->m_buflen, NULL);
	if (error) {
		free(elements, M_MACTEMP);
		return (error);
	}

	buffer = malloc(mac->m_buflen, M_MACTEMP, M_WAITOK | M_ZERO);
	intlabel = mac_socket_label_alloc(M_WAITOK);
	SOCK_LOCK(so);
	mac_socket_copy_label(so->so_peerlabel, intlabel);
	SOCK_UNLOCK(so);
	error = mac_socketpeer_externalize_label(intlabel, elements, buffer,
	    mac->m_buflen);
	mac_socket_label_free(intlabel);
	if (error == 0)
		error = copyout(buffer, mac->m_string, strlen(buffer)+1);

	free(buffer, M_MACTEMP);
	free(elements, M_MACTEMP);

	return (error);
}
