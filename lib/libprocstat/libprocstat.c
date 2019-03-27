/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2017 Dell EMC
 * Copyright (c) 2009 Stanislav Sedov <stas@FreeBSD.org>
 * Copyright (c) 1988, 1993
 *      The Regents of the University of California.  All rights reserved.
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
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/elf.h>
#include <sys/time.h>
#include <sys/resourcevar.h>
#define	_WANT_UCRED
#include <sys/ucred.h>
#undef _WANT_UCRED
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/socket.h>
#define	_WANT_SOCKET
#include <sys/socketvar.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/un.h>
#define	_WANT_UNPCB
#include <sys/unpcb.h>
#include <sys/sysctl.h>
#include <sys/tty.h>
#include <sys/filedesc.h>
#include <sys/queue.h>
#define	_WANT_FILE
#include <sys/file.h>
#include <sys/conf.h>
#include <sys/ksem.h>
#include <sys/mman.h>
#include <sys/capsicum.h>
#include <sys/ptrace.h>
#define	_KERNEL
#include <sys/mount.h>
#include <sys/pipe.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <fs/devfs/devfs.h>
#include <fs/devfs/devfs_int.h>
#undef _KERNEL
#include <nfs/nfsproto.h>
#include <nfsclient/nfs.h>
#include <nfsclient/nfsnode.h>

#include <vm/vm.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>

#include <net/route.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#define	_WANT_INPCB
#include <netinet/in_pcb.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <kvm.h>
#include <libutil.h>
#include <limits.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>

#include <libprocstat.h>
#include "libprocstat_internal.h"
#include "common_kvm.h"
#include "core.h"

int     statfs(const char *, struct statfs *);	/* XXX */

#define	PROCSTAT_KVM	1
#define	PROCSTAT_SYSCTL	2
#define	PROCSTAT_CORE	3

static char	**getargv(struct procstat *procstat, struct kinfo_proc *kp,
    size_t nchr, int env);
static char	*getmnton(kvm_t *kd, struct mount *m);
static struct kinfo_vmentry *	kinfo_getvmmap_core(struct procstat_core *core,
    int *cntp);
static Elf_Auxinfo	*procstat_getauxv_core(struct procstat_core *core,
    unsigned int *cntp);
static Elf_Auxinfo	*procstat_getauxv_sysctl(pid_t pid, unsigned int *cntp);
static struct filestat_list	*procstat_getfiles_kvm(
    struct procstat *procstat, struct kinfo_proc *kp, int mmapped);
static struct filestat_list	*procstat_getfiles_sysctl(
    struct procstat *procstat, struct kinfo_proc *kp, int mmapped);
static int	procstat_get_pipe_info_sysctl(struct filestat *fst,
    struct pipestat *pipe, char *errbuf);
static int	procstat_get_pipe_info_kvm(kvm_t *kd, struct filestat *fst,
    struct pipestat *pipe, char *errbuf);
static int	procstat_get_pts_info_sysctl(struct filestat *fst,
    struct ptsstat *pts, char *errbuf);
static int	procstat_get_pts_info_kvm(kvm_t *kd, struct filestat *fst,
    struct ptsstat *pts, char *errbuf);
static int	procstat_get_sem_info_sysctl(struct filestat *fst,
    struct semstat *sem, char *errbuf);
static int	procstat_get_sem_info_kvm(kvm_t *kd, struct filestat *fst,
    struct semstat *sem, char *errbuf);
static int	procstat_get_shm_info_sysctl(struct filestat *fst,
    struct shmstat *shm, char *errbuf);
static int	procstat_get_shm_info_kvm(kvm_t *kd, struct filestat *fst,
    struct shmstat *shm, char *errbuf);
static int	procstat_get_socket_info_sysctl(struct filestat *fst,
    struct sockstat *sock, char *errbuf);
static int	procstat_get_socket_info_kvm(kvm_t *kd, struct filestat *fst,
    struct sockstat *sock, char *errbuf);
static int	to_filestat_flags(int flags);
static int	procstat_get_vnode_info_kvm(kvm_t *kd, struct filestat *fst,
    struct vnstat *vn, char *errbuf);
static int	procstat_get_vnode_info_sysctl(struct filestat *fst,
    struct vnstat *vn, char *errbuf);
static gid_t	*procstat_getgroups_core(struct procstat_core *core,
    unsigned int *count);
static gid_t *	procstat_getgroups_kvm(kvm_t *kd, struct kinfo_proc *kp,
    unsigned int *count);
static gid_t	*procstat_getgroups_sysctl(pid_t pid, unsigned int *count);
static struct kinfo_kstack	*procstat_getkstack_sysctl(pid_t pid,
    int *cntp);
static int	procstat_getosrel_core(struct procstat_core *core,
    int *osrelp);
static int	procstat_getosrel_kvm(kvm_t *kd, struct kinfo_proc *kp,
    int *osrelp);
static int	procstat_getosrel_sysctl(pid_t pid, int *osrelp);
static int	procstat_getpathname_core(struct procstat_core *core,
    char *pathname, size_t maxlen);
static int	procstat_getpathname_sysctl(pid_t pid, char *pathname,
    size_t maxlen);
static int	procstat_getrlimit_core(struct procstat_core *core, int which,
    struct rlimit* rlimit);
static int	procstat_getrlimit_kvm(kvm_t *kd, struct kinfo_proc *kp,
    int which, struct rlimit* rlimit);
static int	procstat_getrlimit_sysctl(pid_t pid, int which,
    struct rlimit* rlimit);
static int	procstat_getumask_core(struct procstat_core *core,
    unsigned short *maskp);
static int	procstat_getumask_kvm(kvm_t *kd, struct kinfo_proc *kp,
    unsigned short *maskp);
static int	procstat_getumask_sysctl(pid_t pid, unsigned short *maskp);
static int	vntype2psfsttype(int type);

void
procstat_close(struct procstat *procstat)
{

	assert(procstat);
	if (procstat->type == PROCSTAT_KVM)
		kvm_close(procstat->kd);
	else if (procstat->type == PROCSTAT_CORE)
		procstat_core_close(procstat->core);
	procstat_freeargv(procstat);
	procstat_freeenvv(procstat);
	free(procstat);
}

struct procstat *
procstat_open_sysctl(void)
{
	struct procstat *procstat;

	procstat = calloc(1, sizeof(*procstat));
	if (procstat == NULL) {
		warn("malloc()");
		return (NULL);
	}
	procstat->type = PROCSTAT_SYSCTL;
	return (procstat);
}

struct procstat *
procstat_open_kvm(const char *nlistf, const char *memf)
{
	struct procstat *procstat;
	kvm_t *kd;
	char buf[_POSIX2_LINE_MAX];

	procstat = calloc(1, sizeof(*procstat));
	if (procstat == NULL) {
		warn("malloc()");
		return (NULL);
	}
	kd = kvm_openfiles(nlistf, memf, NULL, O_RDONLY, buf);
	if (kd == NULL) {
		warnx("kvm_openfiles(): %s", buf);
		free(procstat);
		return (NULL);
	}
	procstat->type = PROCSTAT_KVM;
	procstat->kd = kd;
	return (procstat);
}

struct procstat *
procstat_open_core(const char *filename)
{
	struct procstat *procstat;
	struct procstat_core *core;

	procstat = calloc(1, sizeof(*procstat));
	if (procstat == NULL) {
		warn("malloc()");
		return (NULL);
	}
	core = procstat_core_open(filename);
	if (core == NULL) {
		free(procstat);
		return (NULL);
	}
	procstat->type = PROCSTAT_CORE;
	procstat->core = core;
	return (procstat);
}

struct kinfo_proc *
procstat_getprocs(struct procstat *procstat, int what, int arg,
    unsigned int *count)
{
	struct kinfo_proc *p0, *p;
	size_t len, olen;
	int name[4];
	int cnt;
	int error;

	assert(procstat);
	assert(count);
	p = NULL;
	if (procstat->type == PROCSTAT_KVM) {
		*count = 0;
		p0 = kvm_getprocs(procstat->kd, what, arg, &cnt);
		if (p0 == NULL || cnt <= 0)
			return (NULL);
		*count = cnt;
		len = *count * sizeof(*p);
		p = malloc(len);
		if (p == NULL) {
			warnx("malloc(%zu)", len);
			goto fail;
		}
		bcopy(p0, p, len);
		return (p);
	} else if (procstat->type == PROCSTAT_SYSCTL) {
		len = 0;
		name[0] = CTL_KERN;
		name[1] = KERN_PROC;
		name[2] = what;
		name[3] = arg;
		error = sysctl(name, nitems(name), NULL, &len, NULL, 0);
		if (error < 0 && errno != EPERM) {
			warn("sysctl(kern.proc)");
			goto fail;
		}
		if (len == 0) {
			warnx("no processes?");
			goto fail;
		}
		do {
			len += len / 10;
			p = reallocf(p, len);
			if (p == NULL) {
				warnx("reallocf(%zu)", len);
				goto fail;
			}
			olen = len;
			error = sysctl(name, nitems(name), p, &len, NULL, 0);
		} while (error < 0 && errno == ENOMEM && olen == len);
		if (error < 0 && errno != EPERM) {
			warn("sysctl(kern.proc)");
			goto fail;
		}
		/* Perform simple consistency checks. */
		if ((len % sizeof(*p)) != 0 || p->ki_structsize != sizeof(*p)) {
			warnx("kinfo_proc structure size mismatch (len = %zu)", len);
			goto fail;
		}
		*count = len / sizeof(*p);
		return (p);
	} else if (procstat->type == PROCSTAT_CORE) {
		p = procstat_core_get(procstat->core, PSC_TYPE_PROC, NULL,
		    &len);
		if ((len % sizeof(*p)) != 0 || p->ki_structsize != sizeof(*p)) {
			warnx("kinfo_proc structure size mismatch");
			goto fail;
		}
		*count = len / sizeof(*p);
		return (p);
	} else {
		warnx("unknown access method: %d", procstat->type);
		return (NULL);
	}
fail:
	if (p)
		free(p);
	return (NULL);
}

void
procstat_freeprocs(struct procstat *procstat __unused, struct kinfo_proc *p)
{

	if (p != NULL)
		free(p);
	p = NULL;
}

struct filestat_list *
procstat_getfiles(struct procstat *procstat, struct kinfo_proc *kp, int mmapped)
{

	switch(procstat->type) {
	case PROCSTAT_KVM:
		return (procstat_getfiles_kvm(procstat, kp, mmapped));
	case PROCSTAT_SYSCTL:
	case PROCSTAT_CORE:
		return (procstat_getfiles_sysctl(procstat, kp, mmapped));
	default:
		warnx("unknown access method: %d", procstat->type);
		return (NULL);
	}
}

