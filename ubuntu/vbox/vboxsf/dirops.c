/** @file
 *
 * vboxsf -- VirtualBox Guest Additions for Linux:
 * Directory inode and file operations
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

#include "vfsmod.h"

/**
 * Open a directory. Read the complete content into a buffer.
 *
 * @param inode     inode
 * @param file      file
 * @returns 0 on success, Linux error code otherwise
 */
static int sf_dir_open(struct inode *inode, struct file *file)
{
    int rc;
    int err;
    struct sf_glob_info *sf_g = GET_GLOB_INFO(inode->i_sb);
    struct sf_dir_info *sf_d;
    struct sf_inode_info *sf_i = GET_INODE_INFO(inode);
    SHFLCREATEPARMS params;

    TRACE();
    BUG_ON(!sf_g);
    BUG_ON(!sf_i);

    if (file->private_data)
    {
        LogFunc(("sf_dir_open() called on already opened directory '%s'\n",
                sf_i->path->String.utf8));
        return 0;
    }

    sf_d = sf_dir_info_alloc();
    if (!sf_d)
    {
        LogRelFunc(("could not allocate directory info for '%s'\n",
                    sf_i->path->String.utf8));
        return -ENOMEM;
    }

    RT_ZERO(params);
    params.Handle = SHFL_HANDLE_NIL;
    params.CreateFlags = 0
                       | SHFL_CF_DIRECTORY
                       | SHFL_CF_ACT_OPEN_IF_EXISTS
                       | SHFL_CF_ACT_FAIL_IF_NEW
                       | SHFL_CF_ACCESS_READ
                       ;

    LogFunc(("sf_dir_open(): calling VbglR0SfCreate, folder %s, flags %#x\n",
             sf_i->path->String.utf8, params.CreateFlags));
    rc = VbglR0SfCreate(&client_handle, &sf_g->map, sf_i->path, &params);
    if (RT_SUCCESS(rc))
    {
        if (params.Result == SHFL_FILE_EXISTS)
        {
            err = sf_dir_read_all(sf_g, sf_i, sf_d, params.Handle);
            if (!err)
                file->private_data = sf_d;
        }
        else
            err = -ENOENT;

        rc = VbglR0SfClose(&client_handle, &sf_g->map, params.Handle);
        if (RT_FAILURE(rc))
            LogFunc(("sf_dir_open(): VbglR0SfClose(%s) after err=%d failed rc=%Rrc\n",
                     sf_i->path->String.utf8, err, rc));
    }
    else
        err = -EPERM;

    if (err)
        sf_dir_info_free(sf_d);

    return err;
}


/**
 * This is called when reference count of [file] goes to zero. Notify
 * the host that it can free whatever is associated with this directory
 * and deallocate our own internal buffers
 *
 * @param inode     inode
 * @param file      file
 * returns 0 on success, Linux error code otherwise
 */
static int sf_dir_release(struct inode *inode, struct file *file)
{
    TRACE();

    if (file->private_data)
        sf_dir_info_free(file->private_data);

    return 0;
}

/**
 * Translate RTFMODE into DT_xxx (in conjunction to rtDirType())
 * @param fMode     file mode
 * returns d_type
 */
static int sf_get_d_type(RTFMODE fMode)
{
    int d_type;
    switch (fMode & RTFS_TYPE_MASK)
    {
        case RTFS_TYPE_FIFO:      d_type = DT_FIFO;    break;
        case RTFS_TYPE_DEV_CHAR:  d_type = DT_CHR;     break;
        case RTFS_TYPE_DIRECTORY: d_type = DT_DIR;     break;
        case RTFS_TYPE_DEV_BLOCK: d_type = DT_BLK;     break;
        case RTFS_TYPE_FILE:      d_type = DT_REG;     break;
        case RTFS_TYPE_SYMLINK:   d_type = DT_LNK;     break;
        case RTFS_TYPE_SOCKET:    d_type = DT_SOCK;    break;
        case RTFS_TYPE_WHITEOUT:  d_type = DT_WHT;     break;
        default:                  d_type = DT_UNKNOWN; break;
    }
    return d_type;
}

