/******************************************************************************
 * version.h
 *
 * Xen version, type, and compile information.
 *
 * Copyright (c) 2005, Nguyen Anh Quynh <aquynh@gmail.com>
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 */

#ifndef __XEN_PUBLIC_VERSION_H__
#define __XEN_PUBLIC_VERSION_H__

/* NB. All ops return zero on success, except XENVER_version. */

/* arg == NULL; returns major:minor (16:16). */
#define XENVER_version      0

/* arg == xen_extraversion_t. */
#define XENVER_extraversion 1
struct xen_extraversion {
    char extraversion[16];
};
#define XEN_EXTRAVERSION_LEN (sizeof(struct xen_extraversion))

/* arg == xen_compile_info_t. */
#define XENVER_compile_info 2
struct xen_compile_info {
    char compiler[64];
    char compile_by[16];
    char compile_domain[32];
    char compile_date[32];
};

#define XENVER_capabilities 3
struct xen_capabilities_info {
    char info[1024];
};
#define XEN_CAPABILITIES_INFO_LEN (sizeof(struct xen_capabilities_info))

#define XENVER_changeset 4
struct xen_changeset_info {
    char info[64];
};
#define XEN_CHANGESET_INFO_LEN (sizeof(struct xen_changeset_info))

#define XENVER_platform_parameters 5
struct xen_platform_parameters {
    xen_ulong_t virt_start;
};

#define XENVER_get_features 6
struct xen_feature_info {
    unsigned int submap_idx;    /* IN: which 32-bit submap to return */
    uint32_t     submap;        /* OUT: 32-bit submap */
};

/* Declares the features reported by XENVER_get_features. */
#include <xen/interface/features.h>

/* arg == NULL; returns host memory page size. */
#define XENVER_pagesize 7

/* arg == xen_domain_handle_t. */
#define XENVER_guest_handle 8

#endif /* __XEN_PUBLIC_VERSION_H__ */
