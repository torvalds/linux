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
 */

#ifndef	_SYS_FM_PROTOCOL_H
#define	_SYS_FM_PROTOCOL_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL
#include <sys/varargs.h>
#include <sys/nvpair.h>
#else
#include <libnvpair.h>
#include <stdarg.h>
#endif
#include <sys/processor.h>

/* FM common member names */
#define	FM_CLASS			"class"
#define	FM_VERSION			"version"

/* FM protocol category 1 class names */
#define	FM_EREPORT_CLASS		"ereport"
#define	FM_FAULT_CLASS			"fault"
#define	FM_DEFECT_CLASS			"defect"
#define	FM_RSRC_CLASS			"resource"
#define	FM_LIST_EVENT			"list"
#define	FM_IREPORT_CLASS		"ireport"
#define	FM_SYSEVENT_CLASS		"sysevent"

/* FM list.* event class values */
#define	FM_LIST_SUSPECT_CLASS		FM_LIST_EVENT ".suspect"
#define	FM_LIST_ISOLATED_CLASS		FM_LIST_EVENT ".isolated"
#define	FM_LIST_REPAIRED_CLASS		FM_LIST_EVENT ".repaired"
#define	FM_LIST_UPDATED_CLASS		FM_LIST_EVENT ".updated"
#define	FM_LIST_RESOLVED_CLASS		FM_LIST_EVENT ".resolved"

/* ereport class subcategory values */
#define	FM_ERROR_CPU			"cpu"
#define	FM_ERROR_IO			"io"

/* ereport version and payload member names */
#define	FM_EREPORT_VERS0		0
#define	FM_EREPORT_VERSION		FM_EREPORT_VERS0

/* ereport payload member names */
#define	FM_EREPORT_DETECTOR		"detector"
#define	FM_EREPORT_ENA			"ena"
#define	FM_EREPORT_TIME			"time"
#define	FM_EREPORT_EID			"eid"

/* list.* event payload member names */
#define	FM_LIST_EVENT_SIZE		"list-sz"

/* ireport.* event payload member names */
#define	FM_IREPORT_DETECTOR		"detector"
#define	FM_IREPORT_UUID			"uuid"
#define	FM_IREPORT_PRIORITY		"pri"
#define	FM_IREPORT_ATTRIBUTES		"attr"

/*
 * list.suspect, isolated, updated, repaired and resolved
 * versions/payload member names.
 */
#define	FM_SUSPECT_UUID			"uuid"
#define	FM_SUSPECT_DIAG_CODE		"code"
#define	FM_SUSPECT_DIAG_TIME		"diag-time"
#define	FM_SUSPECT_DE			"de"
#define	FM_SUSPECT_FAULT_LIST		"fault-list"
#define	FM_SUSPECT_FAULT_SZ		"fault-list-sz"
#define	FM_SUSPECT_FAULT_STATUS		"fault-status"
#define	FM_SUSPECT_INJECTED		"__injected"
#define	FM_SUSPECT_MESSAGE		"message"
#define	FM_SUSPECT_RETIRE		"retire"
#define	FM_SUSPECT_RESPONSE		"response"
#define	FM_SUSPECT_SEVERITY		"severity"

#define	FM_SUSPECT_VERS0		0
#define	FM_SUSPECT_VERSION		FM_SUSPECT_VERS0

#define	FM_SUSPECT_FAULTY		0x1
#define	FM_SUSPECT_UNUSABLE		0x2
#define	FM_SUSPECT_NOT_PRESENT		0x4
#define	FM_SUSPECT_DEGRADED		0x8
#define	FM_SUSPECT_REPAIRED		0x10
#define	FM_SUSPECT_REPLACED		0x20
#define	FM_SUSPECT_ACQUITTED		0x40

/* fault event versions and payload member names */
#define	FM_FAULT_VERS0			0
#define	FM_FAULT_VERSION		FM_FAULT_VERS0

#define	FM_FAULT_ASRU			"asru"
#define	FM_FAULT_FRU			"fru"
#define	FM_FAULT_FRU_LABEL		"fru-label"
#define	FM_FAULT_CERTAINTY		"certainty"
#define	FM_FAULT_RESOURCE		"resource"
#define	FM_FAULT_LOCATION		"location"

/* resource event versions and payload member names */
#define	FM_RSRC_VERS0			0
#define	FM_RSRC_VERSION			FM_RSRC_VERS0
#define	FM_RSRC_RESOURCE		"resource"

