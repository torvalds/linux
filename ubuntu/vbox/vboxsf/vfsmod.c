/** @file
 *
 * vboxsf -- VirtualBox Guest Additions for Linux:
 * Virtual File System for VirtualBox Shared Folders
 *
 * Module initialization/finalization
 * File system registration/deregistration
 * Superblock reading
 * Few utility functions
 */

/*
 * Copyright (C) 2006-2017 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/**
 * @note Anyone wishing to make changes here might wish to take a look at
 *  http://www.atnf.csiro.au/people/rgooch/linux/vfs.txt
 * which seems to be the closest there is to official documentation on
 * writing filesystem drivers for Linux.
 */

#include "vfsmod.h"
#include "version-generated.h"
#include "revision-generated.h"
#include "product-generated.h"
#include "VBoxGuestR0LibInternal.h"

MODULE_DESCRIPTION(VBOX_PRODUCT " VFS Module for Host File System Access");
MODULE_AUTHOR(VBOX_VENDOR);
MODULE_LICENSE("GPL");
#ifdef MODULE_ALIAS_FS
MODULE_ALIAS_FS("vboxsf");
#endif
#ifdef MODULE_VERSION
MODULE_VERSION(VBOX_VERSION_STRING " r" RT_XSTR(VBOX_SVN_REV));
#endif

/* globals */
VBGLSFCLIENT client_handle;

/* forward declarations */
static struct super_operations sf_super_ops;

/* allocate global info, try to map host share */
static int sf_glob_alloc(struct vbsf_mount_info_new *info, struct sf_glob_info **sf_gp)
{
    int err, rc;
    SHFLSTRING *str_name;
    size_t name_len, str_len;
    struct sf_glob_info *sf_g;

    TRACE();
    sf_g = kmalloc(sizeof(*sf_g), GFP_KERNEL);
    if (!sf_g)
    {
        err = -ENOMEM;
        LogRelFunc(("could not allocate memory for global info\n"));
        goto fail0;
    }

    RT_ZERO(*sf_g);

    if (   info->nullchar     != '\0'
        || info->signature[0] != VBSF_MOUNT_SIGNATURE_BYTE_0
        || info->signature[1] != VBSF_MOUNT_SIGNATURE_BYTE_1
        || info->signature[2] != VBSF_MOUNT_SIGNATURE_BYTE_2)
    {
        err = -EINVAL;
        goto fail1;
    }

    info->name[sizeof(info->name) - 1] = 0;
    info->nls_name[sizeof(info->nls_name) - 1] = 0;

    name_len = strlen(info->name);
    str_len = offsetof(SHFLSTRING, String.utf8) + name_len + 1;
    str_name = kmalloc(str_len, GFP_KERNEL);
    if (!str_name)
    {
        err = -ENOMEM;
        LogRelFunc(("could not allocate memory for host name\n"));
        goto fail1;
    }

    str_name->u16Length = name_len;
    str_name->u16Size = name_len + 1;
    memcpy(str_name->String.utf8, info->name, name_len + 1);

#define _IS_UTF8(_str) \
    (strcmp(_str, "utf8") == 0)
#define _IS_EMPTY(_str) \
    (strcmp(_str, "") == 0)

    /* Check if NLS charset is valid and not points to UTF8 table */
    if (info->nls_name[0])
    {
        if (_IS_UTF8(info->nls_name))
            sf_g->nls = NULL;
        else
        {
            sf_g->nls = load_nls(info->nls_name);
            if (!sf_g->nls)
            {
                err = -EINVAL;
                LogFunc(("failed to load nls %s\n", info->nls_name));
                kfree(str_name);
                goto fail1;
            }
        }
    }
    else
    {
#ifdef CONFIG_NLS_DEFAULT
        /* If no NLS charset specified, try to load the default
         * one if it's not points to UTF8. */
        if (!_IS_UTF8(CONFIG_NLS_DEFAULT) && !_IS_EMPTY(CONFIG_NLS_DEFAULT))
            sf_g->nls = load_nls_default();
        else
            sf_g->nls = NULL;
#else
        sf_g->nls = NULL;
#endif

#undef _IS_UTF8
#undef _IS_EMPTY
    }

    rc = VbglR0SfMapFolder(&client_handle, str_name, &sf_g->map);
    kfree(str_name);

    if (RT_FAILURE(rc))
    {
        err = -EPROTO;
        LogFunc(("VbglR0SfMapFolder failed rc=%d\n", rc));
        goto fail2;
    }

    sf_g->ttl = info->ttl;
    sf_g->uid = info->uid;
    sf_g->gid = info->gid;

    if ((unsigned)info->length >= sizeof(struct vbsf_mount_info_new))
    {
        /* new fields */
        sf_g->dmode = info->dmode;
        sf_g->fmode = info->fmode;
        sf_g->dmask = info->dmask;
        sf_g->fmask = info->fmask;
    }
    else
    {
        sf_g->dmode = ~0;
        sf_g->fmode = ~0;
    }

    *sf_gp = sf_g;
    return 0;

fail2:
    if (sf_g->nls)
        unload_nls(sf_g->nls);

fail1:
    kfree(sf_g);

fail0:
    return err;
}

