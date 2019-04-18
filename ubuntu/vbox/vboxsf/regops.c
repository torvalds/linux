/* $Id: regops.c $ */
/** @file
 * vboxsf - VBox Linux Shared Folders VFS, regular file inode and file operations.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "vfsmod.h"
#include <linux/uio.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 32)
# include <linux/aio.h> /* struct kiocb before 4.1 */
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 12)
# include <linux/buffer_head.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 12) \
 && LINUX_VERSION_CODE <  KERNEL_VERSION(2, 6, 31)
# include <linux/writeback.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23) \
 && LINUX_VERSION_CODE <  KERNEL_VERSION(3, 16, 0)
# include <linux/splice.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 17) \
 && LINUX_VERSION_CODE <  KERNEL_VERSION(2, 6, 23)
# include <linux/pipe_fs_i.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 10)
# include <linux/swap.h> /* for mark_page_accessed */
#endif
#include <iprt/err.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18)
# define SEEK_END 2
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
# define iter_is_iovec(a_pIter) ( !((a_pIter)->type & ITER_KVEC) )
#elif LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
# define iter_is_iovec(a_pIter) ( !((a_pIter)->type & (ITER_KVEC | ITER_BVEC)) )
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 17, 0)
# define vm_fault_t int
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 20)
# define pgoff_t    unsigned long
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 12)
# define PageUptodate(a_pPage) Page_Uptodate(a_pPage)
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
struct vbsf_iov_iter {
    unsigned int        type;
    unsigned int        v_write : 1;
    size_t              iov_offset;
    size_t              nr_segs;
    struct iovec const *iov;
# ifdef VBOX_STRICT
    struct iovec const *iov_org;
    size_t              nr_segs_org;
# endif
};
# ifdef VBOX_STRICT
#  define VBSF_IOV_ITER_INITIALIZER(a_cSegs, a_pIov, a_fWrite) \
    { vbsf_iov_iter_detect_type(a_pIov, a_cSegs), a_fWrite, 0, a_cSegs, a_pIov, a_pIov, a_cSegs }
# else
#  define VBSF_IOV_ITER_INITIALIZER(a_cSegs, a_pIov, a_fWrite) \
    { vbsf_iov_iter_detect_type(a_pIov, a_cSegs), a_fWrite, 0, a_cSegs, a_pIov }
# endif
# define ITER_KVEC 1
# define iov_iter vbsf_iov_iter
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
/** Used by vbsf_iter_lock_pages() to keep the first page of the next segment. */
struct vbsf_iter_stash {
    struct page    *pPage;
    size_t          off;
    size_t          cb;
# if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
    size_t          offFromEnd;
    struct iov_iter Copy;
# endif
};
#endif /* >= 3.16.0 */
/** Initializer for struct vbsf_iter_stash. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
# define VBSF_ITER_STASH_INITIALIZER    { NULL, 0 }
#else
# define VBSF_ITER_STASH_INITIALIZER    { NULL, 0, ~(size_t)0 }
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
DECLINLINE(void) vbsf_put_page(struct page *pPage);
static void vbsf_unlock_user_pages(struct page **papPages, size_t cPages, bool fSetDirty, bool fLockPgHack);
static void vbsf_reg_write_sync_page_cache(struct address_space *mapping, loff_t offFile, uint32_t cbRange,
                                           uint8_t const *pbSrcBuf, struct page **papSrcPages,
                                           uint32_t offSrcPage, size_t cSrcPages);


/*********************************************************************************************************************************
*   Provide more recent uio.h functionality to older kernels.                                                                    *
*********************************************************************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0) && LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)

/**
 * Detects the vector type.
 */
static int vbsf_iov_iter_detect_type(struct iovec const *paIov, size_t cSegs)
{
    /* Check the first segment with a non-zero length. */
    while (cSegs-- > 0) {
        if (paIov->iov_len > 0) {
            if (access_ok(VERIFY_READ, paIov->iov_base, paIov->iov_len))
                return (uintptr_t)paIov->iov_base >= USER_DS.seg ? ITER_KVEC : 0;
            AssertMsgFailed(("%p LB %#zx\n", paIov->iov_base, paIov->iov_len));
            break;
        }
        paIov++;
    }
    return 0;
}


# undef  iov_iter_count
# define iov_iter_count(a_pIter)                vbsf_iov_iter_count(a_pIter)
static size_t vbsf_iov_iter_count(struct vbsf_iov_iter const *iter)
{
    size_t              cbRet = 0;
    size_t              cLeft = iter->nr_segs;
    struct iovec const *iov   = iter->iov;
    while (cLeft-- > 0) {
        cbRet += iov->iov_len;
        iov++;
    }
    return cbRet - iter->iov_offset;
}


# undef  iov_iter_single_seg_count
# define iov_iter_single_seg_count(a_pIter)     vbsf_iov_iter_single_seg_count(a_pIter)
static size_t vbsf_iov_iter_single_seg_count(struct vbsf_iov_iter const *iter)
{
    if (iter->nr_segs > 0)
        return iter->iov->iov_len - iter->iov_offset;
    return 0;
}


# undef  iov_iter_advance
# define iov_iter_advance(a_pIter, a_cbSkip)    vbsf_iov_iter_advance(a_pIter, a_cbSkip)
static void vbsf_iov_iter_advance(struct vbsf_iov_iter *iter, size_t cbSkip)
{
    SFLOG2(("vbsf_iov_iter_advance: cbSkip=%#zx\n", cbSkip));
    if (iter->nr_segs > 0) {
        size_t const cbLeftCur = iter->iov->iov_len - iter->iov_offset;
        Assert(iter->iov_offset <= iter->iov->iov_len);
        if (cbLeftCur > cbSkip) {
            iter->iov_offset += cbSkip;
        } else {
            cbSkip -= cbLeftCur;
            iter->iov_offset = 0;
            iter->iov++;
            iter->nr_segs--;
            while (iter->nr_segs > 0) {
                size_t const cbSeg = iter->iov->iov_len;
                if (cbSeg > cbSkip) {
                    iter->iov_offset = cbSkip;
                    break;
                }
                cbSkip -= cbSeg;
                iter->iov++;
                iter->nr_segs--;
            }
        }
    }
}


# undef  iov_iter_get_pages
# define iov_iter_get_pages(a_pIter, a_papPages, a_cbMax, a_cMaxPages, a_poffPg0) \
    vbsf_iov_iter_get_pages(a_pIter, a_papPages, a_cbMax, a_cMaxPages, a_poffPg0)
static ssize_t vbsf_iov_iter_get_pages(struct vbsf_iov_iter *iter, struct page **papPages,
                                       size_t cbMax, unsigned cMaxPages, size_t *poffPg0)
{
    while (iter->nr_segs > 0) {
        size_t const cbLeft = iter->iov->iov_len - iter->iov_offset;
        Assert(iter->iov->iov_len >= iter->iov_offset);
        if (cbLeft > 0) {
            uintptr_t           uPtrFrom   = (uintptr_t)iter->iov->iov_base + iter->iov_offset;
            size_t              offPg0     = *poffPg0 = uPtrFrom & PAGE_OFFSET_MASK;
            size_t              cPagesLeft = RT_ALIGN_Z(offPg0 + cbLeft, PAGE_SIZE) >> PAGE_SHIFT;
            size_t              cPages     = RT_MIN(cPagesLeft, cMaxPages);
            struct task_struct *pTask      = current;
            size_t              cPagesLocked;

            down_read(&pTask->mm->mmap_sem);
            cPagesLocked = get_user_pages(pTask, pTask->mm, uPtrFrom, cPages, iter->v_write, 1 /*force*/, papPages, NULL);
            up_read(&pTask->mm->mmap_sem);
            if (cPagesLocked == cPages) {
                size_t cbRet = (cPages << PAGE_SHIFT) - offPg0;
                if (cPages == cPagesLeft) {
                    size_t offLastPg = (uPtrFrom + cbLeft) & PAGE_OFFSET_MASK;
                    if (offLastPg)
                        cbRet -= PAGE_SIZE - offLastPg;
                }
                Assert(cbRet <= cbLeft);
                return cbRet;
            }
            if (cPagesLocked > 0)
                vbsf_unlock_user_pages(papPages, cPagesLocked, false /*fSetDirty*/, false /*fLockPgHack*/);
            return -EFAULT;
        }
        iter->iov_offset = 0;
        iter->iov++;
        iter->nr_segs--;
    }
    AssertFailed();
    return 0;
}


# undef  iov_iter_truncate
# define iov_iter_truncate(iter, cbNew)         vbsf_iov_iter_truncate(iter, cbNew)
static void vbsf_iov_iter_truncate(struct vbsf_iov_iter *iter, size_t cbNew)
{
    /* we have no counter or stuff, so it's a no-op. */
    RT_NOREF(iter, cbNew);
}


# undef  iov_iter_revert
# define iov_iter_revert(a_pIter, a_cbRewind) vbsf_iov_iter_revert(a_pIter, a_cbRewind)
void vbsf_iov_iter_revert(struct vbsf_iov_iter *iter, size_t cbRewind)
{
    SFLOG2(("vbsf_iov_iter_revert: cbRewind=%#zx\n", cbRewind));
    if (iter->iov_offset > 0) {
        if (cbRewind <= iter->iov_offset) {
            iter->iov_offset -= cbRewind;
            return;
        }
        cbRewind -= iter->iov_offset;
        iter->iov_offset = 0;
    }

    while (cbRewind > 0) {
        struct iovec const *pIov  = --iter->iov;
        size_t const        cbSeg = pIov->iov_len;
        iter->nr_segs++;

        Assert((uintptr_t)pIov >= (uintptr_t)iter->iov_org);
        Assert(iter->nr_segs <= iter->nr_segs_org);

        if (cbRewind <= cbSeg) {
            iter->iov_offset = cbSeg - cbRewind;
            break;
        }
        cbRewind -= cbSeg;
    }
}

#endif /* 2.6.19 <= linux < 3.16.0 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0) && LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)

/** This is for implementing cMaxPage on 3.16 which doesn't have it. */
static ssize_t vbsf_iov_iter_get_pages_3_16(struct iov_iter *iter, struct page **papPages,
                                            size_t cbMax, unsigned cMaxPages, size_t *poffPg0)
{
    if (!(iter->type & ITER_BVEC)) {
        size_t const offPg0     = iter->iov_offset & PAGE_OFFSET_MASK;
        size_t const cbMaxPages = ((size_t)cMaxPages << PAGE_SHIFT) - offPg0;
        if (cbMax > cbMaxPages)
            cbMax = cbMaxPages;
    }
    /* else: BVEC works a page at a time and shouldn't have much of a problem here. */
    return iov_iter_get_pages(iter, papPages, cbMax, poffPg0);
}
# undef  iov_iter_get_pages
# define iov_iter_get_pages(a_pIter, a_papPages, a_cbMax, a_cMaxPages, a_poffPg0) \
    vbsf_iov_iter_get_pages_3_16(a_pIter, a_papPages, a_cbMax, a_cMaxPages, a_poffPg0)

#endif /* 3.16.x */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19) && LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)

static size_t copy_from_iter(uint8_t *pbDst, size_t cbToCopy, struct iov_iter *pSrcIter)
{
    size_t const cbTotal = cbToCopy;
    Assert(iov_iter_count(pSrcIter) >= cbToCopy);
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    if (pSrcIter->type & ITER_BVEC) {
        while (cbToCopy > 0) {
            size_t const offPage    = (uintptr_t)pbDst & PAGE_OFFSET_MASK;
            size_t const cbThisCopy = RT_MIN(PAGE_SIZE - offPage, cbToCopy);
            struct page *pPage      = rtR0MemObjLinuxVirtToPage(pbDst);
            size_t       cbCopied   = copy_page_from_iter(pPage, offPage, cbThisCopy, pSrcIter);
            AssertStmt(cbCopied <= cbThisCopy, cbCopied = cbThisCopy);
            pbDst    += cbCopied;
            cbToCopy -= cbCopied;
            if (cbCopied != cbToCopy)
                break;
        }
    } else
# endif
    {
        while (cbToCopy > 0) {
            size_t cbThisCopy = iov_iter_single_seg_count(pSrcIter);
            if (cbThisCopy > 0) {
                if (cbThisCopy > cbToCopy)
                    cbThisCopy = cbToCopy;
                if (pSrcIter->type & ITER_KVEC)
                    memcpy(pbDst, (void *)pSrcIter->iov->iov_base + pSrcIter->iov_offset, cbThisCopy);
                else if (!copy_from_user(pbDst, pSrcIter->iov->iov_base + pSrcIter->iov_offset, cbThisCopy))
                    break;
                pbDst    += cbThisCopy;
                cbToCopy -= cbThisCopy;
            }
            iov_iter_advance(pSrcIter, cbThisCopy);
        }
    }
    return cbTotal - cbToCopy;
}


static size_t copy_to_iter(uint8_t const *pbSrc, size_t cbToCopy, struct iov_iter *pDstIter)
{
    size_t const cbTotal = cbToCopy;
    Assert(iov_iter_count(pDstIter) >= cbToCopy);
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    if (pDstIter->type & ITER_BVEC) {
        while (cbToCopy > 0) {
            size_t const offPage    = (uintptr_t)pbSrc & PAGE_OFFSET_MASK;
            size_t const cbThisCopy = RT_MIN(PAGE_SIZE - offPage, cbToCopy);
            struct page *pPage      = rtR0MemObjLinuxVirtToPage((void *)pbSrc);
            size_t       cbCopied   = copy_page_to_iter(pPage, offPage, cbThisCopy, pDstIter);
            AssertStmt(cbCopied <= cbThisCopy, cbCopied = cbThisCopy);
            pbSrc    += cbCopied;
            cbToCopy -= cbCopied;
            if (cbCopied != cbToCopy)
                break;
        }
    } else
# endif
    {
        while (cbToCopy > 0) {
            size_t cbThisCopy = iov_iter_single_seg_count(pDstIter);
            if (cbThisCopy > 0) {
                if (cbThisCopy > cbToCopy)
                    cbThisCopy = cbToCopy;
                if (pDstIter->type & ITER_KVEC)
                    memcpy((void *)pDstIter->iov->iov_base + pDstIter->iov_offset, pbSrc, cbThisCopy);
                else if (!copy_to_user(pDstIter->iov->iov_base + pDstIter->iov_offset, pbSrc, cbThisCopy)) {
                    break;
                }
                pbSrc    += cbThisCopy;
                cbToCopy -= cbThisCopy;
            }
            iov_iter_advance(pDstIter, cbThisCopy);
        }
    }
    return cbTotal - cbToCopy;
}

#endif /* 3.16.0 <= linux < 3.18.0 */



/*********************************************************************************************************************************
*   Handle management                                                                                                            *
*********************************************************************************************************************************/

/**
 * Called when an inode is released to unlink all handles that might impossibly
 * still be associated with it.
 *
 * @param   pInodeInfo  The inode which handles to drop.
 */
void vbsf_handle_drop_chain(struct vbsf_inode_info *pInodeInfo)
{
    struct vbsf_handle *pCur, *pNext;
    unsigned long     fSavedFlags;
    SFLOGFLOW(("vbsf_handle_drop_chain: %p\n", pInodeInfo));
    spin_lock_irqsave(&g_SfHandleLock, fSavedFlags);

    RTListForEachSafe(&pInodeInfo->HandleList, pCur, pNext, struct vbsf_handle, Entry) {
        AssertMsg(   (pCur->fFlags & (VBSF_HANDLE_F_MAGIC_MASK | VBSF_HANDLE_F_ON_LIST))
                  ==                 (VBSF_HANDLE_F_MAGIC      | VBSF_HANDLE_F_ON_LIST), ("%p %#x\n", pCur, pCur->fFlags));
        pCur->fFlags |= VBSF_HANDLE_F_ON_LIST;
        RTListNodeRemove(&pCur->Entry);
    }

    spin_unlock_irqrestore(&g_SfHandleLock, fSavedFlags);
}


/**
 * Locates a handle that matches all the flags in @a fFlags.
 *
 * @returns Pointer to handle on success (retained), use vbsf_handle_release() to
 *          release it.  NULL if no suitable handle was found.
 * @param   pInodeInfo  The inode info to search.
 * @param   fFlagsSet   The flags that must be set.
 * @param   fFlagsClear The flags that must be clear.
 */
struct vbsf_handle *vbsf_handle_find(struct vbsf_inode_info *pInodeInfo, uint32_t fFlagsSet, uint32_t fFlagsClear)
{
    struct vbsf_handle *pCur;
    unsigned long     fSavedFlags;
    spin_lock_irqsave(&g_SfHandleLock, fSavedFlags);

    RTListForEach(&pInodeInfo->HandleList, pCur, struct vbsf_handle, Entry) {
        AssertMsg(   (pCur->fFlags & (VBSF_HANDLE_F_MAGIC_MASK | VBSF_HANDLE_F_ON_LIST))
                  ==                 (VBSF_HANDLE_F_MAGIC      | VBSF_HANDLE_F_ON_LIST), ("%p %#x\n", pCur, pCur->fFlags));
        if ((pCur->fFlags & (fFlagsSet | fFlagsClear)) == fFlagsSet) {
            uint32_t cRefs = ASMAtomicIncU32(&pCur->cRefs);
            if (cRefs > 1) {
                spin_unlock_irqrestore(&g_SfHandleLock, fSavedFlags);
                SFLOGFLOW(("vbsf_handle_find: returns %p\n", pCur));
                return pCur;
            }
            /* Oops, already being closed (safe as it's only ever increased here). */
            ASMAtomicDecU32(&pCur->cRefs);
        }
    }

    spin_unlock_irqrestore(&g_SfHandleLock, fSavedFlags);
    SFLOGFLOW(("vbsf_handle_find: returns NULL!\n"));
    return NULL;
}


/**
 * Slow worker for vbsf_handle_release() that does the freeing.
 *
 * @returns 0 (ref count).
 * @param   pHandle     The handle to release.
 * @param   pSuperInfo  The info structure for the shared folder associated with
 *                      the handle.
 * @param   pszCaller   The caller name (for logging failures).
 */
uint32_t vbsf_handle_release_slow(struct vbsf_handle *pHandle, struct vbsf_super_info *pSuperInfo, const char *pszCaller)
{
    int rc;
    unsigned long fSavedFlags;

    SFLOGFLOW(("vbsf_handle_release_slow: %p (%s)\n", pHandle, pszCaller));

    /*
     * Remove from the list.
     */
    spin_lock_irqsave(&g_SfHandleLock, fSavedFlags);

    AssertMsg((pHandle->fFlags & VBSF_HANDLE_F_MAGIC_MASK) == VBSF_HANDLE_F_MAGIC, ("%p %#x\n", pHandle, pHandle->fFlags));
    Assert(pHandle->pInodeInfo);
    Assert(pHandle->pInodeInfo && pHandle->pInodeInfo->u32Magic == SF_INODE_INFO_MAGIC);

    if (pHandle->fFlags & VBSF_HANDLE_F_ON_LIST) {
        pHandle->fFlags &= ~VBSF_HANDLE_F_ON_LIST;
        RTListNodeRemove(&pHandle->Entry);
    }

    spin_unlock_irqrestore(&g_SfHandleLock, fSavedFlags);

    /*
     * Actually destroy it.
     */
    rc = VbglR0SfHostReqCloseSimple(pSuperInfo->map.root, pHandle->hHost);
    if (RT_FAILURE(rc))
        LogFunc(("Caller %s: VbglR0SfHostReqCloseSimple %#RX64 failed with rc=%Rrc\n", pszCaller, pHandle->hHost, rc));
    pHandle->hHost  = SHFL_HANDLE_NIL;
    pHandle->fFlags = VBSF_HANDLE_F_MAGIC_DEAD;
    kfree(pHandle);
    return 0;
}


