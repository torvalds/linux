/* $Id: vbsfmount.h $ */
/** @file
 * vboxsf - VBox Linux Shared Folders VFS, mount(2) parameter structure.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef GA_INCLUDED_SRC_linux_sharedfolders_vbsfmount_h
#define GA_INCLUDED_SRC_linux_sharedfolders_vbsfmount_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Linux constraints the size of data mount argument to PAGE_SIZE - 1. */
#define MAX_HOST_NAME  256
#define MAX_NLS_NAME    32
#define VBSF_DEFAULT_TTL_MS     200

#define VBSF_MOUNT_SIGNATURE_BYTE_0 '\377'
#define VBSF_MOUNT_SIGNATURE_BYTE_1 '\376'
#define VBSF_MOUNT_SIGNATURE_BYTE_2 '\375'

/**
 * VBox Linux Shared Folders VFS caching mode.
 */
enum vbsf_cache_mode {
    /** Use the kernel modules default caching mode (kVbsfCacheMode_Strict). */
    kVbsfCacheMode_Default = 0,
    /** No caching, go to the host for everything.  This will have some minor
     *  coherency issues for memory mapping with unsynced dirty pages.  */
    kVbsfCacheMode_None,
    /** No caching, except for files with writable memory mappings.
     * (Note to future: if we do oplock like stuff, it goes in here.) */
    kVbsfCacheMode_Strict,
    /** Use page cache for reads.
     * This improves guest performance for read intensive jobs, like compiling
     * building.  The flip side is that the guest may not see host modification in a
     * timely manner and possibly update files with out-of-date cache information,
     * as there exists no protocol for the host to notify the guest about file
     * modifications. */
    kVbsfCacheMode_Read,
    /** Use page cache for both reads and writes as far as that's possible.
     * This is good for guest performance, but the price is that the guest possibly
     * ignoring host changes and the host not seeing guest changes in a timely
     * manner. */
    kVbsfCacheMode_ReadWrite,
    /** End of valid values (exclusive). */
    kVbsfCacheMode_End,
    /** Make sure the enum is sizeof(int32_t). */
    kVbsfCacheMode_32BitHack = 0x7fffffff
};

/**
 * VBox Linux Shared Folders VFS mount options.
 */
struct vbsf_mount_info_new {
    /**
     * The old version of the mount_info struct started with a
     * char name[MAX_HOST_NAME] field, where name cannot be '\0'.
     * So the new version of the mount_info struct starts with a
     * nullchar field which is always 0 so that we can detect and
     * reject the old structure being passed.
     */
    char                    nullchar;
    /** Signature */
    char                    signature[3];
    /** Length of the whole structure */
    int                     length;
    /** Share name */
    char                    name[MAX_HOST_NAME];
    /** Name of an I/O charset */
    char                    nls_name[MAX_NLS_NAME];
    /** User ID for all entries, default 0=root */
    int                     uid;
    /** Group ID for all entries, default 0=root */
    int                     gid;
    /** Directory entry and inode time to live in milliseconds.
     * -1 for kernel default, 0 to disable caching.
     * @sa vbsf_mount_info_new::msDirCacheTTL, vbsf_mount_info_new::msInodeTTL */
    int                     ttl;
    /** Mode for directories if != -1. */
    int                     dmode;
    /** Mode for regular files if != -1. */
    int                     fmode;
    /** umask applied to directories */
    int                     dmask;
    /** umask applied to regular files */
    int                     fmask;
    /** Mount tag for VBoxService automounter.
     * @since 6.0.0 */
    char                    szTag[32];
    /** Max pages to read & write at a time.
     * @since 6.0.6 */
    uint32_t                cMaxIoPages;
    /** The directory content buffer size.  Set to 0 for kernel module default.
     * Larger value reduces the number of host calls on large directories. */
    uint32_t                cbDirBuf;
    /** The time to live for directory entries (in milliseconds). @a ttl is used
     * if negative.
     * @since 6.0.6 */
    int32_t                 msDirCacheTTL;
    /** The time to live for inode information (in milliseconds). @a ttl is used
     * if negative.
     * @since 6.0.6 */
    int32_t                 msInodeTTL;
    /** The cache and coherency mode.
     * @since 6.0.6 */
    enum vbsf_cache_mode    enmCacheMode;
};
#ifdef AssertCompileSize
AssertCompileSize(struct vbsf_mount_info_new, 2*4 + MAX_HOST_NAME + MAX_NLS_NAME + 7*4 + 32 + 5*4);
#endif

/**
 * For use with the vbsfmount_complete() helper.
 */
struct vbsf_mount_opts {
    int                     ttl;
    int32_t                 msDirCacheTTL;
    int32_t                 msInodeTTL;
    uint32_t                cMaxIoPages;
    uint32_t                cbDirBuf;
    enum vbsf_cache_mode    enmCacheMode;
    int uid;
    int gid;
    int dmode;
    int fmode;
    int dmask;
    int fmask;
    int ronly;
    int sloppy;
    int noexec;
    int nodev;
    int nosuid;
    int remount;
    char nls_name[MAX_NLS_NAME];
    char *convertcp;
};

/** Completes the mount operation by adding the new mount point to mtab if required. */
int vbsfmount_complete(const char *host_name, const char *mount_point,
                       unsigned long flags, struct vbsf_mount_opts *opts);

#endif /* !GA_INCLUDED_SRC_linux_sharedfolders_vbsfmount_h */
