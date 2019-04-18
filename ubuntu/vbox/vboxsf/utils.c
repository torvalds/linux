/* $Id: utils.c $ */
/** @file
 * vboxsf - VBox Linux Shared Folders VFS, utility functions.
 *
 * Utility functions (mainly conversion from/to VirtualBox/Linux data structures).
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

#include "vfsmod.h"
#include <iprt/asm.h>
#include <iprt/err.h>
#include <linux/vfs.h>


int vbsf_nlscpy(struct vbsf_super_info *pSuperInfo, char *name, size_t name_bound_len,
                const unsigned char *utf8_name, size_t utf8_len)
{
    Assert(name_bound_len > 1);
    Assert(RTStrNLen(utf8_name, utf8_len) == utf8_len);

    if (pSuperInfo->nls) {
        const char *in              = utf8_name;
        size_t      in_bound_len    = utf8_len;
        char       *out             = name;
        size_t      out_bound_len   = name_bound_len - 1;
        size_t      out_len         = 0;

        while (in_bound_len) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
            unicode_t uni;
            int cbInEnc = utf8_to_utf32(in, in_bound_len, &uni);
#else
            linux_wchar_t uni;
            int cbInEnc = utf8_mbtowc(&uni, in, in_bound_len);
#endif
            if (cbInEnc >= 0) {
                int cbOutEnc = pSuperInfo->nls->uni2char(uni, out, out_bound_len);
                if (cbOutEnc >= 0) {
                    /*SFLOG3(("vbsf_nlscpy: cbOutEnc=%d cbInEnc=%d uni=%#x in_bound_len=%u\n", cbOutEnc, cbInEnc, uni, in_bound_len));*/
                    out           += cbOutEnc;
                    out_bound_len -= cbOutEnc;
                    out_len       += cbOutEnc;

                    in            += cbInEnc;
                    in_bound_len  -= cbInEnc;
                } else {
                    SFLOG(("vbsf_nlscpy: nls->uni2char failed with %d on %#x (pos %u in '%s'), out_bound_len=%u\n",
                           cbOutEnc, uni, in - (const char *)utf8_name, (const char *)utf8_name, (unsigned)out_bound_len));
                    return cbOutEnc;
                }
            } else {
                SFLOG(("vbsf_nlscpy: utf8_to_utf32/utf8_mbtowc failed with %d on %x (pos %u in '%s'), in_bound_len=%u!\n",
                       cbInEnc, *in, in - (const char *)utf8_name, (const char *)utf8_name, (unsigned)in_bound_len));
                return -EINVAL;
            }
        }

        *out = '\0';
    } else {
        if (utf8_len + 1 > name_bound_len)
            return -ENAMETOOLONG;

        memcpy(name, utf8_name, utf8_len + 1);
    }
    return 0;
}


/**
 * Converts the given NLS string to a host one, kmalloc'ing
 * the output buffer (use kfree on result).
 */
int vbsf_nls_to_shflstring(struct vbsf_super_info *pSuperInfo, const char *pszNls, PSHFLSTRING *ppString)
{
    int          rc;
    size_t const cchNls = strlen(pszNls);
    PSHFLSTRING  pString = NULL;
    if (pSuperInfo->nls) {
        /*
         * NLS -> UTF-8 w/ SHLF string header.
         */
        /* Calc length first: */
        size_t cchUtf8 = 0;
        size_t offNls  = 0;
        while (offNls < cchNls) {
            linux_wchar_t uc; /* Note! We renamed the type due to clashes. */
            int const cbNlsCodepoint = pSuperInfo->nls->char2uni(&pszNls[offNls], cchNls - offNls, &uc);
            if (cbNlsCodepoint >= 0) {
                char achTmp[16];
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
                int cbUtf8Codepoint = utf32_to_utf8(uc, achTmp, sizeof(achTmp));
#else
                int cbUtf8Codepoint = utf8_wctomb(achTmp, uc, sizeof(achTmp));
#endif
                if (cbUtf8Codepoint > 0) {
                    cchUtf8 += cbUtf8Codepoint;
                    offNls  += cbNlsCodepoint;
                } else {
                    Log(("vbsf_nls_to_shflstring: nls->uni2char(%#x) failed: %d\n", uc, cbUtf8Codepoint));
                    return -EINVAL;
                }
            } else {
                Log(("vbsf_nls_to_shflstring: nls->char2uni(%.*Rhxs) failed: %d\n",
                     RT_MIN(8, cchNls - offNls), &pszNls[offNls], cbNlsCodepoint));
                return -EINVAL;
            }
        }
        if (cchUtf8 + 1 < _64K) {
            /* Allocate: */
            pString = (PSHFLSTRING)kmalloc(SHFLSTRING_HEADER_SIZE + cchUtf8 + 1, GFP_KERNEL);
            if (pString) {
                char *pchDst = pString->String.ach;
                pString->u16Length = (uint16_t)cchUtf8;
                pString->u16Size   = (uint16_t)(cchUtf8 + 1);

                /* Do the conversion (cchUtf8 is counted down): */
                rc     = 0;
                offNls = 0;
                while (offNls < cchNls) {
                    linux_wchar_t uc; /* Note! We renamed the type due to clashes. */
                    int const cbNlsCodepoint = pSuperInfo->nls->char2uni(&pszNls[offNls], cchNls - offNls, &uc);
                    if (cbNlsCodepoint >= 0) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
                        int cbUtf8Codepoint = utf32_to_utf8(uc, pchDst, cchUtf8);
#else
                        int cbUtf8Codepoint = utf8_wctomb(pchDst, uc, cchUtf8);
#endif
                        if (cbUtf8Codepoint > 0) {
                            AssertBreakStmt(cbUtf8Codepoint <= cchUtf8, rc = -EINVAL);
                            cchUtf8 -= cbUtf8Codepoint;
                            pchDst  += cbUtf8Codepoint;
                            offNls  += cbNlsCodepoint;
                        } else {
                            Log(("vbsf_nls_to_shflstring: nls->uni2char(%#x) failed! %d, cchUtf8=%zu\n",
                                 uc, cbUtf8Codepoint, cchUtf8));
                            rc = -EINVAL;
                            break;
                        }
                    } else {
                        Log(("vbsf_nls_to_shflstring: nls->char2uni(%.*Rhxs) failed! %d\n",
                             RT_MIN(8, cchNls - offNls), &pszNls[offNls], cbNlsCodepoint));
                        rc = -EINVAL;
                        break;
                    }
                }
                if (rc == 0) {
                    /*
                     * Succeeded.  Just terminate the string and we're good.
                     */
                    Assert(pchDst - pString->String.ach == pString->u16Length);
                    *pchDst = '\0';
                } else {
                    kfree(pString);
                    pString = NULL;
                }
            } else {
                Log(("vbsf_nls_to_shflstring: failed to allocate %u bytes\n", SHFLSTRING_HEADER_SIZE + cchUtf8 + 1));
                rc = -ENOMEM;
            }
        } else {
            Log(("vbsf_nls_to_shflstring: too long: %zu bytes (%zu nls bytes)\n", cchUtf8, cchNls));
            rc = -ENAMETOOLONG;
        }
    } else {
        /*
         * UTF-8 -> UTF-8 w/ SHLF string header.
         */
        if (cchNls + 1 < _64K) {
            pString = (PSHFLSTRING)kmalloc(SHFLSTRING_HEADER_SIZE + cchNls + 1, GFP_KERNEL);
            if (pString) {
                pString->u16Length = (uint16_t)cchNls;
                pString->u16Size   = (uint16_t)(cchNls + 1);
                memcpy(pString->String.ach, pszNls, cchNls);
                pString->String.ach[cchNls] = '\0';
                rc = 0;
            } else {
                Log(("vbsf_nls_to_shflstring: failed to allocate %u bytes\n", SHFLSTRING_HEADER_SIZE + cchNls + 1));
                rc = -ENOMEM;
            }
        } else {
            Log(("vbsf_nls_to_shflstring: too long: %zu bytes\n", cchNls));
            rc = -ENAMETOOLONG;
        }
    }
    *ppString = pString;
    return rc;
}


