/* $Id: vfsmod.h $ */
/** @file
 * vboxsf - Linux Shared Folders VFS, internal header.
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

#ifndef GA_INCLUDED_SRC_linux_sharedfolders_vfsmod_h
#define GA_INCLUDED_SRC_linux_sharedfolders_vfsmod_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#if 0 /* Enables strict checks. */
# define RT_STRICT
# define VBOX_STRICT
#endif

#define LOG_GROUP LOG_GROUP_SHARED_FOLDERS
#include "the-linux-kernel.h"
#include <iprt/list.h>
#include <iprt/asm.h>
#include <VBox/log.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
# include <linux/backing-dev.h>
#endif

#include <VBox/VBoxGuestLibSharedFolders.h>
#include <VBox/VBoxGuestLibSharedFoldersInline.h>
#include <iprt/asm.h>
#include "vbsfmount.h"


/*
 * Logging wrappers.
 */
#if 1
# define TRACE()                LogFunc(("tracepoint\n"))
# define SFLOG(aArgs)           Log(aArgs)
# define SFLOGFLOW(aArgs)       LogFlow(aArgs)
# define SFLOG2(aArgs)          Log2(aArgs)
# define SFLOG3(aArgs)          Log3(aArgs)
# define SFLOGRELBOTH(aArgs)    LogRel(aArgs)
# ifdef LOG_ENABLED
#  define SFLOG_ENABLED   1
# endif
#else
# define TRACE()                RTLogBackdoorPrintf("%s: tracepoint\n", __FUNCTION__)
# define SFLOG(aArgs)           RTLogBackdoorPrintf aArgs
# define SFLOGFLOW(aArgs)       RTLogBackdoorPrintf aArgs
# define SFLOG2(aArgs)          RTLogBackdoorPrintf aArgs
# define SFLOG3(aArgs)          RTLogBackdoorPrintf aArgs
# define SFLOG_ENABLED          1
# define SFLOGRELBOTH(aArgs)    do { RTLogBackdoorPrintf aArgs; printk aArgs; } while (0)
#endif


/*
 * inode compatibility glue.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)

DECLINLINE(loff_t) i_size_read(struct inode *pInode)
{
    AssertCompile(sizeof(loff_t) == sizeof(uint64_t));
    return ASMAtomicReadU64((uint64_t volatile *)&pInode->i_size);
}

DECLINLINE(void) i_size_write(struct inode *pInode, loff_t cbNew)
{
    AssertCompile(sizeof(pInode->i_size) == sizeof(uint64_t));
    ASMAtomicWriteU64((uint64_t volatile *)&pInode->i_size, cbNew);
}

#endif /* < 2.6.0 */

#if  LINUX_VERSION_CODE < KERNEL_VERSION(3, 2, 0) \
  && (!defined(RHEL_MAJOR) || RHEL_MAJOR != 6)
DECLINLINE(void) set_nlink(struct inode *pInode, unsigned int cLinks)
{
    pInode->i_nlink = cLinks;
}
#endif


/* global variables */
extern VBGLSFCLIENT                    g_SfClient;
extern spinlock_t                      g_SfHandleLock;
extern uint32_t                        g_uSfLastFunction;
extern uint64_t                        g_fSfFeatures;

extern struct inode_operations         vbsf_dir_iops;
extern struct inode_operations         vbsf_lnk_iops;
extern struct inode_operations         vbsf_reg_iops;
extern struct file_operations          vbsf_dir_fops;
extern struct file_operations          vbsf_reg_fops;
extern struct dentry_operations        vbsf_dentry_ops;
extern struct address_space_operations vbsf_reg_aops;


/**
 * VBox specific per-mount (shared folder) information.
 */
struct vbsf_super_info {
    VBGLSFMAP map;
    struct nls_table *nls;
    /** Set if the NLS table is UTF-8. */
    bool fNlsIsUtf8;
    int uid;
    int gid;
    int dmode;
    int fmode;
    int dmask;
    int fmask;
    /** Maximum number of pages to allow in an I/O buffer with the host.
     * This applies to read and write operations.  */
    uint32_t                cMaxIoPages;
    /** The default directory buffer size. */
    uint32_t                cbDirBuf;
    /** The time to live for directory entries in jiffies, zero if disabled. */
    uint32_t                cJiffiesDirCacheTTL;
    /** The time to live for inode information in jiffies, zero if disabled. */
    uint32_t                cJiffiesInodeTTL;
    /** The cache and coherency mode. */
    enum vbsf_cache_mode    enmCacheMode;
    /** Mount tag for VBoxService automounter.  @since 6.0 */
    char                    szTag[32];
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
    /** The backing device info structure. */
    struct backing_dev_info bdi;
#endif
    /** The mount option value for /proc/mounts. */
    int32_t                 msTTL;
    /** The time to live for directory entries in milliseconds, for /proc/mounts. */
    int32_t                 msDirCacheTTL;
    /** The time to live for inode information in milliseconds, for /proc/mounts. */
    int32_t                 msInodeTTL;
};

