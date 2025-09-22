/*	$OpenBSD: kvm_file2.c,v 1.58 2024/02/11 21:29:12 bluhm Exp $	*/

/*
 * Copyright (c) 2009 Todd C. Miller <millert@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*-
 * Copyright (c) 1989, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Extended file list interface for kvm.  pstat, fstat and netstat are
 * users of this code, so we've factored it out into a separate module.
 * Thus, we keep this grunge out of the other kvm applications (i.e.,
 * most other applications are interested only in open/close/read/nlist).
 */

#define __need_process

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/uio.h>
#include <sys/ucred.h>
#include <sys/proc.h>
#define _KERNEL
#include <sys/file.h>
#include <sys/mount.h>
#undef _KERNEL
#include <sys/vnode.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/event.h>
#include <sys/eventvar.h>
#include <sys/un.h>
#include <sys/unpcb.h>
#include <sys/filedesc.h>
#include <sys/mbuf.h>
#include <sys/pipe.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/specdev.h>

#define _KERNEL
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#undef _KERNEL

#include <nfs/nfsproto.h>
#include <nfs/rpcv2.h>
#include <nfs/nfs.h>
#include <nfs/nfsnode.h>

#include <msdosfs/bpb.h>
#include <msdosfs/denode.h>
#include <msdosfs/msdosfsmount.h>

#include <net/route.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/tcp.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>

#ifdef INET6
#include <netinet/ip6.h>
#endif

#include <fcntl.h>
#include <nlist.h>
#include <kvm.h>
#include <db.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#include "kvm_private.h"
#include "kvm_file.h"

static struct kinfo_file *kvm_deadfile_byfile(kvm_t *, int, int,
    size_t, int *);
static struct kinfo_file *kvm_deadfile_byid(kvm_t *, int, int,
    size_t, int *);
static int fill_file(kvm_t *, struct kinfo_file *, struct file *, u_long,
    struct vnode *, struct process *, int, pid_t);
static int filestat(kvm_t *, struct kinfo_file *, struct vnode *);

LIST_HEAD(processlist, process);

struct kinfo_file *
kvm_getfiles(kvm_t *kd, int op, int arg, size_t esize, int *cnt)
{
	int mib[6], rv;
	void *filebase;
	size_t size;

	if (ISALIVE(kd)) {
		mib[0] = CTL_KERN;
		mib[1] = KERN_FILE;
		mib[2] = op;
		mib[3] = arg;
		mib[4] = esize;

		do {
			mib[5] = 0;

			/* find size and alloc buffer */
			rv = sysctl(mib, 6, NULL, &size, NULL, 0);
			if (rv == -1) {
				if (errno != ESRCH && kd->vmfd != -1)
					goto deadway;
				_kvm_syserr(kd, kd->program, "kvm_getfiles");
				return (NULL);
			}

			size += size / 8; /* add ~10% */

			filebase = _kvm_realloc(kd, kd->filebase, size);
			if (filebase == NULL)
				return (NULL);

			kd->filebase = filebase;

			/* get actual data */
			mib[5] = size / esize;
			rv = sysctl(mib, 6, kd->filebase, &size, NULL, 0);
			if (rv == -1 && errno != ENOMEM) {
				_kvm_syserr(kd, kd->program,
				    "kvm_getfiles");
				return (NULL);
			}
		} while (rv == -1);

		*cnt = size / esize;
		return (kd->filebase);
	} else {
		if (esize > sizeof(struct kinfo_file)) {
			_kvm_syserr(kd, kd->program,
			    "kvm_getfiles: unknown fields requested: libkvm out of date?");
			return (NULL);
		}
	    deadway:
		switch (op) {
		case KERN_FILE_BYFILE:
			return (kvm_deadfile_byfile(kd, op, arg, esize, cnt));
			break;
		case KERN_FILE_BYPID:
		case KERN_FILE_BYUID:
			return (kvm_deadfile_byid(kd, op, arg, esize, cnt));
			break;
		default:
			return (NULL);
		}
	}
}

