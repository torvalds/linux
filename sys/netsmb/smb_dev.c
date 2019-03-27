/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2001 Boris Popov
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/capsicum.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/file.h>		/* Must come after sys/malloc.h */
#include <sys/filedesc.h>
#include <sys/mbuf.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/uio.h>
#include <sys/vnode.h>

#include <net/if.h>

#include <netsmb/smb.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_subr.h>
#include <netsmb/smb_dev.h>

static struct cdev *nsmb_dev;

static d_open_t	 nsmb_dev_open;
static d_ioctl_t nsmb_dev_ioctl;

MODULE_DEPEND(netsmb, libiconv, 1, 1, 2);
MODULE_VERSION(netsmb, NSMB_VERSION);

static int smb_version = NSMB_VERSION;
struct sx smb_lock;


SYSCTL_DECL(_net_smb);
SYSCTL_INT(_net_smb, OID_AUTO, version, CTLFLAG_RD, &smb_version, 0, "");

static MALLOC_DEFINE(M_NSMBDEV, "NETSMBDEV", "NET/SMB device");

static struct cdevsw nsmb_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	nsmb_dev_open,
	.d_ioctl =	nsmb_dev_ioctl,
	.d_name =	NSMB_NAME
};

static int
nsmb_dev_init(void)
{

	nsmb_dev = make_dev(&nsmb_cdevsw, 0, UID_ROOT, GID_OPERATOR,
	    0600, "nsmb");
	if (nsmb_dev == NULL)
		return (ENOMEM);  
	return (0);
}

static void 
nsmb_dev_destroy(void)
{

	MPASS(nsmb_dev != NULL);
	destroy_dev(nsmb_dev);
	nsmb_dev = NULL;
}

static struct smb_dev *
smbdev_alloc(struct cdev *dev)
{
	struct smb_dev *sdp;

	sdp = malloc(sizeof(struct smb_dev), M_NSMBDEV, M_WAITOK | M_ZERO);
	sdp->dev = dev;	
	sdp->sd_level = -1;
	sdp->sd_flags |= NSMBFL_OPEN;
	sdp->refcount = 1;
	return (sdp);	
} 

void
sdp_dtor(void *arg)
{
	struct smb_dev *dev;

	dev = (struct smb_dev *)arg;	
	SMB_LOCK();
	sdp_trydestroy(dev);
	SMB_UNLOCK();
}

static int
nsmb_dev_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	struct smb_dev *sdp;
	int error;

	sdp = smbdev_alloc(dev);
	error = devfs_set_cdevpriv(sdp, sdp_dtor);
	if (error) {
		free(sdp, M_NSMBDEV);	
		return (error);
	}
	return (0);
}

void
sdp_trydestroy(struct smb_dev *sdp)
{
	struct smb_vc *vcp;
	struct smb_share *ssp;
	struct smb_cred *scred;

	SMB_LOCKASSERT();
	if (!sdp)
		panic("No smb_dev upon device close");
	MPASS(sdp->refcount > 0);
	sdp->refcount--;
	if (sdp->refcount) 
		return;
	scred = malloc(sizeof(struct smb_cred), M_NSMBDEV, M_WAITOK);
	smb_makescred(scred, curthread, NULL);
	ssp = sdp->sd_share;
	if (ssp != NULL) {
		smb_share_lock(ssp);
		smb_share_rele(ssp, scred);
	}
	vcp = sdp->sd_vc;
	if (vcp != NULL) {
		smb_vc_lock(vcp);
		smb_vc_rele(vcp, scred);
	}
	free(scred, M_NSMBDEV);
	free(sdp, M_NSMBDEV);
	return;
}