/**
 * Convert from VBox to linux time.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
DECLINLINE(void) vbsf_time_to_linux(time_t *pLinuxDst, PCRTTIMESPEC pVBoxSrc)
{
    int64_t t = RTTimeSpecGetNano(pVBoxSrc);
    do_div(t, RT_NS_1SEC);
    *pLinuxDst = t;
}
#else   /* >= 2.6.0 */
# if LINUX_VERSION_CODE < KERNEL_VERSION(4, 18, 0)
DECLINLINE(void) vbsf_time_to_linux(struct timespec *pLinuxDst, PCRTTIMESPEC pVBoxSrc)
# else
DECLINLINE(void) vbsf_time_to_linux(struct timespec64 *pLinuxDst, PCRTTIMESPEC pVBoxSrc)
# endif
{
    int64_t t = RTTimeSpecGetNano(pVBoxSrc);
    pLinuxDst->tv_nsec = do_div(t, RT_NS_1SEC);
    pLinuxDst->tv_sec  = t;
}
#endif  /* >= 2.6.0 */


/**
 * Convert from linux to VBox time.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 0)
DECLINLINE(void) vbsf_time_to_vbox(PRTTIMESPEC pVBoxDst, time_t *pLinuxSrc)
{
    RTTimeSpecSetNano(pVBoxDst, RT_NS_1SEC_64 * *pLinuxSrc);
}
#else   /* >= 2.6.0 */
# if LINUX_VERSION_CODE < KERNEL_VERSION(4, 18, 0)
DECLINLINE(void) vbsf_time_to_vbox(PRTTIMESPEC pVBoxDst, struct timespec const *pLinuxSrc)
# else
DECLINLINE(void) vbsf_time_to_vbox(PRTTIMESPEC pVBoxDst, struct timespec64 const *pLinuxSrc)
# endif
{
    RTTimeSpecSetNano(pVBoxDst, pLinuxSrc->tv_nsec + pLinuxSrc->tv_sec * (int64_t)RT_NS_1SEC);
}
#endif  /* >= 2.6.0 */


/**
 * Converts VBox access permissions  to Linux ones (mode & 0777).
 *
 * @note Currently identical.
 * @sa   sf_access_permissions_to_vbox
 */
DECLINLINE(int) sf_access_permissions_to_linux(uint32_t fAttr)
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


/**
 * Produce the Linux mode mask, given VBox, mount options and file type.
 */
DECLINLINE(int) sf_file_mode_to_linux(uint32_t fVBoxMode, int fFixedMode, int fClearMask, int fType)
{
    int fLnxMode = sf_access_permissions_to_linux(fVBoxMode);
    if (fFixedMode != ~0)
        fLnxMode = fFixedMode & 0777;
    fLnxMode &= ~fClearMask;
    fLnxMode |= fType;
    return fLnxMode;
}


/**
 * Initializes the @a inode attributes based on @a pObjInfo and @a pSuperInfo
 * options.
 */
void vbsf_init_inode(struct inode *inode, struct vbsf_inode_info *sf_i, PSHFLFSOBJINFO pObjInfo,
                     struct vbsf_super_info *pSuperInfo)
{
    PCSHFLFSOBJATTR pAttr = &pObjInfo->Attr;

    TRACE();

    sf_i->ts_up_to_date = jiffies;
    sf_i->force_restat  = 0;

    if (RTFS_IS_DIRECTORY(pAttr->fMode)) {
        inode->i_mode = sf_file_mode_to_linux(pAttr->fMode, pSuperInfo->dmode, pSuperInfo->dmask, S_IFDIR);
        inode->i_op = &vbsf_dir_iops;
        inode->i_fop = &vbsf_dir_fops;

        /* XXX: this probably should be set to the number of entries
           in the directory plus two (. ..) */
        set_nlink(inode, 1);
    }
    else if (RTFS_IS_SYMLINK(pAttr->fMode)) {
        /** @todo r=bird: Aren't System V symlinks w/o any mode mask? IIRC there is
         *        no lchmod on Linux. */
        inode->i_mode = sf_file_mode_to_linux(pAttr->fMode, pSuperInfo->fmode, pSuperInfo->fmask, S_IFLNK);
        inode->i_op = &vbsf_lnk_iops;
        set_nlink(inode, 1);
    } else {
        inode->i_mode = sf_file_mode_to_linux(pAttr->fMode, pSuperInfo->fmode, pSuperInfo->fmask, S_IFREG);
        inode->i_op = &vbsf_reg_iops;
        inode->i_fop = &vbsf_reg_fops;
        inode->i_mapping->a_ops = &vbsf_reg_aops;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 17) \
 && LINUX_VERSION_CODE <  KERNEL_VERSION(4, 0, 0)
        inode->i_mapping->backing_dev_info = &pSuperInfo->bdi; /* This is needed for mmap. */
#endif
        set_nlink(inode, 1);
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 5, 0)
    inode->i_uid = make_kuid(current_user_ns(), pSuperInfo->uid);
    inode->i_gid = make_kgid(current_user_ns(), pSuperInfo->gid);