/* unmap the share and free global info [sf_g] */
static void
sf_glob_free(struct sf_glob_info *sf_g)
{
    int rc;

    TRACE();
    rc = VbglR0SfUnmapFolder(&client_handle, &sf_g->map);
    if (RT_FAILURE(rc))
        LogFunc(("VbglR0SfUnmapFolder failed rc=%d\n", rc));

    if (sf_g->nls)
        unload_nls(sf_g->nls);

    kfree(sf_g);
}

/**
 * This is called (by sf_read_super_[24|26] when vfs mounts the fs and
 * wants to read super_block.
 *
 * calls [sf_glob_alloc] to map the folder and allocate global
 * information structure.
 *
 * initializes [sb], initializes root inode and dentry.
 *
 * should respect [flags]
 */
static int sf_read_super_aux(struct super_block *sb, void *data, int flags)
{
    int err;
    struct dentry *droot;
    struct inode *iroot;
    struct sf_inode_info *sf_i;
    struct sf_glob_info *sf_g;
    SHFLFSOBJINFO fsinfo;
    struct vbsf_mount_info_new *info;
    bool fInodePut = true;

    TRACE();
    if (!data)
    {
        LogFunc(("no mount info specified\n"));
        return -EINVAL;
    }

    info = data;

    if (flags & MS_REMOUNT)
    {
        LogFunc(("remounting is not supported\n"));
        return -ENOSYS;
    }

    err = sf_glob_alloc(info, &sf_g);
    if (err)
        goto fail0;

    sf_i = kmalloc(sizeof (*sf_i), GFP_KERNEL);
    if (!sf_i)
    {
        err = -ENOMEM;
        LogRelFunc(("could not allocate memory for root inode info\n"));
        goto fail1;
    }

    sf_i->handle = SHFL_HANDLE_NIL;
    sf_i->path = kmalloc(sizeof(SHFLSTRING) + 1, GFP_KERNEL);
    if (!sf_i->path)
    {
        err = -ENOMEM;
        LogRelFunc(("could not allocate memory for root inode path\n"));
        goto fail2;
    }

    sf_i->path->u16Length = 1;
    sf_i->path->u16Size = 2;
    sf_i->path->String.utf8[0] = '/';
    sf_i->path->String.utf8[1] = 0;
    sf_i->force_reread = 0;

    err = sf_stat(__func__, sf_g, sf_i->path, &fsinfo, 0);
    if (err)
    {
        LogFunc(("could not stat root of share\n"));
        goto fail3;
    }

    sb->s_magic = 0xface;
    sb->s_blocksize = 1024;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 3)
    /* Required for seek/sendfile.
     *
     * Must by less than or equal to INT64_MAX despite the fact that the
     * declaration of this variable is unsigned long long. See determination
     * of 'loff_t max' in fs/read_write.c / do_sendfile(). I don't know the
     * correct limit but MAX_LFS_FILESIZE (8TB-1 on 32-bit boxes) takes the
     * page cache into account and is the suggested limit. */
# if defined MAX_LFS_FILESIZE
    sb->s_maxbytes = MAX_LFS_FILESIZE;
# else
    sb->s_maxbytes = 0x7fffffffffffffffULL;
# endif
#endif
    sb->s_op = &sf_super_ops;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
    iroot = iget_locked(sb, 0);
#else
    iroot = iget(sb, 0);
#endif
    if (!iroot)
    {
        err = -ENOMEM;  /* XXX */
        LogFunc(("could not get root inode\n"));
        goto fail3;
    }

    if (sf_init_backing_dev(sf_g))
    {
        err = -EINVAL;
        LogFunc(("could not init bdi\n"));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
        unlock_new_inode(iroot);
#endif
        goto fail4;
    }

    sf_init_inode(sf_g, iroot, &fsinfo);
    SET_INODE_INFO(iroot, sf_i);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
    unlock_new_inode(iroot);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
    droot = d_make_root(iroot);
