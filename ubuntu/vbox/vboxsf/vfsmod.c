/* $Id: vfsmod.c $ */
/** @file
 * vboxsf - VBox Linux Shared Folders VFS, module init/term, super block management.
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

/**
 * @note Anyone wishing to make changes here might wish to take a look at
 * https://github.com/torvalds/linux/blob/master/Documentation/filesystems/vfs.txt
 * which seems to be the closest there is to official documentation on
 * writing filesystem drivers for Linux.
 *
 * See also: http://us1.samba.org/samba/ftp/cifs-cvs/ols2006-fs-tutorial-smf.odp
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "vfsmod.h"
#include "version-generated.h"
#include "revision-generated.h"
#include "product-generated.h"
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
# include <uapi/linux/mount.h> /* for MS_REMOUNT */
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
# include <linux/mount.h>
#endif
#include <linux/seq_file.h>
#include <linux/vfs.h>
#include <VBox/err.h>
#include <iprt/path.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define VBSF_DEFAULT_MAX_IO_PAGES RT_MIN(_16K / sizeof(RTGCPHYS64) /* => 8MB buffer */, VMMDEV_MAX_HGCM_DATA_SIZE >> PAGE_SHIFT)
#define VBSF_DEFAULT_DIR_BUF_SIZE _64K


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
VBGLSFCLIENT g_SfClient;
uint32_t     g_fHostFeatures = 0;
/** Last valid shared folders function number. */
uint32_t     g_uSfLastFunction = SHFL_FN_SET_FILE_SIZE;
/** Shared folders features. */
uint64_t     g_fSfFeatures = 0;

/** Protects all the vbsf_inode_info::HandleList lists. */
spinlock_t   g_SfHandleLock;

/** The 'follow_symlinks' module parameter.
 * @todo Figure out how do this for 2.4.x! */
static int   g_fFollowSymlinks = 0;

/* forward declaration */
static struct super_operations g_vbsf_super_ops;



/**
 * Copies options from the mount info structure into @a pSuperInfo.
 *
 * This is used both by vbsf_super_info_alloc_and_map_it() and
 * vbsf_remount_fs().
 */
