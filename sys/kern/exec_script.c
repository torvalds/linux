/*	$OpenBSD: exec_script.c,v 1.48 2019/07/15 04:11:03 visa Exp $	*/
/*	$NetBSD: exec_script.c,v 1.13 1996/02/04 02:15:06 christos Exp $	*/

/*
 * Copyright (c) 1993, 1994 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/vnode.h>
#include <sys/lock.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/exec.h>

#include <sys/exec_script.h>


/*
 * exec_script_makecmds(): Check if it's an executable shell script.
 *
 * Given a proc pointer and an exec package pointer, see if the referent
 * of the epp is in shell script.  If it is, then set things up so that
 * the script can be run.  This involves preparing the address space
 * and arguments for the shell which will run the script.
 *
 * This function is ultimately responsible for creating a set of vmcmds
 * which can be used to build the process's vm space and inserting them
 * into the exec package.
 */
int
exec_script_makecmds(struct proc *p, struct exec_package *epp)
{
	int error, hdrlinelen, shellnamelen, shellarglen;
	char *hdrstr = epp->ep_hdr;
	char *cp, *shellname, *shellarg, *oldpnbuf;
	char **shellargp = NULL, **tmpsap;
	struct vnode *scriptvp;
	uid_t script_uid = -1;
	gid_t script_gid = -1;
	u_short script_sbits;

	/*
	 * remember the old vp and pnbuf for later, so we can restore
	 * them if check_exec() fails.
	 */
	scriptvp = epp->ep_vp;
	oldpnbuf = epp->ep_ndp->ni_cnd.cn_pnbuf;

	/*
	 * if the magic isn't that of a shell script, or we've already
	 * done shell script processing for this exec, punt on it.
	 */
	if ((epp->ep_flags & EXEC_INDIR) != 0 ||
	    epp->ep_hdrvalid < EXEC_SCRIPT_MAGICLEN ||
	    strncmp(hdrstr, EXEC_SCRIPT_MAGIC, EXEC_SCRIPT_MAGICLEN))
		return ENOEXEC;

	/*
	 * check that the shell spec is terminated by a newline,
	 * and that it isn't too large.  Don't modify the
	 * buffer unless we're ready to commit to handling it.
	 * (The latter requirement means that we have to check
	 * for both spaces and tabs later on.)
	 */
	hdrlinelen = min(epp->ep_hdrvalid, MAXINTERP);
	for (cp = hdrstr + EXEC_SCRIPT_MAGICLEN; cp < hdrstr + hdrlinelen;
	    cp++) {
		if (*cp == '\n') {
			*cp = '\0';
			break;
		}
	}
	if (cp >= hdrstr + hdrlinelen)
		return ENOEXEC;

	shellname = NULL;
	shellarg = NULL;
	shellarglen = 0;

	/* strip spaces before the shell name */
	for (cp = hdrstr + EXEC_SCRIPT_MAGICLEN; *cp == ' ' || *cp == '\t';
	    cp++)
		;

	/* collect the shell name; remember its length for later */
	shellname = cp;
	shellnamelen = 0;
	if (*cp == '\0')
		goto check_shell;
	for ( /* cp = cp */ ; *cp != '\0' && *cp != ' ' && *cp != '\t'; cp++)
		shellnamelen++;
	if (*cp == '\0')
		goto check_shell;
	*cp++ = '\0';

	/* skip spaces before any argument */
	for ( /* cp = cp */ ; *cp == ' ' || *cp == '\t'; cp++)
		;
	if (*cp == '\0')
		goto check_shell;

	/*
	 * collect the shell argument.  everything after the shell name
	 * is passed as ONE argument; that's the correct (historical)
	 * behaviour.
	 */
	shellarg = cp;
	for ( /* cp = cp */ ; *cp != '\0'; cp++)
		shellarglen++;
	*cp++ = '\0';

check_shell:
	/*
	 * MNT_NOSUID and STRC are already taken care of by check_exec,
	 * so we don't need to worry about them now or later.
	 */
	script_sbits = epp->ep_vap->va_mode & (VSUID | VSGID);
	if (script_sbits != 0) {
		script_uid = epp->ep_vap->va_uid;
		script_gid = epp->ep_vap->va_gid;
	}
	/*
	 * if the script isn't readable, or it's set-id, then we've
	 * gotta supply a "/dev/fd/..." for the shell to read.
	 * Note that stupid shells (csh) do the wrong thing, and
	 * close all open fd's when they start.  That kills this
	 * method of implementing "safe" set-id and x-only scripts.
	 */
	vn_lock(scriptvp, LK_EXCLUSIVE|LK_RETRY);
	error = VOP_ACCESS(scriptvp, VREAD, p->p_ucred, p);
	VOP_UNLOCK(scriptvp);
	if (error == EACCES || script_sbits) {
		struct file *fp;

#ifdef DIAGNOSTIC
		if (epp->ep_flags & EXEC_HASFD)
			panic("exec_script_makecmds: epp already has a fd");
#endif

		fdplock(p->p_fd);
		error = falloc(p, &fp, &epp->ep_fd);
		if (error) {
			fdpunlock(p->p_fd);
			goto fail;
		}

		epp->ep_flags |= EXEC_HASFD;
		fp->f_type = DTYPE_VNODE;
		fp->f_ops = &vnops;
		fp->f_data = (caddr_t) scriptvp;
		fp->f_flag = FREAD;
		fdinsert(p->p_fd, epp->ep_fd, 0, fp);
		fdpunlock(p->p_fd);
		FRELE(fp, p);
	}

	/* set up the parameters for the recursive check_exec() call */
	epp->ep_ndp->ni_dirfd = AT_FDCWD;
	epp->ep_ndp->ni_dirp = shellname;
	epp->ep_ndp->ni_segflg = UIO_SYSSPACE;
	epp->ep_flags |= EXEC_INDIR;

	/* and set up the fake args list, for later */
	shellargp = mallocarray(4, sizeof(char *), M_EXEC, M_WAITOK);
	tmpsap = shellargp;
	*tmpsap = malloc(shellnamelen + 1, M_EXEC, M_WAITOK);
	strlcpy(*tmpsap++, shellname, shellnamelen + 1);
	if (shellarg != NULL) {
		*tmpsap = malloc(shellarglen + 1, M_EXEC, M_WAITOK);
		strlcpy(*tmpsap++, shellarg, shellarglen + 1);
	}
	*tmpsap = malloc(MAXPATHLEN, M_EXEC, M_WAITOK);
	if ((epp->ep_flags & EXEC_HASFD) == 0) {
		error = copyinstr(epp->ep_name, *tmpsap, MAXPATHLEN,
		    NULL);
		if (error != 0) {
			*(tmpsap + 1) = NULL;
			goto fail;
		}
	} else
		snprintf(*tmpsap, MAXPATHLEN, "/dev/fd/%d", epp->ep_fd);
	tmpsap++;
	*tmpsap = NULL;

	/*
	 * mark the header we have as invalid; check_exec will read
	 * the header from the new executable
	 */
	epp->ep_hdrvalid = 0;

	if ((error = check_exec(p, epp)) == 0) {
		/* note that we've clobbered the header */
		epp->ep_flags |= EXEC_DESTR;

		/*
		 * It succeeded.  Unlock the script and
		 * close it if we aren't using it any more.
		 * Also, set things up so that the fake args
		 * list will be used.
		 */
		if ((epp->ep_flags & EXEC_HASFD) == 0)
			vn_close(scriptvp, FREAD, p->p_ucred, p);

		/* free the old pathname buffer */
		pool_put(&namei_pool, oldpnbuf);

		epp->ep_flags |= (EXEC_HASARGL | EXEC_SKIPARG);
		epp->ep_fa = shellargp;
		/*
		 * set things up so that set-id scripts will be
		 * handled appropriately
		 */
		epp->ep_vap->va_mode |= script_sbits;
		if (script_sbits & VSUID)
			epp->ep_vap->va_uid = script_uid;
		if (script_sbits & VSGID)
			epp->ep_vap->va_gid = script_gid;
		return (0);
	}

	/* XXX oldpnbuf not set for "goto fail" path */
	epp->ep_ndp->ni_cnd.cn_pnbuf = oldpnbuf;
fail:
	/* note that we've clobbered the header */
	epp->ep_flags |= EXEC_DESTR;

	/* kill the opened file descriptor, else close the file */
	if (epp->ep_flags & EXEC_HASFD) {
		epp->ep_flags &= ~EXEC_HASFD;
		fdplock(p->p_fd);
		/* fdrelease() unlocks p->p_fd. */
		(void) fdrelease(p, epp->ep_fd);
	} else
		vn_close(scriptvp, FREAD, p->p_ucred, p);

	pool_put(&namei_pool, epp->ep_ndp->ni_cnd.cn_pnbuf);

	/* free the fake arg list, because we're not returning it */
	if (shellargp != NULL) {
		free(shellargp[0], M_EXEC, shellnamelen + 1);
		if (shellargp[2] != NULL) {
			free(shellargp[1], M_EXEC, shellarglen + 1);
			free(shellargp[2], M_EXEC, MAXPATHLEN);
		} else
			free(shellargp[1], M_EXEC, MAXPATHLEN);
		free(shellargp, M_EXEC, 4 * sizeof(char *));
	}

	/*
	 * free any vmspace-creation commands,
	 * and release their references
	 */
	kill_vmcmds(&epp->ep_vmcmds);

	return error;
}
