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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2017 RackTop Systems.
 */

#ifndef	_SYS_FM_UTIL_H
#define	_SYS_FM_UTIL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/nvpair.h>
#include <sys/errorq.h>

/*
 * Shared user/kernel definitions for class length, error channel name,
 * and kernel event publisher string.
 */
#define	FM_MAX_CLASS 100
#define	FM_ERROR_CHAN	"com.sun:fm:error"
#define	FM_PUB		"fm"

/*
 * ereport dump device transport support
 *
 * Ereports are written out to the dump device at a proscribed offset from the
 * end, similar to in-transit log messages.  The ereports are represented as a
 * erpt_dump_t header followed by ed_size bytes of packed native nvlist data.
 *
 * NOTE: All of these constants and the header must be defined so they have the
 * same representation for *both* 32-bit and 64-bit producers and consumers.
 */
#define	ERPT_MAGIC	0xf00d4eddU
#define	ERPT_MAX_ERRS	16
#define	ERPT_DATA_SZ	(6 * 1024)
#define	ERPT_EVCH_MAX	256
#define	ERPT_HIWAT	64

typedef struct erpt_dump {
	uint32_t ed_magic;	/* ERPT_MAGIC or zero to indicate end */
	uint32_t ed_chksum;	/* checksum32() of packed nvlist data */
	uint32_t ed_size;	/* ereport (nvl) fixed buf size */
	uint32_t ed_pad;	/* reserved for future use */
	hrtime_t ed_hrt_nsec;	/* hrtime of this ereport */
	hrtime_t ed_hrt_base;	/* hrtime sample corresponding to ed_tod_base */
	struct {
		uint64_t sec;	/* seconds since gettimeofday() Epoch */
		uint64_t nsec;	/* nanoseconds past ed_tod_base.sec */
	} ed_tod_base;
} erpt_dump_t;

#if defined(_KERNEL) || defined(_FAKE_KERNEL)
#include <sys/systm.h>

#define	FM_STK_DEPTH	20	/* maximum stack depth */
#define	FM_SYM_SZ	64	/* maximum symbol size */
#define	FM_ERR_PIL	2	/* PIL for ereport_errorq drain processing */

#define	FM_EREPORT_PAYLOAD_NAME_STACK		"stack"

extern errorq_t *ereport_errorq;
extern void *ereport_dumpbuf;
extern size_t ereport_dumplen;

extern void fm_init(void);
extern void fm_nvprint(nvlist_t *);
#define	fm_panic	panic
extern void fm_banner(void);

extern void fm_ereport_dump(void);
extern void fm_ereport_post(nvlist_t *, int);

extern int is_fm_panic();
#endif  /* _KERNEL || _FAKE_KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_FM_UTIL_H */