static void vbsf_super_info_copy_remount_options(struct vbsf_super_info *pSuperInfo, struct vbsf_mount_info_new *info)
{
    pSuperInfo->uid = info->uid;
    pSuperInfo->gid = info->gid;

    if ((unsigned)info->length >= RT_UOFFSETOF(struct vbsf_mount_info_new, szTag)) {
        /* new fields */
        pSuperInfo->dmode = info->dmode;
        pSuperInfo->fmode = info->fmode;
        pSuperInfo->dmask = info->dmask;
        pSuperInfo->fmask = info->fmask;
    } else {
        pSuperInfo->dmode = ~0;
        pSuperInfo->fmode = ~0;
    }

    if ((unsigned)info->length >= RT_UOFFSETOF(struct vbsf_mount_info_new, cMaxIoPages)) {
        AssertCompile(sizeof(pSuperInfo->szTag) >= sizeof(info->szTag));
        memcpy(pSuperInfo->szTag, info->szTag, sizeof(info->szTag));
        pSuperInfo->szTag[sizeof(pSuperInfo->szTag) - 1] = '\0';
    } else {
        pSuperInfo->szTag[0] = '\0';
    }

    /* The max number of pages in an I/O request.  This must take into
       account that the physical heap generally grows in 64 KB chunks,
       so we should not try push that limit.   It also needs to take
       into account that the host will allocate temporary heap buffers
       for the I/O bytes we send/receive, so don't push the host heap
       too hard as we'd have to retry with smaller requests when this
       happens, which isn't too efficient. */
    pSuperInfo->cMaxIoPages = VBSF_DEFAULT_MAX_IO_PAGES;
    if (   (unsigned)info->length >= sizeof(struct vbsf_mount_info_new)
        && info->cMaxIoPages > 0) {
        if (info->cMaxIoPages <= VMMDEV_MAX_HGCM_DATA_SIZE >> PAGE_SHIFT)
            pSuperInfo->cMaxIoPages = RT_MAX(info->cMaxIoPages, 2); /* read_iter/write_iter requires a minimum of 2. */
        else
            printk(KERN_WARNING "vboxsf: max I/O page count (%#x) is out of range, using default (%#x) instead.\n",
                   info->cMaxIoPages, pSuperInfo->cMaxIoPages);
    }

    pSuperInfo->cbDirBuf = VBSF_DEFAULT_DIR_BUF_SIZE;
    if (   (unsigned)info->length >= RT_UOFFSETOF(struct vbsf_mount_info_new, cbDirBuf)
        && info->cbDirBuf > 0) {
        if (info->cbDirBuf <= _16M)
            pSuperInfo->cbDirBuf = RT_ALIGN_32(info->cbDirBuf, PAGE_SIZE);
        else
            printk(KERN_WARNING "vboxsf: max directory buffer size (%#x) is out of range, using default (%#x) instead.\n",
                   info->cMaxIoPages, pSuperInfo->cMaxIoPages);
    }

    /*
     * TTLs.
     */
    pSuperInfo->msTTL = info->ttl;
    if (info->ttl > 0)
        pSuperInfo->cJiffiesDirCacheTTL = msecs_to_jiffies(info->ttl);
    else if (info->ttl == 0 || info->ttl != -1)
        pSuperInfo->cJiffiesDirCacheTTL = pSuperInfo->msTTL = 0;
    else
        pSuperInfo->cJiffiesDirCacheTTL = msecs_to_jiffies(VBSF_DEFAULT_TTL_MS);
    pSuperInfo->cJiffiesInodeTTL = pSuperInfo->cJiffiesDirCacheTTL;

    pSuperInfo->msDirCacheTTL = -1;
    if (   (unsigned)info->length >= RT_UOFFSETOF(struct vbsf_mount_info_new, msDirCacheTTL)
        && info->msDirCacheTTL >= 0) {
        if (info->msDirCacheTTL > 0) {
            pSuperInfo->msDirCacheTTL       = info->msDirCacheTTL;
            pSuperInfo->cJiffiesDirCacheTTL = msecs_to_jiffies(info->msDirCacheTTL);
        } else {
            pSuperInfo->msDirCacheTTL       = 0;
            pSuperInfo->cJiffiesDirCacheTTL = 0;
        }
    }

    pSuperInfo->msInodeTTL = -1;
    if (   (unsigned)info->length >= RT_UOFFSETOF(struct vbsf_mount_info_new, msInodeTTL)
        && info->msInodeTTL >= 0) {
        if (info->msInodeTTL > 0) {
            pSuperInfo->msInodeTTL       = info->msInodeTTL;
            pSuperInfo->cJiffiesInodeTTL = msecs_to_jiffies(info->msInodeTTL);
        } else {
            pSuperInfo->msInodeTTL       = 0;
            pSuperInfo->cJiffiesInodeTTL = 0;
        }
    }

    /*
     * Caching.
     */
    pSuperInfo->enmCacheMode = kVbsfCacheMode_Strict;
    if ((unsigned)info->length >= RT_UOFFSETOF(struct vbsf_mount_info_new, enmCacheMode)) {
        switch (info->enmCacheMode) {
            case kVbsfCacheMode_Default:
            case kVbsfCacheMode_Strict:
                break;
            case kVbsfCacheMode_None:
            case kVbsfCacheMode_Read:
            case kVbsfCacheMode_ReadWrite:
                pSuperInfo->enmCacheMode = info->enmCacheMode;
                break;
            default:
                printk(KERN_WARNING "vboxsf: cache mode (%#x) is out of range, using default instead.\n", info->enmCacheMode);
                break;
        }
    }
}

/**
 * Allocate the super info structure and try map the host share.
 */
