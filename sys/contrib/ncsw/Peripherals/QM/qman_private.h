/******************************************************************************

 © 1995-2003, 2004, 2005-2011 Freescale Semiconductor, Inc.
 All rights reserved.

 This is proprietary source code of Freescale Semiconductor Inc.,
 and its use is subject to the NetComm Device Drivers EULA.
 The copyright notice above does not evidence any actual or intended
 publication of such source code.

 ALTERNATIVELY, redistribution and use in source and binary forms, with
 or without modification, are permitted provided that the following
 conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Freescale Semiconductor nor the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *

 **************************************************************************/
/******************************************************************************
 @File          qman_private.h

 @Description   QM private header
*//***************************************************************************/
#ifndef __QMAN_PRIVATE_H
#define __QMAN_PRIVATE_H

#include "fsl_qman.h"


#define __ERR_MODULE__  MODULE_QM

#if defined(DEBUG) || !defined(DISABLE_ASSERTIONS)
/* Optionally compile-in assertion-checking */
#define QM_CHECKING
#endif /* defined(DEBUG) || ... */

/* TODO: NB, we currently assume that CORE_MemoryBarier() and lwsync() imply compiler barriers
 * and that dcbzl(), dcbfl(), and dcbi() won't fall victim to compiler or
 * execution reordering with respect to other code/instructions that manipulate
 * the same cacheline. */

#define dcbf(addr)  \
    do { \
        __asm__ __volatile__ ("dcbf 0, %0" : : "r" (addr)); \
    } while(0)

#ifdef CORE_E500MC
#define dcbt_ro(addr)   \
    do { \
        __asm__ __volatile__ ("dcbt 0, %0" : : "r" (addr)); \
    } while(0)

#define dcbt_rw(addr)   \
    do { \
        __asm__ __volatile__ ("dcbtst 0, %0" : : "r" (addr)); \
    } while(0)

#define dcbzl(p) \
    do { \
        __asm__ __volatile__ ("dcbzl 0,%0" : : "r" (p)); \
    } while(0)

#define dcbz_64(p) \
    do { \
        dcbzl(p); \
    } while (0)

#define dcbf_64(p) \
    do { \
        dcbf(p); \
    } while (0)

/* Commonly used combo */
#define dcbit_ro(p) \
    do { \
        dcbi(p); \
        dcbt_ro(p); \
    } while (0)

#else

#define dcbt_ro(p) \
    do { \
        __asm__ __volatile__ ("dcbt 0,%0" : : "r" (p)); \
        lwsync(); \
    } while(0)
#define dcbt_rw(p) \
    do { \
        __asm__ __volatile__ ("dcbtst 0,%0" : : "r" (p)); \
    } while(0)
#define dcbz(p) \
    do { \
        __asm__ __volatile__ ("dcbz 0,%0" : : "r" (p)); \
    } while (0)
#define dcbz_64(p) \
    do { \
        dcbz((char *)p + 32); \
        dcbz(p);    \
    } while (0)
#define dcbf_64(p) \
    do { \
        dcbf((char *)p + 32); \
        dcbf(p); \
    } while (0)
/* Commonly used combo */
#define dcbit_ro(p) \
    do { \
        dcbi(p); \
        dcbi((char *)p + 32); \
        dcbt_ro(p); \
        dcbt_ro((char *)p + 32); \
    } while (0)

#endif /* CORE_E500MC */

#define dcbi(p) dcbf(p)

struct qm_addr {
    void  *addr_ce;    /* cache-enabled */
    void  *addr_ci;    /* cache-inhibited */
};

/* EQCR state */
struct qm_eqcr {
    struct qm_eqcr_entry *ring, *cursor;
    uint8_t ci, available, ithresh, vbit;

#ifdef QM_CHECKING
    uint32_t busy;
    e_QmPortalProduceMode       pmode;
    e_QmPortalEqcrConsumeMode   cmode;
#endif /* QM_CHECKING */
};

/* DQRR state */
struct qm_dqrr {
    struct qm_dqrr_entry *ring, *cursor;
    uint8_t pi, ci, fill, ithresh, vbit, flags;

#ifdef QM_CHECKING
    e_QmPortalDequeueMode       dmode;
    e_QmPortalProduceMode       pmode;
    e_QmPortalDqrrConsumeMode   cmode;
#endif /* QM_CHECKING */
};
#define QM_DQRR_FLAG_RE 0x01 /* Stash ring entries */
#define QM_DQRR_FLAG_SE 0x02 /* Stash data */

/* MR state */
struct qm_mr {
    struct qm_mr_entry *ring, *cursor;
    uint8_t pi, ci, fill, ithresh, vbit;

#ifdef QM_CHECKING
    e_QmPortalProduceMode       pmode;
    e_QmPortalMrConsumeMode     cmode;
#endif /* QM_CHECKING */
};

/* MC state */
struct qm_mc {
    struct qm_mc_command *cr;
    struct qm_mc_result *rr;
    uint8_t rridx, vbit;
#ifdef QM_CHECKING
    enum {
        /* Can be _mc_start()ed */
        mc_idle,
        /* Can be _mc_commit()ed or _mc_abort()ed */
        mc_user,
        /* Can only be _mc_retry()ed */
        mc_hw
    } state;
#endif /* QM_CHECKING */
};

/********************/
/* Portal structure */
/********************/

struct qm_portal {
    /* In the non-QM_CHECKING case, everything up to and
     * including 'mc' fits in a cacheline (yay!). The 'config' part is setup-only, so isn't a
     * cause for a concern. In other words, don't rearrange this structure
     * on a whim, there be dragons ... */
    struct qm_addr addr;
    struct qm_eqcr eqcr;
    struct qm_dqrr dqrr;
    struct qm_mr mr;
    struct qm_mc mc;
    struct qm_portal_config config;
    t_Handle bind_lock;
    /* Logical index (not cell-index) */
    int index;
};

#endif /* __QMAN_PRIVATE_H */
