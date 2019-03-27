/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2018 Turing Robotic Industries Inc.
 * Copyright (c) 2000 Marcel Moolenaar
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/fcntl.h>
#include <sys/imgact.h>
#include <sys/ktr.h>
#include <sys/proc.h>
#include <sys/sdt.h>

#include <arm64/linux/linux.h>
#include <arm64/linux/linux_proto.h>
#include <compat/linux/linux_dtrace.h>
#include <compat/linux/linux_emul.h>
#include <compat/linux/linux_misc.h>
#include <compat/linux/linux_mmap.h>
#include <compat/linux/linux_util.h>

/* DTrace init */
LIN_SDT_PROVIDER_DECLARE(LINUX_DTRACE);

/* DTrace probes */
LIN_SDT_PROBE_DEFINE0(machdep, linux_set_upcall_kse, todo);
LIN_SDT_PROBE_DEFINE0(machdep, linux_mmap2, todo);
LIN_SDT_PROBE_DEFINE0(machdep, linux_rt_sigsuspend, todo);
LIN_SDT_PROBE_DEFINE0(machdep, linux_sigaltstack, todo);
LIN_SDT_PROBE_DEFINE0(machdep, linux_set_cloned_tls, todo);

/*
 * LINUXTODO: deduplicate; linux_execve is common across archs, except that on
 * amd64 compat linuxulator it calls freebsd32_exec_copyin_args.
 */
int
linux_execve(struct thread *td, struct linux_execve_args *uap)
{
	struct image_args eargs;
	char *path;
	int error;

	LCONVPATHEXIST(td, uap->path, &path);

	error = exec_copyin_args(&eargs, path, UIO_SYSSPACE, uap->argp,
	    uap->envp);
	free(path, M_TEMP);
	if (error == 0)
		error = linux_common_execve(td, &eargs);
	return (error);
}

/* LINUXTODO: implement (or deduplicate) arm64 linux_set_upcall_kse */
int
linux_set_upcall_kse(struct thread *td, register_t stack)
{

	LIN_SDT_PROBE0(machdep, linux_set_upcall_kse, todo);
	return (EDOOFUS);
}

/* LINUXTODO: deduplicate arm64 linux_mmap2 */
int
linux_mmap2(struct thread *td, struct linux_mmap2_args *uap)
{

	LIN_SDT_PROBE0(machdep, linux_mmap2, todo);
	return (linux_mmap_common(td, PTROUT(uap->addr), uap->len, uap->prot,
	    uap->flags, uap->fd, uap->pgoff));
}

int
linux_mprotect(struct thread *td, struct linux_mprotect_args *uap)
{

	return (linux_mprotect_common(td, PTROUT(uap->addr), uap->len,
	    uap->prot));
}

/* LINUXTODO: implement arm64 linux_rt_sigsuspend */
int
linux_rt_sigsuspend(struct thread *td, struct linux_rt_sigsuspend_args *uap)
{

	LIN_SDT_PROBE0(machdep, linux_rt_sigsuspend, todo);
	return (EDOOFUS);
}

/* LINUXTODO: implement arm64 linux_sigaltstack */
int
linux_sigaltstack(struct thread *td, struct linux_sigaltstack_args *uap)
{

	LIN_SDT_PROBE0(machdep, linux_sigaltstack, todo);
	return (EDOOFUS);
}

/* LINUXTODO: implement arm64 linux_set_cloned_tls */
int
linux_set_cloned_tls(struct thread *td, void *desc)
{

	LIN_SDT_PROBE0(machdep, linux_set_cloned_tls, todo);
	return (EDOOFUS);
}