static int vbsf_super_info_alloc_and_map_it(struct vbsf_mount_info_new *info, struct vbsf_super_info **sf_gp)
{
    int rc;
    SHFLSTRING *str_name;
    size_t name_len, str_len;
    struct vbsf_super_info *pSuperInfo;

    TRACE();

    /*
     * Validate info.
     */
    if (   info->nullchar != '\0'
        || info->signature[0] != VBSF_MOUNT_SIGNATURE_BYTE_0
        || info->signature[1] != VBSF_MOUNT_SIGNATURE_BYTE_1
        || info->signature[2] != VBSF_MOUNT_SIGNATURE_BYTE_2) {
        SFLOGRELBOTH(("vboxsf: Invalid info signature: %#x %#x %#x %#x!\n",
                      info->nullchar, info->signature[0], info->signature[1], info->signature[2]));
        return -EINVAL;
    }
    name_len = RTStrNLen(info->name, sizeof(info->name));
    if (name_len >= sizeof(info->name)) {
        SFLOGRELBOTH(("vboxsf: Specified shared folder name is not zero terminated!\n"));
        return -EINVAL;
    }
    if (RTStrNLen(info->nls_name, sizeof(info->nls_name)) >= sizeof(info->nls_name)) {
        SFLOGRELBOTH(("vboxsf: Specified nls name is not zero terminated!\n"));
        return -EINVAL;
    }

    /*
     * Allocate memory.
     */
    str_len    = offsetof(SHFLSTRING, String.utf8) + name_len + 1;
    str_name   = (PSHFLSTRING)kmalloc(str_len, GFP_KERNEL);
    pSuperInfo = (struct vbsf_super_info *)kmalloc(sizeof(*pSuperInfo), GFP_KERNEL);
    if (pSuperInfo && str_name) {
        RT_ZERO(*pSuperInfo);

        str_name->u16Length = name_len;
        str_name->u16Size = name_len + 1;
        memcpy(str_name->String.utf8, info->name, name_len + 1);

        /*
         * Init the NLS support, if needed.
         */
        rc = 0;
#define _IS_UTF8(_str)  (strcmp(_str, "utf8") == 0)
#define _IS_EMPTY(_str) (strcmp(_str, "") == 0)

        /* Check if NLS charset is valid and not points to UTF8 table */
        pSuperInfo->fNlsIsUtf8 = true;
        if (info->nls_name[0]) {
            if (_IS_UTF8(info->nls_name)) {
                SFLOGFLOW(("vbsf_super_info_alloc_and_map_it: nls=utf8\n"));
                pSuperInfo->nls = NULL;
            } else {
                pSuperInfo->fNlsIsUtf8 = false;
                pSuperInfo->nls = load_nls(info->nls_name);
                if (pSuperInfo->nls) {
                    SFLOGFLOW(("vbsf_super_info_alloc_and_map_it: nls=%s -> %p\n", info->nls_name, pSuperInfo->nls));
                } else {
                    SFLOGRELBOTH(("vboxsf: Failed to load nls '%s'!\n", info->nls_name));
                    rc = -EINVAL;
                }
            }
        } else {
#ifdef CONFIG_NLS_DEFAULT
            /* If no NLS charset specified, try to load the default
             * one if it's not points to UTF8. */
            if (!_IS_UTF8(CONFIG_NLS_DEFAULT)
                && !_IS_EMPTY(CONFIG_NLS_DEFAULT)) {
                pSuperInfo->fNlsIsUtf8 = false;
                pSuperInfo->nls = load_nls_default();
                SFLOGFLOW(("vbsf_super_info_alloc_and_map_it: CONFIG_NLS_DEFAULT=%s -> %p\n", CONFIG_NLS_DEFAULT, pSuperInfo->nls));
            } else {
                SFLOGFLOW(("vbsf_super_info_alloc_and_map_it: nls=utf8 (default %s)\n", CONFIG_NLS_DEFAULT));
                pSuperInfo->nls = NULL;
            }
#else
            SFLOGFLOW(("vbsf_super_info_alloc_and_map_it: nls=utf8 (no default)\n"));
            pSuperInfo->nls = NULL;
#endif
        }
#undef _IS_UTF8
#undef _IS_EMPTY
        if (rc == 0) {
            /*
             * Try mount it.
             */
            rc = VbglR0SfHostReqMapFolderWithContigSimple(str_name, virt_to_phys(str_name), RTPATH_DELIMITER,
                                                          true /*fCaseSensitive*/, &pSuperInfo->map.root);
            if (RT_SUCCESS(rc)) {
                kfree(str_name);

                /* The rest is shared with remount. */
                vbsf_super_info_copy_remount_options(pSuperInfo, info);

                *sf_gp = pSuperInfo;
                return 0;
            }

            /*
             * bail out:
             */
            if (rc == VERR_FILE_NOT_FOUND) {
                LogRel(("vboxsf: SHFL_FN_MAP_FOLDER failed for '%s': share not found\n", info->name));
                rc = -ENXIO;
            } else {
                LogRel(("vboxsf: SHFL_FN_MAP_FOLDER failed for '%s': %Rrc\n", info->name, rc));
                rc = -EPROTO;
            }
            if (pSuperInfo->nls)
                unload_nls(pSuperInfo->nls);
        }
    } else {
        SFLOGRELBOTH(("vboxsf: Could not allocate memory for super info!\n"));
        rc = -ENOMEM;
    }
    if (str_name)
        kfree(str_name);
    if (pSuperInfo)
        kfree(pSuperInfo);
    return rc;
}

/* unmap the share and free super info [pSuperInfo] */
static void vbsf_super_info_free(struct vbsf_super_info *pSuperInfo)
{
    int rc;

    TRACE();
    rc = VbglR0SfHostReqUnmapFolderSimple(pSuperInfo->map.root);
    if (RT_FAILURE(rc))
        LogFunc(("VbglR0SfHostReqUnmapFolderSimple failed rc=%Rrc\n", rc));

    if (pSuperInfo->nls)
        unload_nls(pSuperInfo->nls);

    kfree(pSuperInfo);
}


/**
 * Initialize backing device related matters.
 */
