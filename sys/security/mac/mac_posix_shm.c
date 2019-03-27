/*-
 * Copyright (c) 2003-2006 SPARTA, Inc.
 * Copyright (c) 2009-2011 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed for the FreeBSD Project in part by Network
 * Associates Laboratories, the Security Research Division of Network
 * Associates, Inc. under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"),
 * as part of the DARPA CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS"). *
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
#include <sys/mman.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sdt.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <security/mac/mac_framework.h>
#include <security/mac/mac_internal.h>
#include <security/mac/mac_policy.h>

static struct label *
mac_posixshm_label_alloc(void)
{
	struct label *label;

	label = mac_labelzone_alloc(M_WAITOK);
	MAC_POLICY_PERFORM(posixshm_init_label, label);
	return (label);
}

void
mac_posixshm_init(struct shmfd *shmfd)
{

	if (mac_labeled & MPC_OBJECT_POSIXSHM)
		shmfd->shm_label = mac_posixshm_label_alloc();
	else
		shmfd->shm_label = NULL;
}

static void
mac_posixshm_label_free(struct label *label)
{

	MAC_POLICY_PERFORM_NOSLEEP(posixshm_destroy_label, label);
	mac_labelzone_free(label);
}

void
mac_posixshm_destroy(struct shmfd *shmfd)
{

	if (shmfd->shm_label != NULL) {
		mac_posixshm_label_free(shmfd->shm_label);
		shmfd->shm_label = NULL;
	}
}

void
mac_posixshm_create(struct ucred *cred, struct shmfd *shmfd)
{

	MAC_POLICY_PERFORM_NOSLEEP(posixshm_create, cred, shmfd,
	    shmfd->shm_label);
}

MAC_CHECK_PROBE_DEFINE2(posixshm_check_create, "struct ucred *",
    "const char *");

int
mac_posixshm_check_create(struct ucred *cred, const char *path)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(posixshm_check_create, cred, path);
	MAC_CHECK_PROBE2(posixshm_check_create, error, cred, path);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(posixshm_check_mmap, "struct ucred *",
    "struct shmfd *", "int", "int");

int
mac_posixshm_check_mmap(struct ucred *cred, struct shmfd *shmfd, int prot,
    int flags)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(posixshm_check_mmap, cred, shmfd,
	    shmfd->shm_label, prot, flags);
	MAC_CHECK_PROBE4(posixshm_check_mmap, error, cred, shmfd, prot,
	    flags);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(posixshm_check_open, "struct ucred *",
    "struct shmfd *", "accmode_t");

int
mac_posixshm_check_open(struct ucred *cred, struct shmfd *shmfd,
    accmode_t accmode)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(posixshm_check_open, cred, shmfd,
	    shmfd->shm_label, accmode);
	MAC_CHECK_PROBE3(posixshm_check_open, error, cred, shmfd, accmode);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(posixshm_check_stat, "struct ucred *",
    "struct ucred *", "struct shmfd *");

int
mac_posixshm_check_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *shmfd)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(posixshm_check_stat, active_cred, file_cred,
	    shmfd, shmfd->shm_label);
	MAC_CHECK_PROBE3(posixshm_check_stat, error, active_cred, file_cred,
	    shmfd);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(posixshm_check_truncate, "struct ucred *",
    "struct ucred *", "struct shmfd *");

int
mac_posixshm_check_truncate(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *shmfd)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(posixshm_check_truncate, active_cred,
	    file_cred, shmfd, shmfd->shm_label);
	MAC_CHECK_PROBE3(posixshm_check_truncate, error, active_cred,
	    file_cred, shmfd);

	return (error);
}

MAC_CHECK_PROBE_DEFINE2(posixshm_check_unlink, "struct ucred *",
    "struct shmfd *");

int
mac_posixshm_check_unlink(struct ucred *cred, struct shmfd *shmfd)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(posixshm_check_unlink, cred, shmfd,
	    shmfd->shm_label);
	MAC_CHECK_PROBE2(posixshm_check_unlink, error, cred, shmfd);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(posixshm_check_setmode, "struct ucred *",
    "struct shmfd *", "mode_t");

int
mac_posixshm_check_setmode(struct ucred *cred, struct shmfd *shmfd, mode_t mode)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(posixshm_check_setmode, cred, shmfd,
	    shmfd->shm_label, mode);
	MAC_CHECK_PROBE3(posixshm_check_setmode, error, cred, shmfd, mode);

	return (error);
}

MAC_CHECK_PROBE_DEFINE4(posixshm_check_setowner, "struct ucred *",
    "struct shmfd *", "uid_t", "gid_t");

int
mac_posixshm_check_setowner(struct ucred *cred, struct shmfd *shmfd, uid_t uid,
    gid_t gid)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(posixshm_check_setowner, cred, shmfd,
	    shmfd->shm_label, uid, gid);
	MAC_CHECK_PROBE4(posixshm_check_setowner, error, cred, shmfd,
	    uid, gid);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(posixshm_check_read, "struct ucred *",
    "struct ucred *", "struct shmfd *");

int
mac_posixshm_check_read(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *shmfd)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(posixshm_check_read, active_cred,
	    file_cred, shmfd, shmfd->shm_label);
	MAC_CHECK_PROBE3(posixshm_check_read, error, active_cred,
	    file_cred, shmfd);

	return (error);
}

MAC_CHECK_PROBE_DEFINE3(posixshm_check_write, "struct ucred *",
    "struct ucred *", "struct shmfd *");

int
mac_posixshm_check_write(struct ucred *active_cred, struct ucred *file_cred,
    struct shmfd *shmfd)
{
	int error;

	MAC_POLICY_CHECK_NOSLEEP(posixshm_check_write, active_cred,
	    file_cred, shmfd, shmfd->shm_label);
	MAC_CHECK_PROBE3(posixshm_check_write, error, active_cred,
	    file_cred, shmfd);

	return (error);
}