static struct kinfo_file *
kvm_deadfile_byfile(kvm_t *kd, int op, int arg, size_t esize, int *cnt)
{
	struct nlist nl[3], *p;
	size_t buflen;
	int n = 0;
	char *where;
	struct kinfo_file kf;
	struct file *fp, file;
	struct filelist filehead;
	int nfiles;

	nl[0].n_name = "_filehead";
	nl[1].n_name = "_numfiles";
	nl[2].n_name = 0;

	if (kvm_nlist(kd, nl) != 0) {
		for (p = nl; p->n_type != 0; ++p)
			;
		_kvm_err(kd, kd->program,
			 "%s: no such symbol", p->n_name);
		return (NULL);
	}
	if (KREAD(kd, nl[0].n_value, &filehead)) {
		_kvm_err(kd, kd->program, "can't read filehead");
		return (NULL);
	}
	if (KREAD(kd, nl[1].n_value, &nfiles)) {
		_kvm_err(kd, kd->program, "can't read nfiles");
		return (NULL);
	}
	where = _kvm_reallocarray(kd, kd->filebase, nfiles, esize);
	if (where == NULL)
		return (NULL);

	kd->filebase = (void *)where;
	buflen = nfiles * esize;

	for (fp = LIST_FIRST(&filehead);
	    fp != NULL && esize <= buflen;
	    fp = LIST_NEXT(&file, f_list)) {
		if (KREAD(kd, (u_long)fp, &file)) {
			_kvm_err(kd, kd->program, "can't read kfp");
			return (NULL);
		}
		if (file.f_count == 0)
			continue;
		if (arg != 0 && file.f_type != arg)
			continue;
		if (fill_file(kd, &kf, &file, (u_long)fp, NULL, NULL, 0, 0)
		    == -1)
			return (NULL);
		memcpy(where, &kf, esize);
		where += esize;
		buflen -= esize;
		n++;
	}
	if (n != nfiles) {
		_kvm_err(kd, kd->program, "inconsistent nfiles");
		return (NULL);
	}
	*cnt = n;
	return (kd->filebase);
}