/**
 * Appends a handle to a handle list.
 *
 * @param   pInodeInfo          The inode to add it to.
 * @param   pHandle             The handle to add.
 */
void vbsf_handle_append(struct vbsf_inode_info *pInodeInfo, struct vbsf_handle *pHandle)
{
#ifdef VBOX_STRICT
    struct vbsf_handle *pCur;
#endif
    unsigned long fSavedFlags;

    SFLOGFLOW(("vbsf_handle_append: %p (to %p)\n", pHandle, pInodeInfo));
    AssertMsg((pHandle->fFlags & (VBSF_HANDLE_F_MAGIC_MASK | VBSF_HANDLE_F_ON_LIST)) == VBSF_HANDLE_F_MAGIC,
              ("%p %#x\n", pHandle, pHandle->fFlags));
    Assert(pInodeInfo->u32Magic == SF_INODE_INFO_MAGIC);

    spin_lock_irqsave(&g_SfHandleLock, fSavedFlags);

    AssertMsg((pHandle->fFlags & (VBSF_HANDLE_F_MAGIC_MASK | VBSF_HANDLE_F_ON_LIST)) == VBSF_HANDLE_F_MAGIC,
          ("%p %#x\n", pHandle, pHandle->fFlags));
#ifdef VBOX_STRICT
    RTListForEach(&pInodeInfo->HandleList, pCur, struct vbsf_handle, Entry) {
        Assert(pCur != pHandle);
        AssertMsg(   (pCur->fFlags & (VBSF_HANDLE_F_MAGIC_MASK | VBSF_HANDLE_F_ON_LIST))
                  ==                  (VBSF_HANDLE_F_MAGIC     | VBSF_HANDLE_F_ON_LIST), ("%p %#x\n", pCur, pCur->fFlags));
    }
    pHandle->pInodeInfo = pInodeInfo;
#endif

    pHandle->fFlags |= VBSF_HANDLE_F_ON_LIST;
    RTListAppend(&pInodeInfo->HandleList, &pHandle->Entry);

    spin_unlock_irqrestore(&g_SfHandleLock, fSavedFlags);
}



/*********************************************************************************************************************************
*   Misc                                                                                                                         *
*********************************************************************************************************************************/

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 6)
/** Any writable mappings? */
DECLINLINE(bool) mapping_writably_mapped(struct address_space const *mapping)
{
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 6)
    return !list_empty(&mapping->i_mmap_shared);
# else
    return mapping->i_mmap_shared != NULL;
# endif
}
#endif


#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 12)
/** Missing in 2.4.x, so just stub it for now. */
DECLINLINE(bool) PageWriteback(struct page const *page)
{
    return false;
}
#endif


/**
 * Helper for deciding wheter we should do a read via the page cache or not.
 *
 * By default we will only use the page cache if there is a writable memory
 * mapping of the file with a chance that it may have modified any of the pages
 * already.
 */
DECLINLINE(bool) vbsf_should_use_cached_read(struct file *file, struct address_space *mapping, struct vbsf_super_info *pSuperInfo)
{
    if (   (file->f_flags & O_DIRECT)
        || pSuperInfo->enmCacheMode == kVbsfCacheMode_None)
        return false;
    if (   pSuperInfo->enmCacheMode == kVbsfCacheMode_Read
        || pSuperInfo->enmCacheMode == kVbsfCacheMode_ReadWrite)
        return true;
    Assert(pSuperInfo->enmCacheMode == kVbsfCacheMode_Strict);
    return mapping
        && mapping->nrpages > 0
        && mapping_writably_mapped(mapping);
}



/*********************************************************************************************************************************
*   Pipe / splice stuff mainly for 2.6.17 >= linux < 2.6.31 (where no fallbacks were available)                                  *
*********************************************************************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 17) \
 && LINUX_VERSION_CODE <  KERNEL_VERSION(3, 16, 0)

# if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 30)
#  define LOCK_PIPE(a_pPipe)   do { if ((a_pPipe)->inode) mutex_lock(&(a_pPipe)->inode->i_mutex); } while (0)
#  define UNLOCK_PIPE(a_pPipe) do { if ((a_pPipe)->inode) mutex_unlock(&(a_pPipe)->inode->i_mutex); } while (0)
# else
#  define LOCK_PIPE(a_pPipe)   pipe_lock(a_pPipe)
#  define UNLOCK_PIPE(a_pPipe) pipe_unlock(a_pPipe)
# endif


/** Waits for the pipe buffer status to change. */
static void vbsf_wait_pipe(struct pipe_inode_info *pPipe)
{
    DEFINE_WAIT(WaitStuff);
# ifdef TASK_NONINTERACTIVE
    prepare_to_wait(&pPipe->wait, &WaitStuff, TASK_INTERRUPTIBLE | TASK_NONINTERACTIVE);
# else
    prepare_to_wait(&pPipe->wait, &WaitStuff, TASK_INTERRUPTIBLE);
# endif
    UNLOCK_PIPE(pPipe);

    schedule();

    finish_wait(&pPipe->wait, &WaitStuff);
    LOCK_PIPE(pPipe);
}


/** Worker for vbsf_feed_pages_to_pipe that wakes up readers. */
static void vbsf_wake_up_pipe(struct pipe_inode_info *pPipe, bool fReaders)
{
    smp_mb();
    if (waitqueue_active(&pPipe->wait))
        wake_up_interruptible_sync(&pPipe->wait);
    if (fReaders)
        kill_fasync(&pPipe->fasync_readers, SIGIO, POLL_IN);
    else
        kill_fasync(&pPipe->fasync_writers, SIGIO, POLL_OUT);
}

#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 17) \
 && LINUX_VERSION_CODE <  KERNEL_VERSION(2, 6, 31)

/** Verify pipe buffer content (needed for page-cache to ensure idle page). */
static int vbsf_pipe_buf_confirm(struct pipe_inode_info *pPipe, struct pipe_buffer *pPipeBuf)
{
    /*SFLOG3(("vbsf_pipe_buf_confirm: %p\n", pPipeBuf));*/
    return 0;
}


/** Maps the buffer page. */
static void *vbsf_pipe_buf_map(struct pipe_inode_info *pPipe, struct pipe_buffer *pPipeBuf, int atomic)
{
    void *pvRet;
    if (!atomic)
        pvRet = kmap(pPipeBuf->page);
    else {
        pPipeBuf->flags |= PIPE_BUF_FLAG_ATOMIC;
        pvRet = kmap_atomic(pPipeBuf->page, KM_USER0);
    }
    /*SFLOG3(("vbsf_pipe_buf_map: %p -> %p\n", pPipeBuf, pvRet));*/
    return pvRet;
}


/** Unmaps the buffer page. */
static void vbsf_pipe_buf_unmap(struct pipe_inode_info *pPipe, struct pipe_buffer *pPipeBuf, void *pvMapping)
{
    /*SFLOG3(("vbsf_pipe_buf_unmap: %p/%p\n", pPipeBuf, pvMapping)); */
    if (!(pPipeBuf->flags & PIPE_BUF_FLAG_ATOMIC))
        kunmap(pPipeBuf->page);
    else {
        pPipeBuf->flags &= ~PIPE_BUF_FLAG_ATOMIC;
        kunmap_atomic(pvMapping, KM_USER0);
    }
}


/** Gets a reference to the page. */
static void vbsf_pipe_buf_get(struct pipe_inode_info *pPipe, struct pipe_buffer *pPipeBuf)
{
    page_cache_get(pPipeBuf->page);
    /*SFLOG3(("vbsf_pipe_buf_get: %p (return count=%d)\n", pPipeBuf, page_count(pPipeBuf->page)));*/
}


/** Release the buffer page (counter to vbsf_pipe_buf_get). */
static void vbsf_pipe_buf_release(struct pipe_inode_info *pPipe, struct pipe_buffer *pPipeBuf)
{
    /*SFLOG3(("vbsf_pipe_buf_release: %p (incoming count=%d)\n", pPipeBuf, page_count(pPipeBuf->page)));*/
    page_cache_release(pPipeBuf->page);
}


/** Attempt to steal the page.
 * @returns 0 success, 1 on failure.  */
static int vbsf_pipe_buf_steal(struct pipe_inode_info *pPipe, struct pipe_buffer *pPipeBuf)
{
    if (page_count(pPipeBuf->page) == 1) {
        lock_page(pPipeBuf->page);
        SFLOG3(("vbsf_pipe_buf_steal: %p -> 0\n", pPipeBuf));
        return 0;
    }
    SFLOG3(("vbsf_pipe_buf_steal: %p -> 1\n", pPipeBuf));
    return 1;
}


/**
 * Pipe buffer operations for used by vbsf_feed_pages_to_pipe.
 */
static struct pipe_buf_operations vbsf_pipe_buf_ops = {
    .can_merge = 0,
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
    .confirm   = vbsf_pipe_buf_confirm,
# else
    .pin       = vbsf_pipe_buf_confirm,
# endif
    .map       = vbsf_pipe_buf_map,
    .unmap     = vbsf_pipe_buf_unmap,
    .get       = vbsf_pipe_buf_get,
    .release   = vbsf_pipe_buf_release,
    .steal     = vbsf_pipe_buf_steal,
};


/**
 * Feeds the pages to the pipe.
 *
 * Pages given to the pipe are set to NULL in papPages.
 */
static ssize_t vbsf_feed_pages_to_pipe(struct pipe_inode_info *pPipe, struct page **papPages, size_t cPages, uint32_t offPg0,
                                       uint32_t cbActual, unsigned fFlags)
{
    ssize_t cbRet       = 0;
    size_t  iPage       = 0;
    bool    fNeedWakeUp = false;

    LOCK_PIPE(pPipe);
    for (;;) {
        if (   pPipe->readers > 0
            && pPipe->nrbufs < PIPE_BUFFERS) {
            struct pipe_buffer *pPipeBuf   = &pPipe->bufs[(pPipe->curbuf + pPipe->nrbufs) % PIPE_BUFFERS];
            uint32_t const      cbThisPage = RT_MIN(cbActual, PAGE_SIZE - offPg0);
            pPipeBuf->len       = cbThisPage;
            pPipeBuf->offset    = offPg0;
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
            pPipeBuf->private   = 0;
# endif
            pPipeBuf->ops       = &vbsf_pipe_buf_ops;
            pPipeBuf->flags     = fFlags & SPLICE_F_GIFT ? PIPE_BUF_FLAG_GIFT : 0;
            pPipeBuf->page      = papPages[iPage];

            papPages[iPage++] = NULL;
            pPipe->nrbufs++;
            fNeedWakeUp |= pPipe->inode != NULL;
            offPg0 = 0;
            cbRet += cbThisPage;

            /* done? */
            cbActual -= cbThisPage;
            if (!cbActual)
                break;
        } else if (pPipe->readers == 0) {
            SFLOGFLOW(("vbsf_feed_pages_to_pipe: no readers!\n"));
            send_sig(SIGPIPE, current, 0);
            if (cbRet == 0)
                cbRet = -EPIPE;
            break;
        } else if (fFlags & SPLICE_F_NONBLOCK) {
            if (cbRet == 0)
                cbRet = -EAGAIN;
            break;
        } else if (signal_pending(current)) {
            if (cbRet == 0)
                cbRet = -ERESTARTSYS;
            SFLOGFLOW(("vbsf_feed_pages_to_pipe: pending signal! (%zd)\n", cbRet));
            break;
        } else {
            if (fNeedWakeUp) {
                vbsf_wake_up_pipe(pPipe, true /*fReaders*/);
                fNeedWakeUp = 0;
            }
            pPipe->waiting_writers++;
            vbsf_wait_pipe(pPipe);
            pPipe->waiting_writers--;
        }
    }
    UNLOCK_PIPE(pPipe);

    if (fNeedWakeUp)
        vbsf_wake_up_pipe(pPipe, true /*fReaders*/);

    return cbRet;
}


/**
 * For splicing from a file to a pipe.
 */
static ssize_t vbsf_splice_read(struct file *file, loff_t *poffset, struct pipe_inode_info *pipe, size_t len, unsigned int flags)
{
    struct inode           *inode      = VBSF_GET_F_DENTRY(file)->d_inode;
    struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
    ssize_t                 cbRet;

    SFLOGFLOW(("vbsf_splice_read: file=%p poffset=%p{%#RX64} pipe=%p len=%#zx flags=%#x\n", file, poffset, *poffset, pipe, len, flags));
    if (vbsf_should_use_cached_read(file, inode->i_mapping, pSuperInfo)) {
        cbRet = generic_file_splice_read(file, poffset, pipe, len, flags);
    } else {
        /*
         * Create a read request.
         */
        loff_t              offFile = *poffset;
        size_t              cPages  = RT_MIN(RT_ALIGN_Z((offFile & ~PAGE_CACHE_MASK) + len, PAGE_CACHE_SIZE) >> PAGE_CACHE_SHIFT,
                                             PIPE_BUFFERS);
        VBOXSFREADPGLSTREQ *pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ,
                                                                                              PgLst.aPages[cPages]));
        if (pReq) {
            /*
             * Allocate pages.
             */
            struct page *apPages[PIPE_BUFFERS];
            size_t       i;
            pReq->PgLst.offFirstPage = (uint16_t)offFile & (uint16_t)PAGE_OFFSET_MASK;
            cbRet = 0;
            for (i = 0; i < cPages; i++) {
                struct page *pPage;
                apPages[i] = pPage = alloc_page(GFP_USER);
                if (pPage) {
                    pReq->PgLst.aPages[i] = page_to_phys(pPage);
# ifdef VBOX_STRICT
                    ASMMemFill32(kmap(pPage), PAGE_SIZE, UINT32_C(0xdeadbeef));
                    kunmap(pPage);
# endif
                } else {
                    cbRet = -ENOMEM;
                    break;
                }
            }
            if (cbRet == 0) {
                /*
                 * Do the reading.
                 */
                uint32_t const          cbToRead = RT_MIN((cPages << PAGE_SHIFT) - (offFile & PAGE_OFFSET_MASK), len);
                struct vbsf_reg_info   *sf_r     = (struct vbsf_reg_info *)file->private_data;
                int vrc = VbglR0SfHostReqReadPgLst(pSuperInfo->map.root, pReq, sf_r->Handle.hHost, offFile, cbToRead, cPages);
                if (RT_SUCCESS(vrc)) {
                    /*
                     * Get the number of bytes read, jettison the request
                     * and, in case of EOF, any unnecessary pages.
                     */
                    uint32_t cbActual = pReq->Parms.cb32Read.u.value32;
                    AssertStmt(cbActual <= cbToRead, cbActual = cbToRead);
                    SFLOG2(("vbsf_splice_read: read -> %#x bytes @ %#RX64\n", cbActual, offFile));

                    VbglR0PhysHeapFree(pReq);
                    pReq = NULL;

                    /*
                     * Now, feed it to the pipe thingy.
                     * This will take ownership of the all pages no matter what happens.
                     */
                    cbRet = vbsf_feed_pages_to_pipe(pipe, apPages, cPages, offFile & PAGE_OFFSET_MASK, cbActual, flags);
                    if (cbRet > 0)
                        *poffset = offFile + cbRet;
                } else {
                    cbRet = -RTErrConvertToErrno(vrc);
                    SFLOGFLOW(("vbsf_splice_read: Read failed: %Rrc -> %zd\n", vrc, cbRet));
                }
                i = cPages;
            }

            while (i-- > 0)
                if (apPages[i])
                    __free_pages(apPages[i], 0);
            if (pReq)
                VbglR0PhysHeapFree(pReq);
        } else {
            cbRet = -ENOMEM;
        }
    }
    SFLOGFLOW(("vbsf_splice_read: returns %zd (%#zx), *poffset=%#RX64\n", cbRet, cbRet, *poffset));
    return cbRet;
}

#endif /* 2.6.17 <= LINUX_VERSION_CODE < 2.6.31 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 17) \
 && LINUX_VERSION_CODE <  KERNEL_VERSION(3, 16, 0)

/**
 * For splicing from a pipe to a file.
 *
 * Since we can combine buffers and request allocations, this should be faster
 * than the default implementation.
 */
