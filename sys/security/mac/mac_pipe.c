/*-
 * Copyright (c) 2002-2003 Networks Associates Technology, Inc.
 * Copyright (c) 2006 SPARTA, Inc.
 * Copyright (c) 2009 Robert N. M. Watson
 * All rights reserved.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_mac.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/sbuf.h>
#include <sys/sdt.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/pipe.h>
#include <sys/sysctl.h>

#include <security/mac/mac_framework.h>
#include <security/mac/mac_internal.h>
#include <security/mac/mac_policy.h>

struct label *
mac_pipe_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(M_WAITOK);
	MAC_POLICY_PERFORM(pipe_init_label, label);
	return (label);
}

void
mac_pipe_init(struct pipepair *pp)
{

	if (mac_labeled & MPC_OBJECT_PIPE)
		pp->pp_label = mac_pipe_label_alloc();
	else
		pp->pp_label = NULL;
}

void
mac_pipe_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(pipe_destroy_label, label);
	mac_labelzone_free(label);
}

void
mac_pipe_destroy(struct pipepair *pp)
{

	if (pp->pp_label != NULL) {
		mac_pipe_label_free(pp->pp_label);
		pp->pp_label = NULL;
	}
}

void
mac_pipe_copy_label(struct label *src, struct label *dest)
{

	MAC_POLICY_PERFORM_NOSLEEP(pipe_copy_label, src, dest);
}

int
mac_pipe_externalize_label(struct label *label, char *elements,
    char *outbuf, size_t outbuflen)
{
	int error;

	MAC_POLICY_EXTERNALIZE(pipe, label, elements, outbuf, outbuflen);

	return (error);
}

int
mac_pipe_internalize_label(struct label *label, char *string)
{
	int error;

	MAC_POLICY_INTERNALIZE(pipe, label, string);

	return (error);
}

void
mac_pipe_create(struct ucred *cred, struct pipepair *pp)
{

	MAC_POLICY_PERFORM_NOSLEEP(pipe_create, cred, pp, pp->pp_label);
}

static void
mac_pipe_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *newlabel)
{

	MAC_POLICY_PERFORM_NOSLEEP(pipe_relabel, cred, pp, pp->pp_label,
	    newlabel);
}

MAC_CHECK_PROBE_DEFINE4(pipe_check_ioctl, "struct ucred *",
    "struct pipepair *", "unsigned long", "void *");

int
mac_pipe_check_ioctl(struct ucred *cred, struct pipepair *pp,
    unsigned long cmd, void *data)
{
	int error;

	mtx_assert(&pp->pp_mtx, MA_OWNED);

	MAC_POLICY_CHECK_NOSLEEP(pipe_check_ioctl, cred, pp, pp->pp_label,
	    cmd, data);
	MAC_CHECK_PROBE4(pipe_check_ioctl, error, cred, pp, cmd, data);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(pipe_check_poll, "struct ucred *",
    "struct pipepair *");

int
mac_pipe_check_poll(struct ucred *cred, struct pipepair *pp)
{
	int error;

	mtx_assert(&pp->pp_mtx, MA_OWNED);

	MAC_POLICY_CHECK_NOSLEEP(pipe_check_poll, cred, pp, pp->pp_label);
	MAC_CHECK_PROBE2(pipe_check_poll, error, cred, pp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(pipe_check_read, "struct ucred *",
    "struct pipepair *");

int
mac_pipe_check_read(struct ucred *cred, struct pipepair *pp)
{
	int error;

	mtx_assert(&pp->pp_mtx, MA_OWNED);

	MAC_POLICY_CHECK_NOSLEEP(pipe_check_read, cred, pp, pp->pp_label);
	MAC_CHECK_PROBE2(pipe_check_read, error, cred, pp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(pipe_check_relabel, "struct ucred *",
    "struct pipepair *", "struct label *");

static int
mac_pipe_check_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *newlabel)
{
	int error;

	mtx_assert(&pp->pp_mtx, MA_OWNED);

	MAC_POLICY_CHECK_NOSLEEP(pipe_check_relabel, cred, pp, pp->pp_label,
	    newlabel);
	MAC_CHECK_PROBE3(pipe_check_relabel, error, cred, pp, newlabel);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(pipe_check_stat, "struct ucred *",
    "struct pipepair *");

int
mac_pipe_check_stat(struct ucred *cred, struct pipepair *pp)
{
	int error;

	mtx_assert(&pp->pp_mtx, MA_OWNED);

	MAC_POLICY_CHECK_NOSLEEP(pipe_check_stat, cred, pp, pp->pp_label);
	MAC_CHECK_PROBE2(pipe_check_stat, error, cred, pp);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(pipe_check_write, "struct ucred *",
    "struct pipepair *");

int
mac_pipe_check_write(struct ucred *cred, struct pipepair *pp)
{
	int error;

	mtx_assert(&pp->pp_mtx, MA_OWNED);

	MAC_POLICY_CHECK_NOSLEEP(pipe_check_write, cred, pp, pp->pp_label);
	MAC_CHECK_PROBE2(pipe_check_write, error, cred, pp);

	return (error);
}

int
mac_pipe_label_set(struct ucred *cred, struct pipepair *pp,
    struct label *label)
{
	int error;

	mtx_assert(&pp->pp_mtx, MA_OWNED);

	error = mac_pipe_check_relabel(cred, pp, label);
	if (error)
		return (error);

	mac_pipe_relabel(cred, pp, label);

	return (0);
}