/**
 * Extract element ([dir]->f_pos) from the directory [dir] into [d_name].
 *
 * @returns 0 for success, 1 for end reached, Linux error code otherwise.
 */
static int sf_getdent(struct file *dir, char d_name[NAME_MAX], int *d_type)
{
    loff_t cur;
    struct sf_glob_info *sf_g;
    struct sf_dir_info *sf_d;
    struct sf_inode_info *sf_i;
    struct inode *inode;
    struct list_head *pos, *list;

    TRACE();

    inode = GET_F_DENTRY(dir)->d_inode;
    sf_i = GET_INODE_INFO(inode);
    sf_g = GET_GLOB_INFO(inode->i_sb);
    sf_d = dir->private_data;

    BUG_ON(!sf_g);
    BUG_ON(!sf_d);
    BUG_ON(!sf_i);

    if (sf_i->force_reread)
    {
        int rc;
        int err;
        SHFLCREATEPARMS params;

        RT_ZERO(params);
        params.Handle = SHFL_HANDLE_NIL;
        params.CreateFlags = 0
                           | SHFL_CF_DIRECTORY
                           | SHFL_CF_ACT_OPEN_IF_EXISTS
                           | SHFL_CF_ACT_FAIL_IF_NEW
                           | SHFL_CF_ACCESS_READ
                           ;

        LogFunc(("sf_getdent: calling VbglR0SfCreate, folder %s, flags %#x\n",
                  sf_i->path->String.utf8, params.CreateFlags));
        rc = VbglR0SfCreate(&client_handle, &sf_g->map, sf_i->path, &params);
        if (RT_FAILURE(rc))
        {
            LogFunc(("VbglR0SfCreate(%s) failed rc=%Rrc\n",
                        sf_i->path->String.utf8, rc));
            return -EPERM;
        }

        if (params.Result != SHFL_FILE_EXISTS)
        {
            LogFunc(("directory %s does not exist\n", sf_i->path->String.utf8));
            sf_dir_info_free(sf_d);
            return -ENOENT;
        }

        sf_dir_info_empty(sf_d);
        err = sf_dir_read_all(sf_g, sf_i, sf_d, params.Handle);
        rc = VbglR0SfClose(&client_handle, &sf_g->map, params.Handle);
        if (RT_FAILURE(rc))
            LogFunc(("VbglR0SfClose(%s) failed rc=%Rrc\n", sf_i->path->String.utf8, rc));
        if (err)
            return err;

        sf_i->force_reread = 0;
    }

    cur = 0;
    list = &sf_d->info_list;
    list_for_each(pos, list)
    {
        struct sf_dir_buf *b;
        SHFLDIRINFO *info;
        loff_t i;

        b = list_entry(pos, struct sf_dir_buf, head);
        if (dir->f_pos >= cur + b->cEntries)
        {
            cur += b->cEntries;
            continue;
        }

        for (i = 0, info = b->buf; i < dir->f_pos - cur; ++i)
        {
            size_t size;

            size = offsetof(SHFLDIRINFO, name.String) + info->name.u16Size;
            info = (SHFLDIRINFO *) ((uintptr_t) info + size);
        }

        *d_type = sf_get_d_type(info->Info.Attr.fMode);

        return sf_nlscpy(sf_g, d_name, NAME_MAX,
                         info->name.String.utf8, info->name.u16Length);
    }

    return 1;
}

