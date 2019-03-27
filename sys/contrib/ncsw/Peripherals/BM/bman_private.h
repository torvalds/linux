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
 @File          bman_private.h

 @Description   BM header
*//***************************************************************************/
#ifndef __BMAN_PRIV_H
#define __BMAN_PRIV_H

#include "fsl_bman.h"

#define __ERR_MODULE__  MODULE_BM

#if defined(DEBUG) || !defined(DISABLE_ASSERTIONS)
/* Optionally compile-in assertion-checking */
#define BM_CHECKING
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

struct bm_addr {
    void  *addr_ce;    /* cache-enabled */
    void  *addr_ci;    /* cache-inhibited */
};

/* RCR state */
struct bm_rcr {
    struct bm_rcr_entry *ring, *cursor;
    uint8_t ci, available, ithresh, vbit;
#ifdef BM_CHECKING
    uint32_t busy;
    e_BmPortalProduceMode pmode;
    e_BmPortalRcrConsumeMode cmode;
#endif /* BM_CHECKING */
};

/* MC state */
struct bm_mc {
    struct bm_mc_command *cr;
    struct bm_mc_result *rr;
    uint8_t rridx, vbit;
#ifdef BM_CHECKING
    enum {
        /* Can only be _mc_start()ed */
        mc_idle,
        /* Can only be _mc_commit()ed or _mc_abort()ed */
        mc_user,
        /* Can only be _mc_retry()ed */
        mc_hw
    } state;
#endif /* BM_CHECKING */
};

/********************/
/* Portal structure */
/********************/

struct bm_portal {
    struct bm_addr addr;
    struct bm_rcr rcr;
    struct bm_mc mc;
};


#endif /* __BMAN_PRIV_H */