/* Following casts are here to prevent assignment of void * to
   pointers of arbitrary type */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
# define VBSF_GET_SUPER_INFO(sb)                ((struct vbsf_super_info *)(sb)->u.generic_sbp)
# define VBSF_SET_SUPER_INFO(sb, a_pSuperInfo)  do { (sb)->u.generic_sbp = a_pSuperInfo; } while (0)
#else
# define VBSF_GET_SUPER_INFO(sb)                ((struct vbsf_super_info *)(sb)->s_fs_info)
# define VBSF_SET_SUPER_INFO(sb, a_pSuperInfo)  do { (sb)->s_fs_info = a_pSuperInfo;} while (0)
#endif


/**
 * For associating inodes with host handles.
 *
 * This is necessary for address_space_operations::vbsf_writepage and allows
 * optimizing stat, lookups and other operations on open files and directories.
 */
struct vbsf_handle {
    /** List entry (head vbsf_inode_info::HandleList). */
    RTLISTNODE              Entry;
    /** Host file/whatever handle. */
    SHFLHANDLE              hHost;
    /** VBSF_HANDLE_F_XXX */
    uint32_t                fFlags;
    /** Reference counter.
     * Close the handle and free the structure when it reaches zero. */
    uint32_t volatile       cRefs;
#ifdef VBOX_STRICT
    /** For strictness checks. */
    struct vbsf_inode_info   *pInodeInfo;
#endif
};

/** @name VBSF_HANDLE_F_XXX - Handle summary flags (vbsf_handle::fFlags).
 * @{  */
#define VBSF_HANDLE_F_READ        UINT32_C(0x00000001)
#define VBSF_HANDLE_F_WRITE       UINT32_C(0x00000002)
#define VBSF_HANDLE_F_APPEND      UINT32_C(0x00000004)
#define VBSF_HANDLE_F_FILE        UINT32_C(0x00000010)
#define VBSF_HANDLE_F_DIR         UINT32_C(0x00000020)
#define VBSF_HANDLE_F_ON_LIST     UINT32_C(0x00000080)
#define VBSF_HANDLE_F_MAGIC_MASK  UINT32_C(0xffffff00)
#define VBSF_HANDLE_F_MAGIC       UINT32_C(0x75030700) /**< Maurice Ravel (1875-03-07). */
#define VBSF_HANDLE_F_MAGIC_DEAD  UINT32_C(0x19371228)
/** @} */


/**
 * VBox specific per-inode information.
 */
struct vbsf_inode_info {
    /** Which file */
    SHFLSTRING *path;
    /** Some information was changed, update data on next revalidate */
    bool force_restat;
    /** The timestamp (jiffies) where the inode info was last updated. */
    unsigned long           ts_up_to_date;
    /** The birth time. */
    RTTIMESPEC              BirthTime;

    /** @name Host modification detection stats.
     *  @{  */
    /** The raw modification time, for mapping invalidation purposes. */
    RTTIMESPEC              ModificationTime;
    /** Copy of ModificationTime from the last time we wrote to the the file. */
    RTTIMESPEC              ModificationTimeAtOurLastWrite;
    /** @} */

    /** handle valid if a file was created with vbsf_create_worker until it will
     * be opened with vbsf_reg_open()
     * @todo r=bird: figure this one out...  */
    SHFLHANDLE handle;

    /** List of open handles (struct vbsf_handle), protected by g_SfHandleLock. */
    RTLISTANCHOR            HandleList;
#ifdef VBOX_STRICT
    uint32_t                u32Magic;
# define SF_INODE_INFO_MAGIC            UINT32_C(0x18620822) /**< Claude Debussy */
# define SF_INODE_INFO_MAGIC_DEAD       UINT32_C(0x19180325)
#endif
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19) || defined(KERNEL_FC6)
/* FC6 kernel 2.6.18, vanilla kernel 2.6.19+ */
# define VBSF_GET_INODE_INFO(i)       ((struct vbsf_inode_info *) (i)->i_private)
# define VBSF_SET_INODE_INFO(i, sf_i) (i)->i_private = sf_i
#else
/* vanilla kernel up to 2.6.18 */
# define VBSF_GET_INODE_INFO(i)       ((struct vbsf_inode_info *) (i)->u.generic_ip)
# define VBSF_SET_INODE_INFO(i, sf_i) (i)->u.generic_ip = sf_i
#endif