static ssize_t vbsf_splice_write(struct pipe_inode_info *pPipe, struct file *file, loff_t *poffset, size_t len, unsigned int flags)
{
    struct inode           *inode      = VBSF_GET_F_DENTRY(file)->d_inode;
    struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
    ssize_t                 cbRet;

    SFLOGFLOW(("vbsf_splice_write: pPipe=%p file=%p poffset=%p{%#RX64} len=%#zx flags=%#x\n", pPipe, file, poffset, *poffset, len, flags));
    /** @todo later if (false) {
        cbRet = generic_file_splice_write(pPipe, file, poffset, len, flags);
    } else */ {
        /*
         * Prepare a write request.
         */
# ifdef PIPE_BUFFERS
        uint32_t const cMaxPages  = RT_MIN(PIPE_BUFFERS, RT_ALIGN_Z(len, PAGE_SIZE) >> PAGE_SHIFT);
# else
        uint32_t const cMaxPages  = RT_MIN(RT_MAX(RT_MIN(pPipe->buffers, 256), PIPE_DEF_BUFFERS),
                                           RT_ALIGN_Z(len, PAGE_SIZE) >> PAGE_SHIFT);
# endif
        VBOXSFWRITEPGLSTREQ *pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ,
                                                                                                PgLst.aPages[cMaxPages]));
        if (pReq) {
            /*
             * Feed from the pipe.
             */
            struct vbsf_reg_info *sf_r        = (struct vbsf_reg_info *)file->private_data;
            struct address_space *mapping     = inode->i_mapping;
            loff_t                offFile     = *poffset;
            bool                  fNeedWakeUp = false;
            cbRet = 0;

            LOCK_PIPE(pPipe);

            for (;;) {
                unsigned cBufs = pPipe->nrbufs;
                /*SFLOG2(("vbsf_splice_write: nrbufs=%#x curbuf=%#x\n", cBufs, pPipe->curbuf));*/
                if (cBufs) {
                    /*
                     * There is data available.  Write it to the file.
                     */
                    int                 vrc;
                    struct pipe_buffer *pPipeBuf      = &pPipe->bufs[pPipe->curbuf];
                    uint32_t            cPagesToWrite = 1;
                    uint32_t            cbToWrite     = pPipeBuf->len;

                    Assert(pPipeBuf->offset < PAGE_SIZE);
                    Assert(pPipeBuf->offset + pPipeBuf->len <= PAGE_SIZE);

                    pReq->PgLst.offFirstPage = pPipeBuf->offset & PAGE_OFFSET;
                    pReq->PgLst.aPages[0]    = page_to_phys(pPipeBuf->page);

                    /* Add any adjacent page buffers: */
                    while (   cPagesToWrite < cBufs
                           && cPagesToWrite < cMaxPages
                           && ((pReq->PgLst.offFirstPage + cbToWrite) & PAGE_OFFSET_MASK) == 0) {
# ifdef PIPE_BUFFERS
                        struct pipe_buffer *pPipeBuf2 = &pPipe->bufs[(pPipe->curbuf + cPagesToWrite) % PIPE_BUFFERS];
# else
                        struct pipe_buffer *pPipeBuf2 = &pPipe->bufs[(pPipe->curbuf + cPagesToWrite) % pPipe->buffers];
# endif
                        Assert(pPipeBuf2->len <= PAGE_SIZE);
                        Assert(pPipeBuf2->offset < PAGE_SIZE);
                        if (pPipeBuf2->offset != 0)
                            break;
                        pReq->PgLst.aPages[cPagesToWrite] = page_to_phys(pPipeBuf2->page);
                        cbToWrite     += pPipeBuf2->len;
                        cPagesToWrite += 1;
                    }

                    /* Check that we don't have signals pending before we issue the write, as
                       we'll only end up having to cancel the HGCM request 99% of the time: */
                    if (!signal_pending(current)) {
                        struct vbsf_inode_info *sf_i = VBSF_GET_INODE_INFO(inode);
                        vrc = VbglR0SfHostReqWritePgLst(pSuperInfo->map.root, pReq, sf_r->Handle.hHost, offFile,
                                                        cbToWrite, cPagesToWrite);
                        sf_i->ModificationTimeAtOurLastWrite = sf_i->ModificationTime;
                    } else
                        vrc = VERR_INTERRUPTED;
                    if (RT_SUCCESS(vrc)) {
                        /*
                         * Get the number of bytes actually written, update file position
                         * and return value, and advance the pipe buffer.
                         */
                        uint32_t cbActual = pReq->Parms.cb32Write.u.value32;
                        AssertStmt(cbActual <= cbToWrite, cbActual = cbToWrite);
                        SFLOG2(("vbsf_splice_write: write -> %#x bytes @ %#RX64\n", cbActual, offFile));

                        cbRet += cbActual;

                        while (cbActual > 0) {
                            uint32_t cbAdvance = RT_MIN(pPipeBuf->len, cbActual);

                            vbsf_reg_write_sync_page_cache(mapping, offFile, cbAdvance, NULL,
                                                           &pPipeBuf->page, pPipeBuf->offset, 1);

                            offFile          += cbAdvance;
                            cbActual         -= cbAdvance;
                            pPipeBuf->offset += cbAdvance;
                            pPipeBuf->len    -= cbAdvance;

                            if (!pPipeBuf->len) {
                                struct pipe_buf_operations const *pOps = pPipeBuf->ops;
                                pPipeBuf->ops = NULL;
                                pOps->release(pPipe, pPipeBuf);

# ifdef PIPE_BUFFERS
                                pPipe->curbuf  = (pPipe->curbuf + 1) % PIPE_BUFFERS;
# else
                                pPipe->curbuf  = (pPipe->curbuf + 1) % pPipe->buffers;
# endif
                                pPipe->nrbufs -= 1;
                                pPipeBuf = &pPipe->bufs[pPipe->curbuf];

# if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 30)
                                fNeedWakeUp |= pPipe->inode != NULL;
# else
                                fNeedWakeUp = true;
# endif
                            } else {
                                Assert(cbActual == 0);
                                break;
                            }
                        }

                        *poffset = offFile;
                    } else {
                        if (cbRet == 0)
                            cbRet = vrc == VERR_INTERRUPTED ? -ERESTARTSYS : -RTErrConvertToErrno(vrc);
                        SFLOGFLOW(("vbsf_splice_write: Write failed: %Rrc -> %zd (cbRet=%#zx)\n",
                                   vrc, -RTErrConvertToErrno(vrc), cbRet));
                        break;
                    }
                } else {
                    /*
                     * Wait for data to become available, if there is chance that'll happen.
                     */
                    /* Quit if there are no writers (think EOF): */
                    if (pPipe->writers == 0) {
                        SFLOGFLOW(("vbsf_splice_write: No buffers. No writers. The show is done!\n"));
                        break;
                    }

                    /* Quit if if we've written some and no writers waiting on the lock: */
                    if (cbRet > 0 && pPipe->waiting_writers == 0) {
                        SFLOGFLOW(("vbsf_splice_write: No waiting writers, returning what we've got.\n"));
                        break;
                    }

                    /* Quit with EAGAIN if non-blocking: */
                    if (flags & SPLICE_F_NONBLOCK) {
                        if (cbRet == 0)
                            cbRet = -EAGAIN;
                        break;
                    }

                    /* Quit if we've got pending signals: */
                    if (signal_pending(current)) {
                        if (cbRet == 0)
                            cbRet = -ERESTARTSYS;
                        SFLOGFLOW(("vbsf_splice_write: pending signal! (%zd)\n", cbRet));
                        break;
                    }

                    /* Wake up writers before we start waiting: */
                    if (fNeedWakeUp) {
                        vbsf_wake_up_pipe(pPipe, false /*fReaders*/);
                        fNeedWakeUp = false;
                    }
                    vbsf_wait_pipe(pPipe);
                }
            } /* feed loop */

            if (fNeedWakeUp)
                vbsf_wake_up_pipe(pPipe, false /*fReaders*/);

            UNLOCK_PIPE(pPipe);

            VbglR0PhysHeapFree(pReq);
        } else {
            cbRet = -ENOMEM;
        }
    }
    SFLOGFLOW(("vbsf_splice_write: returns %zd (%#zx), *poffset=%#RX64\n", cbRet, cbRet, *poffset));
    return cbRet;
}

#endif /* 2.6.17 <= LINUX_VERSION_CODE < 3.16.0 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 30) \
 && LINUX_VERSION_CODE <  KERNEL_VERSION(2, 6, 23)
/**
 * Our own senfile implementation that does not go via the page cache like
 * generic_file_sendfile() does.
 */
static ssize_t vbsf_reg_sendfile(struct file *pFile, loff_t *poffFile, size_t cbToSend, read_actor_t pfnActor,
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 8)
                                 void *pvUser
# else
                                 void __user *pvUser
# endif
                                )
{
    struct inode           *inode      = VBSF_GET_F_DENTRY(pFile)->d_inode;
    struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
    ssize_t                 cbRet;
    SFLOGFLOW(("vbsf_reg_sendfile: pFile=%p poffFile=%p{%#RX64} cbToSend=%#zx pfnActor=%p pvUser=%p\n",
               pFile, poffFile, poffFile ? *poffFile : 0, cbToSend, pfnActor, pvUser));
    Assert(pSuperInfo);

    /*
     * Return immediately if asked to send nothing.
     */
    if (cbToSend == 0)
        return 0;

    /*
     * Like for vbsf_reg_read() and vbsf_reg_read_iter(), we allow going via
     * the page cache in some cases or configs.
     */
    if (vbsf_should_use_cached_read(pFile, inode->i_mapping, pSuperInfo)) {
        cbRet = generic_file_sendfile(pFile, poffFile, cbToSend, pfnActor, pvUser);
        SFLOGFLOW(("vbsf_reg_sendfile: returns %#zx *poffFile=%#RX64 [generic_file_sendfile]\n", cbRet, poffFile ? *poffFile : UINT64_MAX));
    } else {
        /*
         * Allocate a request and a bunch of pages for reading from the file.
         */
        struct page        *apPages[16];
        loff_t              offFile = poffFile ? *poffFile : 0;
        size_t const        cPages  = cbToSend + ((size_t)offFile & PAGE_OFFSET_MASK) >= RT_ELEMENTS(apPages) * PAGE_SIZE
                                    ? RT_ELEMENTS(apPages)
                                    : RT_ALIGN_Z(cbToSend + ((size_t)offFile & PAGE_OFFSET_MASK), PAGE_SIZE) >> PAGE_SHIFT;
        size_t              iPage;
        VBOXSFREADPGLSTREQ *pReq    = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ,
                                                                                                 PgLst.aPages[cPages]));
        if (pReq) {
            Assert(cPages > 0);
            cbRet = 0;
            for (iPage = 0; iPage < cPages; iPage++) {
                struct page *pPage;
                apPages[iPage] = pPage = alloc_page(GFP_USER);
                if (pPage) {
                    Assert(page_count(pPage) == 1);
                    pReq->PgLst.aPages[iPage] = page_to_phys(pPage);
                } else {
                    while (iPage-- > 0)
                        vbsf_put_page(apPages[iPage]);
                    cbRet = -ENOMEM;
                    break;
                }
            }
            if (cbRet == 0) {
                /*
                 * Do the job.
                 */
                struct vbsf_reg_info *sf_r = (struct vbsf_reg_info *)pFile->private_data;
                read_descriptor_t     RdDesc;
                RdDesc.count    = cbToSend;
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 8)
                RdDesc.arg.data = pvUser;
# else
                RdDesc.buf      = pvUser;
# endif
                RdDesc.written  = 0;
                RdDesc.error    = 0;

                Assert(sf_r);
                Assert((sf_r->Handle.fFlags & VBSF_HANDLE_F_MAGIC_MASK) == VBSF_HANDLE_F_MAGIC);

                while (cbToSend > 0) {
                    /*
                     * Read another chunk.  For paranoid reasons, we keep data where the page cache
                     * would keep it, i.e. page offset bits corresponds to the file offset bits.
                     */
                    uint32_t const offPg0       = (uint32_t)offFile & (uint32_t)PAGE_OFFSET_MASK;
                    uint32_t const cbToRead     = RT_MIN((cPages << PAGE_SHIFT) - offPg0, cbToSend);
                    uint32_t const cPagesToRead = RT_ALIGN_Z(cbToRead + offPg0, PAGE_SIZE) >> PAGE_SHIFT;
                    int            vrc;
                    pReq->PgLst.offFirstPage = (uint16_t)offPg0;
                    if (!signal_pending(current))
                        vrc = VbglR0SfHostReqReadPgLst(pSuperInfo->map.root, pReq, sf_r->Handle.hHost, offFile,
                                                       cbToRead, cPagesToRead);
                    else
                        vrc = VERR_INTERRUPTED;
                    if (RT_SUCCESS(vrc)) {
                        /*
                         * Pass what we read to the actor.
                         */
                        uint32_t    off      = offPg0;
                        uint32_t    cbActual = pReq->Parms.cb32Read.u.value32;
                        bool const  fIsEof   = cbActual < cbToRead;
                        AssertStmt(cbActual <= cbToRead, cbActual = cbToRead);
                        SFLOG3(("vbsf_reg_sendfile: Read %#x bytes (offPg0=%#x), wanted %#x ...\n", cbActual, offPg0, cbToRead));

                        iPage = 0;
                        while (cbActual > 0) {
                            uint32_t const cbPage     = RT_MIN(cbActual, PAGE_SIZE - off);
                            int const      cbRetActor = pfnActor(&RdDesc, apPages[iPage], off, cbPage);
                            Assert(cbRetActor >= 0); /* Returns zero on failure, with RdDesc.error holding the status code. */

                            AssertMsg(iPage < cPages && iPage < cPagesToRead, ("iPage=%#x cPages=%#x cPagesToRead=%#x\n", iPage, cPages, cPagesToRead));

                            offFile += cbRetActor;
                            if ((uint32_t)cbRetActor == cbPage && RdDesc.count > 0) {
                                cbActual -= cbPage;
                                cbToSend -= cbPage;
                                iPage++;
                            } else {
                                SFLOG3(("vbsf_reg_sendfile: cbRetActor=%#x (%d) cbPage=%#x RdDesc{count=%#lx error=%d} iPage=%#x/%#x/%#x cbToSend=%#zx\n",
                                        cbRetActor, cbRetActor, cbPage, RdDesc.count, RdDesc.error, iPage, cPagesToRead, cPages, cbToSend));
                                vrc = VERR_CALLBACK_RETURN;
                                break;
                            }
                            off = 0;
                        }

                        /*
                         * Are we done yet?
                         */
                        if (RT_FAILURE_NP(vrc) || cbToSend == 0 || RdDesc.error != 0 || fIsEof) {
                            break;
                        }

                        /*
                         * Replace pages held by the actor.
                         */
                        vrc = VINF_SUCCESS;
                        for (iPage = 0; iPage < cPages; iPage++) {
                            struct page *pPage = apPages[iPage];
                            if (page_count(pPage) != 1) {
                                struct page *pNewPage = alloc_page(GFP_USER);
                                if (pNewPage) {
                                    SFLOGFLOW(("vbsf_reg_sendfile: Replacing page #%x: %p -> %p\n", iPage, pPage, pNewPage));
                                    vbsf_put_page(pPage);
                                    apPages[iPage] = pNewPage;
                                } else {
                                    SFLOGFLOW(("vbsf_reg_sendfile: Failed to allocate a replacement page.\n"));
                                    vrc = VERR_NO_MEMORY;
                                    break;
                                }
                            }
                        }
                        if (RT_FAILURE(vrc))
                            break; /* RdDesc.written should be non-zero, so don't bother with setting error. */
                    } else {
                        RdDesc.error = vrc == VERR_INTERRUPTED ? -ERESTARTSYS : -RTErrConvertToErrno(vrc);
                        SFLOGFLOW(("vbsf_reg_sendfile: Read failed: %Rrc -> %zd (RdDesc.error=%#d)\n",
                                   vrc, -RTErrConvertToErrno(vrc), RdDesc.error));
                        break;
                    }
                }

                /*
                 * Free memory.
                 */
                for (iPage = 0; iPage < cPages; iPage++)
                    vbsf_put_page(apPages[iPage]);

                /*
                 * Set the return values.
                 */
                if (RdDesc.written) {
                    cbRet = RdDesc.written;
                    if (poffFile)
                        *poffFile = offFile;
                } else {
                    cbRet = RdDesc.error;
                }
            }
            VbglR0PhysHeapFree(pReq);
        } else {
            cbRet = -ENOMEM;
        }
        SFLOGFLOW(("vbsf_reg_sendfile: returns %#zx offFile=%#RX64\n", cbRet, offFile));
    }
    return cbRet;
}
#endif /* 2.5.30 <= LINUX_VERSION_CODE < 2.6.23 */


/*********************************************************************************************************************************
*   File operations on regular files                                                                                             *
*********************************************************************************************************************************/

/** Wrapper around put_page / page_cache_release.  */
DECLINLINE(void) vbsf_put_page(struct page *pPage)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    put_page(pPage);
#else
    page_cache_release(pPage);
#endif
}


/** Wrapper around get_page / page_cache_get.  */
DECLINLINE(void) vbsf_get_page(struct page *pPage)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    get_page(pPage);
#else
    page_cache_get(pPage);
#endif
}


/** Companion to vbsf_lock_user_pages(). */
DECLINLINE(void) vbsf_unlock_user_pages(struct page **papPages, size_t cPages, bool fSetDirty, bool fLockPgHack)
{
    /* We don't mark kernel pages dirty: */
    if (fLockPgHack)
        fSetDirty = false;

    while (cPages-- > 0)
    {
        struct page *pPage = papPages[cPages];
        Assert((ssize_t)cPages >= 0);
        if (fSetDirty && !PageReserved(pPage))
            set_page_dirty(pPage);
        vbsf_put_page(pPage);
    }
}


/**
 * Worker for vbsf_lock_user_pages_failed_check_kernel() and
 * vbsf_iter_lock_pages().
 */
static int vbsf_lock_kernel_pages(uint8_t *pbStart, bool fWrite, size_t cPages, struct page **papPages)
{
    uintptr_t const uPtrFrom = (uintptr_t)pbStart;
    uintptr_t const uPtrLast = (uPtrFrom & ~(uintptr_t)PAGE_OFFSET_MASK) + (cPages << PAGE_SHIFT) - 1;
    uint8_t        *pbPage   = (uint8_t *)uPtrLast;
    size_t          iPage    = cPages;

    /*
     * Touch the pages first (paranoia^2).
     */
    if (fWrite) {
        uint8_t volatile *pbProbe = (uint8_t volatile *)uPtrFrom;
        while (iPage-- > 0) {
            *pbProbe = *pbProbe;
            pbProbe += PAGE_SIZE;
        }
    } else {
        uint8_t const *pbProbe = (uint8_t const *)uPtrFrom;
        while (iPage-- > 0) {
            ASMProbeReadByte(pbProbe);
            pbProbe += PAGE_SIZE;
        }
    }

    /*
     * Get the pages.
     * Note! Fixes here probably applies to rtR0MemObjNativeLockKernel as well.
     */
    iPage = cPages;
    if (   uPtrFrom >= (unsigned long)__va(0)
        && uPtrLast <  (unsigned long)high_memory) {
        /* The physical page mapping area: */
        while (iPage-- > 0) {
            struct page *pPage = papPages[iPage] = virt_to_page(pbPage);
            vbsf_get_page(pPage);
            pbPage -= PAGE_SIZE;
        }
    } else {
        /* This is vmalloc or some such thing, so go thru page tables: */
        while (iPage-- > 0) {
            struct page *pPage = rtR0MemObjLinuxVirtToPage(pbPage);
            if (pPage) {
                papPages[iPage] = pPage;
                vbsf_get_page(pPage);
                pbPage -= PAGE_SIZE;
            } else {
                while (++iPage < cPages) {
                    pPage = papPages[iPage];
                    vbsf_put_page(pPage);
                }
                return -EFAULT;
            }
        }
    }
    return 0;
}


/**
 * Catches kernel_read() and kernel_write() calls and works around them.
 *
 * The file_operations::read and file_operations::write callbacks supposedly
 * hands us the user buffers to read into and write out of.  To allow the kernel
 * to read and write without allocating buffers in userland, they kernel_read()
 * and kernel_write() increases the user space address limit before calling us
 * so that copyin/copyout won't reject it.  Our problem is that get_user_pages()
 * works on the userspace address space structures and will not be fooled by an
 * increased addr_limit.
 *
 * This code tries to detect this situation and fake get_user_lock() for the
 * kernel buffer.
 */
static int vbsf_lock_user_pages_failed_check_kernel(uintptr_t uPtrFrom, size_t cPages, bool fWrite, int rcFailed,
                                                    struct page **papPages, bool *pfLockPgHack)
{
    /*
     * Check that this is valid user memory that is actually in the kernel range.
     */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
    if (   access_ok((void *)uPtrFrom, cPages << PAGE_SHIFT)
        && uPtrFrom >= USER_DS.seg)
#else
    if (   access_ok(fWrite ? VERIFY_WRITE : VERIFY_READ, (void *)uPtrFrom, cPages << PAGE_SHIFT)
        && uPtrFrom >= USER_DS.seg)
#endif
    {
        int rc = vbsf_lock_kernel_pages((uint8_t *)uPtrFrom, fWrite, cPages, papPages);
        if (rc == 0) {
            *pfLockPgHack = true;
            return 0;
        }
    }

    return rcFailed;
}


/** Wrapper around get_user_pages. */
DECLINLINE(int) vbsf_lock_user_pages(uintptr_t uPtrFrom, size_t cPages, bool fWrite, struct page **papPages, bool *pfLockPgHack)
{
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
    ssize_t cPagesLocked = get_user_pages_unlocked(uPtrFrom, cPages, papPages,
                                                   fWrite ? FOLL_WRITE | FOLL_FORCE : FOLL_FORCE);
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0)
    ssize_t cPagesLocked = get_user_pages_unlocked(uPtrFrom, cPages, fWrite, 1 /*force*/, papPages);
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 168) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)
    ssize_t cPagesLocked = get_user_pages_unlocked(current, current->mm, uPtrFrom, cPages, papPages,
                                                   fWrite ? FOLL_WRITE | FOLL_FORCE : FOLL_FORCE);
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
    ssize_t cPagesLocked = get_user_pages_unlocked(current, current->mm, uPtrFrom, cPages, fWrite, 1 /*force*/, papPages);