static struct kinfo_file *
kvm_deadfile_byid(kvm_t *kd, int op, int arg, size_t esize, int *cnt)
{
	size_t buflen;
	struct nlist nl[4], *np;
	int n = 0, matched = 0;
	char *where;
	struct kinfo_file kf;
	struct file *fp, file;
	struct filelist filehead;
	struct filedesc0 filed0;
#define filed	filed0.fd_fd
	struct processlist allprocess;
	struct process *pr, process;
	struct ucred ucred;
	char *filebuf = NULL;
	int i, nfiles;

	nl[0].n_name = "_filehead";
	nl[1].n_name = "_numfiles";
	nl[2].n_name = "_allprocess";
	nl[3].n_name = 0;

	if (kvm_nlist(kd, nl) != 0) {
		for (np = nl; np->n_type != 0; ++np)
			;
		_kvm_err(kd, kd->program,
			 "%s: no such symbol", np->n_name);
		return (NULL);
	}
	if (KREAD(kd, nl[0].n_value, &filehead)) {
		_kvm_err(kd, kd->program, "can't read filehead");
		return (NULL);
	}
	if (KREAD(kd, nl[1].n_value, &nfiles)) {
		_kvm_err(kd, kd->program, "can't read nfiles");
		return (NULL);
	}
	if (KREAD(kd, nl[2].n_value, &allprocess)) {
		_kvm_err(kd, kd->program, "can't read allprocess");
		return (NULL);
	}
	/* this may be more room than we need but counting is expensive */
	where = _kvm_reallocarray(kd, kd->filebase, nfiles + 10, esize);
	if (where == NULL)
		return (NULL);

	kd->filebase = (void *)where;
	buflen = (nfiles + 10) * esize;

	if (op != KERN_FILE_BYPID || arg <= 0)
		matched = 1;

	for (pr = LIST_FIRST(&allprocess);
	    pr != NULL;
	    pr = LIST_NEXT(&process, ps_list)) {
		if (KREAD(kd, (u_long)pr, &process)) {
			_kvm_err(kd, kd->program, "can't read process at %lx",
			    (u_long)pr);
			goto cleanup;
		}

		/* skip system, exiting, embryonic and undead processes */
		if (process.ps_flags & (PS_SYSTEM | PS_EMBRYO | PS_EXITING))
			continue;

		if (op == KERN_FILE_BYPID) {
			/* check if this is the pid we are looking for */
			if (arg > 0 && process.ps_pid != (pid_t)arg)
				continue;
			matched = 1;
		}

		if (KREAD(kd, (u_long)process.ps_ucred, &ucred)) {
			_kvm_err(kd, kd->program, "can't read ucred at %lx",
			    (u_long)process.ps_ucred);
			goto cleanup;
		}
		process.ps_ucred = &ucred;

		if (op == KERN_FILE_BYUID && arg >= 0 &&
		    process.ps_ucred->cr_uid != (uid_t)arg) {
			/* not the uid we are looking for */
			continue;
		}

		if (KREAD(kd, (u_long)process.ps_fd, &filed0)) {
			_kvm_err(kd, kd->program, "can't read filedesc at %lx",
			    (u_long)process.ps_fd);
			goto cleanup;
		}
		if ((char *)process.ps_fd + offsetof(struct filedesc0,fd_dfiles)
		    == (char *)filed.fd_ofiles) {
			filed.fd_ofiles = filed0.fd_dfiles;
			filed.fd_ofileflags = filed0.fd_dfileflags;
		} else {
			size_t fsize;
			char *tmp = reallocarray(filebuf,
			    filed.fd_nfiles, OFILESIZE);

			fsize = filed.fd_nfiles * OFILESIZE;
			if (tmp == NULL) {
				_kvm_syserr(kd, kd->program, "realloc ofiles");
				goto cleanup;
			}
			filebuf = tmp;
			if (kvm_read(kd, (u_long)filed.fd_ofiles, filebuf,
			    fsize) != fsize) {
				_kvm_err(kd, kd->program,
				    "can't read fd_ofiles");
				goto cleanup;
			}
			filed.fd_ofiles = (void *)filebuf;
			filed.fd_ofileflags = filebuf +
			    (filed.fd_nfiles * sizeof(struct file *));
		}
		process.ps_fd = &filed;

		if (process.ps_textvp) {
			if (buflen < esize)
				goto done;
			if (fill_file(kd, &kf, NULL, 0, process.ps_textvp,
			    &process, KERN_FILE_TEXT, process.ps_pid) == -1)
				goto cleanup;
			memcpy(where, &kf, esize);
			where += esize;
			buflen -= esize;
			n++;
		}
		if (filed.fd_cdir) {
			if (buflen < esize)
				goto done;
			if (fill_file(kd, &kf, NULL, 0, filed.fd_cdir,
			    &process, KERN_FILE_CDIR, process.ps_pid) == -1)
				goto cleanup;
			memcpy(where, &kf, esize);
			where += esize;
			buflen -= esize;
			n++;
		}
		if (filed.fd_rdir) {
			if (buflen < esize)
				goto done;
			if (fill_file(kd, &kf, NULL, 0, filed.fd_rdir,
			    &process, KERN_FILE_RDIR, process.ps_pid) == -1)
				goto cleanup;
			memcpy(where, &kf, esize);
			where += esize;
			buflen -= esize;
			n++;
		}
		if (process.ps_tracevp) {
			if (buflen < esize)
				goto done;
			if (fill_file(kd, &kf, NULL, 0, process.ps_tracevp,
			    &process, KERN_FILE_TRACE, process.ps_pid) == -1)
				goto cleanup;
			memcpy(where, &kf, esize);
			where += esize;
			buflen -= esize;
			n++;
		}

		if (filed.fd_nfiles < 0 ||
		    filed.fd_lastfile >= filed.fd_nfiles ||
		    filed.fd_freefile > filed.fd_lastfile + 1) {
			_kvm_err(kd, kd->program,
			    "filedesc corrupted at %lx for pid %d",
			    (u_long)process.ps_fd, process.ps_pid);
			goto cleanup;
		}

		for (i = 0; i < filed.fd_nfiles; i++) {
			if (buflen < esize)
				goto done;
			if ((fp = filed.fd_ofiles[i]) == NULL)
				continue;
			if (KREAD(kd, (u_long)fp, &file)) {
				_kvm_err(kd, kd->program, "can't read file");
				goto cleanup;
			}
			if (fill_file(kd, &kf, &file, (u_long)fp, NULL,
			    &process, i, process.ps_pid) == -1)
				goto cleanup;
			memcpy(where, &kf, esize);
			where += esize;
			buflen -= esize;
			n++;
		}
	}
	if (!matched) {
		errno = ESRCH;
		goto cleanup;
	}
done:
	*cnt = n;
	free(filebuf);
	return (kd->filebase);
cleanup:
	free(filebuf);
	return (NULL);
}