/**
 * This is called when vfs wants to populate internal buffers with
 * directory [dir]s contents. [opaque] is an argument to the
 * [filldir]. [filldir] magically modifies it's argument - [opaque]
 * and takes following additional arguments (which i in turn get from
 * the host via sf_getdent):
 *
 * name : name of the entry (i must also supply it's length huh?)
 * type : type of the entry (FILE | DIR | etc) (i ellect to use DT_UNKNOWN)
 * pos : position/index of the entry
 * ino : inode number of the entry (i fake those)
 *
 * [dir] contains:
 * f_pos : cursor into the directory listing
 * private_data : mean of communication with the host side
 *
 * Extract elements from the directory listing (incrementing f_pos
 * along the way) and feed them to [filldir] until:
 *
 * a. there are no more entries (i.e. sf_getdent set done to 1)
 * b. failure to compute fake inode number
 * c. filldir returns an error (see comment on that)
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
static int sf_dir_iterate(struct file *dir, struct dir_context *ctx)
#else
static int sf_dir_read(struct file *dir, void *opaque, filldir_t filldir)
#endif
{
    TRACE();
    for (;;)
    {
        int err;
        ino_t fake_ino;
        loff_t sanity;
        char d_name[NAME_MAX];
        int d_type = DT_UNKNOWN;

        err = sf_getdent(dir, d_name, &d_type);
        switch (err)
        {
            case 1:
                return 0;

            case 0:
                break;

            case -1:
            default:
                /* skip erroneous entry and proceed */
                LogFunc(("sf_getdent error %d\n", err));
                dir->f_pos += 1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
                ctx->pos += 1;
#endif
                continue;
        }

        /* d_name now contains a valid entry name */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
        sanity = ctx->pos + 0xbeef;
#else
        sanity = dir->f_pos + 0xbeef;
#endif
        fake_ino = sanity;
        if (sanity - fake_ino)
        {
            LogRelFunc(("can not compute ino\n"));
            return -EINVAL;
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
        if (!dir_emit(ctx, d_name, strlen(d_name), fake_ino, d_type))
        {
            LogFunc(("dir_emit failed\n"));
            return 0;
        }
#else
        err = filldir(opaque, d_name, strlen(d_name), dir->f_pos, fake_ino, d_type);
        if (err)
        {
            LogFunc(("filldir returned error %d\n", err));
            /* Rely on the fact that filldir returns error
               only when it runs out of space in opaque */
            return 0;
        }
#endif

        dir->f_pos += 1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
        ctx->pos += 1;
#endif
    }

    BUG();
}

struct file_operations sf_dir_fops =
{
    .open    = sf_dir_open,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
    .iterate = sf_dir_iterate,
#else
    .readdir = sf_dir_read,
#endif
    .release = sf_dir_release,
    .read    = generic_read_dir
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
  , .llseek  = generic_file_llseek
#endif
};


/* iops */

/**
 * This is called when vfs failed to locate dentry in the cache. The
 * job of this function is to allocate inode and link it to dentry.
 * [dentry] contains the name to be looked in the [parent] directory.
 * Failure to locate the name is not a "hard" error, in this case NULL
 * inode is added to [dentry] and vfs should proceed trying to create
 * the entry via other means. NULL(or "positive" pointer) ought to be
 * returned in case of success and "negative" pointer on error
 */