static int
nsmb_dev_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
	struct smb_dev *sdp;
	struct smb_vc *vcp;
	struct smb_share *ssp;
	struct smb_cred *scred;
	int error = 0;

	error = devfs_get_cdevpriv((void **)&sdp);
	if (error)
		return (error);
	scred = malloc(sizeof(struct smb_cred), M_NSMBDEV, M_WAITOK);
	SMB_LOCK();
	smb_makescred(scred, td, NULL);
	switch (cmd) {
	    case SMBIOC_OPENSESSION:
		if (sdp->sd_vc) {
			error = EISCONN;
			goto out;
		}
		error = smb_usr_opensession((struct smbioc_ossn*)data,
		    scred, &vcp);
		if (error)
			break;
		sdp->sd_vc = vcp;
		smb_vc_unlock(vcp);
		sdp->sd_level = SMBL_VC;
		break;
	    case SMBIOC_OPENSHARE:
		if (sdp->sd_share) {
			error = EISCONN;
			goto out;
		}
		if (sdp->sd_vc == NULL) {
			error = ENOTCONN;
			goto out;
		}
		error = smb_usr_openshare(sdp->sd_vc,
		    (struct smbioc_oshare*)data, scred, &ssp);
		if (error)
			break;
		sdp->sd_share = ssp;
		smb_share_unlock(ssp);
		sdp->sd_level = SMBL_SHARE;
		break;
	    case SMBIOC_REQUEST:
		if (sdp->sd_share == NULL) {
			error = ENOTCONN;
			goto out;
		}
		error = smb_usr_simplerequest(sdp->sd_share,
		    (struct smbioc_rq*)data, scred);
		break;
	    case SMBIOC_T2RQ:
		if (sdp->sd_share == NULL) {
			error = ENOTCONN;
			goto out;
		}
		error = smb_usr_t2request(sdp->sd_share,
		    (struct smbioc_t2rq*)data, scred);
		break;
	    case SMBIOC_SETFLAGS: {
		struct smbioc_flags *fl = (struct smbioc_flags*)data;
		int on;
	
		if (fl->ioc_level == SMBL_VC) {
			if (fl->ioc_mask & SMBV_PERMANENT) {
				on = fl->ioc_flags & SMBV_PERMANENT;
				if ((vcp = sdp->sd_vc) == NULL) {
					error = ENOTCONN;
					goto out;
				}
				error = smb_vc_get(vcp, scred);
				if (error)
					break;
				if (on && (vcp->obj.co_flags & SMBV_PERMANENT) == 0) {
					vcp->obj.co_flags |= SMBV_PERMANENT;
					smb_vc_ref(vcp);
				} else if (!on && (vcp->obj.co_flags & SMBV_PERMANENT)) {
					vcp->obj.co_flags &= ~SMBV_PERMANENT;
					smb_vc_rele(vcp, scred);
				}
				smb_vc_put(vcp, scred);
			} else
				error = EINVAL;
		} else if (fl->ioc_level == SMBL_SHARE) {
			if (fl->ioc_mask & SMBS_PERMANENT) {
				on = fl->ioc_flags & SMBS_PERMANENT;
				if ((ssp = sdp->sd_share) == NULL) {
					error = ENOTCONN;
					goto out;
				}
				error = smb_share_get(ssp, scred);
				if (error)
					break;
				if (on && (ssp->obj.co_flags & SMBS_PERMANENT) == 0) {
					ssp->obj.co_flags |= SMBS_PERMANENT;
					smb_share_ref(ssp);
				} else if (!on && (ssp->obj.co_flags & SMBS_PERMANENT)) {
					ssp->obj.co_flags &= ~SMBS_PERMANENT;
					smb_share_rele(ssp, scred);
				}
				smb_share_put(ssp, scred);
			} else
				error = EINVAL;
			break;
		} else
			error = EINVAL;
		break;
	    }
	    case SMBIOC_LOOKUP:
		if (sdp->sd_vc || sdp->sd_share) {
			error = EISCONN;
			goto out;
		}
		vcp = NULL;
		ssp = NULL;
		error = smb_usr_lookup((struct smbioc_lookup*)data, scred, &vcp, &ssp);
		if (error)
			break;
		if (vcp) {
			sdp->sd_vc = vcp;
			smb_vc_unlock(vcp);
			sdp->sd_level = SMBL_VC;
		}
		if (ssp) {
			sdp->sd_share = ssp;
			smb_share_unlock(ssp);
			sdp->sd_level = SMBL_SHARE;
		}
		break;
	    case SMBIOC_READ: case SMBIOC_WRITE: {
		struct smbioc_rw *rwrq = (struct smbioc_rw*)data;
		struct uio auio;
		struct iovec iov;
	
		if ((ssp = sdp->sd_share) == NULL) {
			error = ENOTCONN;
			goto out;
	 	}
		iov.iov_base = rwrq->ioc_base;
		iov.iov_len = rwrq->ioc_cnt;
		auio.uio_iov = &iov;
		auio.uio_iovcnt = 1;
		auio.uio_offset = rwrq->ioc_offset;
		auio.uio_resid = rwrq->ioc_cnt;
		auio.uio_segflg = UIO_USERSPACE;
		auio.uio_rw = (cmd == SMBIOC_READ) ? UIO_READ : UIO_WRITE;
		auio.uio_td = td;
		if (cmd == SMBIOC_READ)
			error = smb_read(ssp, rwrq->ioc_fh, &auio, scred);
		else
			error = smb_write(ssp, rwrq->ioc_fh, &auio, scred);
		rwrq->ioc_cnt -= auio.uio_resid;
		break;
	    }
	    default:
		error = ENODEV;
	}
out:
	free(scred, M_NSMBDEV);
	SMB_UNLOCK();
	return error;
}

static int
nsmb_dev_load(module_t mod, int cmd, void *arg)
{
	int error = 0;

	switch (cmd) {
	    case MOD_LOAD:
		error = smb_sm_init();
		if (error)
			break;
		error = smb_iod_init();
		if (error) {
			smb_sm_done();
			break;
		}
		error = nsmb_dev_init();
		if (error)
			break;
		sx_init(&smb_lock, "samba device lock");
		break;
	    case MOD_UNLOAD:
		smb_iod_done();
		error = smb_sm_done();
		if (error)
			break;
		nsmb_dev_destroy();
		sx_destroy(&smb_lock);
		break;
	    default:
		error = EINVAL;
		break;
	}
	return error;
}

DEV_MODULE (dev_netsmb, nsmb_dev_load, 0);

int
smb_dev2share(int fd, int mode, struct smb_cred *scred,
	struct smb_share **sspp, struct smb_dev **ssdp)
{
	struct file *fp, *fptmp;
	struct smb_dev *sdp;
	struct smb_share *ssp;
	struct thread *td;
	int error;

	td = curthread;
	error = fget(td, fd, &cap_read_rights, &fp);
	if (error)
		return (error);
	fptmp = td->td_fpop;
	td->td_fpop = fp;
	error = devfs_get_cdevpriv((void **)&sdp);
	td->td_fpop = fptmp;
	fdrop(fp, td);
	if (error || sdp == NULL)
		return (error);
	SMB_LOCK();
	*ssdp = sdp;
	ssp = sdp->sd_share;
	if (ssp == NULL) {
		SMB_UNLOCK();
		return (ENOTCONN);
	}
	error = smb_share_get(ssp, scred);
	if (error == 0) {
		sdp->refcount++;
		*sspp = ssp;
	}
	SMB_UNLOCK();
	return error;
}

