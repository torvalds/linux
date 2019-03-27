/*
 * $FreeBSD$
 *
 * Copyright (c) 2011, 2012, 2013, 2015, 2016, Juniper Networks, Inc.
 * All rights reserved.
 *
 * Originally derived from:
 *	$NetBSD: kern_verifiedexec.c,v 1.7 2003/11/18 13:13:03 martin Exp $
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>

#include "opt_mac.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h> 
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/syslog.h>
#include <sys/vnode.h>

#include "mac_veriexec.h"
#include "mac_veriexec_internal.h"

/**
 * @var fpops_list
 * @internal
 * @brief Fingerprint operations list
 *
 * This is essentially the list of fingerprint modules currently loaded
 */
static LIST_HEAD(fpopshead, mac_veriexec_fpops) fpops_list;

static int mac_veriexec_late;

static int sysctl_mac_veriexec_algorithms(SYSCTL_HANDLER_ARGS);

SYSCTL_PROC(_security_mac_veriexec, OID_AUTO, algorithms,
    CTLTYPE_STRING | CTLFLAG_RD, 0, 0, sysctl_mac_veriexec_algorithms, "A",
    "Verified execution supported hashing algorithms");

static int
sysctl_mac_veriexec_algorithms(SYSCTL_HANDLER_ARGS)
{
	struct sbuf sb;
	struct mac_veriexec_fpops *fpops;
	int algorithms, error;

	algorithms = 0;
	sbuf_new(&sb, NULL, 128, SBUF_AUTOEXTEND);
	LIST_FOREACH(fpops, &fpops_list, entries) {
		if (algorithms++)
			sbuf_printf(&sb, " ");
		sbuf_printf(&sb, "%s", fpops->type);
	}
	sbuf_finish(&sb);
	error = SYSCTL_OUT(req, sbuf_data(&sb), sbuf_len(&sb));
	sbuf_delete(&sb);
	return (error);
}

/**
 * @internal
 * @brief Consistently identify file encountering errors
 *
 * @param imgp		image params to display
 * @param td		calling thread
 * @param msg		message to display
 *
 * @return String form of the information stored in @p imgp
 */
static void
identify_error (struct image_params *imgp, struct thread *td, const char *msg)
{
	struct proc *parent;
	pid_t ppid, gppid;

	parent = imgp->proc->p_pptr;
	ppid = (parent != NULL) ? parent->p_pid : 0;
	gppid = (parent != NULL && parent->p_pptr != NULL) ?
	    parent->p_pptr->p_pid : 0;

	log(LOG_ERR, MAC_VERIEXEC_FULLNAME ": %s (file=%s fsid=%ju fileid=%ju "
	    "gen=%lu uid=%u pid=%u ppid=%u gppid=%u)", msg,
	    (imgp->args != NULL) ? imgp->args->fname : "",
	    (uintmax_t)imgp->attr->va_fsid, (uintmax_t)imgp->attr->va_fileid,
	    imgp->attr->va_gen, td->td_ucred->cr_ruid, imgp->proc->p_pid,
	    ppid, gppid);
}

/**
 * @internal
 * @brief Check the fingerprint type for the given file and evaluate the
 * fingerprint for that file.
 *
 * It is assumed that @p fingerprint has sufficient storage to hold the
 * resulting fingerprint string.
 *
 * @param vp		vnode to check
 * @param ip		file info from the meta-data store
 * @param td		calling thread
 * @param file_size	size of the file to read
 * @param fingerprint	resulting fingerprint
 *
 * @return 0 on success, otherwise an error code.
 */
static int
evaluate_fingerprint(struct vnode *vp, struct mac_veriexec_file_info *ip,
    struct thread *td, off_t file_size, unsigned char *fingerprint)
{
	uint8_t *filebuf;
	void *ctx;
	off_t offset;
	size_t count, nread, resid;
	int error = EINVAL;

	filebuf = malloc(PAGE_SIZE, M_VERIEXEC, M_WAITOK);
	ctx = malloc(ip->ops->context_size, M_VERIEXEC, M_WAITOK);