static int vbsf_init_backing_dev(struct super_block *sb, struct vbsf_super_info *pSuperInfo)
{
    int rc = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
    /* Each new shared folder map gets a new uint64_t identifier,
     * allocated in sequence.  We ASSUME the sequence will not wrap. */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
    static uint64_t s_u64Sequence = 0;
    uint64_t idSeqMine = ASMAtomicIncU64(&s_u64Sequence);
# endif
    struct backing_dev_info *bdi;

#  if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 12, 0)
    rc = super_setup_bdi_name(sb, "vboxsf-%llu", (unsigned long long)idSeqMine);
    if (!rc)
        bdi = sb->s_bdi;
    else
        return rc;
#  else
    bdi = &pSuperInfo->bdi;
#  endif

    bdi->ra_pages = 0;                      /* No readahead */

# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 12)
    bdi->capabilities = 0
#  ifdef BDI_CAP_MAP_DIRECT
                      | BDI_CAP_MAP_DIRECT  /* MAP_SHARED */
#  endif
#  ifdef BDI_CAP_MAP_COPY
                      | BDI_CAP_MAP_COPY    /* MAP_PRIVATE */
#  endif
#  ifdef BDI_CAP_READ_MAP
                      | BDI_CAP_READ_MAP    /* can be mapped for reading */
#  endif
#  ifdef BDI_CAP_WRITE_MAP
                      | BDI_CAP_WRITE_MAP   /* can be mapped for writing */
#  endif
#  ifdef BDI_CAP_EXEC_MAP
                      | BDI_CAP_EXEC_MAP    /* can be mapped for execution */
#  endif
#  ifdef BDI_CAP_STRICTLIMIT
#   if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0) /* Trouble with 3.16.x/debian8.  Process stops after dirty page throttling.
                                                       * Only tested successfully with 4.19.  Maybe skip altogether? */
                      | BDI_CAP_STRICTLIMIT;
#   endif
#  endif
              ;
#  ifdef BDI_CAP_STRICTLIMIT
    /* Smalles possible amount of dirty pages: %1 of RAM.  We set this to
       try reduce amount of data that's out of sync with the host side.
       Besides, writepages isn't implemented, so flushing is extremely slow.
       Note! Extremely slow linux 3.0.0 msync doesn't seem to be related to this setting. */
    bdi_set_max_ratio(bdi, 1);
#  endif
# endif /* >= 2.6.12 */

# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
    rc = bdi_init(&pSuperInfo->bdi);
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
    if (!rc)
        rc = bdi_register(&pSuperInfo->bdi, NULL, "vboxsf-%llu", (unsigned long long)idSeqMine);
#  endif /* >= 2.6.26 */
# endif  /* 4.11.0 > version >= 2.6.24 */
#endif   /* >= 2.6.0 */
    return rc;
}


/**
 * Undoes what vbsf_init_backing_dev did.
 */
static void vbsf_done_backing_dev(struct super_block *sb, struct vbsf_super_info *pSuperInfo)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) && LINUX_VERSION_CODE <= KERNEL_VERSION(3, 19, 0)
    bdi_destroy(&pSuperInfo->bdi);    /* includes bdi_unregister() */
#endif
}


/**
 * Creates the root inode and attaches it to the super block.
 *
 * @returns 0 on success, negative errno on failure.
 * @param   sb          The super block.
 * @param   pSuperInfo  Our super block info.
 */
static int vbsf_create_root_inode(struct super_block *sb, struct vbsf_super_info *pSuperInfo)
{
    SHFLFSOBJINFO fsinfo;
    int rc;

    /*
     * Allocate and initialize the memory for our inode info structure.
     */
    struct vbsf_inode_info *sf_i = kmalloc(sizeof(*sf_i), GFP_KERNEL);
    SHFLSTRING             *path = kmalloc(sizeof(SHFLSTRING) + 1, GFP_KERNEL);
    if (sf_i && path) {
        sf_i->handle = SHFL_HANDLE_NIL;
        sf_i->force_restat = false;
        RTListInit(&sf_i->HandleList);
#ifdef VBOX_STRICT
        sf_i->u32Magic = SF_INODE_INFO_MAGIC;
#endif
        sf_i->path = path;

        path->u16Length = 1;
        path->u16Size = 2;
        path->String.utf8[0] = '/';
        path->String.utf8[1] = 0;

        /*
         * Stat the root directory (for inode info).
         */
        rc = vbsf_stat(__func__, pSuperInfo, sf_i->path, &fsinfo, 0);
        if (rc == 0) {
            /*
             * Create the actual inode structure.
             * Note! ls -la does display '.' and '..' entries with st_ino == 0, so root is #1.
             */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
            struct inode *iroot = iget_locked(sb, 1);
#else
            struct inode *iroot = iget(sb, 1);
#endif
            if (iroot) {
                vbsf_init_inode(iroot, sf_i, &fsinfo, pSuperInfo);
                VBSF_SET_INODE_INFO(iroot, sf_i);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
                unlock_new_inode(iroot);
#endif

                /*
                 * Now make it a root inode.
                 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
                sb->s_root = d_make_root(iroot);
#else
                sb->s_root = d_alloc_root(iroot);
#endif
                if (sb->s_root) {

                    return 0;
                }

                SFLOGRELBOTH(("vboxsf: d_make_root failed!\n"));
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0) /* d_make_root calls iput */
                iput(iroot);
#endif
                /* iput() will call vbsf_evict_inode()/vbsf_clear_inode(). */
                sf_i = NULL;
                path = NULL;

                rc = -ENOMEM;
            } else {
                SFLOGRELBOTH(("vboxsf: failed to allocate root inode!\n"));
                rc = -ENOMEM;
            }
        } else
            SFLOGRELBOTH(("vboxsf: could not stat root of share: %d\n", rc));
    } else {
        SFLOGRELBOTH(("vboxsf: Could not allocate memory for root inode info!\n"));
        rc = -ENOMEM;
    }
    if (sf_i)
        kfree(sf_i);
    if (path)
        kfree(path);
    return rc;
}