void
procstat_freefiles(struct procstat *procstat, struct filestat_list *head)
{
	struct filestat *fst, *tmp;

	STAILQ_FOREACH_SAFE(fst, head, next, tmp) {
		if (fst->fs_path != NULL)
			free(fst->fs_path);
		free(fst);
	}
	free(head);
	if (procstat->vmentries != NULL) {
		free(procstat->vmentries);
		procstat->vmentries = NULL;
	}
	if (procstat->files != NULL) {
		free(procstat->files);
		procstat->files = NULL;
	}
}

static struct filestat *
filestat_new_entry(void *typedep, int type, int fd, int fflags, int uflags,
    int refcount, off_t offset, char *path, cap_rights_t *cap_rightsp)
{
	struct filestat *entry;

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		warn("malloc()");
		return (NULL);
	}
	entry->fs_typedep = typedep;
	entry->fs_fflags = fflags;
	entry->fs_uflags = uflags;
	entry->fs_fd = fd;
	entry->fs_type = type;
	entry->fs_ref_count = refcount;
	entry->fs_offset = offset;
	entry->fs_path = path;
	if (cap_rightsp != NULL)
		entry->fs_cap_rights = *cap_rightsp;
	else
		cap_rights_init(&entry->fs_cap_rights);
	return (entry);
}

static struct vnode *
getctty(kvm_t *kd, struct kinfo_proc *kp)
{
	struct pgrp pgrp;
	struct proc proc;
	struct session sess;
	int error;
                        
	assert(kp);
	error = kvm_read_all(kd, (unsigned long)kp->ki_paddr, &proc,
	    sizeof(proc));
	if (error == 0) {
		warnx("can't read proc struct at %p for pid %d",
		    kp->ki_paddr, kp->ki_pid);
		return (NULL);
	}
	if (proc.p_pgrp == NULL)
		return (NULL);
	error = kvm_read_all(kd, (unsigned long)proc.p_pgrp, &pgrp,
	    sizeof(pgrp));
	if (error == 0) {
		warnx("can't read pgrp struct at %p for pid %d",
		    proc.p_pgrp, kp->ki_pid);
		return (NULL);
	}
	error = kvm_read_all(kd, (unsigned long)pgrp.pg_session, &sess,
	    sizeof(sess));
	if (error == 0) {
		warnx("can't read session struct at %p for pid %d",
		    pgrp.pg_session, kp->ki_pid);
		return (NULL);
	}
	return (sess.s_ttyvp);
}

static struct filestat_list *
procstat_getfiles_kvm(struct procstat *procstat, struct kinfo_proc *kp, int mmapped)
{
	struct file file;
	struct filedesc filed;
	struct vm_map_entry vmentry;
	struct vm_object object;
	struct vmspace vmspace;
	vm_map_entry_t entryp;
	vm_map_t map;
	vm_object_t objp;
	struct vnode *vp;
	struct file **ofiles;
	struct filestat *entry;
	struct filestat_list *head;
	kvm_t *kd;
	void *data;
	int i, fflags;
	int prot, type;
	unsigned int nfiles;

	assert(procstat);
	kd = procstat->kd;
	if (kd == NULL)
		return (NULL);
	if (kp->ki_fd == NULL)
		return (NULL);
	if (!kvm_read_all(kd, (unsigned long)kp->ki_fd, &filed,
	    sizeof(filed))) {
		warnx("can't read filedesc at %p", (void *)kp->ki_fd);
		return (NULL);
	}

	/*
	 * Allocate list head.
	 */
	head = malloc(sizeof(*head));
	if (head == NULL)
		return (NULL);
	STAILQ_INIT(head);

	/* root directory vnode, if one. */
	if (filed.fd_rdir) {
		entry = filestat_new_entry(filed.fd_rdir, PS_FST_TYPE_VNODE, -1,
		    PS_FST_FFLAG_READ, PS_FST_UFLAG_RDIR, 0, 0, NULL, NULL);
		if (entry != NULL)
			STAILQ_INSERT_TAIL(head, entry, next);
	}
	/* current working directory vnode. */
	if (filed.fd_cdir) {
		entry = filestat_new_entry(filed.fd_cdir, PS_FST_TYPE_VNODE, -1,
		    PS_FST_FFLAG_READ, PS_FST_UFLAG_CDIR, 0, 0, NULL, NULL);
		if (entry != NULL)
			STAILQ_INSERT_TAIL(head, entry, next);
	}
	/* jail root, if any. */
	if (filed.fd_jdir) {
		entry = filestat_new_entry(filed.fd_jdir, PS_FST_TYPE_VNODE, -1,
		    PS_FST_FFLAG_READ, PS_FST_UFLAG_JAIL, 0, 0, NULL, NULL);
		if (entry != NULL)
			STAILQ_INSERT_TAIL(head, entry, next);
	}
	/* ktrace vnode, if one */
	if (kp->ki_tracep) {
		entry = filestat_new_entry(kp->ki_tracep, PS_FST_TYPE_VNODE, -1,
		    PS_FST_FFLAG_READ | PS_FST_FFLAG_WRITE,
		    PS_FST_UFLAG_TRACE, 0, 0, NULL, NULL);
		if (entry != NULL)
			STAILQ_INSERT_TAIL(head, entry, next);
	}
	/* text vnode, if one */
	if (kp->ki_textvp) {
		entry = filestat_new_entry(kp->ki_textvp, PS_FST_TYPE_VNODE, -1,
		    PS_FST_FFLAG_READ, PS_FST_UFLAG_TEXT, 0, 0, NULL, NULL);
		if (entry != NULL)
			STAILQ_INSERT_TAIL(head, entry, next);
	}
	/* Controlling terminal. */
	if ((vp = getctty(kd, kp)) != NULL) {
		entry = filestat_new_entry(vp, PS_FST_TYPE_VNODE, -1,
		    PS_FST_FFLAG_READ | PS_FST_FFLAG_WRITE,
		    PS_FST_UFLAG_CTTY, 0, 0, NULL, NULL);
		if (entry != NULL)
			STAILQ_INSERT_TAIL(head, entry, next);
	}

	nfiles = filed.fd_lastfile + 1;
	ofiles = malloc(nfiles * sizeof(struct file *));
	if (ofiles == NULL) {
		warn("malloc(%zu)", nfiles * sizeof(struct file *));
		goto do_mmapped;
	}
	if (!kvm_read_all(kd, (unsigned long)filed.fd_ofiles, ofiles,
	    nfiles * sizeof(struct file *))) {
		warnx("cannot read file structures at %p",
		    (void *)filed.fd_ofiles);
		free(ofiles);
		goto do_mmapped;
	}
	for (i = 0; i <= filed.fd_lastfile; i++) {
		if (ofiles[i] == NULL)
			continue;
		if (!kvm_read_all(kd, (unsigned long)ofiles[i], &file,
		    sizeof(struct file))) {
			warnx("can't read file %d at %p", i,
			    (void *)ofiles[i]);
			continue;
		}
		switch (file.f_type) {
		case DTYPE_VNODE:
			type = PS_FST_TYPE_VNODE;
			data = file.f_vnode;
			break;
		case DTYPE_SOCKET:
			type = PS_FST_TYPE_SOCKET;
			data = file.f_data;
			break;
		case DTYPE_PIPE:
			type = PS_FST_TYPE_PIPE;
			data = file.f_data;
			break;
		case DTYPE_FIFO:
			type = PS_FST_TYPE_FIFO;
			data = file.f_vnode;
			break;
#ifdef DTYPE_PTS
		case DTYPE_PTS:
			type = PS_FST_TYPE_PTS;
			data = file.f_data;
			break;
#endif
		case DTYPE_SEM:
			type = PS_FST_TYPE_SEM;
			data = file.f_data;
			break;
		case DTYPE_SHM:
			type = PS_FST_TYPE_SHM;
			data = file.f_data;
			break;
		case DTYPE_PROCDESC:
			type = PS_FST_TYPE_PROCDESC;
			data = file.f_data;
			break;
		case DTYPE_DEV:
			type = PS_FST_TYPE_DEV;
			data = file.f_data;
			break;
		default:
			continue;
		}
		/* XXXRW: No capability rights support for kvm yet. */
		entry = filestat_new_entry(data, type, i,
		    to_filestat_flags(file.f_flag), 0, 0, 0, NULL, NULL);
		if (entry != NULL)
			STAILQ_INSERT_TAIL(head, entry, next);
	}
	free(ofiles);

do_mmapped:

	/*
	 * Process mmapped files if requested.
	 */
	if (mmapped) {
		if (!kvm_read_all(kd, (unsigned long)kp->ki_vmspace, &vmspace,
		    sizeof(vmspace))) {
			warnx("can't read vmspace at %p",
			    (void *)kp->ki_vmspace);
			goto exit;
		}
		map = &vmspace.vm_map;

		for (entryp = map->header.next;
		    entryp != &kp->ki_vmspace->vm_map.header;
		    entryp = vmentry.next) {
			if (!kvm_read_all(kd, (unsigned long)entryp, &vmentry,
			    sizeof(vmentry))) {
				warnx("can't read vm_map_entry at %p",
				    (void *)entryp);
				continue;
			}
			if (vmentry.eflags & MAP_ENTRY_IS_SUB_MAP)
				continue;
			if ((objp = vmentry.object.vm_object) == NULL)
				continue;
			for (; objp; objp = object.backing_object) {
				if (!kvm_read_all(kd, (unsigned long)objp,
				    &object, sizeof(object))) {
					warnx("can't read vm_object at %p",
					    (void *)objp);
					break;
				}
			}

			/* We want only vnode objects. */
			if (object.type != OBJT_VNODE)
				continue;

			prot = vmentry.protection;
			fflags = 0;
			if (prot & VM_PROT_READ)
				fflags = PS_FST_FFLAG_READ;
			if ((vmentry.eflags & MAP_ENTRY_COW) == 0 &&
			    prot & VM_PROT_WRITE)
				fflags |= PS_FST_FFLAG_WRITE;

			/*
			 * Create filestat entry.
			 */
			entry = filestat_new_entry(object.handle,
			    PS_FST_TYPE_VNODE, -1, fflags,
			    PS_FST_UFLAG_MMAP, 0, 0, NULL, NULL);
			if (entry != NULL)
				STAILQ_INSERT_TAIL(head, entry, next);
		}
	}
exit:
	return (head);
}