static struct dentry *sf_lookup(struct inode *parent, struct dentry *dentry
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
                                , unsigned int flags
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
                                , struct nameidata *nd
#endif
                               )
{
    int err;
    struct sf_inode_info *sf_i, *sf_new_i;
    struct sf_glob_info *sf_g;
    SHFLSTRING *path;
    struct inode *inode;
    ino_t ino;
    SHFLFSOBJINFO fsinfo;

    TRACE();
    sf_g = GET_GLOB_INFO(parent->i_sb);
    sf_i = GET_INODE_INFO(parent);

    BUG_ON(!sf_g);
    BUG_ON(!sf_i);

    err = sf_path_from_dentry(__func__, sf_g, sf_i, dentry, &path);
    if (err)
        goto fail0;

    err = sf_stat(__func__, sf_g, path, &fsinfo, 1);
    if (err)
    {
        if (err == -ENOENT)
        {
            /* -ENOENT: add NULL inode to dentry so it later can be
               created via call to create/mkdir/open */
            kfree(path);
            inode = NULL;
        }
        else
            goto fail1;
    }
    else
    {
        sf_new_i = kmalloc(sizeof(*sf_new_i), GFP_KERNEL);
        if (!sf_new_i)
        {
            LogRelFunc(("could not allocate memory for new inode info\n"));
            err = -ENOMEM;
            goto fail1;
        }
        sf_new_i->handle = SHFL_HANDLE_NIL;
        sf_new_i->force_reread = 0;

        ino = iunique(parent->i_sb, 1);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
        inode = iget_locked(parent->i_sb, ino);
#else
        inode = iget(parent->i_sb, ino);
#endif
        if (!inode)
        {
            LogFunc(("iget failed\n"));
            err = -ENOMEM;          /* XXX: ??? */
            goto fail2;
        }

        SET_INODE_INFO(inode, sf_new_i);
        sf_init_inode(sf_g, inode, &fsinfo);
        sf_new_i->path = path;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
        unlock_new_inode(inode);
#endif
    }

    sf_i->force_restat = 0;
    dentry->d_time = jiffies;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
    d_set_d_op(dentry, &sf_dentry_ops);
#else
    dentry->d_op = &sf_dentry_ops;
#endif
    d_add(dentry, inode);
    return NULL;

fail2:
    kfree(sf_new_i);

fail1:
    kfree(path);

fail0:
    return ERR_PTR(err);
}

/**
 * This should allocate memory for sf_inode_info, compute a unique inode
 * number, get an inode from vfs, initialize inode info, instantiate
 * dentry.
 *
 * @param parent        inode entry of the directory
 * @param dentry        directory cache entry
 * @param path          path name
 * @param info          file information
 * @param handle        handle
 * @returns 0 on success, Linux error code otherwise
 */
static int sf_instantiate(struct inode *parent, struct dentry *dentry,
                          SHFLSTRING *path, PSHFLFSOBJINFO info, SHFLHANDLE handle)
{
    int err;
    ino_t ino;
    struct inode *inode;
    struct sf_inode_info *sf_new_i;
    struct sf_glob_info *sf_g = GET_GLOB_INFO(parent->i_sb);

    TRACE();
    BUG_ON(!sf_g);

    sf_new_i = kmalloc(sizeof(*sf_new_i), GFP_KERNEL);
    if (!sf_new_i)
    {
        LogRelFunc(("could not allocate inode info.\n"));
        err = -ENOMEM;
        goto fail0;
    }

    ino = iunique(parent->i_sb, 1);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
    inode = iget_locked(parent->i_sb, ino);
#else
    inode = iget(parent->i_sb, ino);
#endif
    if (!inode)
    {
        LogFunc(("iget failed\n"));
        err = -ENOMEM;
        goto fail1;
    }

    sf_init_inode(sf_g, inode, info);
    sf_new_i->path = path;
    SET_INODE_INFO(inode, sf_new_i);
    sf_new_i->force_restat = 1;
    sf_new_i->force_reread = 0;

    d_instantiate(dentry, inode);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
    unlock_new_inode(inode);
#endif

    /* Store this handle if we leave the handle open. */
    sf_new_i->handle = handle;
    return 0;

fail1:
    kfree(sf_new_i);

fail0:
    return err;

}

/**
 * Create a new regular file / directory.
 *
 * @param parent        inode of the directory
 * @param dentry        directory cache entry
 * @param mode          file mode
 * @param fDirectory    true if directory, false otherwise
 * @returns 0 on success, Linux error code otherwise
 */
