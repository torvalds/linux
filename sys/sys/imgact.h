/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1993, David Greenman
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
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 *
 * $FreeBSD$
 */

#ifndef _SYS_IMGACT_H_
#define	_SYS_IMGACT_H_

#include <sys/_uio.h>

#include <vm/vm.h>

#define MAXSHELLCMDLEN	PAGE_SIZE

struct ucred;

struct image_args {
	char *buf;		/* pointer to string buffer */
	void *bufkva;		/* cookie for string buffer KVA */
	char *begin_argv;	/* beginning of argv in buf */
	char *begin_envv;	/* (interal use only) beginning of envv in buf,
				 * access with exec_args_get_begin_envv(). */
	char *endp;		/* current `end' pointer of arg & env strings */
	char *fname;            /* pointer to filename of executable (system space) */
	char *fname_buf;	/* pointer to optional malloc(M_TEMP) buffer */
	int stringspace;	/* space left in arg & env buffer */
	int argc;		/* count of argument strings */
	int envc;		/* count of environment strings */
	int fd;			/* file descriptor of the executable */
	struct filedesc *fdp;	/* new file descriptor table */
};

struct image_params {
	struct proc *proc;	/* our process struct */
	struct label *execlabel;	/* optional exec label */
	struct vnode *vp;	/* pointer to vnode of file to exec */
	struct vm_object *object;	/* The vm object for this vp */
	struct vattr *attr;	/* attributes of file */
	const char *image_header; /* head of file to exec */
	unsigned long entry_addr; /* entry address of target executable */
	unsigned long reloc_base; /* load address of image */
	char vmspace_destroyed;	/* flag - we've blown away original vm space */
#define IMGACT_SHELL	0x1
#define IMGACT_BINMISC	0x2
	unsigned char interpreted;	/* mask of interpreters that have run */
	char opened;		/* flag - we have opened executable vnode */
	char *interpreter_name;	/* name of the interpreter */
	void *auxargs;		/* ELF Auxinfo structure pointer */
	struct sf_buf *firstpage;	/* first page that we mapped */
	unsigned long ps_strings; /* PS_STRINGS for BSD/OS binaries */
	struct image_args *args;	/* system call arguments */
	struct sysentvec *sysent;	/* system entry vector */
	char *execpath;
	unsigned long execpathp;
	char *freepath;
	unsigned long canary;
	int canarylen;
	unsigned long pagesizes;
	int pagesizeslen;
	vm_prot_t stack_prot;
	u_long stack_sz;
	struct ucred *newcred;		/* new credentials if changing */
	bool credential_setid;		/* true if becoming setid */
	u_int map_flags;
};

#ifdef _KERNEL
struct sysentvec;
struct thread;
struct vmspace;

int	exec_alloc_args(struct image_args *);
int	exec_args_add_arg(struct image_args *args, const char *argp,
	    enum uio_seg segflg);
int	exec_args_add_env(struct image_args *args, const char *envp,
	    enum uio_seg segflg);
int	exec_args_add_fname(struct image_args *args, const char *fname,
	    enum uio_seg segflg);
int	exec_args_adjust_args(struct image_args *args, size_t consume,
	    ssize_t extend);
char	*exec_args_get_begin_envv(struct image_args *args);
int	exec_check_permissions(struct image_params *);
register_t *exec_copyout_strings(struct image_params *);
void	exec_free_args(struct image_args *);
int	exec_new_vmspace(struct image_params *, struct sysentvec *);
void	exec_setregs(struct thread *, struct image_params *, u_long);
int	exec_shell_imgact(struct image_params *);
int	exec_copyin_args(struct image_args *, const char *, enum uio_seg,
	char **, char **);
int	exec_copyin_data_fds(struct thread *, struct image_args *, const void *,
	size_t, const int *, size_t);
int	pre_execve(struct thread *td, struct vmspace **oldvmspace);
void	post_execve(struct thread *td, int error, struct vmspace *oldvmspace);
#endif

#endif /* !_SYS_IMGACT_H_ */