#else
    inode->i_uid = pSuperInfo->uid;
    inode->i_gid = pSuperInfo->gid;
#endif

    inode->i_size = pObjInfo->cbObject;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19) && !defined(KERNEL_FC6)
    inode->i_blksize = 4096;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 11)
    inode->i_blkbits = 12;
#endif
    /* i_blocks always in units of 512 bytes! */
    inode->i_blocks = (pObjInfo->cbAllocated + 511) / 512;

    vbsf_time_to_linux(&inode->i_atime, &pObjInfo->AccessTime);
    vbsf_time_to_linux(&inode->i_ctime, &pObjInfo->ChangeTime);
    vbsf_time_to_linux(&inode->i_mtime, &pObjInfo->ModificationTime);
    sf_i->BirthTime = pObjInfo->BirthTime;
    sf_i->ModificationTime = pObjInfo->ModificationTime;
    RTTimeSpecSetSeconds(&sf_i->ModificationTimeAtOurLastWrite, 0);
}


/**
 * Update the inode with new object info from the host.
 *
 * Called by sf_inode_revalidate() and sf_inode_revalidate_with_handle().
 */
void vbsf_update_inode(struct inode *pInode, struct vbsf_inode_info *pInodeInfo, PSHFLFSOBJINFO pObjInfo,
                       struct vbsf_super_info *pSuperInfo, bool fInodeLocked, unsigned fSetAttrs)
{
    PCSHFLFSOBJATTR pAttr = &pObjInfo->Attr;
    int             fMode;

    TRACE();

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
    if (!fInodeLocked)
        inode_lock(pInode);
#endif

    /*
     * Calc new mode mask and update it if it changed.
     */
    if (RTFS_IS_DIRECTORY(pAttr->fMode))
        fMode = sf_file_mode_to_linux(pAttr->fMode, pSuperInfo->dmode, pSuperInfo->dmask, S_IFDIR);
    else if (RTFS_IS_SYMLINK(pAttr->fMode))
        /** @todo r=bird: Aren't System V symlinks w/o any mode mask? IIRC there is
         *        no lchmod on Linux. */
        fMode = sf_file_mode_to_linux(pAttr->fMode, pSuperInfo->fmode, pSuperInfo->fmask, S_IFLNK);
    else
        fMode = sf_file_mode_to_linux(pAttr->fMode, pSuperInfo->fmode, pSuperInfo->fmask, S_IFREG);

    if (fMode == pInode->i_mode) {
        /* likely */
    } else {
        if ((fMode & S_IFMT) == (pInode->i_mode & S_IFMT))
            pInode->i_mode = fMode;
        else {
            SFLOGFLOW(("vbsf_update_inode: Changed from %o to %o (%s)\n",
                       pInode->i_mode & S_IFMT, fMode & S_IFMT, pInodeInfo->path->String.ach));
            /** @todo we probably need to be more drastic... */
            vbsf_init_inode(pInode, pInodeInfo, pObjInfo, pSuperInfo);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
            if (!fInodeLocked)
                inode_unlock(pInode);
#endif
            return;
        }
    }

    /*
     * Update the sizes.
     * Note! i_blocks is always in units of 512 bytes!
     */
    pInode->i_blocks = (pObjInfo->cbAllocated + 511) / 512;
    i_size_write(pInode, pObjInfo->cbObject);

    /*
     * Update the timestamps.
     */
    vbsf_time_to_linux(&pInode->i_atime, &pObjInfo->AccessTime);
    vbsf_time_to_linux(&pInode->i_ctime, &pObjInfo->ChangeTime);
    vbsf_time_to_linux(&pInode->i_mtime, &pObjInfo->ModificationTime);
    pInodeInfo->BirthTime = pObjInfo->BirthTime;

    /*
     * Mark it as up to date.
     * Best to do this before we start with any expensive map invalidation.
     */
    pInodeInfo->ts_up_to_date = jiffies;
    pInodeInfo->force_restat  = 0;