extern void vbsf_init_inode(struct inode *inode, struct vbsf_inode_info *sf_i, PSHFLFSOBJINFO info,
                            struct vbsf_super_info *pSuperInfo);
extern void vbsf_update_inode(struct inode *pInode, struct vbsf_inode_info *pInodeInfo, PSHFLFSOBJINFO pObjInfo,
                              struct vbsf_super_info *pSuperInfo, bool fInodeLocked, unsigned fSetAttrs);
extern int  vbsf_inode_revalidate_worker(struct dentry *dentry, bool fForced, bool fInodeLocked);
extern int  vbsf_inode_revalidate_with_handle(struct dentry *dentry, SHFLHANDLE hHostFile, bool fForced, bool fInodeLocked);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 18)
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
extern int  vbsf_inode_getattr(const struct path *path, struct kstat *kstat, u32 request_mask, unsigned int query_flags);
# else
extern int  vbsf_inode_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *kstat);
# endif
#else  /* < 2.5.44 */
extern int  vbsf_inode_revalidate(struct dentry *dentry);
#endif /* < 2.5.44 */
extern int  vbsf_inode_setattr(struct dentry *dentry, struct iattr *iattr);


extern void              vbsf_handle_drop_chain(struct vbsf_inode_info *pInodeInfo);
extern struct vbsf_handle *vbsf_handle_find(struct vbsf_inode_info *pInodeInfo, uint32_t fFlagsSet, uint32_t fFlagsClear);
extern uint32_t          vbsf_handle_release_slow(struct vbsf_handle *pHandle, struct vbsf_super_info *pSuperInfo,
                                                  const char *pszCaller);
extern void              vbsf_handle_append(struct vbsf_inode_info *pInodeInfo, struct vbsf_handle *pHandle);

/**
 * Releases a handle.
 *
 * @returns New reference count.
 * @param   pHandle         The handle to release.
 * @param   pSuperInfo      The info structure for the shared folder associated
 *                          with the handle.
 * @param   pszCaller       The caller name (for logging failures).
 */
DECLINLINE(uint32_t) vbsf_handle_release(struct vbsf_handle *pHandle, struct vbsf_super_info *pSuperInfo, const char *pszCaller)
{
    uint32_t cRefs;

    Assert((pHandle->fFlags & VBSF_HANDLE_F_MAGIC_MASK) == VBSF_HANDLE_F_MAGIC);
    Assert(pHandle->pInodeInfo);
    Assert(pHandle->pInodeInfo && pHandle->pInodeInfo->u32Magic == SF_INODE_INFO_MAGIC);

    cRefs = ASMAtomicDecU32(&pHandle->cRefs);
    Assert(cRefs < _64M);
    if (cRefs)
        return cRefs;
    return vbsf_handle_release_slow(pHandle, pSuperInfo, pszCaller);
}


/**
 * VBox specific information for a regular file.
 */
struct vbsf_reg_info {
    /** Handle tracking structure.
     * @note Must be first!  */
    struct vbsf_handle  Handle;
};

uint32_t vbsf_linux_oflags_to_vbox(unsigned fLnxOpen, uint32_t *pfHandle, const char *pszCaller);


/**
 * VBox specific information for an open directory.
 */
struct vbsf_dir_info {
    /** Handle tracking structure.
     * @note Must be first!  */
    struct vbsf_handle  Handle;
    /** Semaphore protecting everything below. */
    struct semaphore    Lock;
    /** A magic number (VBSF_DIR_INFO_MAGIC). */
    uint32_t            u32Magic;
    /** Size of the buffer for directory entries. */
    uint32_t            cbBuf;
    /** Buffer for directory entries on the physical heap. */
    PSHFLDIRINFO        pBuf;
    /** Number of valid bytes in the buffer. */
    uint32_t            cbValid;
    /** Number of entries left in the buffer.   */
    uint32_t            cEntriesLeft;
    /** The position of the next entry.  Incremented by one for each entry. */
    loff_t              offPos;
    /** The next entry. */
    PSHFLDIRINFO        pEntry;
    /** Set if there are no more files.  */
    bool                fNoMoreFiles;
};

/** Magic number for vbsf_dir_info::u32Magic (Robert Anson Heinlein). */
#define VBSF_DIR_INFO_MAGIC         UINT32_C(0x19070707)
/** Value of vbsf_dir_info::u32Magic when freed. */
#define VBSF_DIR_INFO_MAGIC_DEAD    UINT32_C(0x19880508)


/**
 * Sets the update-jiffies value for a dentry.
 *
 * This is used together with vbsf_super_info::cJiffiesDirCacheTTL to reduce
 * re-validation of dentry structures while walking.
 *
 * This used to be living in d_time, but since 4.9.0 that seems to have become
 * unfashionable and d_fsdata is now used to for this purpose.  We do this all
 * the way back, since d_time seems only to have been used by the file system
 * specific code (at least going back to 2.4.0).
 */