#else
    droot = d_alloc_root(iroot);
#endif
    if (!droot)
    {
        err = -ENOMEM;  /* XXX */
        LogFunc(("d_alloc_root failed\n"));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0)
        fInodePut = false;
#endif
        goto fail5;
    }

    sb->s_root = droot;
    SET_GLOB_INFO(sb, sf_g);
    return 0;

fail5:
    sf_done_backing_dev(sf_g);

fail4:
    if (fInodePut)
        iput(iroot);

fail3:
    kfree(sf_i->path);

fail2:
    kfree(sf_i);

fail1:
    sf_glob_free(sf_g);

fail0:
    return err;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
static struct super_block *
sf_read_super_24(struct super_block *sb, void *data, int flags)
{
    int err;

    TRACE();
    err = sf_read_super_aux(sb, data, flags);
    if (err)
        return NULL;

    return sb;
}
#endif

/* this is called when vfs is about to destroy the [inode]. all
   resources associated with this [inode] must be cleared here */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
static void sf_clear_inode(struct inode *inode)
{
    struct sf_inode_info *sf_i;

    TRACE();
    sf_i = GET_INODE_INFO(inode);
    if (!sf_i)
        return;

    BUG_ON(!sf_i->path);
    kfree(sf_i->path);
    kfree(sf_i);
    SET_INODE_INFO(inode, NULL);
}
#else
static void sf_evict_inode(struct inode *inode)
{
    struct sf_inode_info *sf_i;

    TRACE();
    truncate_inode_pages(&inode->i_data, 0);
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
    clear_inode(inode);
# else
    end_writeback(inode);
# endif

    sf_i = GET_INODE_INFO(inode);
    if (!sf_i)
        return;

    BUG_ON(!sf_i->path);
    kfree(sf_i->path);
    kfree(sf_i);
    SET_INODE_INFO(inode, NULL);
}
#endif

/* this is called by vfs when it wants to populate [inode] with data.
   the only thing that is known about inode at this point is its index
   hence we can't do anything here, and let lookup/whatever with the
   job to properly fill then [inode] */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25)
static void sf_read_inode(struct inode *inode)
{
}
#endif

/* vfs is done with [sb] (umount called) call [sf_glob_free] to unmap
   the folder and free [sf_g] */
static void sf_put_super(struct super_block *sb)
{
    struct sf_glob_info *sf_g;

    sf_g = GET_GLOB_INFO(sb);
    BUG_ON(!sf_g);
    sf_done_backing_dev(sf_g);
    sf_glob_free(sf_g);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18)
static int sf_statfs(struct super_block *sb, STRUCT_STATFS *stat)
{
    return sf_get_volume_info(sb, stat);
}
#else
static int sf_statfs(struct dentry *dentry, STRUCT_STATFS *stat)
{
    struct super_block *sb = dentry->d_inode->i_sb;
    return sf_get_volume_info(sb, stat);
}
#endif

static int sf_remount_fs(struct super_block *sb, int *flags, char *data)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 23)
    struct sf_glob_info *sf_g;
    struct sf_inode_info *sf_i;
    struct inode *iroot;
    SHFLFSOBJINFO fsinfo;
    int err;

    sf_g = GET_GLOB_INFO(sb);
    BUG_ON(!sf_g);
    if (data && data[0] != 0)
    {
        struct vbsf_mount_info_new *info =
            (struct vbsf_mount_info_new *)data;
        if (   info->signature[0] == VBSF_MOUNT_SIGNATURE_BYTE_0
            && info->signature[1] == VBSF_MOUNT_SIGNATURE_BYTE_1
            && info->signature[2] == VBSF_MOUNT_SIGNATURE_BYTE_2)
        {
            sf_g->uid = info->uid;
            sf_g->gid = info->gid;
            sf_g->ttl = info->ttl;
            sf_g->dmode = info->dmode;
            sf_g->fmode = info->fmode;
            sf_g->dmask = info->dmask;
            sf_g->fmask = info->fmask;
        }
    }

    iroot = ilookup(sb, 0);
    if (!iroot)
        return -ENOSYS;

    sf_i = GET_INODE_INFO(iroot);
    err = sf_stat(__func__, sf_g, sf_i->path, &fsinfo, 0);
    BUG_ON(err != 0);
    sf_init_inode(sf_g, iroot, &fsinfo);
    /*unlock_new_inode(iroot);*/
    return 0;
#else
    return -ENOSYS;
#endif
}