/*
 * kinfo types to filestat translation.
 */
static int
kinfo_type2fst(int kftype)
{
	static struct {
		int	kf_type;
		int	fst_type;
	} kftypes2fst[] = {
		{ KF_TYPE_PROCDESC, PS_FST_TYPE_PROCDESC },
		{ KF_TYPE_CRYPTO, PS_FST_TYPE_CRYPTO },
		{ KF_TYPE_DEV, PS_FST_TYPE_DEV },
		{ KF_TYPE_FIFO, PS_FST_TYPE_FIFO },
		{ KF_TYPE_KQUEUE, PS_FST_TYPE_KQUEUE },
		{ KF_TYPE_MQUEUE, PS_FST_TYPE_MQUEUE },
		{ KF_TYPE_NONE, PS_FST_TYPE_NONE },
		{ KF_TYPE_PIPE, PS_FST_TYPE_PIPE },
		{ KF_TYPE_PTS, PS_FST_TYPE_PTS },
		{ KF_TYPE_SEM, PS_FST_TYPE_SEM },
		{ KF_TYPE_SHM, PS_FST_TYPE_SHM },
		{ KF_TYPE_SOCKET, PS_FST_TYPE_SOCKET },
		{ KF_TYPE_VNODE, PS_FST_TYPE_VNODE },
		{ KF_TYPE_UNKNOWN, PS_FST_TYPE_UNKNOWN }
	};
#define NKFTYPES	(sizeof(kftypes2fst) / sizeof(*kftypes2fst))
	unsigned int i;

	for (i = 0; i < NKFTYPES; i++)
		if (kftypes2fst[i].kf_type == kftype)
			break;
	if (i == NKFTYPES)
		return (PS_FST_TYPE_UNKNOWN);
	return (kftypes2fst[i].fst_type);
}

/*
 * kinfo flags to filestat translation.
 */
static int
kinfo_fflags2fst(int kfflags)
{
	static struct {
		int	kf_flag;
		int	fst_flag;
	} kfflags2fst[] = {
		{ KF_FLAG_APPEND, PS_FST_FFLAG_APPEND },
		{ KF_FLAG_ASYNC, PS_FST_FFLAG_ASYNC },
		{ KF_FLAG_CREAT, PS_FST_FFLAG_CREAT },
		{ KF_FLAG_DIRECT, PS_FST_FFLAG_DIRECT },
		{ KF_FLAG_EXCL, PS_FST_FFLAG_EXCL },
		{ KF_FLAG_EXEC, PS_FST_FFLAG_EXEC },
		{ KF_FLAG_EXLOCK, PS_FST_FFLAG_EXLOCK },
		{ KF_FLAG_FSYNC, PS_FST_FFLAG_SYNC },
		{ KF_FLAG_HASLOCK, PS_FST_FFLAG_HASLOCK },
		{ KF_FLAG_NOFOLLOW, PS_FST_FFLAG_NOFOLLOW },
		{ KF_FLAG_NONBLOCK, PS_FST_FFLAG_NONBLOCK },
		{ KF_FLAG_READ, PS_FST_FFLAG_READ },
		{ KF_FLAG_SHLOCK, PS_FST_FFLAG_SHLOCK },
		{ KF_FLAG_TRUNC, PS_FST_FFLAG_TRUNC },
		{ KF_FLAG_WRITE, PS_FST_FFLAG_WRITE }
	};
#define NKFFLAGS	(sizeof(kfflags2fst) / sizeof(*kfflags2fst))
	unsigned int i;
	int flags;

	flags = 0;
	for (i = 0; i < NKFFLAGS; i++)
		if ((kfflags & kfflags2fst[i].kf_flag) != 0)
			flags |= kfflags2fst[i].fst_flag;
	return (flags);
}

static int
kinfo_uflags2fst(int fd)
{

	switch (fd) {
	case KF_FD_TYPE_CTTY:
		return (PS_FST_UFLAG_CTTY);
	case KF_FD_TYPE_CWD:
		return (PS_FST_UFLAG_CDIR);
	case KF_FD_TYPE_JAIL:
		return (PS_FST_UFLAG_JAIL);
	case KF_FD_TYPE_TEXT:
		return (PS_FST_UFLAG_TEXT);
	case KF_FD_TYPE_TRACE:
		return (PS_FST_UFLAG_TRACE);
	case KF_FD_TYPE_ROOT:
		return (PS_FST_UFLAG_RDIR);
	}
	return (0);
}

static struct kinfo_file *
kinfo_getfile_core(struct procstat_core *core, int *cntp)
{
	int cnt;
	size_t len;
	char *buf, *bp, *eb;
	struct kinfo_file *kif, *kp, *kf;

	buf = procstat_core_get(core, PSC_TYPE_FILES, NULL, &len);
	if (buf == NULL)
		return (NULL);
	/*
	 * XXXMG: The code below is just copy&past from libutil.
	 * The code duplication can be avoided if libutil
	 * is extended to provide something like:
	 *   struct kinfo_file *kinfo_getfile_from_buf(const char *buf,
	 *       size_t len, int *cntp);
	 */

	/* Pass 1: count items */
	cnt = 0;
	bp = buf;
	eb = buf + len;
	while (bp < eb) {
		kf = (struct kinfo_file *)(uintptr_t)bp;
		if (kf->kf_structsize == 0)
			break;
		bp += kf->kf_structsize;
		cnt++;
	}

	kif = calloc(cnt, sizeof(*kif));
	if (kif == NULL) {
		free(buf);
		return (NULL);
	}
	bp = buf;
	eb = buf + len;
	kp = kif;
	/* Pass 2: unpack */
	while (bp < eb) {
		kf = (struct kinfo_file *)(uintptr_t)bp;
		if (kf->kf_structsize == 0)
			break;
		/* Copy/expand into pre-zeroed buffer */
		memcpy(kp, kf, kf->kf_structsize);
		/* Advance to next packed record */
		bp += kf->kf_structsize;
		/* Set field size to fixed length, advance */
		kp->kf_structsize = sizeof(*kp);
		kp++;
	}
	free(buf);
	*cntp = cnt;
	return (kif);	/* Caller must free() return value */
}

static struct filestat_list *
procstat_getfiles_sysctl(struct procstat *procstat, struct kinfo_proc *kp,
    int mmapped)
{
	struct kinfo_file *kif, *files;
	struct kinfo_vmentry *kve, *vmentries;
	struct filestat_list *head;
	struct filestat *entry;
	char *path;
	off_t offset;
	int cnt, fd, fflags;
	int i, type, uflags;
	int refcount;
	cap_rights_t cap_rights;

	assert(kp);
	if (kp->ki_fd == NULL)
		return (NULL);
	switch(procstat->type) {
	case PROCSTAT_SYSCTL:
		files = kinfo_getfile(kp->ki_pid, &cnt);
		break;
	case PROCSTAT_CORE:
		files = kinfo_getfile_core(procstat->core, &cnt);
		break;
	default:
		assert(!"invalid type");
	}
	if (files == NULL && errno != EPERM) {
		warn("kinfo_getfile()");
		return (NULL);
	}
	procstat->files = files;

	/*
	 * Allocate list head.
	 */
	head = malloc(sizeof(*head));
	if (head == NULL)
		return (NULL);
	STAILQ_INIT(head);
	for (i = 0; i < cnt; i++) {
		kif = &files[i];

		type = kinfo_type2fst(kif->kf_type);
		fd = kif->kf_fd >= 0 ? kif->kf_fd : -1;
		fflags = kinfo_fflags2fst(kif->kf_flags);
		uflags = kinfo_uflags2fst(kif->kf_fd);
		refcount = kif->kf_ref_count;
		offset = kif->kf_offset;
		if (*kif->kf_path != '\0')
			path = strdup(kif->kf_path);
		else
			path = NULL;
		cap_rights = kif->kf_cap_rights;

		/*
		 * Create filestat entry.
		 */
		entry = filestat_new_entry(kif, type, fd, fflags, uflags,
		    refcount, offset, path, &cap_rights);
		if (entry != NULL)
			STAILQ_INSERT_TAIL(head, entry, next);
	}
	if (mmapped != 0) {
		vmentries = procstat_getvmmap(procstat, kp, &cnt);
		procstat->vmentries = vmentries;
		if (vmentries == NULL || cnt == 0)
			goto fail;
		for (i = 0; i < cnt; i++) {
			kve = &vmentries[i];
			if (kve->kve_type != KVME_TYPE_VNODE)
				continue;
			fflags = 0;
			if (kve->kve_protection & KVME_PROT_READ)
				fflags = PS_FST_FFLAG_READ;
			if ((kve->kve_flags & KVME_FLAG_COW) == 0 &&
			    kve->kve_protection & KVME_PROT_WRITE)
				fflags |= PS_FST_FFLAG_WRITE;
			offset = kve->kve_offset;
			refcount = kve->kve_ref_count;
			if (*kve->kve_path != '\0')
				path = strdup(kve->kve_path);
			else
				path = NULL;
			entry = filestat_new_entry(kve, PS_FST_TYPE_VNODE, -1,
			    fflags, PS_FST_UFLAG_MMAP, refcount, offset, path,
			    NULL);
			if (entry != NULL)
				STAILQ_INSERT_TAIL(head, entry, next);
		}
	}
fail:
	return (head);
}

int
procstat_get_pipe_info(struct procstat *procstat, struct filestat *fst,
    struct pipestat *ps, char *errbuf)
{

	assert(ps);
	if (procstat->type == PROCSTAT_KVM) {
		return (procstat_get_pipe_info_kvm(procstat->kd, fst, ps,
		    errbuf));
	} else if (procstat->type == PROCSTAT_SYSCTL ||
		procstat->type == PROCSTAT_CORE) {
		return (procstat_get_pipe_info_sysctl(fst, ps, errbuf));
	} else {
		warnx("unknown access method: %d", procstat->type);
		if (errbuf != NULL)
			snprintf(errbuf, _POSIX2_LINE_MAX, "error");
		return (1);
	}
}

static int
procstat_get_pipe_info_kvm(kvm_t *kd, struct filestat *fst,
    struct pipestat *ps, char *errbuf)
{
	struct pipe pi;
	void *pipep;

