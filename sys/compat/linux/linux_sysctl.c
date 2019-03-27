/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Marcel Moolenaar
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

#include "opt_compat.h"

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sdt.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/sbuf.h>

#ifdef COMPAT_LINUX32
#include <machine/../linux32/linux.h>
#include <machine/../linux32/linux32_proto.h>
#else
#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#endif

#include <compat/linux/linux_dtrace.h>
#include <compat/linux/linux_misc.h>
#include <compat/linux/linux_util.h>

#define	LINUX_CTL_KERN		1
#define	LINUX_CTL_VM		2
#define	LINUX_CTL_NET		3
#define	LINUX_CTL_PROC		4
#define	LINUX_CTL_FS		5
#define	LINUX_CTL_DEBUG		6
#define	LINUX_CTL_DEV		7
#define	LINUX_CTL_BUS		8

/* CTL_KERN names */
#define	LINUX_KERN_OSTYPE	1
#define	LINUX_KERN_OSRELEASE	2
#define	LINUX_KERN_OSREV	3
#define	LINUX_KERN_VERSION	4

/* DTrace init */
LIN_SDT_PROVIDER_DECLARE(LINUX_DTRACE);

/**
 * DTrace probes in this module.
 */
LIN_SDT_PROBE_DEFINE2(sysctl, handle_string, entry, "struct l___sysctl_args *",
    "char *");
LIN_SDT_PROBE_DEFINE1(sysctl, handle_string, copyout_error, "int");
LIN_SDT_PROBE_DEFINE1(sysctl, handle_string, return, "int");
LIN_SDT_PROBE_DEFINE2(sysctl, linux_sysctl, entry, "struct l___sysctl_args *",
    "struct thread *");
LIN_SDT_PROBE_DEFINE1(sysctl, linux_sysctl, copyin_error, "int");
LIN_SDT_PROBE_DEFINE2(sysctl, linux_sysctl, wrong_length, "int", "int");
LIN_SDT_PROBE_DEFINE1(sysctl, linux_sysctl, unsupported_sysctl, "char *");
LIN_SDT_PROBE_DEFINE1(sysctl, linux_sysctl, return, "int");

#ifdef LINUX_LEGACY_SYSCALLS
static int
handle_string(struct l___sysctl_args *la, char *value)
{
	int error;

	LIN_SDT_PROBE2(sysctl, handle_string, entry, la, value);

	if (la->oldval != 0) {
		l_int len = strlen(value);
		error = copyout(value, PTRIN(la->oldval), len + 1);
		if (!error && la->oldlenp != 0)
			error = copyout(&len, PTRIN(la->oldlenp), sizeof(len));
		if (error) {
			LIN_SDT_PROBE1(sysctl, handle_string, copyout_error,
			    error);
			LIN_SDT_PROBE1(sysctl, handle_string, return, error);
			return (error);
		}
	}

	if (la->newval != 0) {
		LIN_SDT_PROBE1(sysctl, handle_string, return, ENOTDIR);
		return (ENOTDIR);
	}

	LIN_SDT_PROBE1(sysctl, handle_string, return, 0);
	return (0);
}

int
linux_sysctl(struct thread *td, struct linux_sysctl_args *args)
{
	struct l___sysctl_args la;
	struct sbuf *sb;
	l_int *mib;
	char *sysctl_string;
	int error, i;

	LIN_SDT_PROBE2(sysctl, linux_sysctl, entry, td, args->args);

	error = copyin(args->args, &la, sizeof(la));
	if (error) {
		LIN_SDT_PROBE1(sysctl, linux_sysctl, copyin_error, error);
		LIN_SDT_PROBE1(sysctl, linux_sysctl, return, error);
		return (error);
	}

	if (la.nlen <= 0 || la.nlen > LINUX_CTL_MAXNAME) {
		LIN_SDT_PROBE2(sysctl, linux_sysctl, wrong_length, la.nlen,
		    LINUX_CTL_MAXNAME);
		LIN_SDT_PROBE1(sysctl, linux_sysctl, return, ENOTDIR);
		return (ENOTDIR);
	}

	mib = malloc(la.nlen * sizeof(l_int), M_LINUX, M_WAITOK);
	error = copyin(PTRIN(la.name), mib, la.nlen * sizeof(l_int));
	if (error) {
		LIN_SDT_PROBE1(sysctl, linux_sysctl, copyin_error, error);
		LIN_SDT_PROBE1(sysctl, linux_sysctl, return, error);
		free(mib, M_LINUX);
		return (error);
	}

	switch (mib[0]) {
	case LINUX_CTL_KERN:
		if (la.nlen < 2)
			break;

		switch (mib[1]) {
		case LINUX_KERN_VERSION:
			error = handle_string(&la, version);
			free(mib, M_LINUX);
			LIN_SDT_PROBE1(sysctl, linux_sysctl, return, error);
			return (error);
		default:
			break;
		}
		break;
	default:
		break;
	}

	sb = sbuf_new(NULL, NULL, 20 + la.nlen * 5, SBUF_AUTOEXTEND);
	if (sb == NULL) {
		linux_msg(td, "sysctl is not implemented");
		LIN_SDT_PROBE1(sysctl, linux_sysctl, unsupported_sysctl,
		    "unknown sysctl, ENOMEM during lookup");
	} else {
		sbuf_printf(sb, "sysctl ");
		for (i = 0; i < la.nlen; i++)
			sbuf_printf(sb, "%c%d", (i) ? ',' : '{', mib[i]);
		sbuf_printf(sb, "} is not implemented");
		sbuf_finish(sb);
		sysctl_string = sbuf_data(sb);
		linux_msg(td, "%s", sbuf_data(sb));
		LIN_SDT_PROBE1(sysctl, linux_sysctl, unsupported_sysctl,
		    sysctl_string);
		sbuf_delete(sb);
	}

	free(mib, M_LINUX);

	LIN_SDT_PROBE1(sysctl, linux_sysctl, return, ENOTDIR);
	return (ENOTDIR);
}
#endif