static int sf_create_aux(struct inode *parent, struct dentry *dentry,
                         umode_t mode, int fDirectory)
{
    int rc, err;
    SHFLCREATEPARMS params;
    SHFLSTRING *path;
    struct sf_inode_info *sf_i = GET_INODE_INFO(parent);
    struct sf_glob_info *sf_g = GET_GLOB_INFO(parent->i_sb);

    TRACE();
    BUG_ON(!sf_i);
    BUG_ON(!sf_g);

    err = sf_path_from_dentry(__func__, sf_g, sf_i, dentry, &path);
    if (err)
        goto fail0;

    RT_ZERO(params);
    params.Handle = SHFL_HANDLE_NIL;
    params.CreateFlags = 0
                       | SHFL_CF_ACT_CREATE_IF_NEW
                       | SHFL_CF_ACT_FAIL_IF_EXISTS
                       | SHFL_CF_ACCESS_READWRITE
                       | (fDirectory ? SHFL_CF_DIRECTORY : 0)
                       ;
    params.Info.Attr.fMode = 0
                           | (fDirectory ? RTFS_TYPE_DIRECTORY : RTFS_TYPE_FILE)
                           | (mode & S_IRWXUGO)
                           ;
    params.Info.Attr.enmAdditional = RTFSOBJATTRADD_NOTHING;

    LogFunc(("sf_create_aux: calling VbglR0SfCreate, folder %s, flags %#x\n",
              path->String.utf8, params.CreateFlags));
    rc = VbglR0SfCreate(&client_handle, &sf_g->map, path, &params);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_WRITE_PROTECT)
        {
            err = -EROFS;
            goto fail1;
        }
        err = -EPROTO;
        LogFunc(("(%d): VbglR0SfCreate(%s) failed rc=%Rrc\n",
                    fDirectory, sf_i->path->String.utf8, rc));
        goto fail1;
    }

    if (params.Result != SHFL_FILE_CREATED)
    {
        err = -EPERM;
        LogFunc(("(%d): could not create file %s result=%d\n",
                    fDirectory, sf_i->path->String.utf8, params.Result));
        goto fail1;
    }

    err = sf_instantiate(parent, dentry, path, &params.Info,
                         fDirectory ? SHFL_HANDLE_NIL : params.Handle);
    if (err)
    {
        LogFunc(("(%d): could not instantiate dentry for %s err=%d\n",
                    fDirectory, sf_i->path->String.utf8, err));
        goto fail2;
    }

    /*
     * Don't close this handle right now. We assume that the same file is
     * opened with sf_reg_open() and later closed with sf_reg_close(). Save
     * the handle in between. Does not apply to directories. True?
     */
    if (fDirectory)
    {
        rc = VbglR0SfClose(&client_handle, &sf_g->map, params.Handle);
        if (RT_FAILURE(rc))
            LogFunc(("(%d): VbglR0SfClose failed rc=%Rrc\n", fDirectory, rc));
    }

    sf_i->force_restat = 1;
    return 0;

fail2:
    rc = VbglR0SfClose(&client_handle, &sf_g->map, params.Handle);
    if (RT_FAILURE(rc))
        LogFunc(("(%d): VbglR0SfClose failed rc=%Rrc\n", fDirectory, rc));

fail1:
    kfree(path);

fail0:
    return err;
}

/**
 * Create a new regular file.
 *
 * @param parent        inode of the directory
 * @param dentry        directory cache entry
 * @param mode          file mode
 * @param excl          Possible O_EXCL...
 * @returns 0 on success, Linux error code otherwise
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0) || defined(DOXYGEN_RUNNING)
static int sf_create(struct inode *parent, struct dentry *dentry, umode_t mode, bool excl)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
static int sf_create(struct inode *parent, struct dentry *dentry, umode_t mode, struct nameidata *nd)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
static int sf_create(struct inode *parent, struct dentry *dentry, int mode, struct nameidata *nd)
#else
static int sf_create(struct inode *parent, struct dentry *dentry, int mode)
#endif
{
    TRACE();
    return sf_create_aux(parent, dentry, mode, 0);
}

/**
 * Create a new directory.
 *
 * @param parent        inode of the directory
 * @param dentry        directory cache entry
 * @param mode          file mode
 * @returns 0 on success, Linux error code otherwise
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
static int sf_mkdir(struct inode *parent, struct dentry *dentry, umode_t mode)
#else
static int sf_mkdir(struct inode *parent, struct dentry *dentry, int mode)
#endif
{
    TRACE();
    return sf_create_aux(parent, dentry, mode, 1);
}

/**
 * Remove a regular file / directory.
 *
 * @param parent        inode of the directory
 * @param dentry        directory cache entry
 * @param fDirectory    true if directory, false otherwise
 * @returns 0 on success, Linux error code otherwise
 */