	assert(kd);
	assert(ps);
	assert(fst);
	bzero(ps, sizeof(*ps));
	pipep = fst->fs_typedep;
	if (pipep == NULL)
		goto fail;
	if (!kvm_read_all(kd, (unsigned long)pipep, &pi, sizeof(struct pipe))) {
		warnx("can't read pipe at %p", (void *)pipep);
		goto fail;
	}
	ps->addr = (uintptr_t)pipep;
	ps->peer = (uintptr_t)pi.pipe_peer;
	ps->buffer_cnt = pi.pipe_buffer.cnt;
	return (0);

fail:
	if (errbuf != NULL)
		snprintf(errbuf, _POSIX2_LINE_MAX, "error");
	return (1);
}

static int
procstat_get_pipe_info_sysctl(struct filestat *fst, struct pipestat *ps,
    char *errbuf __unused)
{
	struct kinfo_file *kif;

	assert(ps);
	assert(fst);
	bzero(ps, sizeof(*ps));
	kif = fst->fs_typedep;
	if (kif == NULL)
		return (1);
	ps->addr = kif->kf_un.kf_pipe.kf_pipe_addr;
	ps->peer = kif->kf_un.kf_pipe.kf_pipe_peer;
	ps->buffer_cnt = kif->kf_un.kf_pipe.kf_pipe_buffer_cnt;
	return (0);
}

int
procstat_get_pts_info(struct procstat *procstat, struct filestat *fst,
    struct ptsstat *pts, char *errbuf)
{

	assert(pts);
	if (procstat->type == PROCSTAT_KVM) {
		return (procstat_get_pts_info_kvm(procstat->kd, fst, pts,
		    errbuf));
	} else if (procstat->type == PROCSTAT_SYSCTL ||
		procstat->type == PROCSTAT_CORE) {
		return (procstat_get_pts_info_sysctl(fst, pts, errbuf));
	} else {
		warnx("unknown access method: %d", procstat->type);
		if (errbuf != NULL)
			snprintf(errbuf, _POSIX2_LINE_MAX, "error");
		return (1);
	}
}

static int
procstat_get_pts_info_kvm(kvm_t *kd, struct filestat *fst,
    struct ptsstat *pts, char *errbuf)
{
	struct tty tty;
	void *ttyp;

	assert(kd);
	assert(pts);
	assert(fst);
	bzero(pts, sizeof(*pts));
	ttyp = fst->fs_typedep;
	if (ttyp == NULL)
		goto fail;
	if (!kvm_read_all(kd, (unsigned long)ttyp, &tty, sizeof(struct tty))) {
		warnx("can't read tty at %p", (void *)ttyp);
		goto fail;
	}
	pts->dev = dev2udev(kd, tty.t_dev);
	(void)kdevtoname(kd, tty.t_dev, pts->devname);
	return (0);

fail:
	if (errbuf != NULL)
		snprintf(errbuf, _POSIX2_LINE_MAX, "error");
	return (1);
}

static int
procstat_get_pts_info_sysctl(struct filestat *fst, struct ptsstat *pts,
    char *errbuf __unused)
{
	struct kinfo_file *kif;

	assert(pts);
	assert(fst);
	bzero(pts, sizeof(*pts));
	kif = fst->fs_typedep;
	if (kif == NULL)
		return (0);
	pts->dev = kif->kf_un.kf_pts.kf_pts_dev;
	strlcpy(pts->devname, kif->kf_path, sizeof(pts->devname));
	return (0);
}

int
procstat_get_sem_info(struct procstat *procstat, struct filestat *fst,
    struct semstat *sem, char *errbuf)
{

	assert(sem);
	if (procstat->type == PROCSTAT_KVM) {
		return (procstat_get_sem_info_kvm(procstat->kd, fst, sem,
		    errbuf));
	} else if (procstat->type == PROCSTAT_SYSCTL ||
	    procstat->type == PROCSTAT_CORE) {
		return (procstat_get_sem_info_sysctl(fst, sem, errbuf));
	} else {
		warnx("unknown access method: %d", procstat->type);
		if (errbuf != NULL)
			snprintf(errbuf, _POSIX2_LINE_MAX, "error");
		return (1);
	}
}

static int
procstat_get_sem_info_kvm(kvm_t *kd, struct filestat *fst,
    struct semstat *sem, char *errbuf)
{
	struct ksem ksem;
	void *ksemp;
	char *path;
	int i;

	assert(kd);
	assert(sem);
	assert(fst);
	bzero(sem, sizeof(*sem));
	ksemp = fst->fs_typedep;
	if (ksemp == NULL)
		goto fail;
	if (!kvm_read_all(kd, (unsigned long)ksemp, &ksem,
	    sizeof(struct ksem))) {
		warnx("can't read ksem at %p", (void *)ksemp);
		goto fail;
	}
	sem->mode = S_IFREG | ksem.ks_mode;
	sem->value = ksem.ks_value;
	if (fst->fs_path == NULL && ksem.ks_path != NULL) {
		path = malloc(MAXPATHLEN);
		for (i = 0; i < MAXPATHLEN - 1; i++) {
			if (!kvm_read_all(kd, (unsigned long)ksem.ks_path + i,
			    path + i, 1))
				break;
			if (path[i] == '\0')
				break;
		}
		path[i] = '\0';
		if (i == 0)
			free(path);
		else
			fst->fs_path = path;
	}
	return (0);

fail:
	if (errbuf != NULL)
		snprintf(errbuf, _POSIX2_LINE_MAX, "error");
	return (1);
}

static int
procstat_get_sem_info_sysctl(struct filestat *fst, struct semstat *sem,
    char *errbuf __unused)
{
	struct kinfo_file *kif;

	assert(sem);
	assert(fst);
	bzero(sem, sizeof(*sem));
	kif = fst->fs_typedep;
	if (kif == NULL)
		return (0);
	sem->value = kif->kf_un.kf_sem.kf_sem_value;
	sem->mode = kif->kf_un.kf_sem.kf_sem_mode;
	return (0);
}

int
procstat_get_shm_info(struct procstat *procstat, struct filestat *fst,
    struct shmstat *shm, char *errbuf)
{

	assert(shm);
	if (procstat->type == PROCSTAT_KVM) {
		return (procstat_get_shm_info_kvm(procstat->kd, fst, shm,
		    errbuf));
	} else if (procstat->type == PROCSTAT_SYSCTL ||
	    procstat->type == PROCSTAT_CORE) {
		return (procstat_get_shm_info_sysctl(fst, shm, errbuf));
	} else {
		warnx("unknown access method: %d", procstat->type);
		if (errbuf != NULL)
			snprintf(errbuf, _POSIX2_LINE_MAX, "error");
		return (1);
	}
}

static int
procstat_get_shm_info_kvm(kvm_t *kd, struct filestat *fst,
    struct shmstat *shm, char *errbuf)
{
	struct shmfd shmfd;
	void *shmfdp;
	char *path;
	int i;

	assert(kd);
	assert(shm);
	assert(fst);
	bzero(shm, sizeof(*shm));
	shmfdp = fst->fs_typedep;
	if (shmfdp == NULL)
		goto fail;
	if (!kvm_read_all(kd, (unsigned long)shmfdp, &shmfd,
	    sizeof(struct shmfd))) {
		warnx("can't read shmfd at %p", (void *)shmfdp);
		goto fail;
	}
	shm->mode = S_IFREG | shmfd.shm_mode;
	shm->size = shmfd.shm_size;
	if (fst->fs_path == NULL && shmfd.shm_path != NULL) {
		path = malloc(MAXPATHLEN);
		for (i = 0; i < MAXPATHLEN - 1; i++) {
			if (!kvm_read_all(kd, (unsigned long)shmfd.shm_path + i,
			    path + i, 1))
				break;
			if (path[i] == '\0')
				break;
		}
		path[i] = '\0';
		if (i == 0)
			free(path);
		else
			fst->fs_path = path;
	}
	return (0);

fail:
	if (errbuf != NULL)
		snprintf(errbuf, _POSIX2_LINE_MAX, "error");
	return (1);
}

static int
procstat_get_shm_info_sysctl(struct filestat *fst, struct shmstat *shm,
    char *errbuf __unused)
{
	struct kinfo_file *kif;

	assert(shm);
	assert(fst);
	bzero(shm, sizeof(*shm));
	kif = fst->fs_typedep;
	if (kif == NULL)
		return (0);
	shm->size = kif->kf_un.kf_file.kf_file_size;
	shm->mode = kif->kf_un.kf_file.kf_file_mode;
	return (0);
}

int
procstat_get_vnode_info(struct procstat *procstat, struct filestat *fst,
    struct vnstat *vn, char *errbuf)
{

	assert(vn);
	if (procstat->type == PROCSTAT_KVM) {
		return (procstat_get_vnode_info_kvm(procstat->kd, fst, vn,
		    errbuf));
	} else if (procstat->type == PROCSTAT_SYSCTL ||
		procstat->type == PROCSTAT_CORE) {
		return (procstat_get_vnode_info_sysctl(fst, vn, errbuf));
	} else {
		warnx("unknown access method: %d", procstat->type);
		if (errbuf != NULL)
			snprintf(errbuf, _POSIX2_LINE_MAX, "error");
		return (1);
	}
}

static int
procstat_get_vnode_info_kvm(kvm_t *kd, struct filestat *fst,
    struct vnstat *vn, char *errbuf)
{
	/* Filesystem specific handlers. */
	#define FSTYPE(fst)     {#fst, fst##_filestat}
	struct {
		const char	*tag;
		int		(*handler)(kvm_t *kd, struct vnode *vp,
		    struct vnstat *vn);
	} fstypes[] = {
		FSTYPE(devfs),
		FSTYPE(isofs),
		FSTYPE(msdosfs),
		FSTYPE(nfs),
		FSTYPE(smbfs),
		FSTYPE(udf), 
		FSTYPE(ufs),
#ifdef LIBPROCSTAT_ZFS
		FSTYPE(zfs),
#endif
	};
#define	NTYPES	(sizeof(fstypes) / sizeof(*fstypes))
	struct vnode vnode;
	char tagstr[12];
	void *vp;
	int error;
	unsigned int i;

	assert(kd);
	assert(vn);
	assert(fst);
	vp = fst->fs_typedep;
	if (vp == NULL)
		goto fail;
	error = kvm_read_all(kd, (unsigned long)vp, &vnode, sizeof(vnode));
	if (error == 0) {
		warnx("can't read vnode at %p", (void *)vp);
		goto fail;
	}
	bzero(vn, sizeof(*vn));
	vn->vn_type = vntype2psfsttype(vnode.v_type);
	if (vnode.v_type == VNON || vnode.v_type == VBAD)
		return (0);
	error = kvm_read_all(kd, (unsigned long)vnode.v_tag, tagstr,
	    sizeof(tagstr));
	if (error == 0) {
		warnx("can't read v_tag at %p", (void *)vp);
		goto fail;
	}
	tagstr[sizeof(tagstr) - 1] = '\0';