# else
    struct task_struct *pTask = current;
    ssize_t cPagesLocked;
    down_read(&pTask->mm->mmap_sem);
    cPagesLocked = get_user_pages(pTask, pTask->mm, uPtrFrom, cPages, fWrite, 1 /*force*/, papPages, NULL);
    up_read(&pTask->mm->mmap_sem);
# endif
    *pfLockPgHack = false;
    if (cPagesLocked == cPages)
        return 0;

    /*
     * It failed.
     */
    if (cPagesLocked < 0)
        return vbsf_lock_user_pages_failed_check_kernel(uPtrFrom, cPages, fWrite, (int)cPagesLocked, papPages, pfLockPgHack);

    vbsf_unlock_user_pages(papPages, cPagesLocked, false /*fSetDirty*/, false /*fLockPgHack*/);

    /* We could use uPtrFrom + cPagesLocked to get the correct status here... */
    return -EFAULT;
}


/**
 * Read function used when accessing files that are memory mapped.
 *
 * We read from the page cache here to present the a cohertent picture of the
 * the file content.
 */
static ssize_t vbsf_reg_read_mapped(struct file *file, char /*__user*/ *buf, size_t size, loff_t *off)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    struct iovec    iov = { .iov_base = buf, .iov_len = size };
    struct iov_iter iter;
    struct kiocb    kiocb;
    ssize_t         cbRet;

    init_sync_kiocb(&kiocb, file);
    kiocb.ki_pos = *off;
    iov_iter_init(&iter, READ, &iov, 1, size);

    cbRet = generic_file_read_iter(&kiocb, &iter);

    *off = kiocb.ki_pos;
    return cbRet;

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    struct iovec    iov = { .iov_base = buf, .iov_len = size };
    struct kiocb    kiocb;
    ssize_t         cbRet;

    init_sync_kiocb(&kiocb, file);
    kiocb.ki_pos = *off;

    cbRet = generic_file_aio_read(&kiocb, &iov, 1, *off);
    if (cbRet == -EIOCBQUEUED)
        cbRet = wait_on_sync_kiocb(&kiocb);

    *off = kiocb.ki_pos;
    return cbRet;

#else /* 2.6.18 or earlier: */
    return generic_file_read(file, buf, size, off);
#endif
}


/**
 * Fallback case of vbsf_reg_read() that locks the user buffers and let the host
 * write directly to them.
 */
static ssize_t vbsf_reg_read_locking(struct file *file, char /*__user*/ *buf, size_t size, loff_t *off,
                                     struct vbsf_super_info *pSuperInfo, struct vbsf_reg_info *sf_r)
{
    /*
     * Lock pages and execute the read, taking care not to pass the host
     * more than it can handle in one go or more than we care to allocate
     * page arrays for.  The latter limit is set at just short of 32KB due
     * to how the physical heap works.
     */
    struct page        *apPagesStack[16];
    struct page       **papPages     = &apPagesStack[0];
    struct page       **papPagesFree = NULL;
    VBOXSFREADPGLSTREQ *pReq;
    loff_t              offFile      = *off;
    ssize_t             cbRet        = -ENOMEM;
    size_t              cPages       = (((uintptr_t)buf & PAGE_OFFSET_MASK) + size + PAGE_OFFSET_MASK) >> PAGE_SHIFT;
    size_t              cMaxPages    = RT_MIN(RT_MAX(pSuperInfo->cMaxIoPages, 1), cPages);
    bool                fLockPgHack;

    pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ, PgLst.aPages[cMaxPages]));
    while (!pReq && cMaxPages > 4) {
        cMaxPages /= 2;
        pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ, PgLst.aPages[cMaxPages]));
    }
    if (pReq && cMaxPages > RT_ELEMENTS(apPagesStack))
        papPagesFree = papPages = kmalloc(cMaxPages * sizeof(sizeof(papPages[0])), GFP_KERNEL);
    if (pReq && papPages) {
        cbRet = 0;
        for (;;) {
            /*
             * Figure out how much to process now and lock the user pages.
             */
            int    rc;
            size_t cbChunk = (uintptr_t)buf & PAGE_OFFSET_MASK;
            pReq->PgLst.offFirstPage = (uint16_t)cbChunk;
            cPages  = RT_ALIGN_Z(cbChunk + size, PAGE_SIZE) >> PAGE_SHIFT;
            if (cPages <= cMaxPages)
                cbChunk = size;
            else {
                cPages  = cMaxPages;
                cbChunk = (cMaxPages << PAGE_SHIFT) - cbChunk;
            }

            rc = vbsf_lock_user_pages((uintptr_t)buf, cPages, true /*fWrite*/, papPages, &fLockPgHack);
            if (rc == 0) {
                size_t iPage = cPages;
                while (iPage-- > 0)
                    pReq->PgLst.aPages[iPage] = page_to_phys(papPages[iPage]);
            } else {
                cbRet = rc;
                break;
            }

            /*
             * Issue the request and unlock the pages.
             */
            rc = VbglR0SfHostReqReadPgLst(pSuperInfo->map.root, pReq, sf_r->Handle.hHost, offFile, cbChunk, cPages);

            Assert(cPages <= cMaxPages);
            vbsf_unlock_user_pages(papPages, cPages, true /*fSetDirty*/, fLockPgHack);

            if (RT_SUCCESS(rc)) {
                /*
                 * Success, advance position and buffer.
                 */
                uint32_t cbActual = pReq->Parms.cb32Read.u.value32;
                AssertStmt(cbActual <= cbChunk, cbActual = cbChunk);
                cbRet   += cbActual;
                offFile += cbActual;
                buf      = (uint8_t *)buf + cbActual;
                size    -= cbActual;

                /*
                 * Are we done already?  If so commit the new file offset.
                 */
                if (!size || cbActual < cbChunk) {
                    *off = offFile;
                    break;
                }
            } else if (rc == VERR_NO_MEMORY && cMaxPages > 4) {
                /*
                 * The host probably doesn't have enough heap to handle the
                 * request, reduce the page count and retry.
                 */
                cMaxPages /= 4;
                Assert(cMaxPages > 0);
            } else {
                /*
                 * If we've successfully read stuff, return it rather than
                 * the error.  (Not sure if this is such a great idea...)
                 */
                if (cbRet > 0) {
                    SFLOGFLOW(("vbsf_reg_read: read at %#RX64 -> %Rrc; got cbRet=%#zx already\n", offFile, rc, cbRet));
                    *off = offFile;
                } else {
                    SFLOGFLOW(("vbsf_reg_read: read at %#RX64 -> %Rrc\n", offFile, rc));
                    cbRet = -EPROTO;
                }
                break;
            }
        }
    }
    if (papPagesFree)
        kfree(papPages);
    if (pReq)
        VbglR0PhysHeapFree(pReq);
    SFLOGFLOW(("vbsf_reg_read: returns %zd (%#zx), *off=%RX64 [lock]\n", cbRet, cbRet, *off));
    return cbRet;
}


/**
 * Read from a regular file.
 *
 * @param file          the file
 * @param buf           the buffer
 * @param size          length of the buffer
 * @param off           offset within the file (in/out).
 * @returns the number of read bytes on success, Linux error code otherwise
 */
static ssize_t vbsf_reg_read(struct file *file, char /*__user*/ *buf, size_t size, loff_t *off)
{
    struct inode            *inode     = VBSF_GET_F_DENTRY(file)->d_inode;
    struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
    struct vbsf_reg_info   *sf_r       = file->private_data;
    struct address_space   *mapping    = inode->i_mapping;

    SFLOGFLOW(("vbsf_reg_read: inode=%p file=%p buf=%p size=%#zx off=%#llx\n", inode, file, buf, size, *off));

    if (!S_ISREG(inode->i_mode)) {
        LogFunc(("read from non regular file %d\n", inode->i_mode));
        return -EINVAL;
    }

    /** @todo XXX Check read permission according to inode->i_mode! */

    if (!size)
        return 0;

    /*
     * If there is a mapping and O_DIRECT isn't in effect, we must at a
     * heed dirty pages in the mapping and read from them.  For simplicity
     * though, we just do page cache reading when there are writable
     * mappings around with any kind of pages loaded.
     */
    if (vbsf_should_use_cached_read(file, mapping, pSuperInfo))
        return vbsf_reg_read_mapped(file, buf, size, off);

    /*
     * For small requests, try use an embedded buffer provided we get a heap block
     * that does not cross page boundraries (see host code).
     */
    if (size <= PAGE_SIZE / 4 * 3 - RT_UOFFSETOF(VBOXSFREADEMBEDDEDREQ, abData[0]) /* see allocator */) {
        uint32_t const         cbReq = RT_UOFFSETOF(VBOXSFREADEMBEDDEDREQ, abData[0]) + size;
        VBOXSFREADEMBEDDEDREQ *pReq  = (VBOXSFREADEMBEDDEDREQ *)VbglR0PhysHeapAlloc(cbReq);
        if (pReq) {
            if ((PAGE_SIZE - ((uintptr_t)pReq & PAGE_OFFSET_MASK)) >= cbReq) {
                ssize_t cbRet;
                int vrc = VbglR0SfHostReqReadEmbedded(pSuperInfo->map.root, pReq, sf_r->Handle.hHost, *off, (uint32_t)size);
                if (RT_SUCCESS(vrc)) {
                    cbRet = pReq->Parms.cb32Read.u.value32;
                    AssertStmt(cbRet <= (ssize_t)size, cbRet = size);
                    if (copy_to_user(buf, pReq->abData, cbRet) == 0)
                        *off += cbRet;
                    else
                        cbRet = -EFAULT;
                } else
                    cbRet = -EPROTO;
                VbglR0PhysHeapFree(pReq);
                SFLOGFLOW(("vbsf_reg_read: returns %zd (%#zx), *off=%RX64 [embed]\n", cbRet, cbRet, *off));
                return cbRet;
            }
            VbglR0PhysHeapFree(pReq);
        }
    }

#if 0 /* Turns out this is slightly slower than locking the pages even for 4KB reads (4.19/amd64). */
    /*
     * For medium sized requests try use a bounce buffer.
     */
    if (size <= _64K /** @todo make this configurable? */) {
        void *pvBounce = kmalloc(size, GFP_KERNEL);
        if (pvBounce) {
            VBOXSFREADPGLSTREQ *pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
            if (pReq) {
                ssize_t cbRet;
                int vrc = VbglR0SfHostReqReadContig(pSuperInfo->map.root, pReq, sf_r->Handle.hHost, *off,
                                                    (uint32_t)size, pvBounce, virt_to_phys(pvBounce));
                if (RT_SUCCESS(vrc)) {
                    cbRet = pReq->Parms.cb32Read.u.value32;
                    AssertStmt(cbRet <= (ssize_t)size, cbRet = size);
                    if (copy_to_user(buf, pvBounce, cbRet) == 0)
                        *off += cbRet;
                    else
                        cbRet = -EFAULT;
                } else
                    cbRet = -EPROTO;
                VbglR0PhysHeapFree(pReq);
                kfree(pvBounce);
                SFLOGFLOW(("vbsf_reg_read: returns %zd (%#zx), *off=%RX64 [bounce]\n", cbRet, cbRet, *off));
                return cbRet;
            }
            kfree(pvBounce);
        }
    }
#endif

    return vbsf_reg_read_locking(file, buf, size, off, pSuperInfo, sf_r);
}


/**
 * Helper the synchronizes the page cache content with something we just wrote
 * to the host.
 */
static void vbsf_reg_write_sync_page_cache(struct address_space *mapping, loff_t offFile, uint32_t cbRange,
                                           uint8_t const *pbSrcBuf, struct page **papSrcPages,
                                           uint32_t offSrcPage, size_t cSrcPages)
{
    Assert(offSrcPage < PAGE_SIZE);
    if (mapping && mapping->nrpages > 0) {
        /*
         * Work the pages in the write range.
         */
        while (cbRange > 0) {
            /*
             * Lookup the page at offFile.  We're fine if there aren't
             * any there.  We're skip if it's dirty or is being written
             * back, at least for now.
             */
            size_t const  offDstPage = offFile & PAGE_OFFSET_MASK;
            size_t const  cbToCopy   = RT_MIN(PAGE_SIZE - offDstPage, cbRange);
            pgoff_t const idxPage    = offFile >> PAGE_SHIFT;
            struct page  *pDstPage   = find_lock_page(mapping, idxPage);
            if (pDstPage) {
                if (   pDstPage->mapping == mapping /* ignore if re-purposed (paranoia) */
                    && pDstPage->index == idxPage
                    && !PageDirty(pDstPage)         /* ignore if dirty */
                    && !PageWriteback(pDstPage)     /* ignore if being written back */ ) {
                    /*
                     * Map the page and do the copying.
                     */
                    uint8_t *pbDst = (uint8_t *)kmap(pDstPage);
                    if (pbSrcBuf)
                        memcpy(&pbDst[offDstPage], pbSrcBuf, cbToCopy);
                    else {
                        uint32_t const cbSrc0 = PAGE_SIZE - offSrcPage;
                        uint8_t const *pbSrc  = (uint8_t const *)kmap(papSrcPages[0]);
                        AssertMsg(cSrcPages >= 1, ("offFile=%#llx cbRange=%#zx cbToCopy=%#zx\n", offFile, cbRange, cbToCopy));
                        memcpy(&pbDst[offDstPage], &pbSrc[offSrcPage], RT_MIN(cbToCopy, cbSrc0));
                        kunmap(papSrcPages[0]);
                        if (cbToCopy > cbSrc0) {
                            AssertMsg(cSrcPages >= 2, ("offFile=%#llx cbRange=%#zx cbToCopy=%#zx\n", offFile, cbRange, cbToCopy));
                            pbSrc = (uint8_t const *)kmap(papSrcPages[1]);
                            memcpy(&pbDst[offDstPage + cbSrc0], pbSrc, cbToCopy - cbSrc0);
                            kunmap(papSrcPages[1]);
                        }
                    }
                    kunmap(pDstPage);
                    flush_dcache_page(pDstPage);
                    if (cbToCopy == PAGE_SIZE)
                        SetPageUptodate(pDstPage);
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 10)
                    mark_page_accessed(pDstPage);
# endif
                } else
                    SFLOGFLOW(("vbsf_reg_write_sync_page_cache: Skipping page %p: mapping=%p (vs %p) writeback=%d offset=%#lx (vs%#lx)\n",
                               pDstPage, pDstPage->mapping, mapping, PageWriteback(pDstPage), pDstPage->index, idxPage));
                unlock_page(pDstPage);
                vbsf_put_page(pDstPage);
            }

            /*
             * Advance.
             */
            if (pbSrcBuf)
                pbSrcBuf += cbToCopy;
            else
            {
                offSrcPage += cbToCopy;
                Assert(offSrcPage < PAGE_SIZE * 2);
                if (offSrcPage >= PAGE_SIZE) {
                    offSrcPage &= PAGE_OFFSET_MASK;
                    papSrcPages++;
# ifdef VBOX_STRICT
                    Assert(cSrcPages > 0);
                    cSrcPages--;
# endif
                }
            }
            offFile += cbToCopy;
            cbRange -= cbToCopy;
        }
    }
    RT_NOREF(cSrcPages);
}


/**
 * Fallback case of vbsf_reg_write() that locks the user buffers and let the host
 * write directly to them.
 */
static ssize_t vbsf_reg_write_locking(struct file *file, const char /*__user*/ *buf, size_t size, loff_t *off, loff_t offFile,
                                      struct inode *inode, struct vbsf_inode_info *sf_i,
                                      struct vbsf_super_info *pSuperInfo, struct vbsf_reg_info *sf_r)
{
    /*
     * Lock pages and execute the write, taking care not to pass the host
     * more than it can handle in one go or more than we care to allocate
     * page arrays for.  The latter limit is set at just short of 32KB due
     * to how the physical heap works.
     */
    struct page         *apPagesStack[16];
    struct page        **papPages     = &apPagesStack[0];
    struct page        **papPagesFree = NULL;
    VBOXSFWRITEPGLSTREQ *pReq;
    ssize_t              cbRet        = -ENOMEM;
    size_t               cPages       = (((uintptr_t)buf & PAGE_OFFSET_MASK) + size + PAGE_OFFSET_MASK) >> PAGE_SHIFT;
    size_t               cMaxPages    = RT_MIN(RT_MAX(pSuperInfo->cMaxIoPages, 1), cPages);
    bool                 fLockPgHack;

    pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFWRITEPGLSTREQ, PgLst.aPages[cMaxPages]));
    while (!pReq && cMaxPages > 4) {
        cMaxPages /= 2;
        pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFWRITEPGLSTREQ, PgLst.aPages[cMaxPages]));
    }
    if (pReq && cMaxPages > RT_ELEMENTS(apPagesStack))
        papPagesFree = papPages = kmalloc(cMaxPages * sizeof(sizeof(papPages[0])), GFP_KERNEL);
    if (pReq && papPages) {
        cbRet = 0;
        for (;;) {
            /*
             * Figure out how much to process now and lock the user pages.
             */
            int    rc;
            size_t cbChunk = (uintptr_t)buf & PAGE_OFFSET_MASK;
            pReq->PgLst.offFirstPage = (uint16_t)cbChunk;
            cPages  = RT_ALIGN_Z(cbChunk + size, PAGE_SIZE) >> PAGE_SHIFT;
            if (cPages <= cMaxPages)
                cbChunk = size;
            else {
                cPages  = cMaxPages;
                cbChunk = (cMaxPages << PAGE_SHIFT) - cbChunk;
            }

            rc = vbsf_lock_user_pages((uintptr_t)buf, cPages, false /*fWrite*/, papPages, &fLockPgHack);
            if (rc == 0) {
                size_t iPage = cPages;
                while (iPage-- > 0)
                    pReq->PgLst.aPages[iPage] = page_to_phys(papPages[iPage]);
            } else {
                cbRet = rc;
                break;
            }

            /*
             * Issue the request and unlock the pages.
             */
            rc = VbglR0SfHostReqWritePgLst(pSuperInfo->map.root, pReq, sf_r->Handle.hHost, offFile, cbChunk, cPages);
            sf_i->ModificationTimeAtOurLastWrite = sf_i->ModificationTime;
            if (RT_SUCCESS(rc)) {
                /*
                 * Success, advance position and buffer.
                 */
                uint32_t cbActual = pReq->Parms.cb32Write.u.value32;
                AssertStmt(cbActual <= cbChunk, cbActual = cbChunk);

                vbsf_reg_write_sync_page_cache(inode->i_mapping, offFile, cbActual, NULL /*pbKrnlBuf*/,
                                               papPages, (uintptr_t)buf & PAGE_OFFSET_MASK, cPages);
                Assert(cPages <= cMaxPages);
                vbsf_unlock_user_pages(papPages, cPages, false /*fSetDirty*/, fLockPgHack);

                cbRet   += cbActual;
                offFile += cbActual;
                buf      = (uint8_t *)buf + cbActual;
                size    -= cbActual;
                if (offFile > i_size_read(inode))
                    i_size_write(inode, offFile);
                sf_i->force_restat = 1; /* mtime (and size) may have changed */

                /*
                 * Are we done already?  If so commit the new file offset.
                 */
                if (!size || cbActual < cbChunk) {
                    *off = offFile;
                    break;
                }
            } else {
                vbsf_unlock_user_pages(papPages, cPages, false /*fSetDirty*/, fLockPgHack);
                if (rc == VERR_NO_MEMORY && cMaxPages > 4) {
                    /*
                     * The host probably doesn't have enough heap to handle the
                     * request, reduce the page count and retry.
                     */
                    cMaxPages /= 4;
                    Assert(cMaxPages > 0);
                } else {
                    /*
                     * If we've successfully written stuff, return it rather than
                     * the error.  (Not sure if this is such a great idea...)
                     */
                    if (cbRet > 0) {
                        SFLOGFLOW(("vbsf_reg_write: write at %#RX64 -> %Rrc; got cbRet=%#zx already\n", offFile, rc, cbRet));
                        *off = offFile;
                    } else {
                        SFLOGFLOW(("vbsf_reg_write: write at %#RX64 -> %Rrc\n", offFile, rc));
                        cbRet = -EPROTO;
                    }
                    break;
                }
            }
        }
    }
    if (papPagesFree)
        kfree(papPages);
    if (pReq)
        VbglR0PhysHeapFree(pReq);
    SFLOGFLOW(("vbsf_reg_write: returns %zd (%#zx), *off=%RX64 [lock]\n", cbRet, cbRet, *off));
    return cbRet;
}


