/*-
 * Copyright (c) 2003-2004 Networks Associates Technology, Inc.
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
#include <sys/mutex.h>
#include <sys/sbuf.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/namei.h>
#include <sys/sdt.h>
#include <sys/sysctl.h>
#include <sys/sem.h>

#include <security/mac/mac_framework.h>
#include <security/mac/mac_internal.h>
#include <security/mac/mac_policy.h>

static struct label *
mac_sysv_sem_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(M_WAITOK);
	MAC_POLICY_PERFORM(sysvsem_init_label, label);
	return (label);
}

void
mac_sysvsem_init(struct semid_kernel *semakptr)
{

	if (mac_labeled & MPC_OBJECT_SYSVSEM)
		semakptr->label = mac_sysv_sem_label_alloc();
	else
		semakptr->label = NULL;
}

static void
mac_sysv_sem_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(sysvsem_destroy_label, label);
	mac_labelzone_free(label);
}

void
mac_sysvsem_destroy(struct semid_kernel *semakptr)
{

	if (semakptr->label != NULL) {
		mac_sysv_sem_label_free(semakptr->label);
		semakptr->label = NULL;
	}
}

void
mac_sysvsem_create(struct ucred *cred, struct semid_kernel *semakptr)
{

	MAC_POLICY_PERFORM_NOSLEEP(sysvsem_create, cred, semakptr,
	    semakptr->label);
}

void
mac_sysvsem_cleanup(struct semid_kernel *semakptr)
{

	MAC_POLICY_PERFORM_NOSLEEP(sysvsem_cleanup, semakptr->label);
}

MAC_CHECK_PROBE_DEFINE3(sysvsem_check_semctl, "struct ucred *",
    "struct semid_kernel *", "int");

int
mac_sysvsem_check_semctl(struct ucred *cred, struct semid_kernel *semakptr,
    int cmd)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(sysvsem_check_semctl, cred, semakptr,
	    semakptr->label, cmd);
	MAC_CHECK_PROBE3(sysvsem_check_semctl, error, cred, semakptr, cmd);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(sysvsem_check_semget, "struct ucred *",
    "struct semid_kernel *");

int
mac_sysvsem_check_semget(struct ucred *cred, struct semid_kernel *semakptr)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(sysvsem_check_semget, cred, semakptr,
	    semakptr->label);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(sysvsem_check_semop, "struct ucred *",
    "struct semid_kernel *", "size_t");

int
mac_sysvsem_check_semop(struct ucred *cred, struct semid_kernel *semakptr,
    size_t accesstype)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(sysvsem_check_semop, cred, semakptr,
	    semakptr->label, accesstype);
	MAC_CHECK_PROBE3(sysvsem_check_semop, error, cred, semakptr,
	    accesstype);

	return (error);
}