/* resource.fm.asru.* payload member names */
#define	FM_RSRC_ASRU_UUID		"uuid"
#define	FM_RSRC_ASRU_CODE		"code"
#define	FM_RSRC_ASRU_FAULTY		"faulty"
#define	FM_RSRC_ASRU_REPAIRED		"repaired"
#define	FM_RSRC_ASRU_REPLACED		"replaced"
#define	FM_RSRC_ASRU_ACQUITTED		"acquitted"
#define	FM_RSRC_ASRU_RESOLVED		"resolved"
#define	FM_RSRC_ASRU_UNUSABLE		"unusable"
#define	FM_RSRC_ASRU_EVENT		"event"

/* resource.fm.xprt.* versions and payload member names */
#define	FM_RSRC_XPRT_VERS0		0
#define	FM_RSRC_XPRT_VERSION		FM_RSRC_XPRT_VERS0
#define	FM_RSRC_XPRT_UUID		"uuid"
#define	FM_RSRC_XPRT_SUBCLASS		"subclass"
#define	FM_RSRC_XPRT_FAULT_STATUS	"fault-status"
#define	FM_RSRC_XPRT_FAULT_HAS_ASRU	"fault-has-asru"

/*
 * FM ENA Format Macros
 */
#define	ENA_FORMAT_MASK			0x3
#define	ENA_FORMAT(ena)			((ena) & ENA_FORMAT_MASK)

/* ENA format types */
#define	FM_ENA_FMT0			0
#define	FM_ENA_FMT1			1
#define	FM_ENA_FMT2			2

/* Format 1 */
#define	ENA_FMT1_GEN_MASK		0x00000000000003FCull
#define	ENA_FMT1_ID_MASK		0xFFFFFFFFFFFFFC00ull
#define	ENA_FMT1_CPUID_MASK		0x00000000000FFC00ull
#define	ENA_FMT1_TIME_MASK		0xFFFFFFFFFFF00000ull
#define	ENA_FMT1_GEN_SHFT		2
#define	ENA_FMT1_ID_SHFT		10
#define	ENA_FMT1_CPUID_SHFT		ENA_FMT1_ID_SHFT
#define	ENA_FMT1_TIME_SHFT		20

/* Format 2 */
#define	ENA_FMT2_GEN_MASK		0x00000000000003FCull
#define	ENA_FMT2_ID_MASK		0xFFFFFFFFFFFFFC00ull
#define	ENA_FMT2_TIME_MASK		ENA_FMT2_ID_MASK
#define	ENA_FMT2_GEN_SHFT		2
#define	ENA_FMT2_ID_SHFT		10
#define	ENA_FMT2_TIME_SHFT		ENA_FMT2_ID_SHFT

/* Common FMRI type names */
#define	FM_FMRI_AUTHORITY		"authority"
#define	FM_FMRI_SCHEME			"scheme"
#define	FM_FMRI_SVC_AUTHORITY		"svc-authority"
#define	FM_FMRI_FACILITY		"facility"

/* FMRI authority-type member names */
#define	FM_FMRI_AUTH_CHASSIS		"chassis-id"
#define	FM_FMRI_AUTH_PRODUCT_SN		"product-sn"
#define	FM_FMRI_AUTH_PRODUCT		"product-id"
#define	FM_FMRI_AUTH_DOMAIN		"domain-id"
#define	FM_FMRI_AUTH_SERVER		"server-id"
#define	FM_FMRI_AUTH_HOST		"host-id"

#define	FM_AUTH_VERS0			0
#define	FM_FMRI_AUTH_VERSION		FM_AUTH_VERS0

/* scheme name values */
#define	FM_FMRI_SCHEME_FMD		"fmd"
#define	FM_FMRI_SCHEME_DEV		"dev"
#define	FM_FMRI_SCHEME_HC		"hc"
#define	FM_FMRI_SCHEME_SVC		"svc"
#define	FM_FMRI_SCHEME_CPU		"cpu"
#define	FM_FMRI_SCHEME_MEM		"mem"
#define	FM_FMRI_SCHEME_MOD		"mod"
#define	FM_FMRI_SCHEME_PKG		"pkg"
#define	FM_FMRI_SCHEME_LEGACY		"legacy-hc"
#define	FM_FMRI_SCHEME_ZFS		"zfs"
#define	FM_FMRI_SCHEME_SW		"sw"