/**
 * Write to a regular file.
 *
 * @param file          the file
 * @param buf           the buffer
 * @param size          length of the buffer
 * @param off           offset within the file
 * @returns the number of written bytes on success, Linux error code otherwise
 */
static ssize_t vbsf_reg_write(struct file *file, const char *buf, size_t size, loff_t * off)
{
    struct inode           *inode      = VBSF_GET_F_DENTRY(file)->d_inode;
    struct vbsf_inode_info *sf_i       = VBSF_GET_INODE_INFO(inode);
    struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
    struct vbsf_reg_info   *sf_r       = file->private_data;
    struct address_space   *mapping    = inode->i_mapping;
    loff_t                  pos;

    SFLOGFLOW(("vbsf_reg_write: inode=%p file=%p buf=%p size=%#zx off=%#llx\n", inode, file, buf, size, *off));
    Assert(sf_i);
    Assert(pSuperInfo);
    Assert(sf_r);
    AssertReturn(S_ISREG(inode->i_mode), -EINVAL);

    pos = *off;
    /** @todo This should be handled by the host, it returning the new file
     *        offset when appending.  We may have an outdated i_size value here! */
    if (file->f_flags & O_APPEND)
        pos = i_size_read(inode);

    /** @todo XXX Check write permission according to inode->i_mode! */

    if (!size) {
        if (file->f_flags & O_APPEND)  /** @todo check if this is the consensus behavior... */
            *off = pos;
        return 0;
    }

    /** @todo Implement the read-write caching mode. */

    /*
     * If there are active writable mappings, coordinate with any
     * pending writes via those.
     */
    if (   mapping
        && mapping->nrpages > 0
        && mapping_writably_mapped(mapping)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
        int err = filemap_fdatawait_range(mapping, pos, pos + size - 1);
        if (err)
            return err;
#else
        /** @todo ...   */
#endif
    }

    /*
     * For small requests, try use an embedded buffer provided we get a heap block
     * that does not cross page boundraries (see host code).
     */
    if (size <= PAGE_SIZE / 4 * 3 - RT_UOFFSETOF(VBOXSFWRITEEMBEDDEDREQ, abData[0]) /* see allocator */) {
        uint32_t const          cbReq = RT_UOFFSETOF(VBOXSFWRITEEMBEDDEDREQ, abData[0]) + size;
        VBOXSFWRITEEMBEDDEDREQ *pReq  = (VBOXSFWRITEEMBEDDEDREQ *)VbglR0PhysHeapAlloc(cbReq);
        if (   pReq
            && (PAGE_SIZE - ((uintptr_t)pReq & PAGE_OFFSET_MASK)) >= cbReq) {
            ssize_t cbRet;
            if (copy_from_user(pReq->abData, buf, size) == 0) {
                int vrc = VbglR0SfHostReqWriteEmbedded(pSuperInfo->map.root, pReq, sf_r->Handle.hHost,
                                                       pos, (uint32_t)size);
                sf_i->ModificationTimeAtOurLastWrite = sf_i->ModificationTime;
                if (RT_SUCCESS(vrc)) {
                    cbRet = pReq->Parms.cb32Write.u.value32;
                    AssertStmt(cbRet <= (ssize_t)size, cbRet = size);
                    vbsf_reg_write_sync_page_cache(mapping, pos, (uint32_t)cbRet, pReq->abData,
                                                   NULL /*papSrcPages*/, 0 /*offSrcPage0*/, 0 /*cSrcPages*/);
                    pos += cbRet;
                    *off = pos;
                    if (pos > i_size_read(inode))
                        i_size_write(inode, pos);
                } else
                    cbRet = -EPROTO;
                sf_i->force_restat = 1; /* mtime (and size) may have changed */
            } else
                cbRet = -EFAULT;

            VbglR0PhysHeapFree(pReq);
            SFLOGFLOW(("vbsf_reg_write: returns %zd (%#zx), *off=%RX64 [embed]\n", cbRet, cbRet, *off));
            return cbRet;
        }
        if (pReq)
            VbglR0PhysHeapFree(pReq);
    }

#if 0 /* Turns out this is slightly slower than locking the pages even for 4KB reads (4.19/amd64). */
    /*
     * For medium sized requests try use a bounce buffer.
     */
    if (size <= _64K /** @todo make this configurable? */) {
        void *pvBounce = kmalloc(size, GFP_KERNEL);
        if (pvBounce) {
            if (copy_from_user(pvBounce, buf, size) == 0) {
                VBOXSFWRITEPGLSTREQ *pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
                if (pReq) {
                    ssize_t cbRet;
                    int vrc = VbglR0SfHostReqWriteContig(pSuperInfo->map.root, pReq, sf_r->handle, pos,
                                                         (uint32_t)size, pvBounce, virt_to_phys(pvBounce));
                    sf_i->ModificationTimeAtOurLastWrite = sf_i->ModificationTime;
                    if (RT_SUCCESS(vrc)) {
                        cbRet = pReq->Parms.cb32Write.u.value32;
                        AssertStmt(cbRet <= (ssize_t)size, cbRet = size);
                        vbsf_reg_write_sync_page_cache(mapping, pos, (uint32_t)cbRet, (uint8_t const *)pvBounce,
                                                       NULL /*papSrcPages*/, 0 /*offSrcPage0*/, 0 /*cSrcPages*/);
                        pos += cbRet;
                        *off = pos;
                        if (pos > i_size_read(inode))
                            i_size_write(inode, pos);
                    } else
                        cbRet = -EPROTO;
                    sf_i->force_restat = 1; /* mtime (and size) may have changed */
                    VbglR0PhysHeapFree(pReq);
                    kfree(pvBounce);
                    SFLOGFLOW(("vbsf_reg_write: returns %zd (%#zx), *off=%RX64 [bounce]\n", cbRet, cbRet, *off));
                    return cbRet;
                }
                kfree(pvBounce);
            } else {
                kfree(pvBounce);
                SFLOGFLOW(("vbsf_reg_write: returns -EFAULT, *off=%RX64 [bounce]\n", *off));
                return -EFAULT;
            }
        }
    }
#endif

    return vbsf_reg_write_locking(file, buf, size, off, pos, inode, sf_i, pSuperInfo, sf_r);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)

/**
 * Companion to vbsf_iter_lock_pages().
 */
DECLINLINE(void) vbsf_iter_unlock_pages(struct iov_iter *iter, struct page **papPages, size_t cPages, bool fSetDirty)
{
    /* We don't mark kernel pages dirty: */
    if (iter->type & ITER_KVEC)
        fSetDirty = false;

    while (cPages-- > 0)
    {
        struct page *pPage = papPages[cPages];
        if (fSetDirty && !PageReserved(pPage))
            set_page_dirty(pPage);
        vbsf_put_page(pPage);
    }
}


/**
 * Locks up to @a cMaxPages from the I/O vector iterator, advancing the
 * iterator.
 *
 * @returns 0 on success, negative errno value on failure.
 * @param   iter        The iterator to lock pages from.
 * @param   fWrite      Whether to write (true) or read (false) lock the pages.
 * @param   pStash      Where we stash peek results.
 * @param   cMaxPages   The maximum number of pages to get.
 * @param   papPages    Where to return the locked pages.
 * @param   pcPages     Where to return the number of pages.
 * @param   poffPage0   Where to return the offset into the first page.
 * @param   pcbChunk    Where to return the number of bytes covered.
 */
static int vbsf_iter_lock_pages(struct iov_iter *iter, bool fWrite, struct vbsf_iter_stash *pStash, size_t cMaxPages,
                                struct page **papPages, size_t *pcPages, size_t *poffPage0, size_t *pcbChunk)
{
    size_t cbChunk  = 0;
    size_t cPages   = 0;
    size_t offPage0 = 0;
    int    rc       = 0;

    Assert(iov_iter_count(iter) + pStash->cb > 0);
    if (!(iter->type & ITER_KVEC)) {
        /*
         * Do we have a stashed page?
         */
        if (pStash->pPage) {
            papPages[0] = pStash->pPage;
            offPage0    = pStash->off;
            cbChunk     = pStash->cb;
            cPages      = 1;
            pStash->pPage = NULL;
            pStash->off   = 0;
            pStash->cb    = 0;
            if (   offPage0 + cbChunk < PAGE_SIZE
                || iov_iter_count(iter) == 0) {
                *poffPage0 = offPage0;
                *pcbChunk  = cbChunk;
                *pcPages   = cPages;
                SFLOGFLOW(("vbsf_iter_lock_pages: returns %d - cPages=%#zx offPage0=%#zx cbChunk=%zx (stashed)\n",
                           rc, cPages, offPage0, cbChunk));
                return 0;
            }
            cMaxPages -= 1;
            SFLOG3(("vbsf_iter_lock_pages: Picked up stashed page: %#zx LB %#zx\n", offPage0, cbChunk));
        } else {
# if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
            /*
             * Copy out our starting point to assist rewinding.
             */
            pStash->offFromEnd = iov_iter_count(iter);
            pStash->Copy       = *iter;
# endif
        }

        /*
         * Get pages segment by segment.
         */
        do {
            /*
             * Make a special case of the first time thru here, since that's
             * the most typical scenario.
             */
            ssize_t cbSegRet;
            if (cPages == 0) {
# if LINUX_VERSION_CODE < KERNEL_VERSION(3, 19, 0)
                while (!iov_iter_single_seg_count(iter)) /* Old code didn't skip empty segments which caused EFAULTs. */
                    iov_iter_advance(iter, 0);
# endif
                cbSegRet = iov_iter_get_pages(iter, papPages, iov_iter_count(iter), cMaxPages, &offPage0);
                if (cbSegRet > 0) {
                    iov_iter_advance(iter, cbSegRet);
                    cbChunk    = (size_t)cbSegRet;
                    cPages     = RT_ALIGN_Z(offPage0 + cbSegRet, PAGE_SIZE) >> PAGE_SHIFT;
                    cMaxPages -= cPages;
                    SFLOG3(("vbsf_iter_lock_pages: iov_iter_get_pages -> %#zx @ %#zx; %#zx pages [first]\n", cbSegRet, offPage0, cPages));
                    if (   cMaxPages == 0
                        || ((offPage0 + (size_t)cbSegRet) & PAGE_OFFSET_MASK))
                        break;
                } else {
                    AssertStmt(cbSegRet < 0, cbSegRet = -EFAULT);
                    rc = (int)cbSegRet;
                    break;
                }
            } else {
                /*
                 * Probe first page of new segment to check that we've got a zero offset and
                 * can continue on the current chunk. Stash the page if the offset isn't zero.
                 */
                size_t offPgProbe;
                size_t cbSeg = iov_iter_single_seg_count(iter);
                while (!cbSeg) {
                    iov_iter_advance(iter, 0);
                    cbSeg = iov_iter_single_seg_count(iter);
                }
                cbSegRet = iov_iter_get_pages(iter, &papPages[cPages], iov_iter_count(iter), 1, &offPgProbe);
                if (cbSegRet > 0) {
                    iov_iter_advance(iter, cbSegRet); /** @todo maybe not do this if we stash the page? */
                    Assert(offPgProbe + cbSegRet <= PAGE_SIZE);
                    if (offPgProbe == 0) {
                        cbChunk   += cbSegRet;
                        cPages    += 1;
                        cMaxPages -= 1;
                        SFLOG3(("vbsf_iter_lock_pages: iov_iter_get_pages(1) -> %#zx @ %#zx\n", cbSegRet, offPgProbe));
                        if (   cMaxPages == 0
                            || cbSegRet != PAGE_SIZE)
                            break;

                        /*
                         * Get the rest of the segment (if anything remaining).
                         */
                        cbSeg -= cbSegRet;
                        if (cbSeg > 0) {
                            cbSegRet = iov_iter_get_pages(iter, &papPages[cPages], iov_iter_count(iter), cMaxPages, &offPgProbe);
                            if (cbSegRet > 0) {
                                size_t const cPgRet = RT_ALIGN_Z((size_t)cbSegRet, PAGE_SIZE) >> PAGE_SHIFT;
                                Assert(offPgProbe == 0);
                                iov_iter_advance(iter, cbSegRet);
                                SFLOG3(("vbsf_iter_lock_pages: iov_iter_get_pages() -> %#zx; %#zx pages\n", cbSegRet, cPgRet));
                                cPages    += cPgRet;
                                cMaxPages -= cPgRet;
                                cbChunk   += cbSegRet;
                                if (   cMaxPages == 0
                                    || ((size_t)cbSegRet & PAGE_OFFSET_MASK))
                                    break;
                            } else {
                                AssertStmt(cbSegRet < 0, cbSegRet = -EFAULT);
                                rc = (int)cbSegRet;
                                break;
                            }
                        }
                    } else {
                        /* The segment didn't start at a page boundrary, so stash it for
                           the next round: */
                        SFLOGFLOW(("vbsf_iter_lock_pages: iov_iter_get_pages(1) -> %#zx @ %#zx; stashed\n", cbSegRet, offPgProbe));
                        Assert(papPages[cPages]);
                        pStash->pPage = papPages[cPages];
                        pStash->off   = offPgProbe;
                        pStash->cb    = cbSegRet;
                        break;
                    }
                } else {
                    AssertStmt(cbSegRet < 0, cbSegRet = -EFAULT);
                    rc = (int)cbSegRet;
                    break;
                }
            }
            Assert(cMaxPages > 0);
        } while (iov_iter_count(iter) > 0);

    } else {
        /*
         * The silly iov_iter_get_pages_alloc() function doesn't handle KVECs,
         * so everyone needs to do that by themselves.
         *
         * Note! Fixes here may apply to rtR0MemObjNativeLockKernel()
         *       and vbsf_lock_user_pages_failed_check_kernel() as well.
         */
# if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
        pStash->offFromEnd = iov_iter_count(iter);
        pStash->Copy       = *iter;
# endif
        do {
            uint8_t *pbBuf;
            size_t   offStart;
            size_t   cPgSeg;

            size_t   cbSeg = iov_iter_single_seg_count(iter);
            while (!cbSeg) {
                iov_iter_advance(iter, 0);
                cbSeg = iov_iter_single_seg_count(iter);
            }

# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
            pbBuf    = iter->kvec->iov_base + iter->iov_offset;
# else
            pbBuf    = iter->iov->iov_base  + iter->iov_offset;
# endif
            offStart = (uintptr_t)pbBuf & PAGE_OFFSET_MASK;
            if (!cPages)
                offPage0 = offStart;
            else if (offStart)
                break;

            cPgSeg = RT_ALIGN_Z(cbSeg, PAGE_SIZE) >> PAGE_SHIFT;
            if (cPgSeg > cMaxPages) {
                cPgSeg = cMaxPages;
                cbSeg  = (cPgSeg << PAGE_SHIFT) - offStart;
            }

            rc = vbsf_lock_kernel_pages(pbBuf, fWrite, cPgSeg, &papPages[cPages]);
            if (rc == 0) {
                iov_iter_advance(iter, cbSeg);
                cbChunk   += cbSeg;
                cPages    += cPgSeg;
                cMaxPages -= cPgSeg;
                if (   cMaxPages == 0
                    || ((offStart + cbSeg) & PAGE_OFFSET_MASK) != 0)
                    break;
            } else
                break;
        } while (iov_iter_count(iter) > 0);
    }

    /*
     * Clean up if we failed; set return values.
     */
    if (rc == 0) {
        /* likely */
    } else {
        if (cPages > 0)
            vbsf_iter_unlock_pages(iter, papPages, cPages, false /*fSetDirty*/);
        offPage0 = cbChunk = cPages = 0;
    }
    *poffPage0 = offPage0;
    *pcbChunk  = cbChunk;
    *pcPages   = cPages;
    SFLOGFLOW(("vbsf_iter_lock_pages: returns %d - cPages=%#zx offPage0=%#zx cbChunk=%zx\n", rc, cPages, offPage0, cbChunk));
    return rc;
}


/**
 * Rewinds the I/O vector.
 */
static bool vbsf_iter_rewind(struct iov_iter *iter, struct vbsf_iter_stash *pStash, size_t cbToRewind, size_t cbChunk)
{
    size_t cbExtra;
    if (!pStash->pPage) {
        cbExtra = 0;
    } else {
        cbExtra = pStash->cb;
        vbsf_put_page(pStash->pPage);
        pStash->pPage = NULL;
        pStash->cb    = 0;
        pStash->off   = 0;
    }

# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0) || LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
    iov_iter_revert(iter, cbToRewind + cbExtra);
    return true;
# else
    /** @todo impl this   */
    return false;
# endif
}


/**
 * Cleans up the page locking stash.
 */
DECLINLINE(void) vbsf_iter_cleanup_stash(struct iov_iter *iter, struct vbsf_iter_stash *pStash)
{
    if (pStash->pPage)
        vbsf_iter_rewind(iter, pStash, 0, 0);
}


/**
 * Calculates the longest span of pages we could transfer to the host in a
 * single request.
 *
 * @returns Page count, non-zero.
 * @param   iter        The I/O vector iterator to inspect.
 */