    /*
     * If the modification time changed, we may have to invalidate the page
     * cache pages associated with this inode if we suspect the change was
     * made by the host.  How supicious we are depends on the cache mode.
     *
     * Note! The invalidate_inode_pages() call is pretty weak.  It will _not_
     *       touch pages that are already mapped into an address space, but it
     *       will help if the file isn't currently mmap'ed or if we're in read
     *       or read/write caching mode.
     */
    if (!RTTimeSpecIsEqual(&pInodeInfo->ModificationTime, &pObjInfo->ModificationTime)) {
        if (RTFS_IS_FILE(pAttr->fMode)) {
            if (!(fSetAttrs & (ATTR_MTIME | ATTR_SIZE))) {
                bool fInvalidate;
                if (pSuperInfo->enmCacheMode == kVbsfCacheMode_None) {
                    fInvalidate = true;      /* No-caching: always invalidate. */
                } else {
                    if (RTTimeSpecIsEqual(&pInodeInfo->ModificationTimeAtOurLastWrite, &pInodeInfo->ModificationTime)) {
                        fInvalidate = false; /* Could be our write, so don't invalidate anything */
                        RTTimeSpecSetSeconds(&pInodeInfo->ModificationTimeAtOurLastWrite, 0);
                    } else {
                        /*RTLogBackdoorPrintf("vbsf_update_inode: Invalidating the mapping %s - %RU64 vs %RU64 vs %RU64 - %#x\n",
                                            pInodeInfo->path->String.ach,
                                            RTTimeSpecGetNano(&pInodeInfo->ModificationTimeAtOurLastWrite),
                                            RTTimeSpecGetNano(&pInodeInfo->ModificationTime),
                                            RTTimeSpecGetNano(&pObjInfo->ModificationTime), fSetAttrs);*/
                        fInvalidate = true;  /* We haven't modified the file recently, so probably a host update. */
                    }
                }
                pInodeInfo->ModificationTime = pObjInfo->ModificationTime;

                if (fInvalidate) {
                    struct address_space *mapping = pInode->i_mapping;
                    if (mapping && mapping->nrpages > 0) {
                        SFLOGFLOW(("vbsf_update_inode: Invalidating the mapping %s (%#x)\n", pInodeInfo->path->String.ach, fSetAttrs));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34)
                        invalidate_mapping_pages(mapping, 0, ~(pgoff_t)0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 41)
                        invalidate_inode_pages(mapping);
#else
                        invalidate_inode_pages(pInode);
#endif
                    }
                }
            } else {
                RTTimeSpecSetSeconds(&pInodeInfo->ModificationTimeAtOurLastWrite, 0);
                pInodeInfo->ModificationTime = pObjInfo->ModificationTime;
            }
        } else
            pInodeInfo->ModificationTime = pObjInfo->ModificationTime;
    }

    /*
     * Done.
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
    if (!fInodeLocked)
        inode_unlock(pInode);
#endif
}


/** @note Currently only used for the root directory during (re-)mount.  */
int vbsf_stat(const char *caller, struct vbsf_super_info *pSuperInfo, SHFLSTRING *path, PSHFLFSOBJINFO result, int ok_to_fail)
{
    int rc;
    VBOXSFCREATEREQ *pReq;
    NOREF(caller);

    TRACE();

    pReq = (VBOXSFCREATEREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq) + path->u16Size);
    if (pReq) {
        RT_ZERO(*pReq);
        memcpy(&pReq->StrPath, path, SHFLSTRING_HEADER_SIZE + path->u16Size);
        pReq->CreateParms.Handle = SHFL_HANDLE_NIL;
        pReq->CreateParms.CreateFlags = SHFL_CF_LOOKUP | SHFL_CF_ACT_FAIL_IF_NEW;

        LogFunc(("Calling VbglR0SfHostReqCreate on %s\n", path->String.utf8));
        rc = VbglR0SfHostReqCreate(pSuperInfo->map.root, pReq);
        if (RT_SUCCESS(rc)) {
            if (pReq->CreateParms.Result == SHFL_FILE_EXISTS) {
                *result = pReq->CreateParms.Info;
                rc = 0;
            } else {
                if (!ok_to_fail)
                    LogFunc(("VbglR0SfHostReqCreate on %s: file does not exist: %d (caller=%s)\n",
                             path->String.utf8, pReq->CreateParms.Result, caller));
                rc = -ENOENT;
            }
        } else if (rc == VERR_INVALID_NAME) {
            rc = -ENOENT; /* this can happen for names like 'foo*' on a Windows host */
        } else {
            LogFunc(("VbglR0SfHostReqCreate failed on %s: %Rrc (caller=%s)\n", path->String.utf8, rc, caller));
            rc = -EPROTO;
        }
        VbglR0PhysHeapFree(pReq);
    }
    else
        rc = -ENOMEM;
    return rc;
}


/**
 * Revalidate an inode, inner worker.
 *
 * @sa sf_inode_revalidate()
 */
int vbsf_inode_revalidate_worker(struct dentry *dentry, bool fForced, bool fInodeLocked)
{
    int rc;
    struct inode *pInode = dentry ? dentry->d_inode : NULL;
    if (pInode) {
        struct vbsf_inode_info *sf_i       = VBSF_GET_INODE_INFO(pInode);
        struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(pInode->i_sb);
        AssertReturn(sf_i, -EINVAL);
        AssertReturn(pSuperInfo, -EINVAL);

        /*
         * Can we get away without any action here?
         */
        if (   !fForced
            && !sf_i->force_restat
            && jiffies - sf_i->ts_up_to_date < pSuperInfo->cJiffiesInodeTTL)
            rc = 0;
        else {
            /*
             * No, we have to query the file info from the host.
             * Try get a handle we can query, any kind of handle will do here.
             */
            struct vbsf_handle *pHandle = vbsf_handle_find(sf_i, 0, 0);
            if (pHandle) {
                /* Query thru pHandle. */
                VBOXSFOBJINFOREQ *pReq = (VBOXSFOBJINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
                if (pReq) {
                    RT_ZERO(*pReq);
                    rc = VbglR0SfHostReqQueryObjInfo(pSuperInfo->map.root, pReq, pHandle->hHost);
                    if (RT_SUCCESS(rc)) {
                        /*
                         * Reset the TTL and copy the info over into the inode structure.
                         */
                        vbsf_update_inode(pInode, sf_i, &pReq->ObjInfo, pSuperInfo, fInodeLocked, 0 /*fSetAttrs*/);
                    } else if (rc == VERR_INVALID_HANDLE) {
                        rc = -ENOENT; /* Restore.*/
                    } else {
                        LogFunc(("VbglR0SfHostReqQueryObjInfo failed on %#RX64: %Rrc\n", pHandle->hHost, rc));
                        rc = -RTErrConvertToErrno(rc);
                    }
                    VbglR0PhysHeapFree(pReq);
                } else
                    rc = -ENOMEM;
                vbsf_handle_release(pHandle, pSuperInfo, "vbsf_inode_revalidate_worker");

            } else {
                /* Query via path. */
                SHFLSTRING      *pPath = sf_i->path;
                VBOXSFCREATEREQ *pReq  = (VBOXSFCREATEREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq) + pPath->u16Size);
                if (pReq) {
                    RT_ZERO(*pReq);
                    memcpy(&pReq->StrPath, pPath, SHFLSTRING_HEADER_SIZE + pPath->u16Size);
                    pReq->CreateParms.Handle      = SHFL_HANDLE_NIL;
                    pReq->CreateParms.CreateFlags = SHFL_CF_LOOKUP | SHFL_CF_ACT_FAIL_IF_NEW;

                    rc = VbglR0SfHostReqCreate(pSuperInfo->map.root, pReq);
                    if (RT_SUCCESS(rc)) {
                        if (pReq->CreateParms.Result == SHFL_FILE_EXISTS) {
                            /*
                             * Reset the TTL and copy the info over into the inode structure.
                             */
                            vbsf_update_inode(pInode, sf_i, &pReq->CreateParms.Info, pSuperInfo, fInodeLocked, 0 /*fSetAttrs*/);
                            rc = 0;
                        } else {
                            rc = -ENOENT;
                        }
                    } else if (rc == VERR_INVALID_NAME) {
                        rc = -ENOENT; /* this can happen for names like 'foo*' on a Windows host */
                    } else {
                        LogFunc(("VbglR0SfHostReqCreate failed on %s: %Rrc\n", pPath->String.ach, rc));
                        rc = -EPROTO;
                    }
                    VbglR0PhysHeapFree(pReq);
                }
                else
                    rc = -ENOMEM;
            }
        }
    } else {
        LogFunc(("no dentry(%p) or inode(%p)\n", dentry, pInode));
        rc = -EINVAL;
    }
    return rc;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 18)