	(ip->ops->init)(ctx);
	for (offset = 0; offset < file_size; offset += nread) {
		if ((offset + PAGE_SIZE) > file_size)
			count = file_size - offset;
		else
			count = PAGE_SIZE;

		error = vn_rdwr_inchunks(UIO_READ, vp, filebuf, count, offset,
		    UIO_SYSSPACE, IO_NODELOCKED, td->td_ucred, NOCRED, &resid,
		    td);
		if (error)
			goto failed;

		nread = count - resid;
		(ip->ops->update)(ctx, filebuf, nread);
	}
	(ip->ops->final)(fingerprint, ctx);

#ifdef DEBUG_VERIEXEC_FINGERPRINT
	for (offset = 0; offset < ip->ops->digest_len; offset++)
		printf("%02x", fingerprint[offset]);
	printf("\n");
#endif

failed:
	free(ctx, M_VERIEXEC);
	free(filebuf, M_VERIEXEC);
	return (error);
}

/**
 * @internal
 * @brief Compare the two given fingerprints to see if they are the same.
 *
 * Differing fingerprint methods may have differing lengths which
 * is handled by this routine.
 *
 * @param ip		file info from the meta-data store
 * @param digest	digest to compare
 *
 * @return 0 if the fingerprints match and non-zero if they do not.
 */
static int
fingerprintcmp(struct mac_veriexec_file_info *ip, unsigned char *digest)
{

	return memcmp(ip->fingerprint, digest, ip->ops->digest_len);
}

/**
 * @brief Check if @p fingerprint matches the one associated with the vnode
 *     @p vp
 *
 * @param vp		vnode to check
 * @param ip		file info from the meta-data store
 * @param td		calling thread
 * @param file_size	size of the file to read
 * @param fingerprint	fingerprint to compare
 *
 * @return 0 if they match, otherwise an error code.
 */
int
mac_veriexec_fingerprint_check_vnode(struct vnode *vp,
    struct mac_veriexec_file_info *ip, struct thread *td, off_t file_size,
    unsigned char *fingerprint)
{
	int error;

	/* reject fingerprint if writers are active */
	if (vp->v_writecount)
		return (ETXTBSY);

	if ((vp->v_mount->mnt_flag & MNT_VERIFIED) != 0) {
		VERIEXEC_DEBUG(2, ("file %ju.%lu on verified %s mount\n",
		    (uintmax_t)ip->fileid, ip->gen,
		    vp->v_mount->mnt_vfc->vfc_name));

		/*
		 * The VFS is backed by a file which has been verified.
		 * No need to waste time here.
		 */
		return (0);
	}

	error = evaluate_fingerprint(vp, ip, td, file_size, fingerprint);
	if (error)
		return (error);

	if (fingerprintcmp(ip, fingerprint) != 0)
		return (EAUTH);

	return (0);
}

/**
 * @brief Check a file signature and validate it.
 *
 * @param imgp		parameters for the image to check
 * @param check_files	if 1, check the files list first, otherwise check the
 * 			exectuables list first
 * @param td		calling thread
 *
 * @note Called with imgp->vp locked.
 *
 * @return 0 if the signature is valid, otherwise an error code.
 */
int
mac_veriexec_fingerprint_check_image(struct image_params *imgp,
    int check_files, struct thread *td)
{
	struct vnode *vp = imgp->vp;
	int error;
	fingerprint_status_t status;

	if (!mac_veriexec_in_state(VERIEXEC_STATE_ACTIVE))
		return 0;

	error = mac_veriexec_metadata_fetch_fingerprint_status(vp, imgp->attr,
	    td, check_files);
	if (error && error != EAUTH)
		return (error);

	/*
	 * By now status is set.
	 */
	status = mac_veriexec_get_fingerprint_status(vp);
	switch (status) {
	case FINGERPRINT_INVALID: /* should not happen */
		identify_error(imgp, td, "got unexpected FINGERPRINT_INVALID");
		error = EPERM;
		break;

	case FINGERPRINT_FILE:
		if (!check_files) {
			if (prison0.pr_securelevel > 1 ||
			    mac_veriexec_in_state(VERIEXEC_STATE_ENFORCE))
				error = EPERM;
		}
		break;

	case FINGERPRINT_VALID: /* is ok - report so if debug is on */
		VERIEXEC_DEBUG(4, ("Fingerprint matches\n"));
		break;

	case FINGERPRINT_INDIRECT: /* fingerprint ok but need to check
				      for direct execution */
		if (!imgp->interpreted) {
			identify_error(imgp, td, "attempted direct execution");
			if (prison0.pr_securelevel > 1 ||
			    mac_veriexec_in_state(VERIEXEC_STATE_ENFORCE))
				error = EPERM;
		}
		break;

	case FINGERPRINT_NOMATCH: /* does not match - whine about it */
		identify_error(imgp, td,
		    "fingerprint does not match loaded value");
		if (prison0.pr_securelevel > 1 ||
		    mac_veriexec_in_state(VERIEXEC_STATE_ENFORCE))
			error = EAUTH;
		break;

	case FINGERPRINT_NOENTRY: /* no entry in the list, complain */
		identify_error(imgp, td, "no fingerprint");
		if (prison0.pr_securelevel > 1 ||
		    mac_veriexec_in_state(VERIEXEC_STATE_ENFORCE))
			error = EAUTH;
		break;

	case FINGERPRINT_NODEV: /* no signatures for the device, complain */
		identify_error(imgp, td, "no signatures for device");
		if (prison0.pr_securelevel > 1 ||
		    mac_veriexec_in_state(VERIEXEC_STATE_ENFORCE))
			error = EAUTH;
		break;

	default: /* this should never happen. */
		identify_error(imgp, td, "invalid status field for vnode");
		error = EPERM;
	}
	return error; 
}

