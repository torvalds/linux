/*-
 * Copyright (c) 1999-2002, 2009 Robert N. M. Watson
 * Copyright (c) 2001 Ilmar S. Habibulin
 * Copyright (c) 2001-2004 Networks Associates Technology, Inc.
 * Copyright (c) 2006 SPARTA, Inc.
 * Copyright (c) 2008 Apple Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson and Ilmar Habibulin for the
 * TrustedBSD Project.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
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
#include <sys/priv.h>
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

#include <security/mac/mac_framework.h>
#include <security/mac/mac_internal.h>
#include <security/mac/mac_policy.h>

/*
 * XXXRW: struct ifnet locking is incomplete in the network code, so we use
 * our own global mutex for struct ifnet.  Non-ideal, but should help in the
 * SMP environment.
 */
struct mtx mac_ifnet_mtx;
MTX_SYSINIT(mac_ifnet_mtx, &mac_ifnet_mtx, "mac_ifnet", MTX_DEF);

/*
 * Retrieve the label associated with an mbuf by searching for the tag.
 * Depending on the value of mac_labelmbufs, it's possible that a label will
 * not be present, in which case NULL is returned.  Policies must handle the
 * possibility of an mbuf not having label storage if they do not enforce
 * early loading.
 */
struct label *
mac_mbuf_to_label(struct mbuf *m)
{
	struct m_tag *tag;
	struct label *label;

	if (m == NULL)
		return (NULL);
	tag = m_tag_find(m, PACKET_TAG_MACLABEL, NULL);
	if (tag == NULL)
		return (NULL);
	label = (struct label *)(tag+1);
	return (label);
}

static struct label *
mac_bpfdesc_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(M_WAITOK);
	MAC_POLICY_PERFORM(bpfdesc_init_label, label);
	return (label);
}

void
mac_bpfdesc_init(struct bpf_d *d)
{

	if (mac_labeled & MPC_OBJECT_BPFDESC)
		d->bd_label = mac_bpfdesc_label_alloc();
	else
		d->bd_label = NULL;
}

static struct label *
mac_ifnet_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(M_WAITOK);
	MAC_POLICY_PERFORM(ifnet_init_label, label);
	return (label);
}

void
mac_ifnet_init(struct ifnet *ifp)
{

	if (mac_labeled & MPC_OBJECT_IFNET)
		ifp->if_label = mac_ifnet_label_alloc();
	else
		ifp->if_label = NULL;
}

int
mac_mbuf_tag_init(struct m_tag *tag, int flag)
{
	struct label *label;
	int error;

	label = (struct label *) (tag + 1);
	mac_init_label(label);

	if (flag & M_WAITOK)
		MAC_POLICY_CHECK(mbuf_init_label, label, flag);
	else
		MAC_POLICY_CHECK_NOSLEEP(mbuf_init_label, label, flag);
	if (error) {
		MAC_POLICY_PERFORM_NOSLEEP(mbuf_destroy_label, label);
		mac_destroy_label(label);
	}
	return (error);
}

int
mac_mbuf_init(struct mbuf *m, int flag)
{
	struct m_tag *tag;
	int error;

	M_ASSERTPKTHDR(m);

	if (mac_labeled & MPC_OBJECT_MBUF) {
		tag = m_tag_get(PACKET_TAG_MACLABEL, sizeof(struct label),
		    flag);
		if (tag == NULL)
			return (ENOMEM);
		error = mac_mbuf_tag_init(tag, flag);
		if (error) {
			m_tag_free(tag);
			return (error);
		}
		m_tag_prepend(m, tag);
	}
	return (0);
}

static void
mac_bpfdesc_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(bpfdesc_destroy_label, label);
	mac_labelzone_free(label);
}

void
mac_bpfdesc_destroy(struct bpf_d *d)
{

	if (d->bd_label != NULL) {
		mac_bpfdesc_label_free(d->bd_label);
		d->bd_label = NULL;
	}
}

static void
mac_ifnet_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(ifnet_destroy_label, label);
	mac_labelzone_free(label);
}

void
mac_ifnet_destroy(struct ifnet *ifp)
{

	if (ifp->if_label != NULL) {
		mac_ifnet_label_free(ifp->if_label);
		ifp->if_label = NULL;
	}
}