/* Scheme versions */
#define	FMD_SCHEME_VERSION0		0
#define	FM_FMD_SCHEME_VERSION		FMD_SCHEME_VERSION0
#define	DEV_SCHEME_VERSION0		0
#define	FM_DEV_SCHEME_VERSION		DEV_SCHEME_VERSION0
#define	FM_HC_VERS0			0
#define	FM_HC_SCHEME_VERSION		FM_HC_VERS0
#define	CPU_SCHEME_VERSION0		0
#define	CPU_SCHEME_VERSION1		1
#define	FM_CPU_SCHEME_VERSION		CPU_SCHEME_VERSION1
#define	MEM_SCHEME_VERSION0		0
#define	FM_MEM_SCHEME_VERSION		MEM_SCHEME_VERSION0
#define	MOD_SCHEME_VERSION0		0
#define	FM_MOD_SCHEME_VERSION		MOD_SCHEME_VERSION0
#define	PKG_SCHEME_VERSION0		0
#define	FM_PKG_SCHEME_VERSION		PKG_SCHEME_VERSION0
#define	LEGACY_SCHEME_VERSION0		0
#define	FM_LEGACY_SCHEME_VERSION	LEGACY_SCHEME_VERSION0
#define	SVC_SCHEME_VERSION0		0
#define	FM_SVC_SCHEME_VERSION		SVC_SCHEME_VERSION0
#define	ZFS_SCHEME_VERSION0		0
#define	FM_ZFS_SCHEME_VERSION		ZFS_SCHEME_VERSION0
#define	SW_SCHEME_VERSION0		0
#define	FM_SW_SCHEME_VERSION		SW_SCHEME_VERSION0

/* hc scheme member names */
#define	FM_FMRI_HC_SERIAL_ID		"serial"
#define	FM_FMRI_HC_PART			"part"
#define	FM_FMRI_HC_REVISION		"revision"
#define	FM_FMRI_HC_ROOT			"hc-root"
#define	FM_FMRI_HC_LIST_SZ		"hc-list-sz"
#define	FM_FMRI_HC_LIST			"hc-list"
#define	FM_FMRI_HC_SPECIFIC		"hc-specific"

/* facility member names */
#define	FM_FMRI_FACILITY_NAME		"facility-name"
#define	FM_FMRI_FACILITY_TYPE		"facility-type"

/* hc-list version and member names */
#define	FM_FMRI_HC_NAME			"hc-name"
#define	FM_FMRI_HC_ID			"hc-id"

#define	HC_LIST_VERSION0		0
#define	FM_HC_LIST_VERSION		HC_LIST_VERSION0

/* hc-specific member names */
#define	FM_FMRI_HC_SPECIFIC_OFFSET	"offset"
#define	FM_FMRI_HC_SPECIFIC_PHYSADDR	"physaddr"

/* fmd module scheme member names */
#define	FM_FMRI_FMD_NAME		"mod-name"
#define	FM_FMRI_FMD_VERSION		"mod-version"

/* dev scheme member names */
#define	FM_FMRI_DEV_ID			"devid"
#define	FM_FMRI_DEV_TGTPTLUN0		"target-port-l0id"
#define	FM_FMRI_DEV_PATH		"device-path"

/* pkg scheme member names */
#define	FM_FMRI_PKG_BASEDIR		"pkg-basedir"
#define	FM_FMRI_PKG_INST		"pkg-inst"
#define	FM_FMRI_PKG_VERSION		"pkg-version"

/* svc scheme member names */
#define	FM_FMRI_SVC_NAME		"svc-name"
#define	FM_FMRI_SVC_INSTANCE		"svc-instance"
#define	FM_FMRI_SVC_CONTRACT_ID		"svc-contract-id"

/* svc-authority member names */
#define	FM_FMRI_SVC_AUTH_SCOPE		"scope"
#define	FM_FMRI_SVC_AUTH_SYSTEM_FQN	"system-fqn"

/* cpu scheme member names */
#define	FM_FMRI_CPU_ID			"cpuid"
#define	FM_FMRI_CPU_SERIAL_ID		"serial"
#define	FM_FMRI_CPU_MASK		"cpumask"
#define	FM_FMRI_CPU_VID			"cpuvid"
#define	FM_FMRI_CPU_CPUFRU		"cpufru"
#define	FM_FMRI_CPU_CACHE_INDEX		"cacheindex"
#define	FM_FMRI_CPU_CACHE_WAY		"cacheway"
#define	FM_FMRI_CPU_CACHE_BIT		"cachebit"
#define	FM_FMRI_CPU_CACHE_TYPE		"cachetype"

#define	FM_FMRI_CPU_CACHE_TYPE_L2	0
#define	FM_FMRI_CPU_CACHE_TYPE_L3	1