static int
fill_file(kvm_t *kd, struct kinfo_file *kf, struct file *fp, u_long fpaddr,
    struct vnode *vp, struct process *pr, int fd, pid_t pid)
{
	struct ucred f_cred;

	memset(kf, 0, sizeof(*kf));

	kf->fd_fd = fd;		/* might not really be an fd */

	if (fp != NULL) {
		/* Fill in f_cred */
		if (KREAD(kd, (u_long)fp->f_cred, &f_cred)) {
			_kvm_err(kd, kd->program, "can't read f_cred");
			return (-1);
		}

		kf->f_fileaddr = PTRTOINT64(fpaddr);
		kf->f_flag = fp->f_flag;
		kf->f_iflags = fp->f_iflags;
		kf->f_type = fp->f_type;
		kf->f_count = fp->f_count;
		kf->f_ucred = PTRTOINT64(fp->f_cred);
		kf->f_uid = f_cred.cr_uid;
		kf->f_gid = f_cred.cr_gid;
		kf->f_ops = PTRTOINT64(fp->f_ops);
		kf->f_offset = fp->f_offset;
		kf->f_data = PTRTOINT64(fp->f_data);
		kf->f_usecount = 0;

		kf->f_rxfer = fp->f_rxfer;
		kf->f_rwfer = fp->f_wxfer;
		kf->f_seek = fp->f_seek;
		kf->f_rbytes = fp->f_rbytes;
		kf->f_wbytes = fp->f_wbytes;
	} else if (vp != NULL) {
		/* fake it */
		kf->f_type = DTYPE_VNODE;
		kf->f_flag = FREAD;
		if (fd == KERN_FILE_TRACE)
			kf->f_flag |= FWRITE;
		kf->f_data = PTRTOINT64(vp);
	}