static size_t vbsf_iter_max_span_of_pages(struct iov_iter *iter)
{
    size_t cPages;
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    if (iter_is_iovec(iter) || (iter->type & ITER_KVEC)) {
#endif
        const struct iovec *pCurIov    = iter->iov;
        size_t              cLeft      = iter->nr_segs;
        size_t              cPagesSpan = 0;

        /* iovect and kvec are identical, except for the __user tagging of iov_base. */
        AssertCompileMembersSameSizeAndOffset(struct iovec, iov_base, struct kvec, iov_base);
        AssertCompileMembersSameSizeAndOffset(struct iovec, iov_len,  struct kvec, iov_len);
        AssertCompile(sizeof(struct iovec) == sizeof(struct kvec));

        cPages = 1;
        AssertReturn(cLeft > 0, cPages);

        /* Special case: segment offset. */
        if (iter->iov_offset > 0) {
            if (iter->iov_offset < pCurIov->iov_len) {
                size_t const cbSegLeft = pCurIov->iov_len - iter->iov_offset;
                size_t const offPage0  = ((uintptr_t)pCurIov->iov_base + iter->iov_offset) & PAGE_OFFSET_MASK;
                cPages = cPagesSpan = RT_ALIGN_Z(offPage0 + cbSegLeft, PAGE_SIZE) >> PAGE_SHIFT;
                if ((offPage0 + cbSegLeft) & PAGE_OFFSET_MASK)
                    cPagesSpan = 0;
            }
            SFLOGFLOW(("vbsf_iter: seg[0]= %p LB %#zx\n", pCurIov->iov_base, pCurIov->iov_len));
            pCurIov++;
            cLeft--;
        }

        /* Full segments. */
        while (cLeft-- > 0) {
            if (pCurIov->iov_len > 0) {
                size_t const offPage0 = (uintptr_t)pCurIov->iov_base & PAGE_OFFSET_MASK;
                if (offPage0 == 0) {
                    if (!(pCurIov->iov_len & PAGE_OFFSET_MASK)) {
                        cPagesSpan += pCurIov->iov_len >> PAGE_SHIFT;
                    } else {
                        cPagesSpan += RT_ALIGN_Z(pCurIov->iov_len, PAGE_SIZE) >> PAGE_SHIFT;
                        if (cPagesSpan > cPages)
                            cPages = cPagesSpan;
                        cPagesSpan = 0;
                    }
                } else {
                    if (cPagesSpan > cPages)
                        cPages = cPagesSpan;
                    if (!((offPage0 + pCurIov->iov_len) & PAGE_OFFSET_MASK)) {
                        cPagesSpan = pCurIov->iov_len >> PAGE_SHIFT;
                    } else {
                        cPagesSpan += RT_ALIGN_Z(offPage0 + pCurIov->iov_len, PAGE_SIZE) >> PAGE_SHIFT;
                        if (cPagesSpan > cPages)
                            cPages = cPagesSpan;
                        cPagesSpan = 0;
                    }
                }
            }
            SFLOGFLOW(("vbsf_iter: seg[%u]= %p LB %#zx\n", iter->nr_segs - cLeft, pCurIov->iov_base, pCurIov->iov_len));
            pCurIov++;
        }
        if (cPagesSpan > cPages)
            cPages = cPagesSpan;
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    } else {
        /* Won't bother with accurate counts for the next two types, just make
           some rough estimates (does pipes have segments?): */
        size_t cSegs = iter->type & ITER_BVEC ? RT_MAX(1, iter->nr_segs) : 1;
        cPages = (iov_iter_count(iter) + (PAGE_SIZE * 2 - 2) * cSegs) >> PAGE_SHIFT;
    }
# endif
    SFLOGFLOW(("vbsf_iter_max_span_of_pages: returns %#zx\n", cPages));
    return cPages;
}


/**
 * Worker for vbsf_reg_read_iter() that deals with larger reads using page
 * locking.
 */
static ssize_t vbsf_reg_read_iter_locking(struct kiocb *kio, struct iov_iter *iter, size_t cbToRead,
                                          struct vbsf_super_info *pSuperInfo, struct vbsf_reg_info *sf_r)
{
    /*
     * Estimate how many pages we may possible submit in a single request so
     * that we can allocate matching request buffer and page array.
     */
    struct page        *apPagesStack[16];
    struct page       **papPages     = &apPagesStack[0];
    struct page       **papPagesFree = NULL;
    VBOXSFREADPGLSTREQ *pReq;
    ssize_t             cbRet        = 0;
    size_t              cMaxPages    = vbsf_iter_max_span_of_pages(iter);
    cMaxPages = RT_MIN(RT_MAX(pSuperInfo->cMaxIoPages, 2), cMaxPages);

    pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ, PgLst.aPages[cMaxPages]));
    while (!pReq && cMaxPages > 4) {
        cMaxPages /= 2;
        pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFREADPGLSTREQ, PgLst.aPages[cMaxPages]));
    }
    if (pReq && cMaxPages > RT_ELEMENTS(apPagesStack))
        papPagesFree = papPages = kmalloc(cMaxPages * sizeof(sizeof(papPages[0])), GFP_KERNEL);
    if (pReq && papPages) {

        /*
         * The read loop.
         */
        struct vbsf_iter_stash Stash = VBSF_ITER_STASH_INITIALIZER;
        do {
            /*
             * Grab as many pages as we can.  This means that if adjacent
             * segments both starts and ends at a page boundrary, we can
             * do them both in the same transfer from the host.
             */
            size_t cPages   = 0;
            size_t cbChunk  = 0;
            size_t offPage0 = 0;
            int rc = vbsf_iter_lock_pages(iter, true /*fWrite*/, &Stash, cMaxPages, papPages, &cPages, &offPage0, &cbChunk);
            if (rc == 0) {
                size_t iPage = cPages;
                while (iPage-- > 0)
                    pReq->PgLst.aPages[iPage] = page_to_phys(papPages[iPage]);
                pReq->PgLst.offFirstPage = (uint16_t)offPage0;
                AssertStmt(cbChunk <= cbToRead, cbChunk = cbToRead);
            } else {
                cbRet = rc;
                break;
            }

            /*
             * Issue the request and unlock the pages.
             */
            rc = VbglR0SfHostReqReadPgLst(pSuperInfo->map.root, pReq, sf_r->Handle.hHost, kio->ki_pos, cbChunk, cPages);
            SFLOGFLOW(("vbsf_reg_read_iter_locking: VbglR0SfHostReqReadPgLst -> %d (cbActual=%#x cbChunk=%#zx of %#zx cPages=%#zx offPage0=%#x\n",
                       rc, pReq->Parms.cb32Read.u.value32, cbChunk, cbToRead, cPages, offPage0));

            vbsf_iter_unlock_pages(iter, papPages, cPages, true /*fSetDirty*/);

            if (RT_SUCCESS(rc)) {
                /*
                 * Success, advance position and buffer.
                 */
                uint32_t cbActual = pReq->Parms.cb32Read.u.value32;
                AssertStmt(cbActual <= cbChunk, cbActual = cbChunk);
                cbRet       += cbActual;
                kio->ki_pos += cbActual;
                cbToRead    -= cbActual;

                /*
                 * Are we done already?
                 */
                if (!cbToRead)
                    break;
                if (cbActual < cbChunk) { /* We ASSUME end-of-file here. */
                    if (vbsf_iter_rewind(iter, &Stash, cbChunk - cbActual, cbActual))
                        iov_iter_truncate(iter, 0);
                    break;
                }
            } else {
                /*
                 * Try rewind the iter structure.
                 */
                bool const fRewindOkay = vbsf_iter_rewind(iter, &Stash, cbChunk, cbChunk);
                if (rc == VERR_NO_MEMORY && cMaxPages > 4 && fRewindOkay) {
                    /*
                     * The host probably doesn't have enough heap to handle the
                     * request, reduce the page count and retry.
                     */
                    cMaxPages /= 4;
                    Assert(cMaxPages > 0);
                } else {
                    /*
                     * If we've successfully read stuff, return it rather than
                     * the error.  (Not sure if this is such a great idea...)
                     */
                    if (cbRet <= 0)
                        cbRet = -EPROTO;
                    break;
                }
            }
        } while (cbToRead > 0);

        vbsf_iter_cleanup_stash(iter, &Stash);
    }
    else
        cbRet = -ENOMEM;
    if (papPagesFree)
        kfree(papPages);
    if (pReq)
        VbglR0PhysHeapFree(pReq);
    SFLOGFLOW(("vbsf_reg_read_iter_locking: returns %#zx (%zd)\n", cbRet, cbRet));
    return cbRet;
}


/**
 * Read into I/O vector iterator.
 *
 * @returns Number of bytes read on success, negative errno on error.
 * @param   kio         The kernel I/O control block (or something like that).
 * @param   iter        The I/O vector iterator describing the buffer.
 */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static ssize_t vbsf_reg_read_iter(struct kiocb *kio, struct iov_iter *iter)
# else
static ssize_t vbsf_reg_aio_read(struct kiocb *kio, const struct iovec *iov, unsigned long cSegs, loff_t offFile)
# endif
{
# if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
    struct vbsf_iov_iter    fake_iter  = VBSF_IOV_ITER_INITIALIZER(cSegs, iov, 0 /*write*/);
    struct vbsf_iov_iter   *iter       = &fake_iter;
# endif
    size_t                  cbToRead   = iov_iter_count(iter);
    struct inode           *inode      = VBSF_GET_F_DENTRY(kio->ki_filp)->d_inode;
    struct address_space   *mapping    = inode->i_mapping;

    struct vbsf_reg_info   *sf_r       = kio->ki_filp->private_data;
    struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);

    SFLOGFLOW(("vbsf_reg_read_iter: inode=%p file=%p size=%#zx off=%#llx type=%#x\n",
               inode, kio->ki_filp, cbToRead, kio->ki_pos, iter->type));
    AssertReturn(S_ISREG(inode->i_mode), -EINVAL);

    /*
     * Do we have anything at all to do here?
     */
    if (!cbToRead)
        return 0;

    /*
     * If there is a mapping and O_DIRECT isn't in effect, we must at a
     * heed dirty pages in the mapping and read from them.  For simplicity
     * though, we just do page cache reading when there are writable
     * mappings around with any kind of pages loaded.
     */
    if (vbsf_should_use_cached_read(kio->ki_filp, mapping, pSuperInfo)) {
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
        return generic_file_read_iter(kio, iter);
# else
        return generic_file_aio_read(kio, iov, cSegs, offFile);
# endif
    }

    /*
     * Now now we reject async I/O requests.
     */
    if (!is_sync_kiocb(kio)) {
        SFLOGFLOW(("vbsf_reg_read_iter: async I/O not yet supported\n")); /** @todo extend FsPerf with AIO tests. */
        return -EOPNOTSUPP;
    }

    /*
     * For small requests, try use an embedded buffer provided we get a heap block
     * that does not cross page boundraries (see host code).
     */
    if (cbToRead <= PAGE_SIZE / 4 * 3 - RT_UOFFSETOF(VBOXSFREADEMBEDDEDREQ, abData[0]) /* see allocator */) {
        uint32_t const         cbReq = RT_UOFFSETOF(VBOXSFREADEMBEDDEDREQ, abData[0]) + cbToRead;
        VBOXSFREADEMBEDDEDREQ *pReq  = (VBOXSFREADEMBEDDEDREQ *)VbglR0PhysHeapAlloc(cbReq);
        if (pReq) {
            if ((PAGE_SIZE - ((uintptr_t)pReq & PAGE_OFFSET_MASK)) >= cbReq) {
                ssize_t cbRet;
                int vrc = VbglR0SfHostReqReadEmbedded(pSuperInfo->map.root, pReq, sf_r->Handle.hHost,
                                                      kio->ki_pos, (uint32_t)cbToRead);
                if (RT_SUCCESS(vrc)) {
                    cbRet = pReq->Parms.cb32Read.u.value32;
                    AssertStmt(cbRet <= (ssize_t)cbToRead, cbRet = cbToRead);
                    if (copy_to_iter(pReq->abData, cbRet, iter) == cbRet) {
                        kio->ki_pos += cbRet;
                        if (cbRet < cbToRead)
                            iov_iter_truncate(iter, 0);
                    } else
                        cbRet = -EFAULT;
                } else
                    cbRet = -EPROTO;
                VbglR0PhysHeapFree(pReq);
                SFLOGFLOW(("vbsf_reg_read_iter: returns %#zx (%zd)\n", cbRet, cbRet));
                return cbRet;
            }
            VbglR0PhysHeapFree(pReq);
        }
    }

    /*
     * Otherwise do the page locking thing.
     */
    return vbsf_reg_read_iter_locking(kio, iter, cbToRead, pSuperInfo, sf_r);
}


/**
 * Worker for vbsf_reg_write_iter() that deals with larger writes using page
 * locking.
 */
static ssize_t vbsf_reg_write_iter_locking(struct kiocb *kio, struct iov_iter *iter, size_t cbToWrite, loff_t offFile,
                                           struct vbsf_super_info *pSuperInfo, struct vbsf_reg_info *sf_r,
                                           struct inode *inode, struct vbsf_inode_info *sf_i, struct address_space *mapping)
{
    /*
     * Estimate how many pages we may possible submit in a single request so
     * that we can allocate matching request buffer and page array.
     */
    struct page         *apPagesStack[16];
    struct page        **papPages     = &apPagesStack[0];
    struct page        **papPagesFree = NULL;
    VBOXSFWRITEPGLSTREQ *pReq;
    ssize_t              cbRet        = 0;
    size_t               cMaxPages    = vbsf_iter_max_span_of_pages(iter);
    cMaxPages = RT_MIN(RT_MAX(pSuperInfo->cMaxIoPages, 2), cMaxPages);

    pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFWRITEPGLSTREQ, PgLst.aPages[cMaxPages]));
    while (!pReq && cMaxPages > 4) {
        cMaxPages /= 2;
        pReq = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(RT_UOFFSETOF_DYN(VBOXSFWRITEPGLSTREQ, PgLst.aPages[cMaxPages]));
    }
    if (pReq && cMaxPages > RT_ELEMENTS(apPagesStack))
        papPagesFree = papPages = kmalloc(cMaxPages * sizeof(sizeof(papPages[0])), GFP_KERNEL);
    if (pReq && papPages) {

        /*
         * The write loop.
         */
        struct vbsf_iter_stash Stash = VBSF_ITER_STASH_INITIALIZER;
        do {
            /*
             * Grab as many pages as we can.  This means that if adjacent
             * segments both starts and ends at a page boundrary, we can
             * do them both in the same transfer from the host.
             */
            size_t cPages   = 0;
            size_t cbChunk  = 0;
            size_t offPage0 = 0;
            int rc = vbsf_iter_lock_pages(iter, false /*fWrite*/, &Stash, cMaxPages, papPages, &cPages, &offPage0, &cbChunk);
            if (rc == 0) {
                size_t iPage = cPages;
                while (iPage-- > 0)
                    pReq->PgLst.aPages[iPage] = page_to_phys(papPages[iPage]);
                pReq->PgLst.offFirstPage = (uint16_t)offPage0;
                AssertStmt(cbChunk <= cbToWrite, cbChunk = cbToWrite);
            } else {
                cbRet = rc;
                break;
            }

            /*
             * Issue the request and unlock the pages.
             */
            rc = VbglR0SfHostReqWritePgLst(pSuperInfo->map.root, pReq, sf_r->Handle.hHost, offFile, cbChunk, cPages);
            sf_i->ModificationTimeAtOurLastWrite = sf_i->ModificationTime;
            SFLOGFLOW(("vbsf_reg_write_iter_locking: VbglR0SfHostReqWritePgLst -> %d (cbActual=%#x cbChunk=%#zx of %#zx cPages=%#zx offPage0=%#x\n",
                       rc, pReq->Parms.cb32Write.u.value32, cbChunk, cbToWrite, cPages, offPage0));
            if (RT_SUCCESS(rc)) {
                /*
                 * Success, advance position and buffer.
                 */
                uint32_t cbActual = pReq->Parms.cb32Write.u.value32;
                AssertStmt(cbActual <= cbChunk, cbActual = cbChunk);

                vbsf_reg_write_sync_page_cache(mapping, offFile, cbActual, NULL /*pbSrcBuf*/, papPages, offPage0, cPages);
                vbsf_iter_unlock_pages(iter, papPages, cPages, false /*fSetDirty*/);

                cbRet      += cbActual;
                offFile    += cbActual;
                kio->ki_pos = offFile;
                cbToWrite  -= cbActual;
                if (offFile > i_size_read(inode))
                    i_size_write(inode, offFile);
                sf_i->force_restat = 1; /* mtime (and size) may have changed */

                /*
                 * Are we done already?
                 */
                if (!cbToWrite)
                    break;
                if (cbActual < cbChunk) { /* We ASSUME end-of-file here. */
                    if (vbsf_iter_rewind(iter, &Stash, cbChunk - cbActual, cbActual))
                        iov_iter_truncate(iter, 0);
                    break;
                }
            } else {
                /*
                 * Try rewind the iter structure.
                 */
                bool fRewindOkay;
                vbsf_iter_unlock_pages(iter, papPages, cPages, false /*fSetDirty*/);
                fRewindOkay = vbsf_iter_rewind(iter, &Stash, cbChunk, cbChunk);
                if (rc == VERR_NO_MEMORY && cMaxPages > 4 && fRewindOkay) {
                    /*
                     * The host probably doesn't have enough heap to handle the
                     * request, reduce the page count and retry.
                     */
                    cMaxPages /= 4;
                    Assert(cMaxPages > 0);
                } else {
                    /*
                     * If we've successfully written stuff, return it rather than
                     * the error.  (Not sure if this is such a great idea...)
                     */
                    if (cbRet <= 0)
                        cbRet = -EPROTO;
                    break;
                }
            }
        } while (cbToWrite > 0);

        vbsf_iter_cleanup_stash(iter, &Stash);
    }
    else
        cbRet = -ENOMEM;
    if (papPagesFree)
        kfree(papPages);
    if (pReq)
        VbglR0PhysHeapFree(pReq);
    SFLOGFLOW(("vbsf_reg_write_iter_locking: returns %#zx (%zd)\n", cbRet, cbRet));
    return cbRet;
}


/**
 * Write from I/O vector iterator.
 *
 * @returns Number of bytes written on success, negative errno on error.
 * @param   kio         The kernel I/O control block (or something like that).
 * @param   iter        The I/O vector iterator describing the buffer.
 */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