/* legacy-hc scheme member names */
#define	FM_FMRI_LEGACY_HC		"component"
#define	FM_FMRI_LEGACY_HC_PREFIX	FM_FMRI_SCHEME_HC":///" \
    FM_FMRI_LEGACY_HC"="

/* mem scheme member names */
#define	FM_FMRI_MEM_UNUM		"unum"
#define	FM_FMRI_MEM_SERIAL_ID		"serial"
#define	FM_FMRI_MEM_PHYSADDR		"physaddr"
#define	FM_FMRI_MEM_MEMCONFIG		"memconfig"
#define	FM_FMRI_MEM_OFFSET		"offset"

/* mod scheme member names */
#define	FM_FMRI_MOD_PKG			"mod-pkg"
#define	FM_FMRI_MOD_NAME		"mod-name"
#define	FM_FMRI_MOD_ID			"mod-id"
#define	FM_FMRI_MOD_DESC		"mod-desc"

/* zfs scheme member names */
#define	FM_FMRI_ZFS_POOL		"pool"
#define	FM_FMRI_ZFS_VDEV		"vdev"

/* sw scheme member names - extra indentation for members of an nvlist */
#define	FM_FMRI_SW_OBJ			"object"
#define	FM_FMRI_SW_OBJ_PATH			"path"
#define	FM_FMRI_SW_OBJ_ROOT			"root"
#define	FM_FMRI_SW_OBJ_PKG			"pkg"
#define	FM_FMRI_SW_SITE			"site"
#define	FM_FMRI_SW_SITE_TOKEN			"token"
#define	FM_FMRI_SW_SITE_MODULE			"module"
#define	FM_FMRI_SW_SITE_FILE			"file"
#define	FM_FMRI_SW_SITE_LINE			"line"
#define	FM_FMRI_SW_SITE_FUNC			"func"
#define	FM_FMRI_SW_CTXT			"context"
#define	FM_FMRI_SW_CTXT_ORIGIN			"origin"
#define	FM_FMRI_SW_CTXT_EXECNAME		"execname"
#define	FM_FMRI_SW_CTXT_PID			"pid"
#define	FM_FMRI_SW_CTXT_ZONE			"zone"
#define	FM_FMRI_SW_CTXT_CTID			"ctid"
#define	FM_FMRI_SW_CTXT_STACK			"stack"
#define	FM_NVA_FREE		0	/* free allocator on nvlist_destroy */
#define	FM_NVA_RETAIN		1	/* keep allocator on nvlist_destroy */

extern nv_alloc_t *fm_nva_xcreate(char *, size_t);
extern void fm_nva_xdestroy(nv_alloc_t *);
extern nvlist_t *fm_nvlist_create(nv_alloc_t *);
extern void fm_nvlist_destroy(nvlist_t *, int);
extern void fm_ereport_set(nvlist_t *, int, const char *, uint64_t,
    const nvlist_t *, ...);
extern void fm_payload_set(nvlist_t *, ...);
extern int i_fm_payload_set(nvlist_t *, const char *, va_list);
extern void fm_fmri_hc_set(nvlist_t *, int, const nvlist_t *, nvlist_t *,
    int, ...);
extern void fm_fmri_dev_set(nvlist_t *, int, const nvlist_t *, const char *,
    const char *, const char *);
extern void fm_fmri_de_set(nvlist_t *, int, const nvlist_t *, const char *);
extern void fm_fmri_cpu_set(nvlist_t *, int, const nvlist_t *, uint32_t,
    uint8_t *, const char *);
extern void fm_fmri_mem_set(nvlist_t *, int, const nvlist_t *, const char *,
    const char *, uint64_t);
extern void fm_fmri_zfs_set(nvlist_t *, int, uint64_t, uint64_t);
extern void fm_fmri_hc_create(nvlist_t *, int, const nvlist_t *, nvlist_t *,
    nvlist_t *, int, ...);

extern uint64_t fm_ena_increment(uint64_t);
extern uint64_t fm_ena_generate(uint64_t, uchar_t);
extern uint64_t fm_ena_generate_cpu(uint64_t, processorid_t, uchar_t);
extern uint64_t fm_ena_generation_get(uint64_t);
extern uchar_t fm_ena_format_get(uint64_t);
extern uint64_t fm_ena_id_get(uint64_t);
extern uint64_t fm_ena_time_get(uint64_t);
extern void fm_erpt_dropped_increment(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_FM_PROTOCOL_H */