static int sf_unlink_aux(struct inode *parent, struct dentry *dentry, int fDirectory)
{
    int rc, err;
    struct sf_glob_info *sf_g = GET_GLOB_INFO(parent->i_sb);
    struct sf_inode_info *sf_i = GET_INODE_INFO(parent);
    SHFLSTRING *path;
    uint32_t fFlags;

    TRACE();
    BUG_ON(!sf_g);

    err = sf_path_from_dentry(__func__, sf_g, sf_i, dentry, &path);
    if (err)
        goto fail0;

    fFlags = fDirectory ? SHFL_REMOVE_DIR : SHFL_REMOVE_FILE;
    if (   dentry
        && dentry->d_inode
        && ((dentry->d_inode->i_mode & S_IFLNK) == S_IFLNK))
        fFlags |= SHFL_REMOVE_SYMLINK;
    rc = VbglR0SfRemove(&client_handle, &sf_g->map, path, fFlags);
    if (RT_FAILURE(rc))
    {
        LogFunc(("(%d): VbglR0SfRemove(%s) failed rc=%Rrc\n", fDirectory, path->String.utf8, rc));
        err = -RTErrConvertToErrno(rc);
        goto fail1;
    }

    /* directory access/change time changed */
    sf_i->force_restat = 1;
    /* directory content changed */
    sf_i->force_reread = 1;

    err = 0;

fail1:
    kfree(path);

fail0:
    return err;
}

/**
 * Remove a regular file.
 *
 * @param parent        inode of the directory
 * @param dentry        directory cache entry
 * @returns 0 on success, Linux error code otherwise
 */
static int sf_unlink(struct inode *parent, struct dentry *dentry)
{
    TRACE();
    return sf_unlink_aux(parent, dentry, 0);
}

/**
 * Remove a directory.
 *
 * @param parent        inode of the directory
 * @param dentry        directory cache entry
 * @returns 0 on success, Linux error code otherwise
 */
static int sf_rmdir(struct inode *parent, struct dentry *dentry)
{
    TRACE();
    return sf_unlink_aux(parent, dentry, 1);
}

/**
 * Rename a regular file / directory.
 *
 * @param old_parent    inode of the old parent directory
 * @param old_dentry    old directory cache entry
 * @param new_parent    inode of the new parent directory
 * @param new_dentry    new directory cache entry
 * @param flags         flags
 * @returns 0 on success, Linux error code otherwise
 */