DECLINLINE(void) vbsf_dentry_set_update_jiffies(struct dentry *pDirEntry, unsigned long uToSet)
{
    /*SFLOG3(("vbsf_dentry_set_update_jiffies: %p: %lx -> %#lx\n", pDirEntry, (unsigned long)pDirEntry->d_fsdata, uToSet));*/
    pDirEntry->d_fsdata = (void *)uToSet;
}

/**
 * Get the update-jiffies value for a dentry.
 */
DECLINLINE(unsigned long) vbsf_dentry_get_update_jiffies(struct dentry *pDirEntry)
{
    return (unsigned long)pDirEntry->d_fsdata;
}

/**
 * Invalidates the update TTL for the given directory entry so that it is
 * revalidate the next time it is used.
 * @param   pDirEntry   The directory entry cache entry to invalidate.
 */
DECLINLINE(void) vbsf_dentry_invalidate_ttl(struct dentry *pDirEntry)
{
    vbsf_dentry_set_update_jiffies(pDirEntry, jiffies - INT32_MAX / 2);
}

/**
 * Increase the time-to-live of @a pDirEntry and all ancestors.
 * @param   pDirEntry   The directory entry cache entry which ancestors
 *                      we should increase the TTL for.
 */
DECLINLINE(void) vbsf_dentry_chain_increase_ttl(struct dentry *pDirEntry)
{
#ifdef VBOX_STRICT
    struct super_block * const pSuper = pDirEntry->d_sb;
#endif
    unsigned long const        uToSet = jiffies;
    do {
        Assert(pDirEntry->d_sb == pSuper);
        vbsf_dentry_set_update_jiffies(pDirEntry, uToSet);
        pDirEntry = pDirEntry->d_parent;
    } while (!IS_ROOT(pDirEntry));
}

/**
 * Increase the time-to-live of all ancestors.
 * @param   pDirEntry   The directory entry cache entry which ancestors
 *                      we should increase the TTL for.
 */
DECLINLINE(void) vbsf_dentry_chain_increase_parent_ttl(struct dentry *pDirEntry)
{
    Assert(!pDirEntry->d_parent || pDirEntry->d_parent->d_sb == pDirEntry->d_sb);
    pDirEntry = pDirEntry->d_parent;
    if (pDirEntry)
        vbsf_dentry_chain_increase_ttl(pDirEntry);
}

/** Macro for getting the dentry for a struct file. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
# define VBSF_GET_F_DENTRY(f)   file_dentry(f)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 20)
# define VBSF_GET_F_DENTRY(f)   (f->f_path.dentry)
#else
# define VBSF_GET_F_DENTRY(f)   (f->f_dentry)
#endif

extern int  vbsf_stat(const char *caller, struct vbsf_super_info *pSuperInfo, SHFLSTRING * path, PSHFLFSOBJINFO result,
                      int ok_to_fail);
extern int  vbsf_path_from_dentry(struct vbsf_super_info *pSuperInfo, struct vbsf_inode_info *sf_i, struct dentry *dentry,
                                  SHFLSTRING ** result, const char *caller);
extern int  vbsf_nlscpy(struct vbsf_super_info *pSuperInfo, char *name, size_t name_bound_len,
                        const unsigned char *utf8_name, size_t utf8_len);
extern int  vbsf_nls_to_shflstring(struct vbsf_super_info *pSuperInfo, const char *pszNls, PSHFLSTRING *ppString);


/**
 * Converts Linux access permissions to VBox ones (mode & 0777).
 *
 * @note Currently identical.
 * @sa   sf_access_permissions_to_linux
 */
DECLINLINE(uint32_t) sf_access_permissions_to_vbox(int fAttr)
{
    /* Access bits should be the same: */
    AssertCompile(RTFS_UNIX_IRUSR == S_IRUSR);
    AssertCompile(RTFS_UNIX_IWUSR == S_IWUSR);
    AssertCompile(RTFS_UNIX_IXUSR == S_IXUSR);
    AssertCompile(RTFS_UNIX_IRGRP == S_IRGRP);
    AssertCompile(RTFS_UNIX_IWGRP == S_IWGRP);
    AssertCompile(RTFS_UNIX_IXGRP == S_IXGRP);
    AssertCompile(RTFS_UNIX_IROTH == S_IROTH);
    AssertCompile(RTFS_UNIX_IWOTH == S_IWOTH);
    AssertCompile(RTFS_UNIX_IXOTH == S_IXOTH);

    return fAttr & RTFS_UNIX_ALL_ACCESS_PERMS;
}

#endif /* !GA_INCLUDED_SRC_linux_sharedfolders_vfsmod_h */