void
mac_mbuf_tag_destroy(struct m_tag *tag)
{
	struct label *label;

	label = (struct label *)(tag+1);

	MAC_POLICY_PERFORM_NOSLEEP(mbuf_destroy_label, label);
	mac_destroy_label(label);
}

/*
 * mac_mbuf_tag_copy is called when an mbuf header is duplicated, in which
 * case the labels must also be duplicated.
 */
void
mac_mbuf_tag_copy(struct m_tag *src, struct m_tag *dest)
{
	struct label *src_label, *dest_label;

	src_label = (struct label *)(src+1);
	dest_label = (struct label *)(dest+1);

	/*
	 * mac_mbuf_tag_init() is called on the target tag in m_tag_copy(),
	 * so we don't need to call it here.
	 */
	MAC_POLICY_PERFORM_NOSLEEP(mbuf_copy_label, src_label, dest_label);
}

void
mac_mbuf_copy(struct mbuf *m_from, struct mbuf *m_to)
{
	struct label *src_label, *dest_label;

	if (mac_policy_count == 0)
		return;

	src_label = mac_mbuf_to_label(m_from);
	dest_label = mac_mbuf_to_label(m_to);

	MAC_POLICY_PERFORM_NOSLEEP(mbuf_copy_label, src_label, dest_label);
}

static void
mac_ifnet_copy_label(struct label *src, struct label *dest)
{

	MAC_POLICY_PERFORM_NOSLEEP(ifnet_copy_label, src, dest);
}

static int
mac_ifnet_externalize_label(struct label *label, char *elements,
    char *outbuf, size_t outbuflen)
{
	int error;

	MAC_POLICY_EXTERNALIZE(ifnet, label, elements, outbuf, outbuflen);

	return (error);
}

static int
mac_ifnet_internalize_label(struct label *label, char *string)
{
	int error;

	MAC_POLICY_INTERNALIZE(ifnet, label, string);

	return (error);
}

void
mac_ifnet_create(struct ifnet *ifp)
{

	if (mac_policy_count == 0)
		return;

	MAC_IFNET_LOCK(ifp);
	MAC_POLICY_PERFORM_NOSLEEP(ifnet_create, ifp, ifp->if_label);
	MAC_IFNET_UNLOCK(ifp);
}

void
mac_bpfdesc_create(struct ucred *cred, struct bpf_d *d)
{

	MAC_POLICY_PERFORM_NOSLEEP(bpfdesc_create, cred, d, d->bd_label);
}

void
mac_bpfdesc_create_mbuf(struct bpf_d *d, struct mbuf *m)
{
	struct label *label;

	/* Assume reader lock is enough. */
	BPFD_LOCK_ASSERT(d);

	if (mac_policy_count == 0)
		return;

	label = mac_mbuf_to_label(m);

	MAC_POLICY_PERFORM_NOSLEEP(bpfdesc_create_mbuf, d, d->bd_label, m,
	    label);
}

void
mac_ifnet_create_mbuf(struct ifnet *ifp, struct mbuf *m)
{
	struct label *label;

	if (mac_policy_count == 0)
		return;

	label = mac_mbuf_to_label(m);

	MAC_IFNET_LOCK(ifp);
	MAC_POLICY_PERFORM_NOSLEEP(ifnet_create_mbuf, ifp, ifp->if_label, m,
	    label);
	MAC_IFNET_UNLOCK(ifp);
}

MAC_CHECK_PROBE_DEFINE2(bpfdesc_check_receive, "struct bpf_d *",
    "struct ifnet *");

