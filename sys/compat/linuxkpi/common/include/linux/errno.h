/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2016 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 *
 * $FreeBSD$
 */
#ifndef	_LINUX_ERRNO_H_
#define	_LINUX_ERRNO_H_

#include <sys/errno.h>

#define	ECHRNG		EDOM
#define	ETIME		ETIMEDOUT
#define	ECOMM		ESTALE
#define	ENODATA		ECONNREFUSED
#define	ENOIOCTLCMD     ENOIOCTL
/* Use same value as Linux, because BSD's ERESTART is negative */
#define	ERESTARTSYS     512
#define	ENOTSUPP	EOPNOTSUPP
#define	ENONET		EHOSTDOWN

#define	ERESTARTNOINTR	513
#define	ERESTARTNOHAND	514
#define	ERESTART_RESTARTBLOCK 516
#define	EPROBE_DEFER	517
#define	EOPENSTALE	518
#define	EBADHANDLE	521
#define	ENOTSYNC	522
#define	EBADCOOKIE	523
#define	ETOOSMALL	525
#define	ESERVERFAULT	526
#define	EBADTYPE	527
#define	EJUKEBOX	528
#define	EIOCBQUEUED	529

#endif					/* _LINUX_ERRNO_H_ */