/**
 * This is called by vbsf_read_super_24() and vbsf_read_super_26() when vfs mounts
 * the fs and wants to read super_block.
 *
 * Calls vbsf_super_info_alloc_and_map_it() to map the folder and allocate super
 * information structure.
 *
 * Initializes @a sb, initializes root inode and dentry.
 *
 * Should respect @a flags.
 */
static int vbsf_read_super_aux(struct super_block *sb, void *data, int flags)
{
    int rc;
    struct vbsf_super_info *pSuperInfo;

    TRACE();
    if (!data) {
        SFLOGRELBOTH(("vboxsf: No mount data. Is mount.vboxsf installed (typically in /sbin)?\n"));
        return -EINVAL;
    }

    if (flags & MS_REMOUNT) {
        SFLOGRELBOTH(("vboxsf: Remounting is not supported!\n"));
        return -ENOSYS;
    }

    /*
     * Create our super info structure and map the shared folder.
     */
    rc = vbsf_super_info_alloc_and_map_it((struct vbsf_mount_info_new *)data, &pSuperInfo);
    if (rc == 0) {
        /*
         * Initialize the super block structure (must be done before
         * root inode creation).
         */
        sb->s_magic     = 0xface;
        sb->s_blocksize = 1024;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 3)
        /* Required for seek/sendfile (see 'loff_t max' in fs/read_write.c / do_sendfile()). */
# if defined MAX_LFS_FILESIZE
        sb->s_maxbytes  = MAX_LFS_FILESIZE;
# elif BITS_PER_LONG == 32
        sb->s_maxbytes  = (loff_t)ULONG_MAX << PAGE_SHIFT;
# else
        sb->s_maxbytes  = INT64_MAX;
# endif
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
        sb->s_time_gran = 1; /* This might be a little optimistic for windows hosts, where it should be 100. */
#endif
        sb->s_op        = &g_vbsf_super_ops;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
        sb->s_d_op      = &vbsf_dentry_ops;
#endif

        /*
         * Initialize the backing device.  This is important for memory mapped
         * files among other things.
         */
        rc = vbsf_init_backing_dev(sb, pSuperInfo);
        if (rc == 0) {
            /*
             * Create the root inode and we're done.
             */
            rc = vbsf_create_root_inode(sb, pSuperInfo);
            if (rc == 0) {
                VBSF_SET_SUPER_INFO(sb, pSuperInfo);
                SFLOGFLOW(("vbsf_read_super_aux: returns successfully\n"));
                return 0;
            }
            vbsf_done_backing_dev(sb, pSuperInfo);
        } else
            SFLOGRELBOTH(("vboxsf: backing device information initialization failed: %d\n", rc));
        vbsf_super_info_free(pSuperInfo);
    }
    return rc;
}


/**
 * This is called when vfs is about to destroy the @a inode.
 *
 * We must free the inode info structure here.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
static void vbsf_evict_inode(struct inode *inode)
#else
static void vbsf_clear_inode(struct inode *inode)
#endif
{
    struct vbsf_inode_info *sf_i;

    TRACE();

    /*
     * Flush stuff.
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
    truncate_inode_pages(&inode->i_data, 0);
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
    clear_inode(inode);
# else
    end_writeback(inode);
# endif
#endif
    /*
     * Clean up our inode info.
     */
    sf_i = VBSF_GET_INODE_INFO(inode);
    if (sf_i) {
        VBSF_SET_INODE_INFO(inode, NULL);

        Assert(sf_i->u32Magic == SF_INODE_INFO_MAGIC);
        BUG_ON(!sf_i->path);
        kfree(sf_i->path);
        vbsf_handle_drop_chain(sf_i);
# ifdef VBOX_STRICT
        sf_i->u32Magic = SF_INODE_INFO_MAGIC_DEAD;
# endif
        kfree(sf_i);
    }
}


