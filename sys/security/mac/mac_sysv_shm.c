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
#include <sys/shm.h>

#include <security/mac/mac_framework.h>
#include <security/mac/mac_internal.h>
#include <security/mac/mac_policy.h>

static struct label *
mac_sysv_shm_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(M_WAITOK);
	MAC_POLICY_PERFORM(sysvshm_init_label, label);
	return (label);
}

void
mac_sysvshm_init(struct shmid_kernel *shmsegptr)
{

	if (mac_labeled & MPC_OBJECT_SYSVSHM)
		shmsegptr->label = mac_sysv_shm_label_alloc();
	else
		shmsegptr->label = NULL;
}

static void
mac_sysv_shm_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(sysvshm_destroy_label, label);
	mac_labelzone_free(label);
}

void
mac_sysvshm_destroy(struct shmid_kernel *shmsegptr)
{

	if (shmsegptr->label != NULL) {
		mac_sysv_shm_label_free(shmsegptr->label);
		shmsegptr->label = NULL;
	}
}

void
mac_sysvshm_create(struct ucred *cred, struct shmid_kernel *shmsegptr)
{

	MAC_POLICY_PERFORM_NOSLEEP(sysvshm_create, cred, shmsegptr,
	    shmsegptr->label);
}

void
mac_sysvshm_cleanup(struct shmid_kernel *shmsegptr)
{

	MAC_POLICY_PERFORM_NOSLEEP(sysvshm_cleanup, shmsegptr->label);
}

MAC_CHECK_PROBE_DEFINE3(sysvshm_check_shmat, "struct ucred *",
    "struct shmid_kernel *", "int");

int
mac_sysvshm_check_shmat(struct ucred *cred, struct shmid_kernel *shmsegptr,
    int shmflg)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(sysvshm_check_shmat, cred, shmsegptr,
	    shmsegptr->label, shmflg);
	MAC_CHECK_PROBE3(sysvshm_check_shmat, error, cred, shmsegptr,
	    shmflg);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(sysvshm_check_shmctl, "struct ucred *",
    "struct shmid_kernel *", "int");

int
mac_sysvshm_check_shmctl(struct ucred *cred, struct shmid_kernel *shmsegptr,
    int cmd)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(sysvshm_check_shmctl, cred, shmsegptr,
	    shmsegptr->label, cmd);
	MAC_CHECK_PROBE3(sysvshm_check_shmctl, error, cred, shmsegptr, cmd);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(sysvshm_check_shmdt, "struct ucred *",
    "struct shmid *");

int
mac_sysvshm_check_shmdt(struct ucred *cred, struct shmid_kernel *shmsegptr)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(sysvshm_check_shmdt, cred, shmsegptr,
	    shmsegptr->label);
	MAC_CHECK_PROBE2(sysvshm_check_shmdt, error, cred, shmsegptr);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(sysvshm_check_shmget, "struct ucred *",
    "struct shmid_kernel *", "int");

int
mac_sysvshm_check_shmget(struct ucred *cred, struct shmid_kernel *shmsegptr,
    int shmflg)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(sysvshm_check_shmget, cred, shmsegptr,
	    shmsegptr->label, shmflg);
	MAC_CHECK_PROBE3(sysvshm_check_shmget, error, cred, shmsegptr,
	    shmflg);

	return (error);
}
