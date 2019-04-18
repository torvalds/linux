/* $Id: lnkops.c $ */
/** @file
 * vboxsf - VBox Linux Shared Folders VFS, operations for symbolic links.
 */

/*
 * Copyright (C) 2010-2019 Oracle Corporation
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "vfsmod.h"


/**
 * Converts error codes as best we can.
 */
DECLINLINE(int) vbsf_convert_symlink_error(int vrc)
{
    if (   vrc == VERR_IS_A_DIRECTORY
        || vrc == VERR_IS_A_FIFO
        || vrc == VERR_IS_A_FILE
        || vrc == VERR_IS_A_BLOCK_DEVICE
        || vrc == VERR_IS_A_CHAR_DEVICE
        || vrc == VERR_IS_A_SOCKET
        || vrc == VERR_NOT_SYMLINK)
        return -EINVAL;
    if (vrc == VERR_PATH_NOT_FOUND)
        return -ENOTDIR;
    if (vrc == VERR_FILE_NOT_FOUND)
        return -ENOENT;
    return -EPROTO;
}


/**
 * Does the NLS conversion of the symlink target.
 */
static int vbsf_symlink_nls_convert_slow(struct vbsf_super_info *pSuperInfo, char *pszTarget, size_t cbTargetBuf)
{
    int          rc;
    size_t const cchUtf8 = RTStrNLen(pszTarget, cbTargetBuf);
    if (cchUtf8 < cbTargetBuf) {
        /*
         * If the target is short and there is a lot of space left in the target
         * buffer (typically PAGE_SIZE in size), we move the  input to the end
         * instead of allocating a temporary buffer for it.  This works because
         * there shouldn't be anything that is more than 8x worse than UTF-8
         * when it comes to efficiency.
         */
        char  *pszFree = NULL;
        char  *pszUtf8;
        if (cchUtf8 - 1 <= cbTargetBuf / 8) {
            pszUtf8 = &pszTarget[cbTargetBuf - cchUtf8 - 1];
            cbTargetBuf -= cchUtf8 - 1;
        } else {
            pszFree = pszUtf8 = kmalloc(cchUtf8 + 1, GFP_KERNEL);
            if (RT_UNLIKELY(!pszUtf8)) {
                LogRelMax(50, ("vbsf_symlink_nls_convert_slow: failed to allocate %u bytes\n", cchUtf8 + 1));
                return -ENOMEM;
            }
        }
        memcpy(pszUtf8, pszTarget, cchUtf8);
        pszUtf8[cchUtf8] = '\0';

        rc = vbsf_nlscpy(pSuperInfo, pszTarget, cbTargetBuf, pszUtf8, cchUtf8);
        if (pszFree)
            kfree(pszFree);
    } else {
        SFLOGFLOW(("vbsf_symlink_nls_convert_slow: Impossible! Unterminated target!\n"));
        rc = -ENAMETOOLONG;
    }
    return rc;
}


/**
 * Does NLS conversion if needed.
 */
DECLINLINE(int) vbsf_symlink_nls_convert(struct vbsf_super_info *pSuperInfo, char *pszTarget, size_t cbTargetBuf)
{
    if (pSuperInfo->fNlsIsUtf8)
        return 0;
    return vbsf_symlink_nls_convert_slow(pSuperInfo, pszTarget, cbTargetBuf);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)

/**
 * Get symbolic link.
 */
static const char *vbsf_get_link(struct dentry *dentry, struct inode *inode, struct delayed_call *done)
{
    char *pszTarget;
    if (dentry) {
        pszTarget = (char *)kzalloc(PAGE_SIZE, GFP_KERNEL);
        if (pszTarget) {
            struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
            struct vbsf_inode_info *sf_i       = VBSF_GET_INODE_INFO(inode);
            int rc = VbglR0SfHostReqReadLinkContigSimple(pSuperInfo->map.root, sf_i->path->String.ach, sf_i->path->u16Length,
                                                         pszTarget, virt_to_phys(pszTarget), RT_MIN(PATH_MAX, PAGE_SIZE - 1));
            if (RT_SUCCESS(rc)) {
                pszTarget[PAGE_SIZE - 1] = '\0';
                SFLOGFLOW(("vbsf_get_link: %s -> %s\n", sf_i->path->String.ach, pszTarget));
                rc = vbsf_symlink_nls_convert(pSuperInfo, pszTarget, PAGE_SIZE);
                if (rc == 0) {
                    vbsf_dentry_chain_increase_ttl(dentry);
                    set_delayed_call(done, kfree_link, pszTarget);
                    return pszTarget;
                }
            } else {
                SFLOGFLOW(("vbsf_get_link: VbglR0SfHostReqReadLinkContigSimple failed on '%s': %Rrc\n",
                           sf_i->path->String.ach, rc));
            }
            kfree(pszTarget);
            pszTarget = ERR_PTR(vbsf_convert_symlink_error(rc));
        } else
            pszTarget = ERR_PTR(-ENOMEM);
    } else
        pszTarget = ERR_PTR(-ECHILD);
    return pszTarget;
}

#else /* < 4.5 */

# if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 8)
/**
 * Reads the link into the given buffer.
 */
