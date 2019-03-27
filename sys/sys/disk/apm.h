/*-
 * Copyright (c) 2007 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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

#ifndef _SYS_DISK_APM_H_
#define	_SYS_DISK_APM_H_

/* Driver Descriptor Record. */
struct apm_ddr {
	uint16_t	ddr_sig;
#define	APM_DDR_SIG		0x4552
	uint16_t	ddr_blksize;
	uint32_t	ddr_blkcount;
};

#define	APM_ENT_NAMELEN		32
#define	APM_ENT_TYPELEN		32

/* Partition Map Entry Record. */
struct apm_ent {
	uint16_t	ent_sig;
#define	APM_ENT_SIG		0x504d
	uint16_t	_pad_;
	uint32_t	ent_pmblkcnt;
	uint32_t	ent_start;
	uint32_t	ent_size;
	char		ent_name[APM_ENT_NAMELEN];
	char		ent_type[APM_ENT_TYPELEN];
};

#define	APM_ENT_TYPE_SELF		"Apple_partition_map"
#define	APM_ENT_TYPE_UNUSED		"Apple_Free"

#define	APM_ENT_TYPE_FREEBSD		"FreeBSD"
#define	APM_ENT_TYPE_FREEBSD_NANDFS	"FreeBSD-nandfs"
#define	APM_ENT_TYPE_FREEBSD_SWAP	"FreeBSD-swap"
#define	APM_ENT_TYPE_FREEBSD_UFS	"FreeBSD-UFS"
#define	APM_ENT_TYPE_FREEBSD_VINUM	"FreeBSD-Vinum"
#define	APM_ENT_TYPE_FREEBSD_ZFS	"FreeBSD-ZFS"

#define	APM_ENT_TYPE_APPLE_BOOT		"Apple_Bootstrap"
#define	APM_ENT_TYPE_APPLE_HFS		"Apple_HFS"
#define	APM_ENT_TYPE_APPLE_UFS		"Apple_UNIX_SVR2"

#endif /* _SYS_DISK_APM_H_ */