	/* information about the object associated with this file */
	switch (kf->f_type) {
	case DTYPE_VNODE: {
		struct vnode vbuf;

		if (KREAD(kd, (u_long)(fp ? fp->f_data : vp), &vbuf)) {
			_kvm_err(kd, kd->program, "can't read vnode");
			return (-1);
		}
		vp = &vbuf;

		kf->v_un = PTRTOINT64(vp->v_un.vu_socket);
		kf->v_type = vp->v_type;
		kf->v_tag = vp->v_tag;
		kf->v_flag = vp->v_flag;
		kf->v_data = PTRTOINT64(vp->v_data);
		kf->v_mount = PTRTOINT64(vp->v_mount);

		if (vp->v_mount != NULL) {
			struct mount mount;

			if (KREAD(kd, (u_long)vp->v_mount, &mount)) {
				_kvm_err(kd, kd->program, "can't read v_mount");
				return (-1);
			}

			strlcpy(kf->f_mntonname, mount.mnt_stat.f_mntonname,
			    sizeof(kf->f_mntonname));
		}

		/* Fill in va_fsid, va_fileid, va_mode, va_size, va_rdev */
		filestat(kd, kf, vp);
		break;
	    }

	case DTYPE_SOCKET: {
		struct socket sock;
		struct sosplice ssp;
		struct protosw protosw;
		struct domain domain;

		if (KREAD(kd, (u_long)fp->f_data, &sock)) {
			_kvm_err(kd, kd->program, "can't read socket");
			return (-1);
		}

		kf->so_type = sock.so_type;
		kf->so_state = sock.so_state;
		kf->so_pcb = PTRTOINT64(sock.so_pcb);
		if (KREAD(kd, (u_long)sock.so_proto, &protosw)) {
			_kvm_err(kd, kd->program, "can't read protosw");
			return (-1);
		}
		kf->so_protocol = protosw.pr_protocol;
		if (KREAD(kd, (u_long)protosw.pr_domain, &domain)) {
			_kvm_err(kd, kd->program, "can't read domain");
			return (-1);
		}
		kf->so_family = domain.dom_family;
		kf->so_rcv_cc = sock.so_rcv.sb_cc;
		kf->so_snd_cc = sock.so_snd.sb_cc;
		if (sock.so_sp) {
			if (KREAD(kd, (u_long)sock.so_sp, &ssp)) {
				_kvm_err(kd, kd->program, "can't read splice");
				return (-1);
			}
			if (ssp.ssp_socket) {
				kf->so_splice = PTRTOINT64(ssp.ssp_socket);
				kf->so_splicelen = ssp.ssp_len;
			} else if (ssp.ssp_soback) {
				kf->so_splicelen = -1;
			}
		}
		if (!sock.so_pcb)
			break;
		switch (kf->so_family) {
		case AF_INET: {
			struct inpcb inpcb;

			if (KREAD(kd, (u_long)sock.so_pcb, &inpcb)) {
				_kvm_err(kd, kd->program, "can't read inpcb");
				return (-1);
			}
			kf->inp_ppcb = PTRTOINT64(inpcb.inp_ppcb);
			kf->inp_lport = inpcb.inp_lport;
			kf->inp_laddru[0] = inpcb.inp_laddr.s_addr;
			kf->inp_fport = inpcb.inp_fport;
			kf->inp_faddru[0] = inpcb.inp_faddr.s_addr;
			kf->inp_rtableid = inpcb.inp_rtableid;
			if (sock.so_type == SOCK_RAW)
				kf->inp_proto = inpcb.inp_ip.ip_p;
			if (protosw.pr_protocol == IPPROTO_TCP) {
				struct tcpcb tcpcb;
				if (KREAD(kd, (u_long)inpcb.inp_ppcb, &tcpcb)) {
					_kvm_err(kd, kd->program,
					    "can't read tcpcb");
					return (-1);
				}
				kf->t_rcv_wnd = tcpcb.rcv_wnd;
				kf->t_snd_wnd = tcpcb.snd_wnd;
				kf->t_snd_cwnd = tcpcb.snd_cwnd;
				kf->t_state = tcpcb.t_state;
			}
			break;
		    }
		case AF_INET6: {
			struct inpcb inpcb;
#define s6_addr32 __u6_addr.__u6_addr32

			if (KREAD(kd, (u_long)sock.so_pcb, &inpcb)) {
				_kvm_err(kd, kd->program, "can't read inpcb");
				return (-1);
			}
			kf->inp_ppcb = PTRTOINT64(inpcb.inp_ppcb);
			kf->inp_lport = inpcb.inp_lport;
			kf->inp_laddru[0] = inpcb.inp_laddr6.s6_addr32[0];
			kf->inp_laddru[1] = inpcb.inp_laddr6.s6_addr32[1];
			kf->inp_laddru[2] = inpcb.inp_laddr6.s6_addr32[2];
			kf->inp_laddru[3] = inpcb.inp_laddr6.s6_addr32[3];
			kf->inp_fport = inpcb.inp_fport;
			kf->inp_faddru[0] = inpcb.inp_laddr6.s6_addr32[0];
			kf->inp_faddru[1] = inpcb.inp_faddr6.s6_addr32[1];
			kf->inp_faddru[2] = inpcb.inp_faddr6.s6_addr32[2];
			kf->inp_faddru[3] = inpcb.inp_faddr6.s6_addr32[3];
			kf->inp_rtableid = inpcb.inp_rtableid;
			if (sock.so_type == SOCK_RAW)
				kf->inp_proto = inpcb.inp_ipv6.ip6_nxt;
			if (protosw.pr_protocol == IPPROTO_TCP) {
				struct tcpcb tcpcb;
				if (KREAD(kd, (u_long)inpcb.inp_ppcb, &tcpcb)) {
					_kvm_err(kd, kd->program,
					    "can't read tcpcb");
					return (-1);
				}
				kf->t_rcv_wnd = tcpcb.rcv_wnd;
				kf->t_snd_wnd = tcpcb.snd_wnd;
				kf->t_snd_cwnd = tcpcb.snd_cwnd;
				kf->t_state = tcpcb.t_state;
			}
			break;
		    }
		case AF_UNIX: {
			struct unpcb unpcb;

			if (KREAD(kd, (u_long)sock.so_pcb, &unpcb)) {
				_kvm_err(kd, kd->program, "can't read unpcb");
				return (-1);
			}
			kf->f_msgcount	= unpcb.unp_msgcount;
			kf->unp_conn	= PTRTOINT64(unpcb.unp_conn);
			kf->unp_refs	= PTRTOINT64(
			    SLIST_FIRST(&unpcb.unp_refs));
			kf->unp_nextref	= PTRTOINT64(
			    SLIST_NEXT(&unpcb, unp_nextref));
			kf->v_un	= PTRTOINT64(unpcb.unp_vnode);
			if (unpcb.unp_addr != NULL) {
				struct mbuf mb;
				struct sockaddr_un un;

				if (KREAD(kd, (u_long)unpcb.unp_addr, &mb)) {
					_kvm_err(kd, kd->program,
					    "can't read sockaddr_un mbuf");
					return (-1);
				}
				if (KREAD(kd, (u_long)mb.m_data, &un)) {
					_kvm_err(kd, kd->program,
					    "can't read sockaddr_un");
					return (-1);
				}

				kf->unp_addr = PTRTOINT64(unpcb.unp_addr);
				memcpy(kf->unp_path, un.sun_path, un.sun_len
				    - offsetof(struct sockaddr_un,sun_path));
			}
 
			break;
		    }
		}
		break;
	    }

	case DTYPE_PIPE: {
		struct pipe pipe;

		if (KREAD(kd, (u_long)fp->f_data, &pipe)) {
			_kvm_err(kd, kd->program, "can't read pipe");
			return (-1);
		}
		kf->pipe_peer = PTRTOINT64(pipe.pipe_peer);
		kf->pipe_state = pipe.pipe_state;
		break;
	    }

	case DTYPE_KQUEUE: {
		struct kqueue kqi;

		if (KREAD(kd, (u_long)fp->f_data, &kqi)) {
			_kvm_err(kd, kd->program, "can't read kqi");
			return (-1);
		}
		kf->kq_count = kqi.kq_count;
		kf->kq_state = kqi.kq_state;
		break;
	    }
	}