static int vbsf_readlink(struct dentry *dentry, char *buffer, int len)
{
    int   rc;
    char *pszTarget = (char *)get_zeroed_page(GFP_KERNEL);
    if (pszTarget) {
        struct inode           *inode = dentry->d_inode;
        struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
        struct vbsf_inode_info *sf_i       = VBSF_GET_INODE_INFO(inode);
        rc = VbglR0SfHostReqReadLinkContigSimple(pSuperInfo->map.root, sf_i->path->String.ach, sf_i->path->u16Length,
                                                 pszTarget, virt_to_phys(pszTarget), RT_MIN(PATH_MAX, PAGE_SIZE - 1));
        if (RT_SUCCESS(rc)) {
            pszTarget[PAGE_SIZE - 1] = '\0';
            SFLOGFLOW(("vbsf_readlink: %s -> %*s\n", sf_i->path->String.ach, pszTarget));
            rc = vbsf_symlink_nls_convert(pSuperInfo, pszTarget, PAGE_SIZE);
            if (rc == 0) {
                vbsf_dentry_chain_increase_ttl(dentry);
                rc = vfs_readlink(dentry, buffer, len, pszTarget);
            }
        } else {
            SFLOGFLOW(("vbsf_readlink: VbglR0SfHostReqReadLinkContigSimple failed on '%s': %Rrc\n", sf_i->path->String.ach, rc));
            rc = vbsf_convert_symlink_error(rc);
        }
        free_page((unsigned long)pszTarget);
    } else
        rc = -ENOMEM;
    return rc;
}
# endif /* < 2.6.8 */

/**
 * Follow link in dentry.
 */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
static const char *vbsf_follow_link(struct dentry *dentry, void **cookie)
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13)
static void       *vbsf_follow_link(struct dentry *dentry, struct nameidata *nd)
# else
static int         vbsf_follow_link(struct dentry *dentry, struct nameidata *nd)
# endif
{
    int rc;
    char *pszTarget = (char *)get_zeroed_page(GFP_KERNEL);
    if (pszTarget) {
        struct inode           *inode = dentry->d_inode;
        struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
        struct vbsf_inode_info *sf_i       = VBSF_GET_INODE_INFO(inode);

        rc = VbglR0SfHostReqReadLinkContigSimple(pSuperInfo->map.root, sf_i->path->String.ach, sf_i->path->u16Length,
                                                 pszTarget, virt_to_phys(pszTarget), RT_MIN(PATH_MAX, PAGE_SIZE - 1));
        if (RT_SUCCESS(rc)) {
            pszTarget[PAGE_SIZE - 1] = '\0';
            SFLOGFLOW(("vbsf_follow_link: %s -> %s\n", sf_i->path->String.ach, pszTarget));
            rc = vbsf_symlink_nls_convert(pSuperInfo, pszTarget, PAGE_SIZE);
            if (rc == 0) {
                /*
                 * Succeeded.  For 2.6.8 and later the page gets associated
                 * with the caller-cookie or nameidata structure and freed
                 * later by vbsf_put_link().  On earlier kernels we have to
                 * call vfs_follow_link() which will try continue the walking
                 * using the buffer we pass it here.
                 */
                vbsf_dentry_chain_increase_ttl(dentry);
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
                *cookie = pszTarget;
                return pszTarget;
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 8)
                nd_set_link(nd, pszTarget);
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13)
                return NULL;
#  else
                return 0;
#  endif
# else /* < 2.6.8 */
                rc = vfs_follow_link(nd, pszTarget);
                free_page((unsigned long)pszTarget);
                return rc;
# endif
            }

            /*
             * Failed.
             */
        } else {
            LogFunc(("VbglR0SfReadLink failed, caller=%s, rc=%Rrc\n", __func__, rc));
            rc = vbsf_convert_symlink_error(rc);
        }
        free_page((unsigned long)pszTarget);
    } else {
        rc = -ENOMEM;
    }
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
    *cookie = ERR_PTR(rc);
    return (const char *)ERR_PTR(rc);
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 8)
    nd_set_link(nd, (char *)ERR_PTR(rc));
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13)
    return NULL;
#  else
    return 0;
#  endif
# else  /* < 2.6.8 */
    return rc;
# endif /* < 2.6.8 */
}

# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 8)
/**
 * For freeing target link buffer allocated by vbsf_follow_link.
 *
 * For kernels before 2.6.8 memory isn't being kept around.
 */
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
static void vbsf_put_link(struct inode *inode, void *cookie)
#  elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13)
static void vbsf_put_link(struct dentry *dentry, struct nameidata *nd, void *cookie)
#  else
static void vbsf_put_link(struct dentry *dentry, struct nameidata *nd)
#  endif
{
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 13)
    char *page = cookie;
#  else
    char *page = nd_get_link(nd);
#  endif
    SFLOGFLOW(("vbsf_put_link: page=%p\n", page));
    if (!IS_ERR(page))
        free_page((unsigned long)page);
}
# endif /* >= 2.6.8 */

#endif /* < 4.5.0 */

/**
 * Symlink inode operations.
 */
struct inode_operations vbsf_lnk_iops = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 8)
    .readlink    = generic_readlink,
# else
    .readlink    = vbsf_readlink,
# endif
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
    .get_link    = vbsf_get_link
#else
    .follow_link = vbsf_follow_link,
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 8)
    .put_link    = vbsf_put_link,
# endif
#endif
};