static struct super_operations sf_super_ops =
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
    .clear_inode = sf_clear_inode,
#else
    .evict_inode = sf_evict_inode,
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 25)
    .read_inode  = sf_read_inode,
#endif
    .put_super   = sf_put_super,
    .statfs      = sf_statfs,
    .remount_fs  = sf_remount_fs
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
static DECLARE_FSTYPE(vboxsf_fs_type, "vboxsf", sf_read_super_24, 0);
#else
static int
sf_read_super_26(struct super_block *sb, void *data, int flags)
{
    int err;

    TRACE();
    err = sf_read_super_aux(sb, data, flags);
    if (err)
        printk(KERN_DEBUG "sf_read_super_aux err=%d\n", err);

    return err;
}

# if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18)
static struct super_block *sf_get_sb(struct file_system_type *fs_type, int flags,
                                     const char *dev_name, void *data)
{
    TRACE();
    return get_sb_nodev(fs_type, flags, data, sf_read_super_26);
}
# elif LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
static int sf_get_sb(struct file_system_type *fs_type, int flags,
                     const char *dev_name, void *data, struct vfsmount *mnt)
{
    TRACE();
    return get_sb_nodev(fs_type, flags, data, sf_read_super_26, mnt);
}
# else
static struct dentry *sf_mount(struct file_system_type *fs_type, int flags,
                               const char *dev_name, void *data)
{
    TRACE();
    return mount_nodev(fs_type, flags, data, sf_read_super_26);
}
# endif

static struct file_system_type vboxsf_fs_type =
{
    .owner   = THIS_MODULE,
    .name    = "vboxsf",
# if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 39)
    .get_sb  = sf_get_sb,
# else
    .mount   = sf_mount,
# endif
    .kill_sb = kill_anon_super
};
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
static int follow_symlinks = 0;
module_param(follow_symlinks, int, 0);
MODULE_PARM_DESC(follow_symlinks, "Let host resolve symlinks rather than showing them");
#endif

/* Module initialization/finalization handlers */
static int __init init(void)
{
    int rcVBox;
    int rcRet = 0;
    int err;

    TRACE();

    if (sizeof(struct vbsf_mount_info_new) > PAGE_SIZE)
    {
        printk(KERN_ERR
                "Mount information structure is too large %lu\n"
                "Must be less than or equal to %lu\n",
                (unsigned long)sizeof (struct vbsf_mount_info_new),
                (unsigned long)PAGE_SIZE);
        return -EINVAL;
    }

    err = register_filesystem(&vboxsf_fs_type);
    if (err)
    {
        LogFunc(("register_filesystem err=%d\n", err));
        return err;
    }

    rcVBox = VbglR0HGCMInit();
    if (RT_FAILURE(rcVBox))
    {
        LogRelFunc(("VbglR0HGCMInit failed, rc=%d\n", rcVBox));
        rcRet = -EPROTO;
        goto fail0;
    }

    rcVBox = VbglR0SfConnect(&client_handle);
    if (RT_FAILURE(rcVBox))
    {
        LogRelFunc(("VbglR0SfConnect failed, rc=%d\n", rcVBox));
        rcRet = -EPROTO;
        goto fail1;
    }

    rcVBox = VbglR0SfSetUtf8(&client_handle);
    if (RT_FAILURE(rcVBox))
    {
        LogRelFunc(("VbglR0SfSetUtf8 failed, rc=%d\n", rcVBox));
        rcRet = -EPROTO;
        goto fail2;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
    if (!follow_symlinks)
    {
        rcVBox = VbglR0SfSetSymlinks(&client_handle);
        if (RT_FAILURE(rcVBox))
        {
            printk(KERN_WARNING
                     "vboxsf: Host unable to show symlinks, rc=%d\n",
                     rcVBox);
        }
    }
#endif

    printk(KERN_DEBUG
            "vboxsf: Successfully loaded version " VBOX_VERSION_STRING
            " (interface " RT_XSTR(VMMDEV_VERSION) ")\n");

    return 0;

fail2:
    VbglR0SfDisconnect(&client_handle);

fail1:
    VbglR0HGCMTerminate();

fail0:
    unregister_filesystem(&vboxsf_fs_type);
    return rcRet;
}

static void __exit fini(void)
{
    TRACE();

    VbglR0SfDisconnect(&client_handle);
    VbglR0HGCMTerminate();
    unregister_filesystem(&vboxsf_fs_type);
}

module_init(init);
module_exit(fini);

/* C++ hack */
int __gxx_personality_v0 = 0xdeadbeef;