static int sf_rename(struct inode *old_parent, struct dentry *old_dentry,
                     struct inode *new_parent, struct dentry *new_dentry
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
                     , unsigned flags
#endif
                     )
{
    int err = 0, rc = VINF_SUCCESS;
    struct sf_glob_info *sf_g = GET_GLOB_INFO(old_parent->i_sb);

    TRACE();

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
    if (flags)
    {
        LogFunc(("rename with flags=%x\n", flags));
        return -EINVAL;
    }
#endif

    if (sf_g != GET_GLOB_INFO(new_parent->i_sb))
    {
        LogFunc(("rename with different roots\n"));
        err = -EINVAL;
    }
    else
    {
        struct sf_inode_info *sf_old_i = GET_INODE_INFO(old_parent);
        struct sf_inode_info *sf_new_i = GET_INODE_INFO(new_parent);
        /* As we save the relative path inside the inode structure, we need to change
           this if the rename is successful. */
        struct sf_inode_info *sf_file_i = GET_INODE_INFO(old_dentry->d_inode);
        SHFLSTRING *old_path;
        SHFLSTRING *new_path;

        BUG_ON(!sf_old_i);
        BUG_ON(!sf_new_i);
        BUG_ON(!sf_file_i);

        old_path = sf_file_i->path;
        err = sf_path_from_dentry(__func__, sf_g, sf_new_i,
                                  new_dentry, &new_path);
        if (err)
            LogFunc(("failed to create new path\n"));
        else
        {
            int fDir = ((old_dentry->d_inode->i_mode & S_IFDIR) != 0);

            rc = VbglR0SfRename(&client_handle, &sf_g->map, old_path,
                                new_path, fDir ? 0 : SHFL_RENAME_FILE | SHFL_RENAME_REPLACE_IF_EXISTS);
            if (RT_SUCCESS(rc))
            {
                kfree(old_path);
                sf_new_i->force_restat = 1;
                sf_old_i->force_restat = 1; /* XXX: needed? */
                /* Set the new relative path in the inode. */
                sf_file_i->path = new_path;
            }
            else
            {
                LogFunc(("VbglR0SfRename failed rc=%Rrc\n", rc));
                err = -RTErrConvertToErrno(rc);
                kfree(new_path);
            }
        }
    }
    return err;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
static int sf_symlink(struct inode *parent, struct dentry *dentry, const char *symname)
{
    int err;
    int rc;
    struct sf_inode_info *sf_i;
    struct sf_glob_info *sf_g;
    SHFLSTRING *path, *ssymname;
    SHFLFSOBJINFO info;
    int symname_len = strlen(symname) + 1;

    TRACE();
    sf_g = GET_GLOB_INFO(parent->i_sb);
    sf_i = GET_INODE_INFO(parent);

    BUG_ON(!sf_g);
    BUG_ON(!sf_i);

    err = sf_path_from_dentry(__func__, sf_g, sf_i, dentry, &path);
    if (err)
        goto fail0;

    ssymname = kmalloc(offsetof(SHFLSTRING, String.utf8) + symname_len, GFP_KERNEL);
    if (!ssymname)
    {
        LogRelFunc(("kmalloc failed, caller=sf_symlink\n"));
        err = -ENOMEM;
        goto fail1;
    }

    ssymname->u16Length = symname_len - 1;
    ssymname->u16Size = symname_len;
    memcpy(ssymname->String.utf8, symname, symname_len);

    rc = VbglR0SfSymlink(&client_handle, &sf_g->map, path, ssymname, &info);
    kfree(ssymname);

    if (RT_FAILURE(rc))
    {
        if (rc == VERR_WRITE_PROTECT)
        {
            err = -EROFS;
            goto fail1;
        }
        LogFunc(("VbglR0SfSymlink(%s) failed rc=%Rrc\n",
                    sf_i->path->String.utf8, rc));
        err = -EPROTO;
        goto fail1;
    }

    err = sf_instantiate(parent, dentry, path, &info, SHFL_HANDLE_NIL);
    if (err)
    {
        LogFunc(("could not instantiate dentry for %s err=%d\n",
                 sf_i->path->String.utf8, err));
        goto fail1;
    }

    sf_i->force_restat = 1;
    return 0;

fail1:
    kfree(path);
fail0:
    return err;
}
#endif

struct inode_operations sf_dir_iops =
{
    .lookup     = sf_lookup,
    .create     = sf_create,
    .mkdir      = sf_mkdir,
    .rmdir      = sf_rmdir,
    .unlink     = sf_unlink,
    .rename     = sf_rename,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
    .revalidate = sf_inode_revalidate
#else
    .getattr    = sf_getattr,
    .setattr    = sf_setattr,
    .symlink    = sf_symlink
#endif
};