/* this is called by vfs when it wants to populate [inode] with data.
   the only thing that is known about inode at this point is its index
   hence we can't do anything here, and let lookup/whatever with the
   job to properly fill then [inode] */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25)
static void vbsf_read_inode(struct inode *inode)
{
}
#endif


/* vfs is done with [sb] (umount called) call [vbsf_super_info_free] to unmap
   the folder and free [pSuperInfo] */
static void vbsf_put_super(struct super_block *sb)
{
    struct vbsf_super_info *pSuperInfo;

    pSuperInfo = VBSF_GET_SUPER_INFO(sb);
    BUG_ON(!pSuperInfo);
    vbsf_done_backing_dev(sb, pSuperInfo);
    vbsf_super_info_free(pSuperInfo);
}


/**
 * Get file system statistics.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18)
static int vbsf_statfs(struct dentry *dentry, struct kstatfs *stat)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 73)
static int vbsf_statfs(struct super_block *sb, struct kstatfs *stat)
#else
static int vbsf_statfs(struct super_block *sb, struct statfs *stat)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18)
    struct super_block *sb = dentry->d_inode->i_sb;
#endif
    int rc;
    VBOXSFVOLINFOREQ *pReq = (VBOXSFVOLINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
    if (pReq) {
        SHFLVOLINFO            *pVolInfo   = &pReq->VolInfo;
        struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(sb);
        rc = VbglR0SfHostReqQueryVolInfo(pSuperInfo->map.root, pReq, SHFL_HANDLE_ROOT);
        if (RT_SUCCESS(rc)) {
            stat->f_type   = UINT32_C(0x786f4256); /* 'VBox' little endian */
            stat->f_bsize  = pVolInfo->ulBytesPerAllocationUnit;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 73)
            stat->f_frsize = pVolInfo->ulBytesPerAllocationUnit;
#endif
            stat->f_blocks = pVolInfo->ullTotalAllocationBytes
                           / pVolInfo->ulBytesPerAllocationUnit;
            stat->f_bfree  = pVolInfo->ullAvailableAllocationBytes
                           / pVolInfo->ulBytesPerAllocationUnit;
            stat->f_bavail = pVolInfo->ullAvailableAllocationBytes
                           / pVolInfo->ulBytesPerAllocationUnit;
            stat->f_files  = 1000;
            stat->f_ffree  = 1000000; /* don't return 0 here since the guest may think
                                       * that it is not possible to create any more files */
            stat->f_fsid.val[0] = 0;
            stat->f_fsid.val[1] = 0;
            stat->f_namelen = 255;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)
            stat->f_flags = 0; /* not valid */
#endif
            RT_ZERO(stat->f_spare);
            rc = 0;
        } else
            rc = -RTErrConvertToErrno(rc);
        VbglR0PhysHeapFree(pReq);
    } else
        rc = -ENOMEM;
    return rc;
}

static int vbsf_remount_fs(struct super_block *sb, int *flags, char *data)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 23)
    struct vbsf_super_info *pSuperInfo = pSuperInfo = VBSF_GET_SUPER_INFO(sb);
    struct vbsf_inode_info *sf_i;
    struct inode *iroot;
    SHFLFSOBJINFO fsinfo;
    int err;
    Assert(pSuperInfo);

    if (data && data[0] != 0) {
        struct vbsf_mount_info_new *info = (struct vbsf_mount_info_new *)data;
        if (   info->nullchar == '\0'
            && info->signature[0] == VBSF_MOUNT_SIGNATURE_BYTE_0
            && info->signature[1] == VBSF_MOUNT_SIGNATURE_BYTE_1
            && info->signature[2] == VBSF_MOUNT_SIGNATURE_BYTE_2) {
            vbsf_super_info_copy_remount_options(pSuperInfo, info);
        }
    }

    iroot = ilookup(sb, 0);
    if (!iroot)
        return -ENOSYS;

    sf_i = VBSF_GET_INODE_INFO(iroot);
    err = vbsf_stat(__func__, pSuperInfo, sf_i->path, &fsinfo, 0);
    BUG_ON(err != 0);
    vbsf_init_inode(iroot, sf_i, &fsinfo, pSuperInfo);
    /*unlock_new_inode(iroot); */
    return 0;
#else  /* LINUX_VERSION_CODE < 2.4.23 */
    return -ENOSYS;
#endif /* LINUX_VERSION_CODE < 2.4.23 */
}


