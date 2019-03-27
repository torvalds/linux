/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 David E. O'Brien
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
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#ifndef _COMPAT_FREEBSD32_IOCTL_H_
#define	_COMPAT_FREEBSD32_IOCTL_H_

#include <cam/scsi/scsi_sg.h>

typedef __uint32_t caddr_t32;

struct mem_range_op32
{
	caddr_t32	mo_desc;
	int		mo_arg[2];
};

struct pci_bar_mmap32 {
	uint32_t	pbm_map_base;
	uint32_t	pbm_map_length;
	uint32_t	pbm_bar_length1, pbm_bar_length2;
	int		pbm_bar_off;
	struct pcisel	pbm_sel;
	int		pbm_reg;
	int		pbm_flags;
	int		pbm_memattr;
};

#define	MEMRANGE_GET32	_IOWR('m', 50, struct mem_range_op32)
#define	MEMRANGE_SET32	_IOW('m', 51, struct mem_range_op32)
#define	SG_IO_32	_IOWR(SGIOC, 0x85, struct sg_io_hdr32)
#define	PCIOCBARMMAP_32	_IOWR('p', 8, struct pci_bar_mmap32)

#endif	/* _COMPAT_FREEBSD32_IOCTL_H_ */