	/*
	 * Find appropriate handler.
	 */
	for (i = 0; i < NTYPES; i++)
		if (!strcmp(fstypes[i].tag, tagstr)) {
			if (fstypes[i].handler(kd, &vnode, vn) != 0) {
				goto fail;
			}
			break;
		}
	if (i == NTYPES) {
		if (errbuf != NULL)
			snprintf(errbuf, _POSIX2_LINE_MAX, "?(%s)", tagstr);
		return (1);
	}
	vn->vn_mntdir = getmnton(kd, vnode.v_mount);
	if ((vnode.v_type == VBLK || vnode.v_type == VCHR) &&
	    vnode.v_rdev != NULL){
		vn->vn_dev = dev2udev(kd, vnode.v_rdev);
		(void)kdevtoname(kd, vnode.v_rdev, vn->vn_devname);
	} else {
		vn->vn_dev = -1;
	}
	return (0);

fail:
	if (errbuf != NULL)
		snprintf(errbuf, _POSIX2_LINE_MAX, "error");
	return (1);
}

/*
 * kinfo vnode type to filestat translation.
 */
static int
kinfo_vtype2fst(int kfvtype)
{
	static struct {
		int	kf_vtype; 
		int	fst_vtype;
	} kfvtypes2fst[] = {
		{ KF_VTYPE_VBAD, PS_FST_VTYPE_VBAD },
		{ KF_VTYPE_VBLK, PS_FST_VTYPE_VBLK },
		{ KF_VTYPE_VCHR, PS_FST_VTYPE_VCHR },
		{ KF_VTYPE_VDIR, PS_FST_VTYPE_VDIR },
		{ KF_VTYPE_VFIFO, PS_FST_VTYPE_VFIFO },
		{ KF_VTYPE_VLNK, PS_FST_VTYPE_VLNK },
		{ KF_VTYPE_VNON, PS_FST_VTYPE_VNON },
		{ KF_VTYPE_VREG, PS_FST_VTYPE_VREG },
		{ KF_VTYPE_VSOCK, PS_FST_VTYPE_VSOCK }
	};
#define	NKFVTYPES	(sizeof(kfvtypes2fst) / sizeof(*kfvtypes2fst))
	unsigned int i;

	for (i = 0; i < NKFVTYPES; i++)
		if (kfvtypes2fst[i].kf_vtype == kfvtype)
			break;
	if (i == NKFVTYPES)
		return (PS_FST_VTYPE_UNKNOWN);
	return (kfvtypes2fst[i].fst_vtype);
}

static int
procstat_get_vnode_info_sysctl(struct filestat *fst, struct vnstat *vn,
    char *errbuf)
{
	struct statfs stbuf;
	struct kinfo_file *kif;
	struct kinfo_vmentry *kve;
	char *name, *path;
	uint64_t fileid;
	uint64_t size;
	uint64_t fsid;
	uint64_t rdev;
	uint16_t mode;
	int vntype;
	int status;

	assert(fst);
	assert(vn);
	bzero(vn, sizeof(*vn));
	if (fst->fs_typedep == NULL)
		return (1);
	if (fst->fs_uflags & PS_FST_UFLAG_MMAP) {
		kve = fst->fs_typedep;
		fileid = kve->kve_vn_fileid;
		fsid = kve->kve_vn_fsid;
		mode = kve->kve_vn_mode;
		path = kve->kve_path;
		rdev = kve->kve_vn_rdev;
		size = kve->kve_vn_size;
		vntype = kinfo_vtype2fst(kve->kve_vn_type);
		status = kve->kve_status;
	} else {
		kif = fst->fs_typedep;
		fileid = kif->kf_un.kf_file.kf_file_fileid;
		fsid = kif->kf_un.kf_file.kf_file_fsid;
		mode = kif->kf_un.kf_file.kf_file_mode;
		path = kif->kf_path;
		rdev = kif->kf_un.kf_file.kf_file_rdev;
		size = kif->kf_un.kf_file.kf_file_size;
		vntype = kinfo_vtype2fst(kif->kf_vnode_type);
		status = kif->kf_status;
	}
	vn->vn_type = vntype;
	if (vntype == PS_FST_VTYPE_VNON || vntype == PS_FST_VTYPE_VBAD)
		return (0);
	if ((status & KF_ATTR_VALID) == 0) {
		if (errbuf != NULL) {
			snprintf(errbuf, _POSIX2_LINE_MAX,
			    "? (no info available)");
		}
		return (1);
	}
	if (path && *path) {
		statfs(path, &stbuf);
		vn->vn_mntdir = strdup(stbuf.f_mntonname);
	} else
		vn->vn_mntdir = strdup("-");
	vn->vn_dev = rdev;
	if (vntype == PS_FST_VTYPE_VBLK) {
		name = devname(rdev, S_IFBLK);
		if (name != NULL)
			strlcpy(vn->vn_devname, name,
			    sizeof(vn->vn_devname));
	} else if (vntype == PS_FST_VTYPE_VCHR) {
		name = devname(vn->vn_dev, S_IFCHR);
		if (name != NULL)
			strlcpy(vn->vn_devname, name,
			    sizeof(vn->vn_devname));
	}
	vn->vn_fsid = fsid;
	vn->vn_fileid = fileid;
	vn->vn_size = size;
	vn->vn_mode = mode;
	return (0);
}

int
procstat_get_socket_info(struct procstat *procstat, struct filestat *fst,
    struct sockstat *sock, char *errbuf)
{

	assert(sock);
	if (procstat->type == PROCSTAT_KVM) {
		return (procstat_get_socket_info_kvm(procstat->kd, fst, sock,
		    errbuf));
	} else if (procstat->type == PROCSTAT_SYSCTL ||
		procstat->type == PROCSTAT_CORE) {
		return (procstat_get_socket_info_sysctl(fst, sock, errbuf));
	} else {
		warnx("unknown access method: %d", procstat->type);
		if (errbuf != NULL)
			snprintf(errbuf, _POSIX2_LINE_MAX, "error");
		return (1);
	}
}

static int
procstat_get_socket_info_kvm(kvm_t *kd, struct filestat *fst,
    struct sockstat *sock, char *errbuf)
{
	struct domain dom;
	struct inpcb inpcb;
	struct protosw proto;
	struct socket s;
	struct unpcb unpcb;
	ssize_t len;
	void *so;

	assert(kd);
	assert(sock);
	assert(fst);
	bzero(sock, sizeof(*sock));
	so = fst->fs_typedep;
	if (so == NULL)
		goto fail;
	sock->so_addr = (uintptr_t)so;
	/* fill in socket */
	if (!kvm_read_all(kd, (unsigned long)so, &s,
	    sizeof(struct socket))) {
		warnx("can't read sock at %p", (void *)so);
		goto fail;
	}
	/* fill in protosw entry */
	if (!kvm_read_all(kd, (unsigned long)s.so_proto, &proto,
	    sizeof(struct protosw))) {
		warnx("can't read protosw at %p", (void *)s.so_proto);
		goto fail;
	}
	/* fill in domain */
	if (!kvm_read_all(kd, (unsigned long)proto.pr_domain, &dom,
	    sizeof(struct domain))) {
		warnx("can't read domain at %p",
		    (void *)proto.pr_domain);
		goto fail;
	}
	if ((len = kvm_read(kd, (unsigned long)dom.dom_name, sock->dname,
	    sizeof(sock->dname) - 1)) < 0) {
		warnx("can't read domain name at %p", (void *)dom.dom_name);
		sock->dname[0] = '\0';
	}
	else
		sock->dname[len] = '\0';
	
	/*
	 * Fill in known data.
	 */
	sock->type = s.so_type;
	sock->proto = proto.pr_protocol;
	sock->dom_family = dom.dom_family;
	sock->so_pcb = (uintptr_t)s.so_pcb;

	/*
	 * Protocol specific data.
	 */
	switch(dom.dom_family) {
	case AF_INET:
	case AF_INET6:
		if (proto.pr_protocol == IPPROTO_TCP) {
			if (s.so_pcb) {
				if (kvm_read(kd, (u_long)s.so_pcb,
				    (char *)&inpcb, sizeof(struct inpcb))
				    != sizeof(struct inpcb)) {
					warnx("can't read inpcb at %p",
					    (void *)s.so_pcb);
				} else
					sock->inp_ppcb =
					    (uintptr_t)inpcb.inp_ppcb;
				sock->sendq = s.so_snd.sb_ccc;
				sock->recvq = s.so_rcv.sb_ccc;
			}
		}
		break;
	case AF_UNIX:
		if (s.so_pcb) {
			if (kvm_read(kd, (u_long)s.so_pcb, (char *)&unpcb,
			    sizeof(struct unpcb)) != sizeof(struct unpcb)){
				warnx("can't read unpcb at %p",
				    (void *)s.so_pcb);
			} else if (unpcb.unp_conn) {
				sock->so_rcv_sb_state = s.so_rcv.sb_state;
				sock->so_snd_sb_state = s.so_snd.sb_state;
				sock->unp_conn = (uintptr_t)unpcb.unp_conn;
				sock->sendq = s.so_snd.sb_ccc;
				sock->recvq = s.so_rcv.sb_ccc;
			}
		}
		break;
	default:
		break;
	}
	return (0);

fail:
	if (errbuf != NULL)
		snprintf(errbuf, _POSIX2_LINE_MAX, "error");
	return (1);
}