/**
 * Revalidate an inode for 2.4.
 *
 * This is called in the stat(), lstat() and readlink() code paths.  In the stat
 * cases the caller will use the result afterwards to produce the stat data.
 *
 * @note 2.4.x has a getattr() inode operation too, but it is not used.
 */
int vbsf_inode_revalidate(struct dentry *dentry)
{
    /*
     * We pretend the inode is locked here, as 2.4.x does not have inode level locking.
     */
    return vbsf_inode_revalidate_worker(dentry, false /*fForced*/, true /*fInodeLocked*/);
}
#endif /* < 2.5.18 */


/**
 * Similar to sf_inode_revalidate, but uses associated host file handle as that
 * is quite a bit faster.
 */
int vbsf_inode_revalidate_with_handle(struct dentry *dentry, SHFLHANDLE hHostFile, bool fForced, bool fInodeLocked)
{
    int err;
    struct inode *pInode = dentry ? dentry->d_inode : NULL;
    if (!pInode) {
        LogFunc(("no dentry(%p) or inode(%p)\n", dentry, pInode));
        err = -EINVAL;
    } else {
        struct vbsf_inode_info *sf_i       = VBSF_GET_INODE_INFO(pInode);
        struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(pInode->i_sb);
        AssertReturn(sf_i, -EINVAL);
        AssertReturn(pSuperInfo, -EINVAL);

        /*
         * Can we get away without any action here?
         */
        if (   !fForced
            && !sf_i->force_restat
            && jiffies - sf_i->ts_up_to_date < pSuperInfo->cJiffiesInodeTTL)
            err = 0;
        else {
            /*
             * No, we have to query the file info from the host.
             */
            VBOXSFOBJINFOREQ *pReq = (VBOXSFOBJINFOREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
            if (pReq) {
                RT_ZERO(*pReq);
                err = VbglR0SfHostReqQueryObjInfo(pSuperInfo->map.root, pReq, hHostFile);
                if (RT_SUCCESS(err)) {
                    /*
                     * Reset the TTL and copy the info over into the inode structure.
                     */
                    vbsf_update_inode(pInode, sf_i, &pReq->ObjInfo, pSuperInfo, fInodeLocked, 0 /*fSetAttrs*/);
                } else {
                    LogFunc(("VbglR0SfHostReqQueryObjInfo failed on %#RX64: %Rrc\n", hHostFile, err));
                    err = -RTErrConvertToErrno(err);
                }
                VbglR0PhysHeapFree(pReq);
            } else
                err = -ENOMEM;
        }
    }
    return err;
}


/* on 2.6 this is a proxy for [sf_inode_revalidate] which (as a side
   effect) updates inode attributes for [dentry] (given that [dentry]
   has inode at all) from these new attributes we derive [kstat] via
   [generic_fillattr] */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 18)

# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
int vbsf_inode_getattr(const struct path *path, struct kstat *kstat, u32 request_mask, unsigned int flags)
# else
int vbsf_inode_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *kstat)
# endif
{
    int            rc;
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
    struct dentry *dentry = path->dentry;
# endif

# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
    SFLOGFLOW(("vbsf_inode_getattr: dentry=%p request_mask=%#x flags=%#x\n", dentry, request_mask, flags));
# else
    SFLOGFLOW(("vbsf_inode_getattr: dentry=%p\n", dentry));
# endif

# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
    /*
     * With the introduction of statx() userland can control whether we
     * update the inode information or not.
     */
    switch (flags & AT_STATX_SYNC_TYPE) {
        default:
            rc = vbsf_inode_revalidate_worker(dentry, false /*fForced*/, false /*fInodeLocked*/);
            break;

        case AT_STATX_FORCE_SYNC:
            rc = vbsf_inode_revalidate_worker(dentry, true /*fForced*/, false /*fInodeLocked*/);
            break;

        case AT_STATX_DONT_SYNC:
            rc = 0;
            break;
    }
# else
    rc = vbsf_inode_revalidate_worker(dentry, false /*fForced*/, false /*fInodeLocked*/);
# endif
    if (rc == 0) {
        /* Do generic filling in of info. */
        generic_fillattr(dentry->d_inode, kstat);

        /* Add birth time. */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
        if (dentry->d_inode) {
            struct vbsf_inode_info *pInodeInfo = VBSF_GET_INODE_INFO(dentry->d_inode);
            if (pInodeInfo) {
                vbsf_time_to_linux(&kstat->btime, &pInodeInfo->BirthTime);
                kstat->result_mask |= STATX_BTIME;
            }
        }
# endif

        /*
         * FsPerf shows the following numbers for sequential file access against
         * a tmpfs folder on an AMD 1950X host running debian buster/sid:
         *
         * block size = r128600    ----- r128755 -----
         *               reads      reads     writes
         *    4096 KB = 2254 MB/s  4953 MB/s 3668 MB/s
         *    2048 KB = 2368 MB/s  4908 MB/s 3541 MB/s
         *    1024 KB = 2208 MB/s  4011 MB/s 3291 MB/s
         *     512 KB = 1908 MB/s  3399 MB/s 2721 MB/s
         *     256 KB = 1625 MB/s  2679 MB/s 2251 MB/s
         *     128 KB = 1413 MB/s  1967 MB/s 1684 MB/s
         *      64 KB = 1152 MB/s  1409 MB/s 1265 MB/s
         *      32 KB =  726 MB/s   815 MB/s  783 MB/s
         *      16 KB =             683 MB/s  475 MB/s
         *       8 KB =             294 MB/s  286 MB/s
         *       4 KB =  145 MB/s   156 MB/s  149 MB/s
         *
         */
        if (S_ISREG(kstat->mode))
            kstat->blksize = _1M;
        else if (S_ISDIR(kstat->mode))
            /** @todo this may need more tuning after we rewrite the directory handling. */
            kstat->blksize = _16K;
    }
    return rc;
}
#endif /* >= 2.5.18 */


/**
 * Modify inode attributes.
 */