	/* per-process information for KERN_FILE_BY[PU]ID */
	if (pr != NULL) {
		kf->p_pid = pid;
		kf->p_uid = pr->ps_ucred->cr_uid;
		kf->p_gid = pr->ps_ucred->cr_gid;
		kf->p_tid = -1;
		strlcpy(kf->p_comm, pr->ps_comm, sizeof(kf->p_comm));
		if (pr->ps_fd != NULL)
			kf->fd_ofileflags = pr->ps_fd->fd_ofileflags[fd];
	}

	return (0);
}

mode_t
_kvm_getftype(enum vtype v_type)
{
	mode_t ftype = 0;

	switch (v_type) {
	case VREG:
		ftype = S_IFREG;
		break;
	case VDIR:
		ftype = S_IFDIR;
		break;
	case VBLK:
		ftype = S_IFBLK;
		break;
	case VCHR:
		ftype = S_IFCHR;
		break;
	case VLNK:
		ftype = S_IFLNK;
		break;
	case VSOCK:
		ftype = S_IFSOCK;
		break;
	case VFIFO:
		ftype = S_IFIFO;
		break;
	case VNON:
	case VBAD:
		break;
	}

	return (ftype);
}

static int
ufs_filestat(kvm_t *kd, struct kinfo_file *kf, struct vnode *vp)
{
	struct inode inode;
	struct ufs1_dinode di1;

	if (KREAD(kd, (u_long)VTOI(vp), &inode)) {
		_kvm_err(kd, kd->program, "can't read inode at %p", VTOI(vp));
		return (-1);
	}

	if (KREAD(kd, (u_long)inode.i_din1, &di1)) {
		_kvm_err(kd, kd->program, "can't read dinode at %p",
		    inode.i_din1);
		return (-1);
	}

	inode.i_din1 = &di1;

	kf->va_fsid = inode.i_dev & 0xffff;
	kf->va_fileid = (long)inode.i_number;
	kf->va_mode = inode.i_ffs1_mode;
	kf->va_size = inode.i_ffs1_size;
	kf->va_rdev = inode.i_ffs1_rdev;
	kf->va_nlink = inode.i_ffs1_nlink;

	return (0);
}