int
mac_bpfdesc_check_receive(struct bpf_d *d, struct ifnet *ifp)
{
	int error;

	/* Assume reader lock is enough. */
	BPFD_LOCK_ASSERT(d);

	if (mac_policy_count == 0)
		return (0);

	MAC_IFNET_LOCK(ifp);
	MAC_POLICY_CHECK_NOSLEEP(bpfdesc_check_receive, d, d->bd_label, ifp,
	    ifp->if_label);
	MAC_CHECK_PROBE2(bpfdesc_check_receive, error, d, ifp);
	MAC_IFNET_UNLOCK(ifp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(ifnet_check_transmit, "struct ifnet *",
    "struct mbuf *");

int
mac_ifnet_check_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct label *label;
	int error;

	M_ASSERTPKTHDR(m);

	if (mac_policy_count == 0)
		return (0);

	label = mac_mbuf_to_label(m);

	MAC_IFNET_LOCK(ifp);
	MAC_POLICY_CHECK_NOSLEEP(ifnet_check_transmit, ifp, ifp->if_label, m,
	    label);
	MAC_CHECK_PROBE2(ifnet_check_transmit, error, ifp, m);
	MAC_IFNET_UNLOCK(ifp);

	return (error);
}

int
mac_ifnet_ioctl_get(struct ucred *cred, struct ifreq *ifr,
    struct ifnet *ifp)
{
	char *elements, *buffer;
	struct label *intlabel;
	struct mac mac;
	int error;

	if (!(mac_labeled & MPC_OBJECT_IFNET))
		return (EINVAL);

	error = copyin(ifr_data_get_ptr(ifr), &mac, sizeof(mac));
	if (error)
		return (error);

	error = mac_check_structmac_consistent(&mac);
	if (error)
		return (error);

	elements = malloc(mac.m_buflen, M_MACTEMP, M_WAITOK);
	error = copyinstr(mac.m_string, elements, mac.m_buflen, NULL);
	if (error) {
		free(elements, M_MACTEMP);
		return (error);
	}

	buffer = malloc(mac.m_buflen, M_MACTEMP, M_WAITOK | M_ZERO);
	intlabel = mac_ifnet_label_alloc();
	MAC_IFNET_LOCK(ifp);
	mac_ifnet_copy_label(ifp->if_label, intlabel);
	MAC_IFNET_UNLOCK(ifp);
	error = mac_ifnet_externalize_label(intlabel, elements, buffer,
	    mac.m_buflen);
	mac_ifnet_label_free(intlabel);
	if (error == 0)
		error = copyout(buffer, mac.m_string, strlen(buffer)+1);

	free(buffer, M_MACTEMP);
	free(elements, M_MACTEMP);

	return (error);
}

int
mac_ifnet_ioctl_set(struct ucred *cred, struct ifreq *ifr, struct ifnet *ifp)
{
	struct label *intlabel;
	struct mac mac;
	char *buffer;
	int error;

	if (!(mac_labeled & MPC_OBJECT_IFNET))
		return (EINVAL);

	error = copyin(ifr_data_get_ptr(ifr), &mac, sizeof(mac));
	if (error)
		return (error);

	error = mac_check_structmac_consistent(&mac);
	if (error)
		return (error);

	buffer = malloc(mac.m_buflen, M_MACTEMP, M_WAITOK);
	error = copyinstr(mac.m_string, buffer, mac.m_buflen, NULL);
	if (error) {
		free(buffer, M_MACTEMP);
		return (error);
	}

	intlabel = mac_ifnet_label_alloc();
	error = mac_ifnet_internalize_label(intlabel, buffer);
	free(buffer, M_MACTEMP);
	if (error) {
		mac_ifnet_label_free(intlabel);
		return (error);
	}

	/*
	 * XXX: Note that this is a redundant privilege check, since policies
	 * impose this check themselves if required by the policy
	 * Eventually, this should go away.
	 */
	error = priv_check_cred(cred, PRIV_NET_SETIFMAC);
	if (error) {
		mac_ifnet_label_free(intlabel);
		return (error);
	}

	MAC_IFNET_LOCK(ifp);
	MAC_POLICY_CHECK_NOSLEEP(ifnet_check_relabel, cred, ifp,
	    ifp->if_label, intlabel);
	if (error) {
		MAC_IFNET_UNLOCK(ifp);
		mac_ifnet_label_free(intlabel);
		return (error);
	}

	MAC_POLICY_PERFORM_NOSLEEP(ifnet_relabel, cred, ifp, ifp->if_label,
	    intlabel);
	MAC_IFNET_UNLOCK(ifp);

	mac_ifnet_label_free(intlabel);
	return (0);
}