static int
procstat_get_socket_info_sysctl(struct filestat *fst, struct sockstat *sock,
    char *errbuf __unused)
{
	struct kinfo_file *kif;

	assert(sock);
	assert(fst);
	bzero(sock, sizeof(*sock));
	kif = fst->fs_typedep;
	if (kif == NULL)
		return (0);

	/*
	 * Fill in known data.
	 */
	sock->type = kif->kf_sock_type;
	sock->proto = kif->kf_sock_protocol;
	sock->dom_family = kif->kf_sock_domain;
	sock->so_pcb = kif->kf_un.kf_sock.kf_sock_pcb;
	strlcpy(sock->dname, kif->kf_path, sizeof(sock->dname));
	bcopy(&kif->kf_un.kf_sock.kf_sa_local, &sock->sa_local,
	    kif->kf_un.kf_sock.kf_sa_local.ss_len);
	bcopy(&kif->kf_un.kf_sock.kf_sa_peer, &sock->sa_peer,
	    kif->kf_un.kf_sock.kf_sa_peer.ss_len);

	/*
	 * Protocol specific data.
	 */
	switch(sock->dom_family) {
	case AF_INET:
	case AF_INET6:
		if (sock->proto == IPPROTO_TCP) {
			sock->inp_ppcb = kif->kf_un.kf_sock.kf_sock_inpcb;
			sock->sendq = kif->kf_un.kf_sock.kf_sock_sendq;
			sock->recvq = kif->kf_un.kf_sock.kf_sock_recvq;
		}
		break;
	case AF_UNIX:
		if (kif->kf_un.kf_sock.kf_sock_unpconn != 0) {
			sock->so_rcv_sb_state =
			    kif->kf_un.kf_sock.kf_sock_rcv_sb_state;
			sock->so_snd_sb_state =
			    kif->kf_un.kf_sock.kf_sock_snd_sb_state;
			sock->unp_conn =
			    kif->kf_un.kf_sock.kf_sock_unpconn;
			sock->sendq = kif->kf_un.kf_sock.kf_sock_sendq;
			sock->recvq = kif->kf_un.kf_sock.kf_sock_recvq;
		}
		break;
	default:
		break;
	}
	return (0);
}

/*
 * Descriptor flags to filestat translation.
 */
static int
to_filestat_flags(int flags)
{
	static struct {
		int flag;
		int fst_flag;
	} fstflags[] = {
		{ FREAD, PS_FST_FFLAG_READ },
		{ FWRITE, PS_FST_FFLAG_WRITE },
		{ O_APPEND, PS_FST_FFLAG_APPEND },
		{ O_ASYNC, PS_FST_FFLAG_ASYNC },
		{ O_CREAT, PS_FST_FFLAG_CREAT },
		{ O_DIRECT, PS_FST_FFLAG_DIRECT },
		{ O_EXCL, PS_FST_FFLAG_EXCL },
		{ O_EXEC, PS_FST_FFLAG_EXEC },
		{ O_EXLOCK, PS_FST_FFLAG_EXLOCK },
		{ O_NOFOLLOW, PS_FST_FFLAG_NOFOLLOW },
		{ O_NONBLOCK, PS_FST_FFLAG_NONBLOCK },
		{ O_SHLOCK, PS_FST_FFLAG_SHLOCK },
		{ O_SYNC, PS_FST_FFLAG_SYNC },
		{ O_TRUNC, PS_FST_FFLAG_TRUNC }
	};
#define NFSTFLAGS	(sizeof(fstflags) / sizeof(*fstflags))
	int fst_flags;
	unsigned int i;

	fst_flags = 0;
	for (i = 0; i < NFSTFLAGS; i++)
		if (flags & fstflags[i].flag)
			fst_flags |= fstflags[i].fst_flag;
	return (fst_flags);
}

/*
 * Vnode type to filestate translation.
 */
static int
vntype2psfsttype(int type)
{
	static struct {
		int	vtype; 
		int	fst_vtype;
	} vt2fst[] = {
		{ VBAD, PS_FST_VTYPE_VBAD },
		{ VBLK, PS_FST_VTYPE_VBLK },
		{ VCHR, PS_FST_VTYPE_VCHR },
		{ VDIR, PS_FST_VTYPE_VDIR },
		{ VFIFO, PS_FST_VTYPE_VFIFO },
		{ VLNK, PS_FST_VTYPE_VLNK },
		{ VNON, PS_FST_VTYPE_VNON },
		{ VREG, PS_FST_VTYPE_VREG },
		{ VSOCK, PS_FST_VTYPE_VSOCK }
	};
#define	NVFTYPES	(sizeof(vt2fst) / sizeof(*vt2fst))
	unsigned int i, fst_type;

	fst_type = PS_FST_VTYPE_UNKNOWN;
	for (i = 0; i < NVFTYPES; i++) {
		if (type == vt2fst[i].vtype) {
			fst_type = vt2fst[i].fst_vtype;
			break;
		}
	}
	return (fst_type);
}

static char *
getmnton(kvm_t *kd, struct mount *m)
{
	struct mount mnt;
	static struct mtab {
		struct mtab *next;
		struct mount *m;
		char mntonname[MNAMELEN + 1];
	} *mhead = NULL;
	struct mtab *mt;

	for (mt = mhead; mt != NULL; mt = mt->next)
		if (m == mt->m)
			return (mt->mntonname);
	if (!kvm_read_all(kd, (unsigned long)m, &mnt, sizeof(struct mount))) {
		warnx("can't read mount table at %p", (void *)m);
		return (NULL);
	}
	if ((mt = malloc(sizeof (struct mtab))) == NULL)
		err(1, NULL);
	mt->m = m;
	bcopy(&mnt.mnt_stat.f_mntonname[0], &mt->mntonname[0], MNAMELEN);
	mt->mntonname[MNAMELEN] = '\0';
	mt->next = mhead;
	mhead = mt;
	return (mt->mntonname);
}

/*
 * Auxiliary structures and functions to get process environment or
 * command line arguments.
 */
struct argvec {
	char	*buf;
	size_t	bufsize;
	char	**argv;
	size_t	argc;
};

static struct argvec *
argvec_alloc(size_t bufsize)
{
	struct argvec *av;

	av = malloc(sizeof(*av));
	if (av == NULL)
		return (NULL);
	av->bufsize = bufsize;
	av->buf = malloc(av->bufsize);
	if (av->buf == NULL) {
		free(av);
		return (NULL);
	}
	av->argc = 32;
	av->argv = malloc(sizeof(char *) * av->argc);
	if (av->argv == NULL) {
		free(av->buf);
		free(av);
		return (NULL);
	}
	return av;
}

static void
argvec_free(struct argvec * av)
{

	free(av->argv);
	free(av->buf);
	free(av);
}

static char **
getargv(struct procstat *procstat, struct kinfo_proc *kp, size_t nchr, int env)
{
	int error, name[4], argc, i;
	struct argvec *av, **avp;
	enum psc_type type;
	size_t len;
	char *p, **argv;

	assert(procstat);
	assert(kp);
	if (procstat->type == PROCSTAT_KVM) {
		warnx("can't use kvm access method");
		return (NULL);
	}
	if (procstat->type != PROCSTAT_SYSCTL &&
	    procstat->type != PROCSTAT_CORE) {
		warnx("unknown access method: %d", procstat->type);
		return (NULL);
	}

	if (nchr == 0 || nchr > ARG_MAX)
		nchr = ARG_MAX;

	avp = (struct argvec **)(env ? &procstat->argv : &procstat->envv);
	av = *avp;

	if (av == NULL)
	{
		av = argvec_alloc(nchr);
		if (av == NULL)
		{
			warn("malloc(%zu)", nchr);
			return (NULL);
		}
		*avp = av;
	} else if (av->bufsize < nchr) {
		av->buf = reallocf(av->buf, nchr);
		if (av->buf == NULL) {
			warn("malloc(%zu)", nchr);
			return (NULL);
		}
	}
	if (procstat->type == PROCSTAT_SYSCTL) {
		name[0] = CTL_KERN;
		name[1] = KERN_PROC;
		name[2] = env ? KERN_PROC_ENV : KERN_PROC_ARGS;
		name[3] = kp->ki_pid;
		len = nchr;
		error = sysctl(name, nitems(name), av->buf, &len, NULL, 0);
		if (error != 0 && errno != ESRCH && errno != EPERM)
			warn("sysctl(kern.proc.%s)", env ? "env" : "args");
		if (error != 0 || len == 0)
			return (NULL);
	} else /* procstat->type == PROCSTAT_CORE */ {
		type = env ? PSC_TYPE_ENVV : PSC_TYPE_ARGV;
		len = nchr;
		if (procstat_core_get(procstat->core, type, av->buf, &len)
		    == NULL) {
			return (NULL);
		}
	}

	argv = av->argv;
	argc = av->argc;
	i = 0;
	for (p = av->buf; p < av->buf + len; p += strlen(p) + 1) {
		argv[i++] = p;
		if (i < argc)
			continue;
		/* Grow argv. */
		argc += argc;
		argv = realloc(argv, sizeof(char *) * argc);
		if (argv == NULL) {
			warn("malloc(%zu)", sizeof(char *) * argc);
			return (NULL);
		}
		av->argv = argv;
		av->argc = argc;
	}
	argv[i] = NULL;

	return (argv);
}

/*
 * Return process command line arguments.
 */
char **
procstat_getargv(struct procstat *procstat, struct kinfo_proc *p, size_t nchr)
{

	return (getargv(procstat, p, nchr, 0));
}

/*
 * Free the buffer allocated by procstat_getargv().
 */
void
procstat_freeargv(struct procstat *procstat)
{

	if (procstat->argv != NULL) {
		argvec_free(procstat->argv);
		procstat->argv = NULL;
	}
}

/*
 * Return process environment.
 */
char **
procstat_getenvv(struct procstat *procstat, struct kinfo_proc *p, size_t nchr)
{

	return (getargv(procstat, p, nchr, 1));
}

/*
 * Free the buffer allocated by procstat_getenvv().
 */
void
procstat_freeenvv(struct procstat *procstat)
{
	if (procstat->envv != NULL) {
		argvec_free(procstat->envv);
		procstat->envv = NULL;
	}
}

static struct kinfo_vmentry *
kinfo_getvmmap_core(struct procstat_core *core, int *cntp)
{
	int cnt;
	size_t len;
	char *buf, *bp, *eb;
	struct kinfo_vmentry *kiv, *kp, *kv;

	buf = procstat_core_get(core, PSC_TYPE_VMMAP, NULL, &len);
	if (buf == NULL)
		return (NULL);

	/*
	 * XXXMG: The code below is just copy&past from libutil.
	 * The code duplication can be avoided if libutil
	 * is extended to provide something like:
	 *   struct kinfo_vmentry *kinfo_getvmmap_from_buf(const char *buf,
	 *       size_t len, int *cntp);
	 */

	/* Pass 1: count items */
	cnt = 0;
	bp = buf;
	eb = buf + len;
	while (bp < eb) {
		kv = (struct kinfo_vmentry *)(uintptr_t)bp;
		if (kv->kve_structsize == 0)
			break;
		bp += kv->kve_structsize;
		cnt++;
	}

	kiv = calloc(cnt, sizeof(*kiv));
	if (kiv == NULL) {
		free(buf);
		return (NULL);
	}
	bp = buf;
	eb = buf + len;
	kp = kiv;
	/* Pass 2: unpack */
	while (bp < eb) {
		kv = (struct kinfo_vmentry *)(uintptr_t)bp;
		if (kv->kve_structsize == 0)
			break;
		/* Copy/expand into pre-zeroed buffer */
		memcpy(kp, kv, kv->kve_structsize);
		/* Advance to next packed record */
		bp += kv->kve_structsize;
		/* Set field size to fixed length, advance */
		kp->kve_structsize = sizeof(*kp);
		kp++;
	}
	free(buf);
	*cntp = cnt;
	return (kiv);	/* Caller must free() return value */
}

