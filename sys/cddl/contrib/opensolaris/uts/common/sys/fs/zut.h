/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _ZUT_H
#define	_ZUT_H

/*
 * IOCTLs for the zfs unit test driver
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#define	ZUT_DRIVER	"zut"
#define	ZUT_DEV		"/dev/zut"

#define	ZUT_VERSION_STRING		"1"

/*
 * /dev/zut ioctl numbers.
 */
#define	ZUT_IOC		('U' << 8)

/* Request flags */
#define	ZUT_IGNORECASE		0x01
#define	ZUT_ACCFILTER		0x02
#define	ZUT_XATTR		0x04
#define	ZUT_EXTRDDIR		0x08
#define	ZUT_GETSTAT		0x10

typedef struct zut_lookup {
	int	zl_reqflags;
	int	zl_deflags;		/* output */
	int	zl_retcode;		/* output */
	char	zl_dir[MAXPATHLEN];
	char	zl_file[MAXNAMELEN];
	char	zl_xfile[MAXNAMELEN];
	char	zl_real[MAXPATHLEN];	/* output */
	uint64_t zl_xvattrs;		/* output */
	struct stat64 zl_statbuf;	/* output */
} zut_lookup_t;

typedef struct zut_readdir {
	uint64_t zr_buf;		/* pointer to output buffer */
	uint64_t zr_loffset;		/* output */
	char	zr_dir[MAXPATHLEN];
	char	zr_file[MAXNAMELEN];
	int	zr_reqflags;
	int	zr_retcode;		/* output */
	int	zr_eof;			/* output */
	uint_t	zr_bytes;		/* output */
	uint_t	zr_buflen;
} zut_readdir_t;

typedef enum zut_ioc {
	ZUT_IOC_MIN_CMD = ZUT_IOC - 1,
	ZUT_IOC_LOOKUP = ZUT_IOC,
	ZUT_IOC_READDIR,
	ZUT_IOC_MAX_CMD
} zut_ioc_t;

#ifdef __cplusplus
}
#endif

#endif /* _ZUT_H */
