/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2007, Juniper Networks, Inc.
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 * $FreeBSD$
 */

#ifndef _SYS_CFICTL_H_
#define _SYS_CFICTL_H_

/*
 * Allow reading of the CFI query structure.
 */

struct cfiocqry {
	unsigned long	offset;
	unsigned long	count;
	u_char		*buffer;
};

#define	CFIOCQRY	_IOWR('q', 0, struct cfiocqry)

/* Intel StrataFlash Protection Register support */
#define	CFIOCGFACTORYPR	_IOR('q', 1, uint64_t)	/* get factory protection reg */
#define	CFIOCGOEMPR	_IOR('q', 2, uint64_t)	/* get oem protection reg */
#define	CFIOCSOEMPR	_IOW('q', 3, uint64_t)	/* set oem protection reg */
#define	CFIOCGPLR	_IOR('q', 4, uint32_t)	/* get protection lock reg */
#define	CFIOCSPLR	_IO('q', 5)		/* set protection log reg */
#endif	/* _SYS_CFICTL_H_ */