struct kinfo_vmentry *
procstat_getvmmap(struct procstat *procstat, struct kinfo_proc *kp,
    unsigned int *cntp)
{

	switch(procstat->type) {
	case PROCSTAT_KVM:
		warnx("kvm method is not supported");
		return (NULL);
	case PROCSTAT_SYSCTL:
		return (kinfo_getvmmap(kp->ki_pid, cntp));
	case PROCSTAT_CORE:
		return (kinfo_getvmmap_core(procstat->core, cntp));
	default:
		warnx("unknown access method: %d", procstat->type);
		return (NULL);
	}
}

void
procstat_freevmmap(struct procstat *procstat __unused,
    struct kinfo_vmentry *vmmap)
{

	free(vmmap);
}

static gid_t *
procstat_getgroups_kvm(kvm_t *kd, struct kinfo_proc *kp, unsigned int *cntp)
{
	struct proc proc;
	struct ucred ucred;
	gid_t *groups;
	size_t len;

	assert(kd != NULL);
	assert(kp != NULL);
	if (!kvm_read_all(kd, (unsigned long)kp->ki_paddr, &proc,
	    sizeof(proc))) {
		warnx("can't read proc struct at %p for pid %d",
		    kp->ki_paddr, kp->ki_pid);
		return (NULL);
	}
	if (proc.p_ucred == NOCRED)
		return (NULL);
	if (!kvm_read_all(kd, (unsigned long)proc.p_ucred, &ucred,
	    sizeof(ucred))) {
		warnx("can't read ucred struct at %p for pid %d",
		    proc.p_ucred, kp->ki_pid);
		return (NULL);
	}
	len = ucred.cr_ngroups * sizeof(gid_t);
	groups = malloc(len);
	if (groups == NULL) {
		warn("malloc(%zu)", len);
		return (NULL);
	}
	if (!kvm_read_all(kd, (unsigned long)ucred.cr_groups, groups, len)) {
		warnx("can't read groups at %p for pid %d",
		    ucred.cr_groups, kp->ki_pid);
		free(groups);
		return (NULL);
	}
	*cntp = ucred.cr_ngroups;
	return (groups);
}

static gid_t *
procstat_getgroups_sysctl(pid_t pid, unsigned int *cntp)
{
	int mib[4];
	size_t len;
	gid_t *groups;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_GROUPS;
	mib[3] = pid;
	len = (sysconf(_SC_NGROUPS_MAX) + 1) * sizeof(gid_t);
	groups = malloc(len);
	if (groups == NULL) {
		warn("malloc(%zu)", len);
		return (NULL);
	}
	if (sysctl(mib, nitems(mib), groups, &len, NULL, 0) == -1) {
		warn("sysctl: kern.proc.groups: %d", pid);
		free(groups);
		return (NULL);
	}
	*cntp = len / sizeof(gid_t);
	return (groups);
}

static gid_t *
procstat_getgroups_core(struct procstat_core *core, unsigned int *cntp)
{
	size_t len;
	gid_t *groups;

	groups = procstat_core_get(core, PSC_TYPE_GROUPS, NULL, &len);
	if (groups == NULL)
		return (NULL);
	*cntp = len / sizeof(gid_t);
	return (groups);
}

gid_t *
procstat_getgroups(struct procstat *procstat, struct kinfo_proc *kp,
    unsigned int *cntp)
{
	switch(procstat->type) {
	case PROCSTAT_KVM:
		return (procstat_getgroups_kvm(procstat->kd, kp, cntp));
	case PROCSTAT_SYSCTL:
		return (procstat_getgroups_sysctl(kp->ki_pid, cntp));
	case PROCSTAT_CORE:
		return (procstat_getgroups_core(procstat->core, cntp));
	default:
		warnx("unknown access method: %d", procstat->type);
		return (NULL);
	}
}

void
procstat_freegroups(struct procstat *procstat __unused, gid_t *groups)
{

	free(groups);
}

static int
procstat_getumask_kvm(kvm_t *kd, struct kinfo_proc *kp, unsigned short *maskp)
{
	struct filedesc fd;

	assert(kd != NULL);
	assert(kp != NULL);
	if (kp->ki_fd == NULL)
		return (-1);
	if (!kvm_read_all(kd, (unsigned long)kp->ki_fd, &fd, sizeof(fd))) {
		warnx("can't read filedesc at %p for pid %d", kp->ki_fd,
		    kp->ki_pid);
		return (-1);
	}
	*maskp = fd.fd_cmask;
	return (0);
}

static int
procstat_getumask_sysctl(pid_t pid, unsigned short *maskp)
{
	int error;
	int mib[4];
	size_t len;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_UMASK;
	mib[3] = pid;
	len = sizeof(*maskp);
	error = sysctl(mib, nitems(mib), maskp, &len, NULL, 0);
	if (error != 0 && errno != ESRCH && errno != EPERM)
		warn("sysctl: kern.proc.umask: %d", pid);
	return (error);
}

static int
procstat_getumask_core(struct procstat_core *core, unsigned short *maskp)
{
	size_t len;
	unsigned short *buf;

	buf = procstat_core_get(core, PSC_TYPE_UMASK, NULL, &len);
	if (buf == NULL)
		return (-1);
	if (len < sizeof(*maskp)) {
		free(buf);
		return (-1);
	}
	*maskp = *buf;
	free(buf);
	return (0);
}

int
procstat_getumask(struct procstat *procstat, struct kinfo_proc *kp,
    unsigned short *maskp)
{
	switch(procstat->type) {
	case PROCSTAT_KVM:
		return (procstat_getumask_kvm(procstat->kd, kp, maskp));
	case PROCSTAT_SYSCTL:
		return (procstat_getumask_sysctl(kp->ki_pid, maskp));
	case PROCSTAT_CORE:
		return (procstat_getumask_core(procstat->core, maskp));
	default:
		warnx("unknown access method: %d", procstat->type);
		return (-1);
	}
}

static int
procstat_getrlimit_kvm(kvm_t *kd, struct kinfo_proc *kp, int which,
    struct rlimit* rlimit)
{
	struct proc proc;
	unsigned long offset;

	assert(kd != NULL);
	assert(kp != NULL);
	assert(which >= 0 && which < RLIM_NLIMITS);
	if (!kvm_read_all(kd, (unsigned long)kp->ki_paddr, &proc,
	    sizeof(proc))) {
		warnx("can't read proc struct at %p for pid %d",
		    kp->ki_paddr, kp->ki_pid);
		return (-1);
	}
	if (proc.p_limit == NULL)
		return (-1);
	offset = (unsigned long)proc.p_limit + sizeof(struct rlimit) * which;
	if (!kvm_read_all(kd, offset, rlimit, sizeof(*rlimit))) {
		warnx("can't read rlimit struct at %p for pid %d",
		    (void *)offset, kp->ki_pid);
		return (-1);
	}
	return (0);
}

static int
procstat_getrlimit_sysctl(pid_t pid, int which, struct rlimit* rlimit)
{
	int error, name[5];
	size_t len;

	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = KERN_PROC_RLIMIT;
	name[3] = pid;
	name[4] = which;
	len = sizeof(struct rlimit);
	error = sysctl(name, nitems(name), rlimit, &len, NULL, 0);
	if (error < 0 && errno != ESRCH) {
		warn("sysctl: kern.proc.rlimit: %d", pid);
		return (-1);
	}
	if (error < 0 || len != sizeof(struct rlimit))
		return (-1);
	return (0);
}

static int
procstat_getrlimit_core(struct procstat_core *core, int which,
    struct rlimit* rlimit)
{
	size_t len;
	struct rlimit* rlimits;

	if (which < 0 || which >= RLIM_NLIMITS) {
		errno = EINVAL;
		warn("getrlimit: which");
		return (-1);
	}
	rlimits = procstat_core_get(core, PSC_TYPE_RLIMIT, NULL, &len);
	if (rlimits == NULL)
		return (-1);
	if (len < sizeof(struct rlimit) * RLIM_NLIMITS) {
		free(rlimits);
		return (-1);
	}
	*rlimit = rlimits[which];
	free(rlimits);
	return (0);
}

int
procstat_getrlimit(struct procstat *procstat, struct kinfo_proc *kp, int which,
    struct rlimit* rlimit)
{
	switch(procstat->type) {
	case PROCSTAT_KVM:
		return (procstat_getrlimit_kvm(procstat->kd, kp, which,
		    rlimit));
	case PROCSTAT_SYSCTL:
		return (procstat_getrlimit_sysctl(kp->ki_pid, which, rlimit));
	case PROCSTAT_CORE:
		return (procstat_getrlimit_core(procstat->core, which, rlimit));
	default:
		warnx("unknown access method: %d", procstat->type);
		return (-1);
	}
}

static int
procstat_getpathname_sysctl(pid_t pid, char *pathname, size_t maxlen)
{
	int error, name[4];
	size_t len;

	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = KERN_PROC_PATHNAME;
	name[3] = pid;
	len = maxlen;
	error = sysctl(name, nitems(name), pathname, &len, NULL, 0);
	if (error != 0 && errno != ESRCH)
		warn("sysctl: kern.proc.pathname: %d", pid);
	if (len == 0)
		pathname[0] = '\0';
	return (error);
}

static int
procstat_getpathname_core(struct procstat_core *core, char *pathname,
    size_t maxlen)
{
	struct kinfo_file *files;
	int cnt, i, result;

	files = kinfo_getfile_core(core, &cnt);
	if (files == NULL)
		return (-1);
	result = -1;
	for (i = 0; i < cnt; i++) {
		if (files[i].kf_fd != KF_FD_TYPE_TEXT)
			continue;
		strncpy(pathname, files[i].kf_path, maxlen);
		result = 0;
		break;
	}
	free(files);
	return (result);
}