/**
 * Show mount options.
 *
 * This is needed by the VBoxService automounter in order for it to pick up
 * the the 'szTag' option value it sets on its mount.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
static int vbsf_show_options(struct seq_file *m, struct vfsmount *mnt)
#else
static int vbsf_show_options(struct seq_file *m, struct dentry *root)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
    struct super_block     *sb         = mnt->mnt_sb;
#else
    struct super_block     *sb         = root->d_sb;
#endif
    struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(sb);
    if (pSuperInfo) {
        /* Performance related options: */
        if (pSuperInfo->msTTL != -1)
            seq_printf(m, ",ttl=%d", pSuperInfo->msTTL);
        if (pSuperInfo->msDirCacheTTL >= 0)
            seq_printf(m, ",dcachettl=%d", pSuperInfo->msDirCacheTTL);
        if (pSuperInfo->msInodeTTL >= 0)
            seq_printf(m, ",inodettl=%d", pSuperInfo->msInodeTTL);
        if (pSuperInfo->cMaxIoPages != VBSF_DEFAULT_MAX_IO_PAGES)
            seq_printf(m, ",maxiopages=%u", pSuperInfo->cMaxIoPages);
        if (pSuperInfo->cbDirBuf != VBSF_DEFAULT_DIR_BUF_SIZE)
            seq_printf(m, ",dirbuf=%u", pSuperInfo->cbDirBuf);
        switch (pSuperInfo->enmCacheMode) {
            default: AssertFailed();
            case kVbsfCacheMode_Strict:
                break;
            case kVbsfCacheMode_None:       seq_puts(m, ",cache=none"); break;
            case kVbsfCacheMode_Read:       seq_puts(m, ",cache=read"); break;
            case kVbsfCacheMode_ReadWrite:  seq_puts(m, ",cache=readwrite"); break;
        }

        /* Attributes and NLS: */
        seq_printf(m, ",iocharset=%s", pSuperInfo->nls ? pSuperInfo->nls->charset : "utf8");
        seq_printf(m, ",uid=%u,gid=%u", pSuperInfo->uid, pSuperInfo->gid);
        if (pSuperInfo->dmode != ~0)
            seq_printf(m, ",dmode=0%o", pSuperInfo->dmode);
        if (pSuperInfo->fmode != ~0)
            seq_printf(m, ",fmode=0%o", pSuperInfo->fmode);
        if (pSuperInfo->dmask != 0)
            seq_printf(m, ",dmask=0%o", pSuperInfo->dmask);
        if (pSuperInfo->fmask != 0)
            seq_printf(m, ",fmask=0%o", pSuperInfo->fmask);

        /* Misc: */
        if (pSuperInfo->szTag[0] != '\0') {
            seq_puts(m, ",tag=");
            seq_escape(m, pSuperInfo->szTag, " \t\n\\");
        }
    }
    return 0;
}


/**
 * Super block operations.
 */
static struct super_operations g_vbsf_super_ops = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
    .clear_inode  = vbsf_clear_inode,
#else
    .evict_inode  = vbsf_evict_inode,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25)
    .read_inode   = vbsf_read_inode,
#endif
    .put_super    = vbsf_put_super,
    .statfs       = vbsf_statfs,
    .remount_fs   = vbsf_remount_fs,
    .show_options = vbsf_show_options
};



/*********************************************************************************************************************************
*   File system type related stuff.                                                                                              *
*********************************************************************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 4)

static int vbsf_read_super_26(struct super_block *sb, void *data, int flags)
{
    int err;

    TRACE();
    err = vbsf_read_super_aux(sb, data, flags);
    if (err)
        printk(KERN_DEBUG "vbsf_read_super_aux err=%d\n", err);

    return err;
}

# if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18)
static struct super_block *vbsf_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    TRACE();
    return get_sb_nodev(fs_type, flags, data, vbsf_read_super_26);
}
# elif LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
static int vbsf_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
    TRACE();
    return get_sb_nodev(fs_type, flags, data, vbsf_read_super_26, mnt);
}
# else /* LINUX_VERSION_CODE >= 2.6.39 */
static struct dentry *sf_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    TRACE();
    return mount_nodev(fs_type, flags, data, vbsf_read_super_26);
}
# endif /* LINUX_VERSION_CODE >= 2.6.39 */

/**
 * File system registration structure.
 */
static struct file_system_type g_vboxsf_fs_type = {
    .owner = THIS_MODULE,
    .name = "vboxsf",
# if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
    .get_sb = vbsf_get_sb,
# else
    .mount = sf_mount,
# endif
    .kill_sb = kill_anon_super
};

#else  /* LINUX_VERSION_CODE < 2.5.4 */

static struct super_block *vbsf_read_super_24(struct super_block *sb, void *data, int flags)
{
    int err;

    TRACE();
    err = vbsf_read_super_aux(sb, data, flags);
    if (err) {
        printk(KERN_DEBUG "vbsf_read_super_aux err=%d\n", err);
        return NULL;
    }

    return sb;
}

static DECLARE_FSTYPE(g_vboxsf_fs_type, "vboxsf", vbsf_read_super_24, 0);

#endif /* LINUX_VERSION_CODE < 2.5.4 */