static ssize_t vbsf_reg_write_iter(struct kiocb *kio, struct iov_iter *iter)
# else
static ssize_t vbsf_reg_aio_write(struct kiocb *kio, const struct iovec *iov, unsigned long cSegs, loff_t offFile)
# endif
{
# if LINUX_VERSION_CODE < KERNEL_VERSION(3, 16, 0)
    struct vbsf_iov_iter    fake_iter  = VBSF_IOV_ITER_INITIALIZER(cSegs, iov, 1 /*write*/);
    struct vbsf_iov_iter   *iter       = &fake_iter;
# endif
    size_t                  cbToWrite  = iov_iter_count(iter);
    struct inode           *inode      = VBSF_GET_F_DENTRY(kio->ki_filp)->d_inode;
    struct vbsf_inode_info *sf_i       = VBSF_GET_INODE_INFO(inode);
    struct address_space   *mapping    = inode->i_mapping;

    struct vbsf_reg_info   *sf_r       = kio->ki_filp->private_data;
    struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    loff_t                  offFile    = kio->ki_pos;
# endif

    SFLOGFLOW(("vbsf_reg_write_iter: inode=%p file=%p size=%#zx off=%#llx type=%#x\n",
               inode, kio->ki_filp, cbToWrite, offFile, iter->type));
    AssertReturn(S_ISREG(inode->i_mode), -EINVAL);

    /*
     * Enforce APPEND flag.
     */
    /** @todo This should be handled by the host, it returning the new file
     *        offset when appending.  We may have an outdated i_size value here! */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
    if (kio->ki_flags & IOCB_APPEND)
# else
    if (kio->ki_filp->f_flags & O_APPEND)
# endif
        kio->ki_pos = offFile = i_size_read(inode);

    /*
     * Do we have anything at all to do here?
     */
    if (!cbToWrite)
        return 0;

    /** @todo Implement the read-write caching mode. */

    /*
     * Now now we reject async I/O requests.
     */
    if (!is_sync_kiocb(kio)) {
        SFLOGFLOW(("vbsf_reg_write_iter: async I/O not yet supported\n")); /** @todo extend FsPerf with AIO tests. */
        return -EOPNOTSUPP;
    }

    /*
     * If there are active writable mappings, coordinate with any
     * pending writes via those.
     */
    if (   mapping
        && mapping->nrpages > 0
        && mapping_writably_mapped(mapping)) {
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32)
        int err = filemap_fdatawait_range(mapping, offFile, offFile + cbToWrite - 1);
        if (err)
            return err;
# else
        /** @todo ... */
# endif
    }

    /*
     * For small requests, try use an embedded buffer provided we get a heap block
     * that does not cross page boundraries (see host code).
     */
    if (cbToWrite <= PAGE_SIZE / 4 * 3 - RT_UOFFSETOF(VBOXSFWRITEEMBEDDEDREQ, abData[0]) /* see allocator */) {
        uint32_t const         cbReq = RT_UOFFSETOF(VBOXSFWRITEEMBEDDEDREQ, abData[0]) + cbToWrite;
        VBOXSFWRITEEMBEDDEDREQ *pReq = (VBOXSFWRITEEMBEDDEDREQ *)VbglR0PhysHeapAlloc(cbReq);
        if (pReq) {
            if ((PAGE_SIZE - ((uintptr_t)pReq & PAGE_OFFSET_MASK)) >= cbReq) {
                ssize_t cbRet;
                if (copy_from_iter(pReq->abData, cbToWrite, iter) == cbToWrite) {
                    int vrc = VbglR0SfHostReqWriteEmbedded(pSuperInfo->map.root, pReq, sf_r->Handle.hHost,
                                                           offFile, (uint32_t)cbToWrite);
                    sf_i->ModificationTimeAtOurLastWrite = sf_i->ModificationTime;
                    if (RT_SUCCESS(vrc)) {
                        cbRet = pReq->Parms.cb32Write.u.value32;
                        AssertStmt(cbRet <= (ssize_t)cbToWrite, cbRet = cbToWrite);
                        vbsf_reg_write_sync_page_cache(mapping, offFile, (uint32_t)cbRet, pReq->abData,
                                                       NULL /*papSrcPages*/, 0 /*offSrcPage0*/, 0 /*cSrcPages*/);
                        kio->ki_pos = offFile += cbRet;
                        if (offFile > i_size_read(inode))
                            i_size_write(inode, offFile);
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
                        if ((size_t)cbRet < cbToWrite)
                            iov_iter_revert(iter, cbToWrite - cbRet);
# endif
                    } else
                        cbRet = -EPROTO;
                    sf_i->force_restat = 1; /* mtime (and size) may have changed */
                } else
                    cbRet = -EFAULT;
                VbglR0PhysHeapFree(pReq);
                SFLOGFLOW(("vbsf_reg_write_iter: returns %#zx (%zd)\n", cbRet, cbRet));
                return cbRet;
            }
            VbglR0PhysHeapFree(pReq);
        }
    }

    /*
     * Otherwise do the page locking thing.
     */
    return vbsf_reg_write_iter_locking(kio, iter, cbToWrite, offFile, pSuperInfo, sf_r, inode, sf_i, mapping);
}

#endif /* >= 2.6.19 */

/**
 * Used by vbsf_reg_open() and vbsf_inode_atomic_open() to
 *
 * @returns shared folders create flags.
 * @param   fLnxOpen    The linux O_XXX flags to convert.
 * @param   pfHandle    Pointer to vbsf_handle::fFlags.
 * @param   pszCaller   Caller, for logging purposes.
 */
uint32_t vbsf_linux_oflags_to_vbox(unsigned fLnxOpen, uint32_t *pfHandle, const char *pszCaller)
{
    uint32_t fVBoxFlags = SHFL_CF_ACCESS_DENYNONE;

    /*
     * Disposition.
     */
    if (fLnxOpen & O_CREAT) {
        Log(("%s: O_CREAT set\n", pszCaller));
        fVBoxFlags |= SHFL_CF_ACT_CREATE_IF_NEW;
        if (fLnxOpen & O_EXCL) {
            Log(("%s: O_EXCL set\n", pszCaller));
            fVBoxFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS;
        } else if (fLnxOpen & O_TRUNC) {
            Log(("%s: O_TRUNC set\n", pszCaller));
            fVBoxFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
        } else
            fVBoxFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
    } else {
        fVBoxFlags |= SHFL_CF_ACT_FAIL_IF_NEW;
        if (fLnxOpen & O_TRUNC) {
            Log(("%s: O_TRUNC set\n", pszCaller));
            fVBoxFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
        }
    }

    /*
     * Access.
     */
    switch (fLnxOpen & O_ACCMODE) {
        case O_RDONLY:
            fVBoxFlags |= SHFL_CF_ACCESS_READ;
            *pfHandle  |= VBSF_HANDLE_F_READ;
            break;

        case O_WRONLY:
            fVBoxFlags |= SHFL_CF_ACCESS_WRITE;
            *pfHandle  |= VBSF_HANDLE_F_WRITE;
            break;

        case O_RDWR:
            fVBoxFlags |= SHFL_CF_ACCESS_READWRITE;
            *pfHandle  |= VBSF_HANDLE_F_READ | VBSF_HANDLE_F_WRITE;
            break;

        default:
            BUG();
    }

    if (fLnxOpen & O_APPEND) {
        Log(("%s: O_APPEND set\n", pszCaller));
        fVBoxFlags |= SHFL_CF_ACCESS_APPEND;
        *pfHandle  |= VBSF_HANDLE_F_APPEND;
    }

    /*
     * Only directories?
     */
    if (fLnxOpen & O_DIRECTORY) {
        Log(("%s: O_DIRECTORY set\n", pszCaller));
        fVBoxFlags |= SHFL_CF_DIRECTORY;
    }

    return fVBoxFlags;
}


/**
 * Open a regular file.
 *
 * @param inode         the inode
 * @param file          the file
 * @returns 0 on success, Linux error code otherwise
 */
static int vbsf_reg_open(struct inode *inode, struct file *file)
{
    int rc, rc_linux = 0;
    struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
    struct vbsf_inode_info *sf_i       = VBSF_GET_INODE_INFO(inode);
    struct dentry          *dentry     = VBSF_GET_F_DENTRY(file);
    struct vbsf_reg_info   *sf_r;
    VBOXSFCREATEREQ        *pReq;

    SFLOGFLOW(("vbsf_reg_open: inode=%p file=%p flags=%#x %s\n", inode, file, file->f_flags, sf_i ? sf_i->path->String.ach : NULL));
    Assert(pSuperInfo);
    Assert(sf_i);

    sf_r = kmalloc(sizeof(*sf_r), GFP_KERNEL);
    if (!sf_r) {
        LogRelFunc(("could not allocate reg info\n"));
        return -ENOMEM;
    }

    RTListInit(&sf_r->Handle.Entry);
    sf_r->Handle.cRefs  = 1;
    sf_r->Handle.fFlags = VBSF_HANDLE_F_FILE | VBSF_HANDLE_F_MAGIC;
    sf_r->Handle.hHost  = SHFL_HANDLE_NIL;

    /* Already open? */
    if (sf_i->handle != SHFL_HANDLE_NIL) {
        /*
         * This inode was created with vbsf_create_worker(). Check the CreateFlags:
         * O_CREAT, O_TRUNC: inherent true (file was just created). Not sure
         * about the access flags (SHFL_CF_ACCESS_*).
         */
        sf_i->force_restat = 1;
        sf_r->Handle.hHost = sf_i->handle;
        sf_i->handle = SHFL_HANDLE_NIL;
        file->private_data = sf_r;

        sf_r->Handle.fFlags |= VBSF_HANDLE_F_READ | VBSF_HANDLE_F_WRITE; /** @todo fix  */
        vbsf_handle_append(sf_i, &sf_r->Handle);
        SFLOGFLOW(("vbsf_reg_open: returns 0 (#1) - sf_i=%p hHost=%#llx\n", sf_i, sf_r->Handle.hHost));
        return 0;
    }

    pReq = (VBOXSFCREATEREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq) + sf_i->path->u16Size);
    if (!pReq) {
        kfree(sf_r);
        LogRelFunc(("Failed to allocate a VBOXSFCREATEREQ buffer!\n"));
        return -ENOMEM;
    }
    memcpy(&pReq->StrPath, sf_i->path, SHFLSTRING_HEADER_SIZE + sf_i->path->u16Size);
    RT_ZERO(pReq->CreateParms);
    pReq->CreateParms.Handle = SHFL_HANDLE_NIL;

    /* We check the value of pReq->CreateParms.Handle afterwards to
     * find out if the call succeeded or failed, as the API does not seem
     * to cleanly distinguish error and informational messages.
     *
     * Furthermore, we must set pReq->CreateParms.Handle to SHFL_HANDLE_NIL
     * to make the shared folders host service use our fMode parameter */

    /* We ignore O_EXCL, as the Linux kernel seems to call create
       beforehand itself, so O_EXCL should always fail. */
    pReq->CreateParms.CreateFlags = vbsf_linux_oflags_to_vbox(file->f_flags & ~O_EXCL, &sf_r->Handle.fFlags, __FUNCTION__);
    pReq->CreateParms.Info.Attr.fMode = inode->i_mode;
    LogFunc(("vbsf_reg_open: calling VbglR0SfHostReqCreate, file %s, flags=%#x, %#x\n",
             sf_i->path->String.utf8, file->f_flags, pReq->CreateParms.CreateFlags));
    rc = VbglR0SfHostReqCreate(pSuperInfo->map.root, pReq);
    if (RT_FAILURE(rc)) {
        LogFunc(("VbglR0SfHostReqCreate failed flags=%d,%#x rc=%Rrc\n", file->f_flags, pReq->CreateParms.CreateFlags, rc));
        kfree(sf_r);
        VbglR0PhysHeapFree(pReq);
        return -RTErrConvertToErrno(rc);
    }

    if (pReq->CreateParms.Handle != SHFL_HANDLE_NIL) {
        vbsf_dentry_chain_increase_ttl(dentry);
        rc_linux = 0;
    } else {
        switch (pReq->CreateParms.Result) {
            case SHFL_PATH_NOT_FOUND:
                rc_linux = -ENOENT;
                break;
            case SHFL_FILE_NOT_FOUND:
                /** @todo sf_dentry_increase_parent_ttl(file->f_dentry); if we can trust it.  */
                rc_linux = -ENOENT;
                break;
            case SHFL_FILE_EXISTS:
                vbsf_dentry_chain_increase_ttl(dentry);
                rc_linux = -EEXIST;
                break;
            default:
                vbsf_dentry_chain_increase_parent_ttl(dentry);
                rc_linux = 0;
                break;
        }
    }

/** @todo update the inode here, pReq carries the latest stats!  Very helpful
 *        for detecting host side changes. */

    sf_i->force_restat = 1; /** @todo Why?!? */
    sf_r->Handle.hHost = pReq->CreateParms.Handle;
    file->private_data = sf_r;
    vbsf_handle_append(sf_i, &sf_r->Handle);
    VbglR0PhysHeapFree(pReq);
    SFLOGFLOW(("vbsf_reg_open: returns 0 (#2) - sf_i=%p hHost=%#llx\n", sf_i, sf_r->Handle.hHost));
    return rc_linux;
}


/**
 * Close a regular file.
 *
 * @param inode         the inode
 * @param file          the file
 * @returns 0 on success, Linux error code otherwise
 */
static int vbsf_reg_release(struct inode *inode, struct file *file)
{
    struct vbsf_inode_info *sf_i = VBSF_GET_INODE_INFO(inode);
    struct vbsf_reg_info   *sf_r = file->private_data;

    SFLOGFLOW(("vbsf_reg_release: inode=%p file=%p\n", inode, file));
    if (sf_r) {
        struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
        struct address_space   *mapping    = inode->i_mapping;
        Assert(pSuperInfo);

        /* If we're closing the last handle for this inode, make sure the flush
           the mapping or we'll end up in vbsf_writepage without a handle. */
        if (   mapping
            && mapping->nrpages > 0
            /** @todo && last writable handle */ ) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 25)
            if (filemap_fdatawrite(mapping) != -EIO)
#else
            if (   filemap_fdatasync(mapping) == 0
                && fsync_inode_data_buffers(inode) == 0)
#endif
                filemap_fdatawait(inode->i_mapping);
        }

        /* Release sf_r, closing the handle if we're the last user. */
        file->private_data = NULL;
        vbsf_handle_release(&sf_r->Handle, pSuperInfo, "vbsf_reg_release");

        sf_i->handle = SHFL_HANDLE_NIL;
    }
    return 0;
}


/**
 * Wrapper around generic/default seek function that ensures that we've got
 * the up-to-date file size when doing anything relative to EOF.
 *
 * The issue is that the host may extend the file while we weren't looking and
 * if the caller wishes to append data, it may end up overwriting existing data
 * if we operate with a stale size.  So, we always retrieve the file size on EOF
 * relative seeks.
 */
static loff_t vbsf_reg_llseek(struct file *file, loff_t off, int whence)
{
    SFLOGFLOW(("vbsf_reg_llseek: file=%p off=%lld whence=%d\n", file, off, whence));

    switch (whence) {
#ifdef SEEK_HOLE
        case SEEK_HOLE:
        case SEEK_DATA:
#endif
        case SEEK_END: {
            struct vbsf_reg_info *sf_r = file->private_data;
            int rc = vbsf_inode_revalidate_with_handle(VBSF_GET_F_DENTRY(file), sf_r->Handle.hHost,
                                                       true /*fForce*/, false /*fInodeLocked*/);
            if (rc == 0)
                break;
            return rc;
        }
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 8)
    return generic_file_llseek(file, off, whence);
#else
    return default_llseek(file, off, whence);
#endif
}


/**
 * Flush region of file - chiefly mmap/msync.
 *
 * We cannot use the noop_fsync / simple_sync_file here as that means
 * msync(,,MS_SYNC) will return before the data hits the host, thereby
 * causing coherency issues with O_DIRECT access to the same file as
 * well as any host interaction with the file.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 1, 0)
static int vbsf_reg_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    return __generic_file_fsync(file, start, end, datasync);
# else
    return generic_file_fsync(file, start, end, datasync);
# endif
}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
static int vbsf_reg_fsync(struct file *file, int datasync)
{
    return generic_file_fsync(file, datasync);
}
#else /* < 2.6.35 */
static int vbsf_reg_fsync(struct file *file, struct dentry *dentry, int datasync)
{
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
    return simple_fsync(file, dentry, datasync);
# else
    int rc;
    struct inode *inode = dentry->d_inode;
    AssertReturn(inode, -EINVAL);

    /** @todo What about file_fsync()? (<= 2.5.11) */

#  if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 12)
    rc = sync_mapping_buffers(inode->i_mapping);
    if (   rc == 0
        && (inode->i_state & I_DIRTY)
        && ((inode->i_state & I_DIRTY_DATASYNC) || !datasync)
       ) {
        struct writeback_control wbc = {
            .sync_mode = WB_SYNC_ALL,
            .nr_to_write = 0
        };
        rc = sync_inode(inode, &wbc);
    }
#  else  /* < 2.5.12 */
    /** @todo
     * Somethings is buggy here or in the 2.4.21-27.EL kernel I'm testing on.
     *
     * In theory we shouldn't need to do anything here, since msync will call
     * writepage() on each dirty page and we write them out synchronously.  So, the
     * problem is elsewhere...  Doesn't happen all the time either.  Sigh.
     */
    rc = fsync_inode_buffers(inode);
#   if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 10)
    if (rc == 0 && datasync)
        rc = fsync_inode_data_buffers(inode);
#   endif

#  endif /* < 2.5.12 */
    return rc;
# endif
}
#endif /* < 2.6.35 */


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
/**
 * Copy a datablock from one file to another on the host side.
 */
static ssize_t vbsf_reg_copy_file_range(struct file *pFileSrc, loff_t offSrc, struct file *pFileDst, loff_t offDst,
                                        size_t cbRange, unsigned int fFlags)
{
    ssize_t cbRet;
    if (g_uSfLastFunction >= SHFL_FN_COPY_FILE_PART) {
        struct inode           *pInodeSrc     = pFileSrc->f_inode;
        struct vbsf_inode_info *pInodeInfoSrc = VBSF_GET_INODE_INFO(pInodeSrc);
        struct vbsf_super_info *pSuperInfoSrc = VBSF_GET_SUPER_INFO(pInodeSrc->i_sb);
        struct vbsf_reg_info   *pFileInfoSrc  = (struct vbsf_reg_info *)pFileSrc->private_data;
        struct inode           *pInodeDst     = pInodeSrc;
        struct vbsf_inode_info *pInodeInfoDst = VBSF_GET_INODE_INFO(pInodeDst);
        struct vbsf_super_info *pSuperInfoDst = VBSF_GET_SUPER_INFO(pInodeDst->i_sb);
        struct vbsf_reg_info   *pFileInfoDst  = (struct vbsf_reg_info *)pFileDst->private_data;
        VBOXSFCOPYFILEPARTREQ  *pReq;

        /*
         * Some extra validation.
         */
        AssertPtrReturn(pInodeInfoSrc, -EOPNOTSUPP);
        Assert(pInodeInfoSrc->u32Magic == SF_INODE_INFO_MAGIC);
        AssertPtrReturn(pInodeInfoDst, -EOPNOTSUPP);
        Assert(pInodeInfoDst->u32Magic == SF_INODE_INFO_MAGIC);

# if LINUX_VERSION_CODE < KERNEL_VERSION(4, 11, 0)
        if (!S_ISREG(pInodeSrc->i_mode) || !S_ISREG(pInodeDst->i_mode))
            return S_ISDIR(pInodeSrc->i_mode) || S_ISDIR(pInodeDst->i_mode) ? -EISDIR : -EINVAL;
# endif

        /*
         * Allocate the request and issue it.
         */
        pReq = (VBOXSFCOPYFILEPARTREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
        if (pReq) {
            int vrc = VbglR0SfHostReqCopyFilePart(pSuperInfoSrc->map.root, pFileInfoSrc->Handle.hHost, offSrc,
                                                  pSuperInfoDst->map.root, pFileInfoDst->Handle.hHost, offDst,
                                                  cbRange, 0 /*fFlags*/, pReq);
            if (RT_SUCCESS(vrc))
                cbRet = pReq->Parms.cb64ToCopy.u.value64;
            else if (vrc == VERR_NOT_IMPLEMENTED)
                cbRet = -EOPNOTSUPP;
            else
                cbRet = -RTErrConvertToErrno(vrc);

            VbglR0PhysHeapFree(pReq);
        } else
            cbRet = -ENOMEM;
    } else {
        cbRet = -EOPNOTSUPP;
    }
    SFLOGFLOW(("vbsf_reg_copy_file_range: returns %zd\n", cbRet));
    return cbRet;
}
#endif /* > 4.5 */


#ifdef SFLOG_ENABLED
/*
 * This is just for logging page faults and such.
 */

/** Pointer to the ops generic_file_mmap returns the first time it's called. */
static struct vm_operations_struct const *g_pGenericFileVmOps = NULL;
/** Merge of g_LoggingVmOpsTemplate and g_pGenericFileVmOps. */
static struct vm_operations_struct        g_LoggingVmOps;


/* Generic page fault callback: */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
static vm_fault_t vbsf_vmlog_fault(struct vm_fault *vmf)
{
    vm_fault_t rc;
    SFLOGFLOW(("vbsf_vmlog_fault: vmf=%p flags=%#x addr=%p\n", vmf, vmf->flags, vmf->address));
    rc = g_pGenericFileVmOps->fault(vmf);
    SFLOGFLOW(("vbsf_vmlog_fault: returns %d\n", rc));
    return rc;
}
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
static int vbsf_vmlog_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    int rc;
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
    SFLOGFLOW(("vbsf_vmlog_fault: vma=%p vmf=%p flags=%#x addr=%p\n", vma, vmf, vmf->flags, vmf->address));
#  else
    SFLOGFLOW(("vbsf_vmlog_fault: vma=%p vmf=%p flags=%#x addr=%p\n", vma, vmf, vmf->flags, vmf->virtual_address));
#  endif
    rc = g_pGenericFileVmOps->fault(vma, vmf);
    SFLOGFLOW(("vbsf_vmlog_fault: returns %d\n", rc));
    return rc;
}
# endif