static int
ext2fs_filestat(kvm_t *kd, struct kinfo_file *kf, struct vnode *vp)
{
	struct inode inode;
	struct ext2fs_dinode e2di;

	if (KREAD(kd, (u_long)VTOI(vp), &inode)) {
		_kvm_err(kd, kd->program, "can't read inode at %p", VTOI(vp));
		return (-1);
	}

	if (KREAD(kd, (u_long)inode.i_e2din, &e2di)) {
		_kvm_err(kd, kd->program, "can't read dinode at %p",
		    inode.i_e2din);
		return (-1);
	}

	inode.i_e2din = &e2di;

	kf->va_fsid = inode.i_dev & 0xffff;
	kf->va_fileid = (long)inode.i_number;
	kf->va_mode = inode.i_e2fs_mode;
	kf->va_size = inode.i_e2fs_size;
	kf->va_rdev = 0;	/* XXX */
	kf->va_nlink = inode.i_e2fs_nlink;

	return (0);
}

static int
msdos_filestat(kvm_t *kd, struct kinfo_file *kf, struct vnode *vp)
{
	struct denode de;
	struct msdosfsmount mp;

	if (KREAD(kd, (u_long)VTODE(vp), &de)) {
		_kvm_err(kd, kd->program, "can't read denode at %p", VTODE(vp));
		return (-1);
	}
	if (KREAD(kd, (u_long)de.de_pmp, &mp)) {
		_kvm_err(kd, kd->program, "can't read mount struct at %p",
		    de.de_pmp);
		return (-1);
	}

	kf->va_fsid = de.de_dev & 0xffff;
	kf->va_fileid = 0; /* XXX see msdosfs_vptofh() for more info */
	kf->va_mode = (mp.pm_mask & 0777) | _kvm_getftype(vp->v_type);
	kf->va_size = de.de_FileSize;
	kf->va_rdev = 0;  /* msdosfs doesn't support device files */
	kf->va_nlink = 1;

	return (0);
}

static int
nfs_filestat(kvm_t *kd, struct kinfo_file *kf, struct vnode *vp)
{
	struct nfsnode nfsnode;

	if (KREAD(kd, (u_long)VTONFS(vp), &nfsnode)) {
		_kvm_err(kd, kd->program, "can't read nfsnode at %p",
		    VTONFS(vp));
		return (-1);
	}
	kf->va_fsid = nfsnode.n_vattr.va_fsid;
	kf->va_fileid = nfsnode.n_vattr.va_fileid;
	kf->va_size = nfsnode.n_size;
	kf->va_rdev = nfsnode.n_vattr.va_rdev;
	kf->va_mode = (mode_t)nfsnode.n_vattr.va_mode | _kvm_getftype(vp->v_type);
	kf->va_nlink = nfsnode.n_vattr.va_nlink;

	return (0);
}

static int
spec_filestat(kvm_t *kd, struct kinfo_file *kf, struct vnode *vp)
{
	struct specinfo		specinfo;
	struct vnode		parent;

	if (KREAD(kd, (u_long)vp->v_specinfo, &specinfo)) {
		_kvm_err(kd, kd->program, "can't read specinfo at %p",
		     vp->v_specinfo);
		return (-1);
	}

	vp->v_specinfo = &specinfo;

	if (KREAD(kd, (u_long)vp->v_specparent, &parent)) {
		_kvm_err(kd, kd->program, "can't read parent vnode at %p",
		     vp->v_specparent);
		return (-1);
	}

	if (ufs_filestat(kd, kf, vp))
		return (-1);

	return (0);
}

static int
filestat(kvm_t *kd, struct kinfo_file *kf, struct vnode *vp)
{
	int ret = 0;

	if (vp->v_type != VNON && vp->v_type != VBAD) {
		switch (vp->v_tag) {
		case VT_UFS:
		case VT_MFS:
			ret = ufs_filestat(kd, kf, vp);
			break;
		case VT_NFS:
			ret = nfs_filestat(kd, kf, vp);
			break;
		case VT_EXT2FS:
			ret = ext2fs_filestat(kd, kf, vp);
			break;
		case VT_ISOFS:
			ret = _kvm_stat_cd9660(kd, kf, vp);
			break;
		case VT_MSDOSFS:
			ret = msdos_filestat(kd, kf, vp);
			break;
		case VT_UDF:
			ret = _kvm_stat_udf(kd, kf, vp);
			break;
		case VT_NTFS:
			ret = _kvm_stat_ntfs(kd, kf, vp);
			break;
		case VT_NON:
			if (vp->v_flag & VCLONE)
				ret = spec_filestat(kd, kf, vp);
			break;
		default:
			ret = -1;
			break;
		}
	}
	return (ret);
}
