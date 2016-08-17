/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_UIO_H
#define	_SPL_UIO_H

#include <linux/uio.h>
#include <linux/blkdev.h>
#include <asm/uaccess.h>
#include <sys/types.h>

typedef struct iovec iovec_t;

typedef enum uio_rw {
	UIO_READ =		0,
	UIO_WRITE =		1,
} uio_rw_t;

typedef enum uio_seg {
	UIO_USERSPACE =		0,
	UIO_SYSSPACE =		1,
	UIO_USERISPACE =	2,
	UIO_BVEC =		3,
} uio_seg_t;

typedef struct uio {
	union {
		const struct iovec	*uio_iov;
		const struct bio_vec	*uio_bvec;
	};
	int		uio_iovcnt;
	offset_t	uio_loffset;
	uio_seg_t	uio_segflg;
	uint16_t	uio_fmode;
	uint16_t	uio_extflg;
	offset_t	uio_limit;
	ssize_t		uio_resid;
	size_t		uio_skip;
} uio_t;

typedef struct aio_req {
	uio_t		*aio_uio;
	void		*aio_private;
} aio_req_t;

typedef enum xuio_type {
	UIOTYPE_ASYNCIO,
	UIOTYPE_ZEROCOPY,
} xuio_type_t;


#define	UIOA_IOV_MAX    16

typedef struct uioa_page_s {
	int	uioa_pfncnt;
	void	**uioa_ppp;
	caddr_t	uioa_base;
	size_t	uioa_len;
} uioa_page_t;

typedef struct xuio {
	uio_t xu_uio;
	enum xuio_type xu_type;
	union {
		struct {
			uint32_t xu_a_state;
			ssize_t xu_a_mbytes;
			uioa_page_t *xu_a_lcur;
			void **xu_a_lppp;
			void *xu_a_hwst[4];
			uioa_page_t xu_a_locked[UIOA_IOV_MAX];
		} xu_aio;

		struct {
			int xu_zc_rw;
			void *xu_zc_priv;
		} xu_zc;
	} xu_ext;
} xuio_t;

#define	XUIO_XUZC_PRIV(xuio)	xuio->xu_ext.xu_zc.xu_zc_priv
#define	XUIO_XUZC_RW(xuio)	xuio->xu_ext.xu_zc.xu_zc_rw

#endif /* SPL_UIO_H */