/* Special/generic page fault handler: */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 1)
static struct page *vbsf_vmlog_nopage(struct vm_area_struct *vma, unsigned long address, int *type)
{
    struct page *page;
    SFLOGFLOW(("vbsf_vmlog_nopage: vma=%p address=%p type=%p:{%#x}\n", vma, address, type, type ? *type : 0));
    page = g_pGenericFileVmOps->nopage(vma, address, type);
    SFLOGFLOW(("vbsf_vmlog_nopage: returns %p\n", page));
    return page;
}
# else
static struct page *vbsf_vmlog_nopage(struct vm_area_struct *vma, unsigned long address, int write_access_or_unused)
{
    struct page *page;
    SFLOGFLOW(("vbsf_vmlog_nopage: vma=%p address=%p wau=%d\n", vma, address, write_access_or_unused));
    page = g_pGenericFileVmOps->nopage(vma, address, write_access_or_unused);
    SFLOGFLOW(("vbsf_vmlog_nopage: returns %p\n", page));
    return page;
}
# endif /* < 2.6.26 */


/* Special page fault callback for making something writable: */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
static vm_fault_t vbsf_vmlog_page_mkwrite(struct vm_fault *vmf)
{
    vm_fault_t rc;
    SFLOGFLOW(("vbsf_vmlog_page_mkwrite: vmf=%p flags=%#x addr=%p\n", vmf, vmf->flags, vmf->address));
    rc = g_pGenericFileVmOps->page_mkwrite(vmf);
    SFLOGFLOW(("vbsf_vmlog_page_mkwrite: returns %d\n", rc));
    return rc;
}
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30)
static int vbsf_vmlog_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    int rc;
#  if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
    SFLOGFLOW(("vbsf_vmlog_page_mkwrite: vma=%p vmf=%p flags=%#x addr=%p\n", vma, vmf, vmf->flags, vmf->address));
#  else
    SFLOGFLOW(("vbsf_vmlog_page_mkwrite: vma=%p vmf=%p flags=%#x addr=%p\n", vma, vmf, vmf->flags, vmf->virtual_address));
#  endif
    rc = g_pGenericFileVmOps->page_mkwrite(vma, vmf);
    SFLOGFLOW(("vbsf_vmlog_page_mkwrite: returns %d\n", rc));
    return rc;
}
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18)
static int vbsf_vmlog_page_mkwrite(struct vm_area_struct *vma, struct page *page)
{
    int rc;
    SFLOGFLOW(("vbsf_vmlog_page_mkwrite: vma=%p page=%p\n", vma, page));
    rc = g_pGenericFileVmOps->page_mkwrite(vma, page);
    SFLOGFLOW(("vbsf_vmlog_page_mkwrite: returns %d\n", rc));
    return rc;
}
# endif


/* Special page fault callback for mapping pages: */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
static void vbsf_vmlog_map_pages(struct vm_fault *vmf, pgoff_t start, pgoff_t end)
{
    SFLOGFLOW(("vbsf_vmlog_map_pages: vmf=%p (flags=%#x addr=%p) start=%p end=%p\n", vmf, vmf->flags, vmf->address, start, end));
    g_pGenericFileVmOps->map_pages(vmf, start, end);
    SFLOGFLOW(("vbsf_vmlog_map_pages: returns\n"));
}
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
static void vbsf_vmlog_map_pages(struct fault_env *fenv, pgoff_t start, pgoff_t end)
{
    SFLOGFLOW(("vbsf_vmlog_map_pages: fenv=%p (flags=%#x addr=%p) start=%p end=%p\n", fenv, fenv->flags, fenv->address, start, end));
    g_pGenericFileVmOps->map_pages(fenv, start, end);
    SFLOGFLOW(("vbsf_vmlog_map_pages: returns\n"));
}
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
static void vbsf_vmlog_map_pages(struct vm_area_struct *vma, struct vm_fault *vmf)
{
    SFLOGFLOW(("vbsf_vmlog_map_pages: vma=%p vmf=%p (flags=%#x addr=%p)\n", vma, vmf, vmf->flags, vmf->virtual_address));
    g_pGenericFileVmOps->map_pages(vma, vmf);
    SFLOGFLOW(("vbsf_vmlog_map_pages: returns\n"));
}
# endif


/** Overload template. */
static struct vm_operations_struct const g_LoggingVmOpsTemplate = {
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 23)
    .fault = vbsf_vmlog_fault,
# endif
# if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 25)
    .nopage = vbsf_vmlog_nopage,
# endif
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 18)
    .page_mkwrite = vbsf_vmlog_page_mkwrite,
# endif
# if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
    .map_pages = vbsf_vmlog_map_pages,
# endif
};

/** file_operations::mmap wrapper for logging purposes. */
extern int vbsf_reg_mmap(struct file *file, struct vm_area_struct *vma)
{
    int rc;
    SFLOGFLOW(("vbsf_reg_mmap: file=%p vma=%p\n", file, vma));
    rc = generic_file_mmap(file, vma);
    if (rc == 0) {
        /* Merge the ops and template the first time thru (there's a race here). */
        if (g_pGenericFileVmOps == NULL) {
            uintptr_t const    *puSrc1 = (uintptr_t *)vma->vm_ops;
            uintptr_t const    *puSrc2 = (uintptr_t *)&g_LoggingVmOpsTemplate;
            uintptr_t volatile *puDst  = (uintptr_t *)&g_LoggingVmOps;
            size_t              cbLeft = sizeof(g_LoggingVmOps) / sizeof(*puDst);
            while (cbLeft-- > 0) {
                *puDst = *puSrc2 && *puSrc1 ? *puSrc2 : *puSrc1;
                puSrc1++;
                puSrc2++;
                puDst++;
            }
            g_pGenericFileVmOps = vma->vm_ops;
            vma->vm_ops = &g_LoggingVmOps;
        } else if (g_pGenericFileVmOps == vma->vm_ops)
            vma->vm_ops = &g_LoggingVmOps;
        else
            SFLOGFLOW(("vbsf_reg_mmap: Warning: vm_ops=%p, expected %p!\n", vma->vm_ops, g_pGenericFileVmOps));
    }
    SFLOGFLOW(("vbsf_reg_mmap: returns %d\n", rc));
    return rc;
}

#endif /* SFLOG_ENABLED */


/**
 * File operations for regular files.
 *
 * Note on splice_read/splice_write/sendfile:
 *      - Splice was introduced in 2.6.17.  The generic_file_splice_read/write
 *        methods go thru the page cache, which is undesirable and is why we
 *        need to cook our own versions of the code as long as we cannot track
 *        host-side writes and correctly invalidate the guest page-cache.
 *      - Sendfile reimplemented using splice in 2.6.23.
 *      - The default_file_splice_read/write no-page-cache fallback functions,
 *        were introduced in 2.6.31.  The write one work in page units.
 *      - Since linux 3.16 there is iter_file_splice_write that uses iter_write.
 *      - Since linux 4.9 the generic_file_splice_read function started using
 *        read_iter.
 */
struct file_operations vbsf_reg_fops = {
    .open            = vbsf_reg_open,
    .read            = vbsf_reg_read,
    .write           = vbsf_reg_write,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    .read_iter       = vbsf_reg_read_iter,
    .write_iter      = vbsf_reg_write_iter,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    .aio_read        = vbsf_reg_aio_read,
    .aio_write       = vbsf_reg_aio_write,
#endif
    .release         = vbsf_reg_release,
#ifdef SFLOG_ENABLED
    .mmap            = vbsf_reg_mmap,
#else
    .mmap            = generic_file_mmap,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 17) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
    .splice_read     = vbsf_splice_read,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
    .splice_write    = iter_file_splice_write,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 17)
    .splice_write    = vbsf_splice_write,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 30) && LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23)
    .sendfile        = vbsf_reg_sendfile,
#endif
    .llseek          = vbsf_reg_llseek,
    .fsync           = vbsf_reg_fsync,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
    .copy_file_range = vbsf_reg_copy_file_range,
#endif
};


/**
 * Inodes operations for regular files.
 */
struct inode_operations vbsf_reg_iops = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 18)
    .getattr    = vbsf_inode_getattr,
#else
    .revalidate = vbsf_inode_revalidate,
#endif
    .setattr    = vbsf_inode_setattr,
};



/*********************************************************************************************************************************
*   Address Space Operations on Regular Files (for mmap, sendfile, direct I/O)                                                   *
*********************************************************************************************************************************/

/**
 * Used to read the content of a page into the page cache.
 *
 * Needed for mmap and reads+writes when the file is mmapped in a
 * shared+writeable fashion.
 */
static int vbsf_readpage(struct file *file, struct page *page)
{
    struct inode *inode = VBSF_GET_F_DENTRY(file)->d_inode;
    int           err;

    SFLOGFLOW(("vbsf_readpage: inode=%p file=%p page=%p off=%#llx\n", inode, file, page, (uint64_t)page->index << PAGE_SHIFT));
    Assert(PageLocked(page));

    if (PageUptodate(page)) {
        unlock_page(page);
        return 0;
    }

    if (!is_bad_inode(inode)) {
        VBOXSFREADPGLSTREQ *pReq = (VBOXSFREADPGLSTREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
        if (pReq) {
            struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
            struct vbsf_reg_info   *sf_r       = file->private_data;
            uint32_t                cbRead;
            int                     vrc;

            pReq->PgLst.offFirstPage = 0;
            pReq->PgLst.aPages[0]    = page_to_phys(page);
            vrc = VbglR0SfHostReqReadPgLst(pSuperInfo->map.root,
                                           pReq,
                                           sf_r->Handle.hHost,
                                           (uint64_t)page->index << PAGE_SHIFT,
                                           PAGE_SIZE,
                                           1 /*cPages*/);

            cbRead = pReq->Parms.cb32Read.u.value32;
            AssertStmt(cbRead <= PAGE_SIZE, cbRead = PAGE_SIZE);
            VbglR0PhysHeapFree(pReq);

            if (RT_SUCCESS(vrc)) {
                if (cbRead == PAGE_SIZE) {
                    /* likely */
                } else {
                    uint8_t *pbMapped = (uint8_t *)kmap(page);
                    RT_BZERO(&pbMapped[cbRead], PAGE_SIZE - cbRead);
                    kunmap(page);
                    /** @todo truncate the inode file size? */
                }

                flush_dcache_page(page);
                SetPageUptodate(page);
                unlock_page(page);
                return 0;
            }
            err = -RTErrConvertToErrno(vrc);
        } else
            err = -ENOMEM;
    } else
        err = -EIO;
    SetPageError(page);
    unlock_page(page);
    return err;
}


/**
 * Used to write out the content of a dirty page cache page to the host file.
 *
 * Needed for mmap and writes when the file is mmapped in a shared+writeable
 * fashion.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 52)
static int vbsf_writepage(struct page *page, struct writeback_control *wbc)
#else
static int vbsf_writepage(struct page *page)
#endif
{
    struct address_space   *mapping = page->mapping;
    struct inode           *inode   = mapping->host;
    struct vbsf_inode_info *sf_i    = VBSF_GET_INODE_INFO(inode);
    struct vbsf_handle     *pHandle = vbsf_handle_find(sf_i, VBSF_HANDLE_F_WRITE, VBSF_HANDLE_F_APPEND);
    int                     err;

    SFLOGFLOW(("vbsf_writepage: inode=%p page=%p off=%#llx pHandle=%p (%#llx)\n",
               inode, page, (uint64_t)page->index << PAGE_SHIFT, pHandle, pHandle ? pHandle->hHost : 0));

    if (pHandle) {
        struct vbsf_super_info *pSuperInfo = VBSF_GET_SUPER_INFO(inode->i_sb);
        VBOXSFWRITEPGLSTREQ    *pReq       = (VBOXSFWRITEPGLSTREQ *)VbglR0PhysHeapAlloc(sizeof(*pReq));
        if (pReq) {
            uint64_t const cbFile          = i_size_read(inode);
            uint64_t const offInFile       = (uint64_t)page->index << PAGE_SHIFT;
            uint32_t const cbToWrite       = page->index != (cbFile >> PAGE_SHIFT) ? PAGE_SIZE
                                           : (uint32_t)cbFile & (uint32_t)PAGE_OFFSET_MASK;
            int            vrc;

            pReq->PgLst.offFirstPage = 0;
            pReq->PgLst.aPages[0]    = page_to_phys(page);
            vrc = VbglR0SfHostReqWritePgLst(pSuperInfo->map.root,
                                            pReq,
                                            pHandle->hHost,
                                            offInFile,
                                            cbToWrite,
                                            1 /*cPages*/);
            sf_i->ModificationTimeAtOurLastWrite = sf_i->ModificationTime;
            AssertMsgStmt(pReq->Parms.cb32Write.u.value32 == cbToWrite || RT_FAILURE(vrc), /* lazy bird */
                          ("%#x vs %#x\n", pReq->Parms.cb32Write, cbToWrite),
                          vrc = VERR_WRITE_ERROR);
            VbglR0PhysHeapFree(pReq);

            if (RT_SUCCESS(vrc)) {
                /* Update the inode if we've extended the file. */
                /** @todo is this necessary given the cbToWrite calc above? */
                uint64_t const offEndOfWrite = offInFile + cbToWrite;
                if (   offEndOfWrite > cbFile
                    && offEndOfWrite > i_size_read(inode))
                    i_size_write(inode, offEndOfWrite);

                /* Update and unlock the page. */
                if (PageError(page))
                    ClearPageError(page);
                SetPageUptodate(page);
                unlock_page(page);

                vbsf_handle_release(pHandle, pSuperInfo, "vbsf_writepage");
                return 0;
            }

            /*
             * We failed.
             */
            err = -EIO;
        } else
            err = -ENOMEM;
        vbsf_handle_release(pHandle, pSuperInfo, "vbsf_writepage");
    } else {
        /** @todo we could re-open the file here and deal with this... */
        static uint64_t volatile s_cCalls = 0;
        if (s_cCalls++ < 16)
            printk("vbsf_writepage: no writable handle for %s..\n", sf_i->path->String.ach);
        err = -EIO;
    }
    SetPageError(page);
    unlock_page(page);
    return err;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
/**
 * Called when writing thru the page cache (which we shouldn't be doing).
 */
int vbsf_write_begin(struct file *file, struct address_space *mapping, loff_t pos,
                     unsigned len, unsigned flags, struct page **pagep, void **fsdata)
{
    /** @todo r=bird: We shouldn't ever get here, should we?  Because we don't use
     *        the page cache for any writes AFAIK.  We could just as well use
     *        simple_write_begin & simple_write_end here if we think we really
     *        need to have non-NULL function pointers in the table... */
    static uint64_t volatile s_cCalls = 0;
    if (s_cCalls++ < 16) {
        printk("vboxsf: Unexpected call to vbsf_write_begin(pos=%#llx len=%#x flags=%#x)! Please report.\n",
               (unsigned long long)pos, len, flags);
        RTLogBackdoorPrintf("vboxsf: Unexpected call to vbsf_write_begin(pos=%#llx len=%#x flags=%#x)!  Please report.\n",
                            (unsigned long long)pos, len, flags);
# ifdef WARN_ON
        WARN_ON(1);
# endif
    }
    return simple_write_begin(file, mapping, pos, len, flags, pagep, fsdata);
}
#endif /* KERNEL_VERSION >= 2.6.24 */


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 10)

# ifdef VBOX_UEK
#  undef iov_iter /* HACK ALERT! Don't put anything needing vbsf_iov_iter after this fun! */
# endif

/**
 * This is needed to make open accept O_DIRECT as well as dealing with direct
 * I/O requests if we don't intercept them earlier.
 */
# if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
static ssize_t vbsf_direct_IO(struct kiocb *iocb, struct iov_iter *iter)
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
static ssize_t vbsf_direct_IO(struct kiocb *iocb, struct iov_iter *iter, loff_t offset)
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0) || defined(VBOX_UEK)
static ssize_t vbsf_direct_IO(int rw, struct kiocb *iocb, struct iov_iter *iter, loff_t offset)
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 6)
static ssize_t vbsf_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov, loff_t offset, unsigned long nr_segs)
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 55)
static int vbsf_direct_IO(int rw, struct kiocb *iocb, const struct iovec *iov, loff_t offset, unsigned long nr_segs)
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 41)
static int vbsf_direct_IO(int rw, struct file *file, const struct iovec *iov, loff_t offset, unsigned long nr_segs)
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 35)
static int vbsf_direct_IO(int rw, struct inode *inode, const struct iovec *iov, loff_t offset, unsigned long nr_segs)
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 26)
static int vbsf_direct_IO(int rw, struct inode *inode, char *buf, loff_t offset, size_t count)
# elif LINUX_VERSION_CODE == KERNEL_VERSION(2, 4, 21) && defined(I_NEW) /* RHEL3 Frankenkernel.  */
static int vbsf_direct_IO(int rw, struct file *file, struct kiobuf *buf, unsigned long whatever1, int whatever2)
# else
static int vbsf_direct_IO(int rw, struct inode *inode, struct kiobuf *buf, unsigned long whatever1, int whatever2)
# endif
{
    TRACE();
    return -EINVAL;
}

#endif

/**
 * Address space (for the page cache) operations for regular files.
 *
 * @todo the FsPerf touch/flush (mmap) test fails on 4.4.0 (ubuntu 16.04 lts).
 */
struct address_space_operations vbsf_reg_aops = {
    .readpage       = vbsf_readpage,
    .writepage      = vbsf_writepage,
    /** @todo Need .writepages if we want msync performance...  */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 12)
    .set_page_dirty = __set_page_dirty_buffers,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
    .write_begin    = vbsf_write_begin,
    .write_end      = simple_write_end,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 45)
    .prepare_write  = simple_prepare_write,
    .commit_write   = simple_commit_write,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 10)
    .direct_IO      = vbsf_direct_IO,
#endif
};

