/*-
 * Copyright (c) 2006 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
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

#ifndef _SECURITY_AUDIT_AUDIT_IOCTL_H_
#define	_SECURITY_AUDIT_AUDIT_IOCTL_H_

#include <bsm/audit.h>

#define	AUDITPIPE_IOBASE	'A'

/*
 * Data structures used for complex ioctl arguments.  Do not change existing
 * structures, add new revised ones to be used by new ioctls, and keep the
 * old structures and ioctls for backwards compatibility.
 */
struct auditpipe_ioctl_preselect {
	au_id_t		aip_auid;
	au_mask_t	aip_mask;
};

/*
 * Possible modes of operation for audit pipe preselection.
 */
#define	AUDITPIPE_PRESELECT_MODE_TRAIL	1	/* Global audit trail. */
#define	AUDITPIPE_PRESELECT_MODE_LOCAL	2	/* Local audit trail. */

/*
 * Ioctls to read and control the behavior of individual audit pipe devices.
 */
#define	AUDITPIPE_GET_QLEN		_IOR(AUDITPIPE_IOBASE, 1, u_int)
#define	AUDITPIPE_GET_QLIMIT		_IOR(AUDITPIPE_IOBASE, 2, u_int)
#define	AUDITPIPE_SET_QLIMIT		_IOW(AUDITPIPE_IOBASE, 3, u_int)
#define	AUDITPIPE_GET_QLIMIT_MIN	_IOR(AUDITPIPE_IOBASE, 4, u_int)
#define	AUDITPIPE_GET_QLIMIT_MAX	_IOR(AUDITPIPE_IOBASE, 5, u_int)
#define	AUDITPIPE_GET_PRESELECT_FLAGS	_IOR(AUDITPIPE_IOBASE, 6, au_mask_t)
#define	AUDITPIPE_SET_PRESELECT_FLAGS	_IOW(AUDITPIPE_IOBASE, 7, au_mask_t)
#define	AUDITPIPE_GET_PRESELECT_NAFLAGS	_IOR(AUDITPIPE_IOBASE, 8, au_mask_t)
#define	AUDITPIPE_SET_PRESELECT_NAFLAGS	_IOW(AUDITPIPE_IOBASE, 9, au_mask_t)
#define	AUDITPIPE_GET_PRESELECT_AUID	_IOR(AUDITPIPE_IOBASE, 10,	\
					    struct auditpipe_ioctl_preselect)
#define	AUDITPIPE_SET_PRESELECT_AUID	_IOW(AUDITPIPE_IOBASE, 11,	\
					    struct auditpipe_ioctl_preselect)
#define	AUDITPIPE_DELETE_PRESELECT_AUID	_IOW(AUDITPIPE_IOBASE, 12, au_id_t)
#define	AUDITPIPE_FLUSH_PRESELECT_AUID	_IO(AUDITPIPE_IOBASE, 13)
#define	AUDITPIPE_GET_PRESELECT_MODE	_IOR(AUDITPIPE_IOBASE, 14, int)
#define	AUDITPIPE_SET_PRESELECT_MODE	_IOW(AUDITPIPE_IOBASE, 15, int)
#define	AUDITPIPE_FLUSH			_IO(AUDITPIPE_IOBASE, 16)
#define	AUDITPIPE_GET_MAXAUDITDATA	_IOR(AUDITPIPE_IOBASE, 17, u_int)

/*
 * Ioctls to retrieve audit pipe statistics.
 */
#define	AUDITPIPE_GET_INSERTS		_IOR(AUDITPIPE_IOBASE, 100, u_int64_t)
#define	AUDITPIPE_GET_READS		_IOR(AUDITPIPE_IOBASE, 101, u_int64_t)
#define	AUDITPIPE_GET_DROPS		_IOR(AUDITPIPE_IOBASE, 102, u_int64_t)
#define	AUDITPIPE_GET_TRUNCATES		_IOR(AUDITPIPE_IOBASE, 103, u_int64_t)

#endif /* _SECURITY_AUDIT_AUDIT_IOCTL_H_ */