/**
 * @brief Look up the fingerprint operations for a specific digest type
 *
 * @return A pointer to fingerprint operations, if found, or else @c NULL.
 */
struct mac_veriexec_fpops *
mac_veriexec_fingerprint_lookup_ops(const char *type)
{
	struct mac_veriexec_fpops *fpops;

	if (type == NULL)
		return (NULL);

	LIST_FOREACH(fpops, &fpops_list, entries) {
		if (!strcasecmp(type, fpops->type))
			break;
	}
	return (fpops);
}

/**
 * @brief Add fingerprint operations for a specific digest type
 *
 * Any attempts to add a duplicate digest type results in an error.
 *
 * @return 0 if the ops were added successfully, otherwise an error code.
 */
int
mac_veriexec_fingerprint_add_ops(struct mac_veriexec_fpops *fpops)
{

	/* Sanity check the ops */
	if (fpops->type == NULL || fpops->digest_len == 0 ||
	    fpops->context_size == 0 || fpops->init == NULL ||
	    fpops->update == NULL || fpops->final == NULL)
		return (EINVAL);

	/* Make sure we do not already have ops for this digest type */
	if (mac_veriexec_fingerprint_lookup_ops(fpops->type))
		return (EEXIST);

	/* Add the ops to the list */
	LIST_INSERT_HEAD(&fpops_list, fpops, entries);

	printf("MAC/veriexec fingerprint module loaded: %s\n", fpops->type);

	return (0);
}

/**
 * @brief Initialize the fingerprint operations list
 */
void
mac_veriexec_fingerprint_init(void)
{

	LIST_INIT(&fpops_list);
}

/**
 * @brief Handle fingerprint module events
 *
 * This function is called by the @c MAC_VERIEXEC_FPMOD macro.
 *
 * @param mod		module information
 * @param type		event type
 * @param data		event-specific data
 *
 * @return On @c MOD_LOAD, 0 if the fingerprint ops were added successfully,
 *     otherwise an error code. All other event types result in an error code.
 */
int
mac_veriexec_fingerprint_modevent(module_t mod, int type, void *data)
{
	struct mac_veriexec_fpops *fpops;
	int error;

	error = 0;
	fpops = (struct mac_veriexec_fpops *) data;

	switch (type) {
	case MOD_LOAD:
		/* We do not allow late loading of fingerprint modules */
		if (mac_veriexec_late) {
			printf("%s: can't load %s fingerprint module after "
			    "booting\n", __func__, fpops->type);
			error = EBUSY;
			break;
		}
		error = mac_veriexec_fingerprint_add_ops(fpops);
		break;
	case MOD_UNLOAD:
		error = EBUSY;
		break;
	default:
		error = EOPNOTSUPP;
		break;
	}

	return (error);
}

/**
 * @internal
 * @brief Mark veriexec late initialization flag
 */
static void
mac_veriexec_late_init(void)
{

	mac_veriexec_late = 1;
}

SYSINIT(mac_veriexec_late, SI_SUB_MAC_LATE, SI_ORDER_ANY,
    mac_veriexec_late_init, NULL);