int vbsf_inode_setattr(struct dentry *dentry, struct iattr *iattr)
{
    struct inode           *pInode     = dentry->d_inode;
    struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(pInode->i_sb);
    struct vbsf_inode_info *sf_i       = VBSF_GET_INODE_INFO(pInode);
    int vrc;
    int rc;

    SFLOGFLOW(("vbsf_inode_setattr: dentry=%p inode=%p ia_valid=%#x %s\n",
               dentry, pInode, iattr->ia_valid, sf_i ? sf_i->path->String.ach : NULL));
    AssertReturn(sf_i, -EINVAL);

    /*
     * Need to check whether the caller is allowed to modify the attributes or not.
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
    rc = setattr_prepare(dentry, iattr);
#else
    rc = inode_change_ok(pInode, iattr);
#endif
    if (rc == 0) {
        /*
         * Don't modify MTIME and CTIME for open(O_TRUNC) and ftruncate, those
         * operations will set those timestamps automatically.  Saves a host call.
         */
        unsigned fAttrs = iattr->ia_valid;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 15)
        fAttrs &= ~ATTR_FILE;
#endif
        if (   fAttrs == (ATTR_SIZE | ATTR_MTIME | ATTR_CTIME)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
            || (fAttrs & (ATTR_OPEN | ATTR_SIZE)) == (ATTR_OPEN | ATTR_SIZE)
#endif
           )
            fAttrs &= ~(ATTR_MTIME | ATTR_CTIME);

        /*
         * We only implement a handful of attributes, so ignore any attempts
         * at setting bits we don't support.
         */
        if (fAttrs & (ATTR_MODE | ATTR_ATIME | ATTR_MTIME | ATTR_CTIME | ATTR_SIZE)) {
            /*
             * Try find a handle which allows us to modify the attributes, otherwise
             * open the file/dir/whatever.
             */
            union SetAttrReqs
            {
                VBOXSFCREATEREQ         Create;
                VBOXSFOBJINFOREQ        Info;
                VBOXSFSETFILESIZEREQ    SetSize;
                VBOXSFCLOSEREQ          Close;
            }                  *pReq;
            size_t              cbReq;
            SHFLHANDLE          hHostFile;
            /** @todo ATTR_FILE (2.6.15+) could be helpful here if we like. */
            struct vbsf_handle *pHandle = fAttrs & ATTR_SIZE
                                        ? vbsf_handle_find(sf_i, VBSF_HANDLE_F_WRITE, 0)
                                        : vbsf_handle_find(sf_i, 0, 0);
            if (pHandle) {
                hHostFile = pHandle->hHost;
                cbReq = RT_MAX(sizeof(VBOXSFOBJINFOREQ), sizeof(VBOXSFSETFILESIZEREQ));
                pReq  = (union SetAttrReqs *)VbglR0PhysHeapAlloc(cbReq);
                if (pReq) {
                    /* likely */
                } else
                    rc = -ENOMEM;
            } else {
                hHostFile = SHFL_HANDLE_NIL;
                cbReq = RT_MAX(sizeof(pReq->Info), sizeof(pReq->Create) + SHFLSTRING_HEADER_SIZE + sf_i->path->u16Size);
                pReq = (union SetAttrReqs *)VbglR0PhysHeapAlloc(cbReq);
                if (pReq) {
                    RT_ZERO(pReq->Create.CreateParms);
                    pReq->Create.CreateParms.Handle      = SHFL_HANDLE_NIL;
                    pReq->Create.CreateParms.CreateFlags = SHFL_CF_ACT_OPEN_IF_EXISTS
                                                         | SHFL_CF_ACT_FAIL_IF_NEW
                                                         | SHFL_CF_ACCESS_ATTR_WRITE;
                    if (fAttrs & ATTR_SIZE)
                        pReq->Create.CreateParms.CreateFlags |= SHFL_CF_ACCESS_WRITE;
                    memcpy(&pReq->Create.StrPath, sf_i->path, SHFLSTRING_HEADER_SIZE + sf_i->path->u16Size);
                    vrc = VbglR0SfHostReqCreate(pSuperInfo->map.root, &pReq->Create);
                    if (RT_SUCCESS(vrc)) {
                        if (pReq->Create.CreateParms.Result == SHFL_FILE_EXISTS) {
                            hHostFile = pReq->Create.CreateParms.Handle;
                            Assert(hHostFile != SHFL_HANDLE_NIL);
                            vbsf_dentry_chain_increase_ttl(dentry);
                        } else {
                            LogFunc(("file %s does not exist\n", sf_i->path->String.utf8));
                            vbsf_dentry_invalidate_ttl(dentry);
                            sf_i->force_restat = true;
                            rc = -ENOENT;
                        }
                    } else {
                        rc = -RTErrConvertToErrno(vrc);
                        LogFunc(("VbglR0SfCreate(%s) failed vrc=%Rrc rc=%d\n", sf_i->path->String.ach, vrc, rc));
                    }
                } else
                    rc = -ENOMEM;
            }
            if (rc == 0) {
                /*
                 * Set mode and/or timestamps.
                 */
                if (fAttrs & (ATTR_MODE | ATTR_ATIME | ATTR_MTIME | ATTR_CTIME)) {
                    /* Fill in the attributes.  Start by setting all to zero
                       since the host will ignore zeroed fields. */
                    RT_ZERO(pReq->Info.ObjInfo);

                    if (fAttrs & ATTR_MODE) {
                        pReq->Info.ObjInfo.Attr.fMode = sf_access_permissions_to_vbox(iattr->ia_mode);
                        if (iattr->ia_mode & S_IFDIR)
                            pReq->Info.ObjInfo.Attr.fMode |= RTFS_TYPE_DIRECTORY;
                        else if (iattr->ia_mode & S_IFLNK)
                            pReq->Info.ObjInfo.Attr.fMode |= RTFS_TYPE_SYMLINK;
                        else
                            pReq->Info.ObjInfo.Attr.fMode |= RTFS_TYPE_FILE;
                    }
                    if (fAttrs & ATTR_ATIME)
                        vbsf_time_to_vbox(&pReq->Info.ObjInfo.AccessTime, &iattr->ia_atime);
                    if (fAttrs & ATTR_MTIME)
                        vbsf_time_to_vbox(&pReq->Info.ObjInfo.ModificationTime, &iattr->ia_mtime);
                    if (fAttrs & ATTR_CTIME)
                        vbsf_time_to_vbox(&pReq->Info.ObjInfo.ChangeTime, &iattr->ia_ctime);

                    /* Make the change. */
                    vrc = VbglR0SfHostReqSetObjInfo(pSuperInfo->map.root, &pReq->Info, hHostFile);
                    if (RT_SUCCESS(vrc)) {
                        vbsf_update_inode(pInode, sf_i, &pReq->Info.ObjInfo, pSuperInfo, true /*fLocked*/, fAttrs);
                    } else {
                        rc = -RTErrConvertToErrno(vrc);
                        LogFunc(("VbglR0SfHostReqSetObjInfo(%s) failed vrc=%Rrc rc=%d\n", sf_i->path->String.ach, vrc, rc));
                    }
                }

                /*
                 * Change the file size.
                 * Note! Old API is more convenient here as it gives us up to date
                 *       inode info back.
                 */
                if ((fAttrs & ATTR_SIZE) && rc == 0) {
                    /*vrc = VbglR0SfHostReqSetFileSize(pSuperInfo->map.root, &pReq->SetSize, hHostFile, iattr->ia_size);
                    if (RT_SUCCESS(vrc)) {
                        i_size_write(pInode, iattr->ia_size);
                    } else if (vrc == VERR_NOT_IMPLEMENTED)*/ {
                        /* Fallback for pre 6.0 hosts: */
                        RT_ZERO(pReq->Info.ObjInfo);
                        pReq->Info.ObjInfo.cbObject = iattr->ia_size;
                        vrc = VbglR0SfHostReqSetFileSizeOld(pSuperInfo->map.root, &pReq->Info, hHostFile);
                        if (RT_SUCCESS(vrc))
                            vbsf_update_inode(pInode, sf_i, &pReq->Info.ObjInfo, pSuperInfo, true /*fLocked*/, fAttrs);
                    }
                    if (RT_SUCCESS(vrc)) {
                        /** @todo there is potentially more to be done here if there are mappings of
                         *        the lovely file. */
                    } else {
                        rc = -RTErrConvertToErrno(vrc);
                        LogFunc(("VbglR0SfHostReqSetFileSize(%s, %#llx) failed vrc=%Rrc rc=%d\n",
                                 sf_i->path->String.ach, (unsigned long long)iattr->ia_size, vrc, rc));
                    }
                }

                /*
                 * Clean up.
                 */
                if (!pHandle) {
                    vrc = VbglR0SfHostReqClose(pSuperInfo->map.root, &pReq->Close, hHostFile);
                    if (RT_FAILURE(vrc))
                        LogFunc(("VbglR0SfHostReqClose(%s [%#llx]) failed vrc=%Rrc\n", sf_i->path->String.utf8, hHostFile, vrc));
                }
            }
            if (pReq)
                VbglR0PhysHeapFree(pReq);
            if (pHandle)
                vbsf_handle_release(pHandle, pSuperInfo, "vbsf_inode_setattr");
        } else
            SFLOGFLOW(("vbsf_inode_setattr: Nothing to do here: %#x (was %#x).\n", fAttrs, iattr->ia_valid));
    }
    return rc;
}


