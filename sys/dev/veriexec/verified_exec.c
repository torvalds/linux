/*
 * $FreeBSD$
 *
 * Copyright (c) 2011-2013, 2015, Juniper Networks, Inc.
 * All rights reserved.
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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/ioccom.h>
#include <sys/jail.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mdioctl.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/vnode.h>

#include <security/mac_veriexec/mac_veriexec.h>
#include <security/mac_veriexec/mac_veriexec_internal.h>

#include "veriexec_ioctl.h"

/*
 * We need a mutex while updating lists etc.
 */
extern struct mtx ve_mutex;

/*
 * Handle the ioctl for the device
 */
static int
verifiedexecioctl(struct cdev *dev __unused, u_long cmd, caddr_t data,
    int flags, struct thread *td)
{
	struct nameidata nid;
	struct vattr vattr;
	struct verified_exec_params *params;
	int error = 0;

	params = (struct verified_exec_params *)data;
	switch (cmd) {
	case VERIEXEC_ACTIVE:
		mtx_lock(&ve_mutex);
		if (mac_veriexec_in_state(VERIEXEC_STATE_LOADED))
			mac_veriexec_set_state(VERIEXEC_STATE_ACTIVE);
		else
			error = EINVAL;
		mtx_unlock(&ve_mutex);
		break;
	case VERIEXEC_DEBUG_ON:
		mtx_lock(&ve_mutex);
		{
			int *ip = (int *)data;
			
			mac_veriexec_debug++;
			if (ip) {
				if (*ip > 0)
					mac_veriexec_debug = *ip;	
				*ip = mac_veriexec_debug;
			}
		}
		mtx_unlock(&ve_mutex);
		break;
	case VERIEXEC_DEBUG_OFF:
		mac_veriexec_debug = 0;
		break;
	case VERIEXEC_ENFORCE:
		mtx_lock(&ve_mutex);
		if (mac_veriexec_in_state(VERIEXEC_STATE_LOADED))
			mac_veriexec_set_state(VERIEXEC_STATE_ACTIVE |
			    VERIEXEC_STATE_ENFORCE);
		else
			error = EINVAL;
		mtx_unlock(&ve_mutex);
		break;
	case VERIEXEC_GETSTATE:
		{
			int *ip = (int *)data;
			
			if (ip)
				*ip = mac_veriexec_get_state();
			else
			    error = EINVAL;
		}
		break;
	case VERIEXEC_LOCK:
		mtx_lock(&ve_mutex);
		mac_veriexec_set_state(VERIEXEC_STATE_LOCKED);
		mtx_unlock(&ve_mutex);
		break;
	case VERIEXEC_LOAD:
	    	if (prison0.pr_securelevel > 0)
			return (EPERM);	/* no updates when secure */

		/* FALLTHROUGH */
	case VERIEXEC_SIGNED_LOAD:
		/*
		 * If we use a loader that will only use a
		 * digitally signed hash list - which it verifies.
		 * We can load fingerprints provided veriexec is not locked.
		 */
	    	if (prison0.pr_securelevel > 0 &&
		    !mac_veriexec_in_state(VERIEXEC_STATE_LOADED)) {
			/*
			 * If securelevel has been raised and we
			 * do not have any fingerprints loaded,
			 * it would dangerous to do so now.
			 */
			return (EPERM);
		}
		if (mac_veriexec_in_state(VERIEXEC_STATE_LOCKED))
			error = EPERM;
		else {
			int flags = FREAD;
			int override = (cmd == VERIEXEC_SIGNED_LOAD);

			/*
			 * Get the attributes for the file name passed
			 * stash the file's device id and inode number
			 * along with it's fingerprint in a list for
			 * exec to use later.
			 */
			/*
			 * FreeBSD seems to copy the args to kernel space
			 */
                        NDINIT(&nid, LOOKUP, FOLLOW, UIO_SYSSPACE,
                               params->file, td);
			if ((error = vn_open(&nid, &flags, 0, NULL)) != 0)
				return (error);

			error = VOP_GETATTR(nid.ni_vp, &vattr, td->td_ucred);
			if (error != 0) {
				mac_veriexec_set_fingerprint_status(nid.ni_vp,
				    FINGERPRINT_INVALID);
				VOP_UNLOCK(nid.ni_vp, 0);
				(void) vn_close(nid.ni_vp, FREAD, td->td_ucred,
				    td);
				return (error);
			}
			if (override) {
				/*
				 * If the file is on a "verified" filesystem
				 * someone may be playing games.
				 */
				if ((nid.ni_vp->v_mount->mnt_flag &
				    MNT_VERIFIED) != 0)
					override = 0;
			}

			/*
			 * invalidate the node fingerprint status
			 * which will have been set in the vn_open
			 * and would always be FINGERPRINT_NOTFOUND
			 */
			mac_veriexec_set_fingerprint_status(nid.ni_vp,
			    FINGERPRINT_INVALID);
			VOP_UNLOCK(nid.ni_vp, 0);
			(void) vn_close(nid.ni_vp, FREAD, td->td_ucred, td);

			mtx_lock(&ve_mutex);
			error = mac_veriexec_metadata_add_file(
			    ((params->flags & VERIEXEC_FILE) != 0),
			    vattr.va_fsid, vattr.va_fileid, vattr.va_gen,
			    params->fingerprint, params->flags,
			    params->fp_type, override);

			mac_veriexec_set_state(VERIEXEC_STATE_LOADED);
			mtx_unlock(&ve_mutex);
		}
		break;
	default:
		error = ENODEV;
	}
	return (error);
}

struct cdevsw veriexec_cdevsw = {
	.d_version =	D_VERSION,
	.d_ioctl =	verifiedexecioctl,
	.d_name =	"veriexec",
};

static void
veriexec_drvinit(void *unused __unused)
{

	make_dev(&veriexec_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "veriexec");
}

SYSINIT(veriexec, SI_SUB_PSEUDO, SI_ORDER_ANY, veriexec_drvinit, NULL);
MODULE_DEPEND(veriexec, mac_veriexec, 1, 1, 1);