int
procstat_getpathname(struct procstat *procstat, struct kinfo_proc *kp,
    char *pathname, size_t maxlen)
{
	switch(procstat->type) {
	case PROCSTAT_KVM:
		/* XXX: Return empty string. */
		if (maxlen > 0)
			pathname[0] = '\0';
		return (0);
	case PROCSTAT_SYSCTL:
		return (procstat_getpathname_sysctl(kp->ki_pid, pathname,
		    maxlen));
	case PROCSTAT_CORE:
		return (procstat_getpathname_core(procstat->core, pathname,
		    maxlen));
	default:
		warnx("unknown access method: %d", procstat->type);
		return (-1);
	}
}

static int
procstat_getosrel_kvm(kvm_t *kd, struct kinfo_proc *kp, int *osrelp)
{
	struct proc proc;

	assert(kd != NULL);
	assert(kp != NULL);
	if (!kvm_read_all(kd, (unsigned long)kp->ki_paddr, &proc,
	    sizeof(proc))) {
		warnx("can't read proc struct at %p for pid %d",
		    kp->ki_paddr, kp->ki_pid);
		return (-1);
	}
	*osrelp = proc.p_osrel;
	return (0);
}

static int
procstat_getosrel_sysctl(pid_t pid, int *osrelp)
{
	int error, name[4];
	size_t len;

	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = KERN_PROC_OSREL;
	name[3] = pid;
	len = sizeof(*osrelp);
	error = sysctl(name, nitems(name), osrelp, &len, NULL, 0);
	if (error != 0 && errno != ESRCH)
		warn("sysctl: kern.proc.osrel: %d", pid);
	return (error);
}

static int
procstat_getosrel_core(struct procstat_core *core, int *osrelp)
{
	size_t len;
	int *buf;

	buf = procstat_core_get(core, PSC_TYPE_OSREL, NULL, &len);
	if (buf == NULL)
		return (-1);
	if (len < sizeof(*osrelp)) {
		free(buf);
		return (-1);
	}
	*osrelp = *buf;
	free(buf);
	return (0);
}

int
procstat_getosrel(struct procstat *procstat, struct kinfo_proc *kp, int *osrelp)
{
	switch(procstat->type) {
	case PROCSTAT_KVM:
		return (procstat_getosrel_kvm(procstat->kd, kp, osrelp));
	case PROCSTAT_SYSCTL:
		return (procstat_getosrel_sysctl(kp->ki_pid, osrelp));
	case PROCSTAT_CORE:
		return (procstat_getosrel_core(procstat->core, osrelp));
	default:
		warnx("unknown access method: %d", procstat->type);
		return (-1);
	}
}

#define PROC_AUXV_MAX	256

#if __ELF_WORD_SIZE == 64
static const char *elf32_sv_names[] = {
	"Linux ELF32",
	"FreeBSD ELF32",
};

static int
is_elf32_sysctl(pid_t pid)
{
	int error, name[4];
	size_t len, i;
	static char sv_name[256];

	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = KERN_PROC_SV_NAME;
	name[3] = pid;
	len = sizeof(sv_name);
	error = sysctl(name, nitems(name), sv_name, &len, NULL, 0);
	if (error != 0 || len == 0)
		return (0);
	for (i = 0; i < sizeof(elf32_sv_names) / sizeof(*elf32_sv_names); i++) {
		if (strncmp(sv_name, elf32_sv_names[i], sizeof(sv_name)) == 0)
			return (1);
	}
	return (0);
}

static Elf_Auxinfo *
procstat_getauxv32_sysctl(pid_t pid, unsigned int *cntp)
{
	Elf_Auxinfo *auxv;
	Elf32_Auxinfo *auxv32;
	void *ptr;
	size_t len;
	unsigned int i, count;
	int name[4];

	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = KERN_PROC_AUXV;
	name[3] = pid;
	len = PROC_AUXV_MAX * sizeof(Elf32_Auxinfo);
	auxv = NULL;
	auxv32 = malloc(len);
	if (auxv32 == NULL) {
		warn("malloc(%zu)", len);
		goto out;
	}
	if (sysctl(name, nitems(name), auxv32, &len, NULL, 0) == -1) {
		if (errno != ESRCH && errno != EPERM)
			warn("sysctl: kern.proc.auxv: %d: %d", pid, errno);
		goto out;
	}
	count = len / sizeof(Elf_Auxinfo);
	auxv = malloc(count  * sizeof(Elf_Auxinfo));
	if (auxv == NULL) {
		warn("malloc(%zu)", count * sizeof(Elf_Auxinfo));
		goto out;
	}
	for (i = 0; i < count; i++) {
		/*
		 * XXX: We expect that values for a_type on a 32-bit platform
		 * are directly mapped to values on 64-bit one, which is not
		 * necessarily true.
		 */
		auxv[i].a_type = auxv32[i].a_type;
		ptr = &auxv32[i].a_un;
		auxv[i].a_un.a_val = *((uint32_t *)ptr);
	}
	*cntp = count;
out:
	free(auxv32);
	return (auxv);
}
#endif /* __ELF_WORD_SIZE == 64 */

static Elf_Auxinfo *
procstat_getauxv_sysctl(pid_t pid, unsigned int *cntp)
{
	Elf_Auxinfo *auxv;
	int name[4];
	size_t len;

#if __ELF_WORD_SIZE == 64
	if (is_elf32_sysctl(pid))
		return (procstat_getauxv32_sysctl(pid, cntp));
#endif
	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = KERN_PROC_AUXV;
	name[3] = pid;
	len = PROC_AUXV_MAX * sizeof(Elf_Auxinfo);
	auxv = malloc(len);
	if (auxv == NULL) {
		warn("malloc(%zu)", len);
		return (NULL);
	}
	if (sysctl(name, nitems(name), auxv, &len, NULL, 0) == -1) {
		if (errno != ESRCH && errno != EPERM)
			warn("sysctl: kern.proc.auxv: %d: %d", pid, errno);
		free(auxv);
		return (NULL);
	}
	*cntp = len / sizeof(Elf_Auxinfo);
	return (auxv);
}

static Elf_Auxinfo *
procstat_getauxv_core(struct procstat_core *core, unsigned int *cntp)
{
	Elf_Auxinfo *auxv;
	size_t len;

	auxv = procstat_core_get(core, PSC_TYPE_AUXV, NULL, &len);
	if (auxv == NULL)
		return (NULL);
	*cntp = len / sizeof(Elf_Auxinfo);
	return (auxv);
}

Elf_Auxinfo *
procstat_getauxv(struct procstat *procstat, struct kinfo_proc *kp,
    unsigned int *cntp)
{
	switch(procstat->type) {
	case PROCSTAT_KVM:
		warnx("kvm method is not supported");
		return (NULL);
	case PROCSTAT_SYSCTL:
		return (procstat_getauxv_sysctl(kp->ki_pid, cntp));
	case PROCSTAT_CORE:
		return (procstat_getauxv_core(procstat->core, cntp));
	default:
		warnx("unknown access method: %d", procstat->type);
		return (NULL);
	}
}

void
procstat_freeauxv(struct procstat *procstat __unused, Elf_Auxinfo *auxv)
{

	free(auxv);
}

static struct ptrace_lwpinfo *
procstat_getptlwpinfo_core(struct procstat_core *core, unsigned int *cntp)
{
	void *buf;
	struct ptrace_lwpinfo *pl;
	unsigned int cnt;
	size_t len;

	cnt = procstat_core_note_count(core, PSC_TYPE_PTLWPINFO);
	if (cnt == 0)
		return (NULL);

	len = cnt * sizeof(*pl);
	buf = calloc(1, len);
	pl = procstat_core_get(core, PSC_TYPE_PTLWPINFO, buf, &len);
	if (pl == NULL) {
		free(buf);
		return (NULL);
	}
	*cntp = len / sizeof(*pl);
	return (pl);
}

struct ptrace_lwpinfo *
procstat_getptlwpinfo(struct procstat *procstat, unsigned int *cntp)
{
	switch (procstat->type) {
	case PROCSTAT_KVM:
		warnx("kvm method is not supported");
		return (NULL);
	case PROCSTAT_SYSCTL:
		warnx("sysctl method is not supported");
		return (NULL);
	case PROCSTAT_CORE:
	 	return (procstat_getptlwpinfo_core(procstat->core, cntp));
	default:
		warnx("unknown access method: %d", procstat->type);
		return (NULL);
	}
}

void
procstat_freeptlwpinfo(struct procstat *procstat __unused,
    struct ptrace_lwpinfo *pl)
{
	free(pl);
}

static struct kinfo_kstack *
procstat_getkstack_sysctl(pid_t pid, int *cntp)
{
	struct kinfo_kstack *kkstp;
	int error, name[4];
	size_t len;

	name[0] = CTL_KERN;
	name[1] = KERN_PROC;
	name[2] = KERN_PROC_KSTACK;
	name[3] = pid;

	len = 0;
	error = sysctl(name, nitems(name), NULL, &len, NULL, 0);
	if (error < 0 && errno != ESRCH && errno != EPERM && errno != ENOENT) {
		warn("sysctl: kern.proc.kstack: %d", pid);
		return (NULL);
	}
	if (error == -1 && errno == ENOENT) {
		warnx("sysctl: kern.proc.kstack unavailable"
		    " (options DDB or options STACK required in kernel)");
		return (NULL);
	}
	if (error == -1)
		return (NULL);
	kkstp = malloc(len);
	if (kkstp == NULL) {
		warn("malloc(%zu)", len);
		return (NULL);
	}
	if (sysctl(name, nitems(name), kkstp, &len, NULL, 0) == -1) {
		warn("sysctl: kern.proc.pid: %d", pid);
		free(kkstp);
		return (NULL);
	}
	*cntp = len / sizeof(*kkstp);

	return (kkstp);
}

struct kinfo_kstack *
procstat_getkstack(struct procstat *procstat, struct kinfo_proc *kp,
    unsigned int *cntp)
{
	switch(procstat->type) {
	case PROCSTAT_KVM:
		warnx("kvm method is not supported");
		return (NULL);
	case PROCSTAT_SYSCTL:
		return (procstat_getkstack_sysctl(kp->ki_pid, cntp));
	case PROCSTAT_CORE:
		warnx("core method is not supported");
		return (NULL);
	default:
		warnx("unknown access method: %d", procstat->type);
		return (NULL);
	}
}

void
procstat_freekstack(struct procstat *procstat __unused,
    struct kinfo_kstack *kkstp)
{

	free(kkstp);
}