/*********************************************************************************************************************************
*   Module stuff                                                                                                                 *
*********************************************************************************************************************************/

/**
 * Called on module initialization.
 */
static int __init init(void)
{
    int rc;
    SFLOGFLOW(("vboxsf: init\n"));

    /*
     * Must be paranoid about the vbsf_mount_info_new size.
     */
    AssertCompile(sizeof(struct vbsf_mount_info_new) <= PAGE_SIZE);
    if (sizeof(struct vbsf_mount_info_new) > PAGE_SIZE) {
        printk(KERN_ERR
               "vboxsf: Mount information structure is too large %lu\n"
               "vboxsf: Must be less than or equal to %lu\n",
               (unsigned long)sizeof(struct vbsf_mount_info_new),
               (unsigned long)PAGE_SIZE);
        return -EINVAL;
    }

    /*
     * Initialize stuff.
     */
    spin_lock_init(&g_SfHandleLock);
    rc = VbglR0SfInit();
    if (RT_SUCCESS(rc)) {
        /*
         * Try connect to the shared folder HGCM service.
         * It is possible it is not there.
         */
        rc = VbglR0SfConnect(&g_SfClient);
        if (RT_SUCCESS(rc)) {
            /*
             * Query host HGCM features and afterwards (must be last) shared folder features.
             */
            rc = VbglR0QueryHostFeatures(&g_fHostFeatures);
            if (RT_FAILURE(rc))
            {
                LogRel(("vboxsf: VbglR0QueryHostFeatures failed: rc=%Rrc (ignored)\n", rc));
                g_fHostFeatures = 0;
            }
            VbglR0SfHostReqQueryFeaturesSimple(&g_fSfFeatures, &g_uSfLastFunction);
            LogRel(("vboxsf: g_fHostFeatures=%#x g_fSfFeatures=%#RX64 g_uSfLastFunction=%u\n",
                    g_fHostFeatures, g_fSfFeatures, g_uSfLastFunction));

            /*
             * Tell the shared folder service about our expectations:
             *      - UTF-8 strings (rather than UTF-16)
             *      - Wheter to return or follow (default) symbolic links.
             */
            rc = VbglR0SfHostReqSetUtf8Simple();
            if (RT_SUCCESS(rc)) {
                if (!g_fFollowSymlinks) {
                    rc = VbglR0SfHostReqSetSymlinksSimple();
                    if (RT_FAILURE(rc))
                        printk(KERN_WARNING "vboxsf: Host unable to enable showing symlinks, rc=%d\n", rc);
                }
                /*
                 * Now that we're ready for action, try register the
                 * file system with the kernel.
                 */
                rc = register_filesystem(&g_vboxsf_fs_type);
                if (rc == 0) {
                    printk(KERN_INFO "vboxsf: Successfully loaded version " VBOX_VERSION_STRING "\n");
                    return 0;
                }

                /*
                 * Failed. Bail out.
                 */
                LogRel(("vboxsf: register_filesystem failed: rc=%d\n", rc));
            } else  {
                LogRel(("vboxsf: VbglR0SfSetUtf8 failed, rc=%Rrc\n", rc));
                rc = -EPROTO;
            }
            VbglR0SfDisconnect(&g_SfClient);
        } else {
            LogRel(("vboxsf: VbglR0SfConnect failed, rc=%Rrc\n", rc));
            rc = rc == VERR_HGCM_SERVICE_NOT_FOUND ? -EHOSTDOWN : -ECONNREFUSED;
        }
        VbglR0SfTerm();
    } else {
        LogRel(("vboxsf: VbglR0SfInit failed, rc=%Rrc\n", rc));
        rc = -EPROTO;
    }
    return rc;
}


/**
 * Called on module finalization.
 */
static void __exit fini(void)
{
    SFLOGFLOW(("vboxsf: fini\n"));

    unregister_filesystem(&g_vboxsf_fs_type);
    VbglR0SfDisconnect(&g_SfClient);
    VbglR0SfTerm();
}


/*
 * Module parameters.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 52)
module_param_named(follow_symlinks, g_fFollowSymlinks, int, 0);
MODULE_PARM_DESC(follow_symlinks,
                 "Let host resolve symlinks rather than showing them");
#endif


/*
 * Module declaration related bits.
 */
module_init(init);
module_exit(fini);

MODULE_DESCRIPTION(VBOX_PRODUCT " VFS Module for Host File System Access");
MODULE_AUTHOR(VBOX_VENDOR);
MODULE_LICENSE("GPL and additional rights");
#ifdef MODULE_ALIAS_FS
MODULE_ALIAS_FS("vboxsf");
#endif
#ifdef MODULE_VERSION
MODULE_VERSION(VBOX_VERSION_STRING " r" RT_XSTR(VBOX_SVN_REV));
#endif