static int vbsf_make_path(const char *caller, struct vbsf_inode_info *sf_i,
                          const char *d_name, size_t d_len, SHFLSTRING **result)
{
    size_t path_len, shflstring_len;
    SHFLSTRING *tmp;
    uint16_t p_len;
    uint8_t *p_name;
    int fRoot = 0;

    TRACE();
    p_len = sf_i->path->u16Length;
    p_name = sf_i->path->String.utf8;

    if (p_len == 1 && *p_name == '/') {
        path_len = d_len + 1;
        fRoot = 1;
    } else {
        /* lengths of constituents plus terminating zero plus slash  */
        path_len = p_len + d_len + 2;
        if (path_len > 0xffff) {
            LogFunc(("path too long.  caller=%s, path_len=%zu\n",
                 caller, path_len));
            return -ENAMETOOLONG;
        }
    }

    shflstring_len = offsetof(SHFLSTRING, String.utf8) + path_len;
    tmp = kmalloc(shflstring_len, GFP_KERNEL);
    if (!tmp) {
        LogRelFunc(("kmalloc failed, caller=%s\n", caller));
        return -ENOMEM;
    }
    tmp->u16Length = path_len - 1;
    tmp->u16Size = path_len;

    if (fRoot)
        memcpy(&tmp->String.utf8[0], d_name, d_len + 1);
    else {
        memcpy(&tmp->String.utf8[0], p_name, p_len);
        tmp->String.utf8[p_len] = '/';
        memcpy(&tmp->String.utf8[p_len + 1], d_name, d_len);
        tmp->String.utf8[p_len + 1 + d_len] = '\0';
    }

    *result = tmp;
    return 0;
}


/**
 * [dentry] contains string encoded in coding system that corresponds
 * to [pSuperInfo]->nls, we must convert it to UTF8 here and pass down to
 * [vbsf_make_path] which will allocate SHFLSTRING and fill it in
 */
int vbsf_path_from_dentry(struct vbsf_super_info *pSuperInfo, struct vbsf_inode_info *sf_i, struct dentry *dentry,
                          SHFLSTRING **result, const char *caller)
{
    int err;
    const char *d_name;
    size_t d_len;
    const char *name;
    size_t len = 0;

    TRACE();
    d_name = dentry->d_name.name;
    d_len = dentry->d_name.len;

    if (pSuperInfo->nls) {
        size_t in_len, i, out_bound_len;
        const char *in;
        char *out;

        in = d_name;
        in_len = d_len;

        out_bound_len = PATH_MAX;
        out = kmalloc(out_bound_len, GFP_KERNEL);
        name = out;

        for (i = 0; i < d_len; ++i) {
            /* We renamed the linux kernel wchar_t type to linux_wchar_t in
               the-linux-kernel.h, as it conflicts with the C++ type of that name. */
            linux_wchar_t uni;
            int nb;

            nb = pSuperInfo->nls->char2uni(in, in_len, &uni);
            if (nb < 0) {
                LogFunc(("nls->char2uni failed %x %d\n",
                     *in, in_len));
                err = -EINVAL;
                goto fail1;
            }
            in_len -= nb;
            in += nb;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
            nb = utf32_to_utf8(uni, out, out_bound_len);
#else
            nb = utf8_wctomb(out, uni, out_bound_len);
#endif
            if (nb < 0) {
                LogFunc(("nls->uni2char failed %x %d\n",
                     uni, out_bound_len));
                err = -EINVAL;
                goto fail1;
            }
            out_bound_len -= nb;
            out += nb;
            len += nb;
        }
        if (len >= PATH_MAX - 1) {
            err = -ENAMETOOLONG;
            goto fail1;
        }

        LogFunc(("result(%d) = %.*s\n", len, len, name));
        *out = 0;
    } else {
        name = d_name;
        len = d_len;
    }

    err = vbsf_make_path(caller, sf_i, name, len, result);
    if (name != d_name)
        kfree(name);

    return err;

 fail1:
    kfree(name);
    return err;
}


