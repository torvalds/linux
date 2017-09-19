/** @file
 * vboxsf -- VirtualBox Guest Additions for Linux: mount(2) parameter structure.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef VBFS_MOUNT_H
#define VBFS_MOUNT_H

#define MAX_HOST_NAME  256
#define MAX_NLS_NAME    32

/* Linux constraints the size of data mount argument to PAGE_SIZE - 1. */
struct vbsf_mount_info_old
{
    char name[MAX_HOST_NAME];
    char nls_name[MAX_NLS_NAME];
    int  uid;
    int  gid;
    int  ttl;
};

#define VBSF_MOUNT_SIGNATURE_BYTE_0 '\377'
#define VBSF_MOUNT_SIGNATURE_BYTE_1 '\376'
#define VBSF_MOUNT_SIGNATURE_BYTE_2 '\375'

struct vbsf_mount_info_new
{
    char nullchar;              /* name cannot be '\0' -- we use this field
                                   to distinguish between the old structure
                                   and the new structure */
    char signature[3];          /* signature */
    int  length;                /* length of the whole structure */
    char name[MAX_HOST_NAME];   /* share name */
    char nls_name[MAX_NLS_NAME];/* name of an I/O charset */
    int  uid;                   /* user ID for all entries, default 0=root */
    int  gid;                   /* group ID for all entries, default 0=root */
    int  ttl;                   /* time to live */
    int  dmode;                 /* mode for directories if != 0xffffffff */
    int  fmode;                 /* mode for regular files if != 0xffffffff */
    int  dmask;                 /* umask applied to directories */
    int  fmask;                 /* umask applied to regular files */
};

struct vbsf_mount_opts
{
    int  uid;
    int  gid;
    int  ttl;
    int  dmode;
    int  fmode;
    int  dmask;
    int  fmask;
    int  ronly;
    int  sloppy;
    int  noexec;
    int  nodev;
    int  nosuid;
    int  remount;
    char nls_name[MAX_NLS_NAME];
    char *convertcp;
};

/** Completes the mount operation by adding the new mount point to mtab if required. */
int vbsfmount_complete(const char *host_name, const char *mount_point,
                       unsigned long flags, struct vbsf_mount_opts *opts);

#endif /* vbsfmount.h */