/**
 * This is called during name resolution/lookup to check if the @a dentry in the
 * cache is still valid.  The actual validation is job is handled by
 * vbsf_inode_revalidate_worker().
 *
 * @note Caller holds no relevant locks, just a dentry reference.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0)
static int vbsf_dentry_revalidate(struct dentry *dentry, unsigned flags)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0)
static int vbsf_dentry_revalidate(struct dentry *dentry, struct nameidata *nd)
#else
static int vbsf_dentry_revalidate(struct dentry *dentry, int flags)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(3, 6, 0)
    int const flags = nd ? nd->flags : 0;
#endif

    int rc;

    Assert(dentry);
    SFLOGFLOW(("vbsf_dentry_revalidate: %p %#x %s\n", dentry, flags,
               dentry->d_inode ? VBSF_GET_INODE_INFO(dentry->d_inode)->path->String.ach : "<negative>"));

    /*
     * See Documentation/filesystems/vfs.txt why we skip LOOKUP_RCU.
     *
     * Also recommended: https://lwn.net/Articles/649115/
     *                   https://lwn.net/Articles/649729/
     *                   https://lwn.net/Articles/650786/
     *
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
    if (flags & LOOKUP_RCU) {
        rc = -ECHILD;
        SFLOGFLOW(("vbsf_dentry_revalidate: RCU -> -ECHILD\n"));
    } else
#endif
    {
        /*
         * Do we have an inode or not?  If not it's probably a negative cache
         * entry, otherwise most likely a positive one.
         */
        struct inode *pInode = dentry->d_inode;
        if (pInode) {
            /*
             * Positive entry.
             *
             * Note! We're more aggressive here than other remote file systems,
             *       current (4.19) CIFS will for instance revalidate the inode
             *       and ignore the dentry timestamp for positive entries.
             */
            unsigned long const     cJiffiesAge = jiffies - vbsf_dentry_get_update_jiffies(dentry);
            struct vbsf_super_info *pSuperInfo  = VBSF_GET_SUPER_INFO(dentry->d_sb);
            if (cJiffiesAge < pSuperInfo->cJiffiesDirCacheTTL) {
                SFLOGFLOW(("vbsf_dentry_revalidate: age: %lu vs. TTL %lu -> 1\n", cJiffiesAge, pSuperInfo->cJiffiesDirCacheTTL));
                rc = 1;
            } else if (!vbsf_inode_revalidate_worker(dentry, true /*fForced*/, false /*fInodeLocked*/)) {
                vbsf_dentry_set_update_jiffies(dentry, jiffies);
                SFLOGFLOW(("vbsf_dentry_revalidate: age: %lu vs. TTL %lu -> reval -> 1\n", cJiffiesAge, pSuperInfo->cJiffiesDirCacheTTL));
                rc = 1;
            } else {
                SFLOGFLOW(("vbsf_dentry_revalidate: age: %lu vs. TTL %lu -> reval -> 0\n", cJiffiesAge, pSuperInfo->cJiffiesDirCacheTTL));
                rc = 0;
            }
        } else {
            /*
             * Negative entry.
             *
             * Invalidate dentries for open and renames here as we'll revalidate
             * these when taking the actual action (also good for case preservation
             * if we do case-insensitive mounts against windows + mac hosts at some
             * later point).
             */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
            if (flags & (LOOKUP_CREATE | LOOKUP_RENAME_TARGET))
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 75)
            if (flags & LOOKUP_CREATE)
#else
            if (0)
#endif
            {
                SFLOGFLOW(("vbsf_dentry_revalidate: negative: create or rename target -> 0\n"));
                rc = 0;
            } else {
                /* Can we skip revalidation based on TTL? */
                unsigned long const     cJiffiesAge = vbsf_dentry_get_update_jiffies(dentry) - jiffies;
                struct vbsf_super_info *pSuperInfo  = VBSF_GET_SUPER_INFO(dentry->d_sb);
                if (cJiffiesAge < pSuperInfo->cJiffiesDirCacheTTL) {
                    SFLOGFLOW(("vbsf_dentry_revalidate: negative: age: %lu vs. TTL %lu -> 1\n", cJiffiesAge, pSuperInfo->cJiffiesDirCacheTTL));
                    rc = 1;
                } else {
                    /* We could revalidate it here, but we could instead just
                       have the caller kick it out. */
                    /** @todo stat the direntry and see if it exists now. */
                    SFLOGFLOW(("vbsf_dentry_revalidate: negative: age: %lu vs. TTL %lu -> 0\n", cJiffiesAge, pSuperInfo->cJiffiesDirCacheTTL));
                    rc = 0;
                }
            }
        }
    }
    return rc;
}

#ifdef SFLOG_ENABLED

/** For logging purposes only. */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
static int vbsf_dentry_delete(const struct dentry *pDirEntry)
# else
static int vbsf_dentry_delete(struct dentry *pDirEntry)
# endif
{
    SFLOGFLOW(("vbsf_dentry_delete: %p\n", pDirEntry));
    return 0;
}

# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
/** For logging purposes only. */
static int vbsf_dentry_init(struct dentry *pDirEntry)
{
    SFLOGFLOW(("vbsf_dentry_init: %p\n", pDirEntry));
    return 0;
}
# endif

#endif /* SFLOG_ENABLED */

/**
 * Directory entry operations.
 *
 * Since 2.6.38 this is used via the super_block::s_d_op member.
 */
struct dentry_operations vbsf_dentry_ops = {
    .d_revalidate = vbsf_dentry_revalidate,
#ifdef SFLOG_ENABLED
    .d_delete = vbsf_dentry_delete,
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
    .d_init = vbsf_dentry_init,
# endif
#endif
};

