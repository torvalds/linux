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
 @File          fsl_qman.h

 @Description   QM header
*//***************************************************************************/
#ifndef __FSL_QMAN_H
#define __FSL_QMAN_H

#include "std_ext.h"
#include "string_ext.h"
#include "qm_ext.h"


/*************************************************/
/*   QMan s/w corenet portal, low-level i/face   */
/*************************************************/
typedef enum {
    e_QmPortalPCI = 0,          /* PI index, cache-inhibited */
    e_QmPortalPCE,              /* PI index, cache-enabled */
    e_QmPortalPVB               /* valid-bit */
} e_QmPortalProduceMode;

typedef enum {
    e_QmPortalEqcrCCI = 0,      /* CI index, cache-inhibited */
    e_QmPortalEqcrCCE           /* CI index, cache-enabled */
} e_QmPortalEqcrConsumeMode;

typedef enum {
    e_QmPortalDqrrCCI = 0,      /* CI index, cache-inhibited */
    e_QmPortalDqrrCCE,          /* CI index, cache-enabled */
    e_QmPortalDqrrDCA           /* Discrete Consumption Acknowledgment */
} e_QmPortalDqrrConsumeMode;

typedef enum {
    e_QmPortalMrCCI = 0,        /* CI index, cache-inhibited */
    e_QmPortalMrCCE             /* CI index, cache-enabled */
} e_QmPortalMrConsumeMode;

typedef enum {
    e_QmPortalDequeuePushMode = 0,  /* SDQCR  + VDQCR */
    e_QmPortalDequeuePullMode       /* PDQCR */
} e_QmPortalDequeueMode;

/* Portal constants */
#define QM_EQCR_SIZE    8
#define QM_DQRR_SIZE    16
#define QM_MR_SIZE      8

/* Hardware constants */

enum qm_isr_reg {
    qm_isr_status = 0,
    qm_isr_enable = 1,
    qm_isr_disable = 2,
    qm_isr_inhibit = 3
};
enum qm_dc_portal {
    qm_dc_portal_fman0 = 0,
    qm_dc_portal_fman1 = 1,
    qm_dc_portal_caam = 2,
    qm_dc_portal_pme = 3
};

/* Represents s/w corenet portal mapped data structures */
struct qm_eqcr_entry;    /* EQCR (EnQueue Command Ring) entries */
struct qm_dqrr_entry;    /* DQRR (DeQueue Response Ring) entries */
struct qm_mr_entry;    /* MR (Message Ring) entries */
struct qm_mc_command;    /* MC (Management Command) command */
struct qm_mc_result;    /* MC result */

/* This type represents a s/w corenet portal space, and is used for creating the
 * portal objects within it (EQCR, DQRR, etc) */
struct qm_portal;

/* When iterating the available portals, this is the exposed config structure */
struct qm_portal_config {
    /* If the caller enables DQRR stashing (and thus wishes to operate the
     * portal from only one cpu), this is the logical CPU that the portal
     * will stash to. Whether stashing is enabled or not, this setting is
     * also used for any "core-affine" portals, ie. default portals
     * associated to the corresponding cpu. -1 implies that there is no core
     * affinity configured. */
    int cpu;
    /* portal interrupt line */
    uintptr_t irq;
    /* The portal's dedicated channel id, use this value for initializing
     * frame queues to target this portal when scheduled. */
    e_QmFQChannel channel;
    /* A mask of which pool channels this portal has dequeue access to
     * (using QM_SDQCR_CHANNELS_POOL(n) for the bitmask) */
    uint32_t pools;
    /* which portal sub-interfaces are already bound (ie. "in use") */
    uint8_t bound;
};
/* qm_portal_config::bound uses these bit masks */
#define QM_BIND_EQCR    0x01
#define QM_BIND_DQRR    0x02
#define QM_BIND_MR      0x04
#define QM_BIND_MC      0x08
#define QM_BIND_ISR     0x10

/* This struct represents a pool channel */
struct qm_pool_channel {
    /* The QM_SDQCR_CHANNELS_POOL(n) bit that corresponds to this channel */
    uint32_t pool;
    /* The channel id, used for initialising frame queues to target this
     * channel. */
    e_QmFQChannel channel;
    /* Bitmask of portal (logical-, not cell-)indices that have dequeue
     * access to this channel;
     * 0x001 -> qm_portal_get(0)
     * 0x002 -> qm_portal_get(1)
     * 0x004 -> qm_portal_get(2)
     * ...
     * 0x200 -> qm_portal_get(9)
     */
    uint32_t portals;
};

/* ------------------------------ */
/* --- Portal enumeration API --- */

/* Obtain the number of portals available */
uint8_t qm_portal_num(void);

/* Obtain a portal handle and configuration information about it */
struct qm_portal *qm_portal_get(uint8_t idx);


/* ------------------------------------ */
/* --- Pool channel enumeration API --- */

/* Obtain a mask of the available pool channels, expressed using
 * QM_SDQCR_CHANNELS_POOL(n). */
uint32_t qm_pools(void);

/* Retrieve a pool channel configuration, given a QM_SDQCR_CHANNEL_POOL(n)
 * bit-mask (the least significant bit of 'mask' is used if more than one bit is
 * set). */
const struct qm_pool_channel *qm_pool_channel(uint32_t mask);

/* Flags to qm_fq_free_flags() */
#define QM_FQ_FREE_WAIT       0x00000001 /* wait if RCR is full */
#define QM_FQ_FREE_WAIT_INT   0x00000002 /* if wait, interruptible? */
#define QM_FQ_FREE_WAIT_SYNC  0x00000004 /* if wait, until consumed? */


#define QM_SDQCR_SOURCE_CHANNELS        0x0
#define QM_SDQCR_SOURCE_SPECIFICWQ      0x40000000
#define QM_SDQCR_COUNT_EXACT1           0x0
#define QM_SDQCR_COUNT_UPTO3            0x20000000
#define QM_SDQCR_DEDICATED_PRECEDENCE   0x10000000
#define QM_SDQCR_TYPE_MASK              0x03000000
#define QM_SDQCR_TYPE_NULL              0x0
#define QM_SDQCR_TYPE_PRIO_QOS          0x01000000
#define QM_SDQCR_TYPE_ACTIVE_QOS        0x02000000
#define QM_SDQCR_TYPE_ACTIVE            0x03000000
#define QM_SDQCR_TYPE_SET(v)            (((v) & 0x03) << (31-7))
#define QM_SDQCR_TOKEN_MASK             0x00ff0000
#define QM_SDQCR_TOKEN_SET(v)           (((v) & 0xff) << 16)
#define QM_SDQCR_TOKEN_GET(v)           (((v) >> 16) & 0xff)
#define QM_SDQCR_CHANNELS_DEDICATED     0x00008000
#define QM_SDQCR_CHANNELS_POOL_MASK     0x00007fff
#define QM_SDQCR_CHANNELS_POOL(n)       (0x00008000 >> (n))
#define QM_SDQCR_SPECIFICWQ_MASK        0x000000f7
#define QM_SDQCR_SPECIFICWQ_DEDICATED   0x00000000
#define QM_SDQCR_SPECIFICWQ_POOL(n)     ((n) << 4)
#define QM_SDQCR_SPECIFICWQ_WQ(n)       (n)

/* For qm_dqrr_vdqcr_set(); Choose one PRECEDENCE. EXACT is optional. Use
 * NUMFRAMES(n) (6-bit) or NUMFRAMES_TILLEMPTY to fill in the frame-count. Use
 * FQID(n) to fill in the frame queue ID. */
#define QM_VDQCR_PRECEDENCE_VDQCR       0x0
#define QM_VDQCR_PRECEDENCE_SDQCR       0x80000000
#define QM_VDQCR_EXACT                  0x40000000
#define QM_VDQCR_NUMFRAMES_MASK         0x3f000000
#define QM_VDQCR_NUMFRAMES_SET(n)       (((n) & 0x3f) << 24)
#define QM_VDQCR_NUMFRAMES_GET(n)       (((n) >> 24) & 0x3f)
#define QM_VDQCR_NUMFRAMES_TILLEMPTY    QM_VDQCR_NUMFRAMES_SET(0)
#define QM_VDQCR_FQID_MASK              0x00ffffff
#define QM_VDQCR_FQID(n)                ((n) & QM_VDQCR_FQID_MASK)

/* For qm_dqrr_pdqcr_set(); Choose one MODE. Choose one COUNT.
 * If MODE==SCHEDULED
 *   Choose SCHEDULED_CHANNELS or SCHEDULED_SPECIFICWQ. Choose one dequeue TYPE.
 *   If CHANNELS,
 *     Choose CHANNELS_DEDICATED and/or CHANNELS_POOL() channels.
 *     You can choose DEDICATED_PRECEDENCE if the portal channel should have
 *     priority.
 *   If SPECIFICWQ,
 *     Either select the work-queue ID with SPECIFICWQ_WQ(), or select the
 *     channel (SPECIFICWQ_DEDICATED or SPECIFICWQ_POOL()) and specify the
 *     work-queue priority (0-7) with SPECIFICWQ_WQ() - either way, you get the
 *     same value.
 * If MODE==UNSCHEDULED
 *     Choose FQID().
 */
#define QM_PDQCR_MODE_SCHEDULED         0x0
#define QM_PDQCR_MODE_UNSCHEDULED       0x80000000
#define QM_PDQCR_SCHEDULED_CHANNELS     0x0
#define QM_PDQCR_SCHEDULED_SPECIFICWQ   0x40000000
#define QM_PDQCR_COUNT_EXACT1           0x0
#define QM_PDQCR_COUNT_UPTO3            0x20000000
#define QM_PDQCR_DEDICATED_PRECEDENCE   0x10000000
#define QM_PDQCR_TYPE_MASK              0x03000000
#define QM_PDQCR_TYPE_NULL              0x0
#define QM_PDQCR_TYPE_PRIO_QOS          0x01000000
#define QM_PDQCR_TYPE_ACTIVE_QOS        0x02000000
#define QM_PDQCR_TYPE_ACTIVE            0x03000000
#define QM_PDQCR_CHANNELS_DEDICATED     0x00008000
#define QM_PDQCR_CHANNELS_POOL(n)       (0x00008000 >> (n))
#define QM_PDQCR_SPECIFICWQ_MASK        0x000000f7
#define QM_PDQCR_SPECIFICWQ_DEDICATED   0x00000000
#define QM_PDQCR_SPECIFICWQ_POOL(n)     ((n) << 4)
#define QM_PDQCR_SPECIFICWQ_WQ(n)       (n)
#define QM_PDQCR_FQID(n)                ((n) & 0xffffff)

/* ------------------------------------- */
/* --- Portal interrupt register API --- */

/* Quick explanation of the Qman interrupt model. Each bit has a source
 * condition, that source is asserted iff the condition is true. Eg. Each
 * DQAVAIL source bit tracks whether the corresponding channel's work queues
 * contain any truly scheduled frame queues. That source exists "asserted" if
 * and while there are truly-scheduled FQs available, it is deasserted as/when
 * there are no longer any truly-scheduled FQs available. The same is true for
 * the various other interrupt source conditions (QM_PIRQ_***). The following
 * steps indicate what those source bits affect;
 *    1. if the corresponding bit is set in the disable register, the source
 *       bit is masked off, we never see any effect from it.
 *    2. otherwise, the corresponding bit is set in the status register. Once
 *       asserted in the status register, it must be write-1-to-clear'd - the
 *       status register bit will stay set even if the source condition
 *       deasserts.
 *    3. if a bit is set in the status register but *not* set in the enable
 *       register, it will not cause the interrupt to assert. Other bits may
 *       still cause the interrupt to assert of course, and a read of the
 *       status register can still reveal un-enabled bits - this is why the
 *       enable and disable registers aren't strictly speaking "opposites".
 *       "Un-enabled" means it won't, on its own, trigger an interrupt.
 *       "Disabled" means it won't even show up in the status register.
 *    4. if a bit is set in the status register *and* the enable register, the
 *       interrupt line will assert if and only if the inhibit register is
 *       zero. The inhibit register is the only interrupt-related register that
 *       does not share the bit definitions - it is a boolean on/off register.
 */

/* Create/destroy */

/* Used by all portal interrupt registers except 'inhibit' */
#define QM_PIRQ_CSCI        0x00100000      /* Congestion State Change */
#define QM_PIRQ_EQCI        0x00080000      /* Enqueue Command Committed */
#define QM_PIRQ_EQRI        0x00040000      /* EQCR Ring (below threshold) */
#define QM_PIRQ_DQRI        0x00020000      /* DQRR Ring (non-empty) */
#define QM_PIRQ_MRI         0x00010000      /* MR Ring (non-empty) */
/* The DQAVAIL interrupt fields break down into these bits; */
#define QM_PIRQ_DQAVAIL     0x0000ffff      /* Channels with frame availability */
#define QM_DQAVAIL_PORTAL   0x8000          /* Portal channel */
#define QM_DQAVAIL_POOL(n)  (0x8000 >> (n)) /* Pool channel, n==[1..15] */

/* These are qm_<reg>_<verb>(). So for example, qm_disable_write() means "write
 * the disable register" rather than "disable the ability to write". */
#define qm_isr_status_read(qm)      __qm_isr_read(qm, qm_isr_status)
#define qm_isr_status_clear(qm, m)  __qm_isr_write(qm, qm_isr_status, m)
#define qm_isr_enable_read(qm)      __qm_isr_read(qm, qm_isr_enable)
#define qm_isr_enable_write(qm, v)  __qm_isr_write(qm, qm_isr_enable, v)
#define qm_isr_disable_read(qm)     __qm_isr_read(qm, qm_isr_disable)
#define qm_isr_disable_write(qm, v) __qm_isr_write(qm, qm_isr_disable, v)
#define qm_isr_inhibit(qm)          __qm_isr_write(qm, qm_isr_inhibit, 1)
#define qm_isr_uninhibit(qm)        __qm_isr_write(qm, qm_isr_inhibit, 0)

/* ------------------------------------------------------- */
/* --- Qman data structures (and associated constants) --- */

/* See David Lapp's "Frame formats" document, "dpateam", Jan 07, 2008 */
#define QM_FD_FORMAT_SG         0x4
#define QM_FD_FORMAT_LONG       0x2
#define QM_FD_FORMAT_COMPOUND   0x1
enum qm_fd_format {
    /* 'contig' implies a contiguous buffer, whereas 'sg' implies a
     * scatter-gather table. 'big' implies a 29-bit length with no offset
     * field, otherwise length is 20-bit and offset is 9-bit. 'compound'
     * implies a s/g-like table, where each entry itself represents a frame
     * (contiguous or scatter-gather) and the 29-bit "length" is
     * interpreted purely for congestion calculations, ie. a "congestion
     * weight". */
    qm_fd_contig = 0,
    qm_fd_contig_big = QM_FD_FORMAT_LONG,
    qm_fd_sg = QM_FD_FORMAT_SG,
    qm_fd_sg_big = QM_FD_FORMAT_SG | QM_FD_FORMAT_LONG,
    qm_fd_compound = QM_FD_FORMAT_COMPOUND
};

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */
#define MEM_MAP_START

 _Packed struct qm_fqd_stashing {
    /* See QM_STASHING_EXCL_<...> */
    volatile uint8_t exclusive;
    volatile uint8_t reserved1:2;
    /* Numbers of cachelines */
    volatile uint8_t annotation_cl:2;
    volatile uint8_t data_cl:2;
    volatile uint8_t context_cl:2;
} _PackedType;

typedef _Packed union {
    /* Treat it as 64-bit opaque */
    _Packed struct {
        volatile uint32_t hi;
        volatile uint32_t lo;
    } _PackedType;
    /* Treat it as s/w portal stashing config */
    /* See 1.5.6.7.1: "FQD Context_A field used for [...] */
    _Packed struct {
        struct qm_fqd_stashing stashing;
        volatile uint8_t reserved1;
        /* 40-bit address of FQ context to
         * stash, must be cacheline-aligned */
        volatile uint8_t context_hi;
        volatile uint32_t context_lo;
    } _PackedType;
} _PackedType u_QmFqdContextA;

/* See 1.5.1.1: "Frame Descriptor (FD)" */
_Packed struct qm_fd {
    volatile uint8_t dd:2;    /* dynamic debug */
    volatile uint8_t liodn_offset:6; /* aka. "Partition ID" in rev1.0 */
    volatile uint8_t bpid;    /* Buffer Pool ID */
    volatile uint8_t eliodn_offset:4;
    volatile uint8_t reserved:4;
    volatile uint8_t addr_hi;    /* high 8-bits of 40-bit address */
    volatile uint32_t addr_lo;    /* low 32-bits of 40-bit address */
    /* The 'format' field indicates the interpretation of the remaining 29
     * bits of the 32-bit word. For packing reasons, it is duplicated in the
     * other union elements. */
    _Packed union {
        /* If 'format' is _contig or _sg, 20b length and 9b offset */
        _Packed struct {
            volatile enum qm_fd_format format:3;
            volatile uint16_t offset:9;
            volatile uint32_t length20:20;
        } _PackedType;
        /* If 'format' is _contig_big or _sg_big, 29b length */
        _Packed struct {
            volatile enum qm_fd_format _format1:3;
            volatile uint32_t length29:29;
        } _PackedType;
        /* If 'format' is _compound, 29b "congestion weight" */
        _Packed struct {
            volatile enum qm_fd_format _format2:3;
            volatile uint32_t cong_weight:29;
        } _PackedType;
        /* For easier/faster copying of this part of the fd (eg. from a
         * DQRR entry to an EQCR entry) copy 'opaque' */
        volatile uint32_t opaque;
    } _PackedType;
    _Packed union {
        volatile uint32_t cmd;
        volatile uint32_t status;
    }_PackedType;
} _PackedType;

#define QM_FD_DD_NULL        0x00
#define QM_FD_PID_MASK        0x3f

/* See 1.5.8.1: "Enqueue Command" */
_Packed struct qm_eqcr_entry {
    volatile uint8_t __dont_write_directly__verb;
    volatile uint8_t dca;
    volatile uint16_t seqnum;
    volatile uint32_t orp;    /* 24-bit */
    volatile uint32_t fqid;    /* 24-bit */
    volatile uint32_t tag;
    volatile struct qm_fd fd;
    volatile uint8_t reserved3[32];
} _PackedType;

#define QM_EQCR_VERB_VBIT               0x80
#define QM_EQCR_VERB_CMD_MASK           0x61    /* but only one value; */
#define QM_EQCR_VERB_CMD_ENQUEUE        0x01
#define QM_EQCR_VERB_COLOUR_MASK        0x18    /* 4 possible values; */
#define QM_EQCR_VERB_COLOUR_GREEN       0x00
#define QM_EQCR_VERB_COLOUR_YELLOW      0x08
#define QM_EQCR_VERB_COLOUR_RED         0x10
#define QM_EQCR_VERB_COLOUR_OVERRIDE    0x18
#define QM_EQCR_VERB_INTERRUPT          0x04    /* on command consumption */
#define QM_EQCR_VERB_ORP                0x02    /* enable order restoration */
#define QM_EQCR_DCA_ENABLE              0x80
#define QM_EQCR_DCA_PARK                0x40
#define QM_EQCR_DCA_IDXMASK             0x0f    /* "DQRR::idx" goes here */
#define QM_EQCR_SEQNUM_NESN             0x8000  /* Advance NESN */
#define QM_EQCR_SEQNUM_NLIS             0x4000  /* More fragments to come */
#define QM_EQCR_SEQNUM_SEQMASK          0x3fff  /* sequence number goes here */
#define QM_EQCR_FQID_NULL               0   /* eg. for an ORP seqnum hole */

/* See 1.5.8.2: "Frame Dequeue Response" */
_Packed struct qm_dqrr_entry {
    volatile uint8_t        verb;
    volatile uint8_t        stat;
    volatile uint16_t       seqnum;    /* 15-bit */
    volatile uint8_t        tok;
    volatile uint8_t        reserved2[3];
    volatile uint32_t       fqid;    /* 24-bit */
    volatile uint32_t       contextB;
    volatile struct qm_fd   fd;
    volatile uint8_t        reserved4[32];
} _PackedType;

#define QM_DQRR_VERB_VBIT               0x80
#define QM_DQRR_VERB_MASK               0x7f    /* where the verb contains; */
#define QM_DQRR_VERB_FRAME_DEQUEUE      0x60    /* "this format" */
#define QM_DQRR_STAT_FQ_EMPTY           0x80    /* FQ empty */
#define QM_DQRR_STAT_FQ_HELDACTIVE      0x40    /* FQ held active */
#define QM_DQRR_STAT_FQ_FORCEELIGIBLE   0x20    /* FQ was force-eligible'd */
#define QM_DQRR_STAT_FD_VALID           0x10    /* has a non-NULL FD */
#define QM_DQRR_STAT_UNSCHEDULED        0x02    /* Unscheduled dequeue */
#define QM_DQRR_STAT_DQCR_EXPIRED       0x01    /* VDQCR or PDQCR expired*/

#define VDQCR_DONE (QM_DQRR_STAT_UNSCHEDULED | QM_DQRR_STAT_DQCR_EXPIRED)


/* See 1.5.8.3: "ERN Message Response" */
/* See 1.5.8.4: "FQ State Change Notification" */
_Packed struct qm_mr_entry {
    volatile uint8_t verb;
    _Packed union {
        _Packed struct {
            volatile uint8_t dca;
            volatile uint16_t seqnum;
            volatile uint8_t rc;        /* Rejection Code */
            volatile uint32_t orp:24;
            volatile uint32_t fqid;    /* 24-bit */
            volatile uint32_t tag;
            volatile struct qm_fd fd;
        } _PackedType ern;
        _Packed struct {
            volatile uint8_t colour:2;    /* See QM_MR_DCERN_COLOUR_* */
            volatile uint8_t reserved1:3;
            volatile enum qm_dc_portal portal:3;
            volatile uint16_t reserved2;
            volatile uint8_t rc;        /* Rejection Code */
            volatile uint32_t reserved3:24;
            volatile uint32_t fqid;    /* 24-bit */
            volatile uint32_t tag;
            volatile struct qm_fd fd;
        } _PackedType dcern;
        _Packed struct {
            volatile uint8_t fqs;        /* Frame Queue Status */
            volatile uint8_t reserved1[6];
            volatile uint32_t fqid;    /* 24-bit */
            volatile uint32_t contextB;
            volatile uint8_t reserved2[16];
        } _PackedType fq;        /* FQRN/FQRNI/FQRL/FQPN */
    } _PackedType;
    volatile uint8_t reserved2[32];
} _PackedType;

#define QM_MR_VERB_VBIT            0x80
/* The "ern" VERB bits match QM_EQCR_VERB_*** so aren't reproduced here. ERNs
 * originating from direct-connect portals ("dcern") use 0x20 as a verb which
 * would be invalid as a s/w enqueue verb. A s/w ERN can be distinguished from
 * the other MR types by noting if the 0x20 bit is unset. */
#define QM_MR_VERB_TYPE_MASK        0x23
#define QM_MR_VERB_DC_ERN           0x20
#define QM_MR_VERB_FQRN             0x21
#define QM_MR_VERB_FQRNI            0x22
#define QM_MR_VERB_FQRL             0x23
#define QM_MR_VERB_FQPN             0x24
#define QM_MR_RC_MASK               0xf0    /* contains one of; */
#define QM_MR_RC_CGR_TAILDROP       0x00
#define QM_MR_RC_WRED               0x10
#define QM_MR_RC_ERROR              0x20
#define QM_MR_RC_ORPWINDOW_EARLY    0x30
#define QM_MR_RC_ORPWINDOW_LATE     0x40
#define QM_MR_RC_FQ_TAILDROP        0x50
#define QM_MR_RC_ORP_RETIRED        0x60
#define QM_MR_RC_ORP_DISABLE        0x70
#define QM_MR_FQS_ORLPRESENT        0x02    /* ORL fragments to come */
#define QM_MR_FQS_NOTEMPTY          0x01    /* FQ has enqueued frames */
#define QM_MR_DCERN_COLOUR_GREEN    0x00
#define QM_MR_DCERN_COLOUR_YELLOW   0x01
#define QM_MR_DCERN_COLOUR_RED      0x02
#define QM_MR_DCERN_COLOUR_OVERRIDE 0x03

/* This identical structure of FQD fields is present in the "Init FQ" command
 * and the "Query FQ" result. It's suctioned out here into its own struct. It's
 * also used as the qman_query_fq() result structure in the high-level API. */

/* TODO What about OAC for intra-class? */
#define QM_FQD_TD_THRESH_OAC_EN     0x4000

_Packed struct qm_fqd {
    _Packed union {
        volatile uint8_t orpc;
        _Packed struct {
            volatile uint8_t reserved1:2;
            volatile uint8_t orprws:3;
            volatile uint8_t oa:1;
            volatile uint8_t olws:2;
        } _PackedType;
    } _PackedType;
    volatile uint8_t cgid;
    volatile uint16_t fq_ctrl;    /* See QM_FQCTRL_<...> */
    _Packed union {
        volatile uint16_t dest_wq;
        _Packed struct {
            volatile uint16_t channel:13; /* enum qm_channel */
            volatile uint16_t wq:3;
        } _PackedType dest;
    } _PackedType;
    volatile uint16_t reserved2:1;
    volatile uint16_t ics_cred:15;
    _Packed union {
        volatile uint16_t td_thresh;
        _Packed struct {
            volatile uint16_t reserved1:3;
            volatile uint16_t mant:8;
            volatile uint16_t exp:5;
        } _PackedType td;
    } _PackedType;
    volatile uint32_t           context_b;
    volatile u_QmFqdContextA    context_a;
} _PackedType;

/* See 1.5.2.2: "Frame Queue Descriptor (FQD)" */
/* Frame Queue Descriptor (FQD) field 'fq_ctrl' uses these constants */
#define QM_FQCTRL_MASK          0x07ff    /* 'fq_ctrl' flags; */
#define QM_FQCTRL_CGE           0x0400    /* Congestion Group Enable */
#define QM_FQCTRL_TDE           0x0200    /* Tail-Drop Enable */
#define QM_FQCTRL_ORP           0x0100    /* ORP Enable */
#define QM_FQCTRL_CTXASTASHING  0x0080    /* Context-A stashing */
#define QM_FQCTRL_CPCSTASH      0x0040    /* CPC Stash Enable */
#define QM_FQCTRL_FORCESFDR     0x0008    /* High-priority SFDRs */
#define QM_FQCTRL_AVOIDBLOCK    0x0004    /* Don't block active */
#define QM_FQCTRL_HOLDACTIVE    0x0002    /* Hold active in portal */
#define QM_FQCTRL_LOCKINCACHE   0x0001    /* Aggressively cache FQD */

/* See 1.5.6.7.1: "FQD Context_A field used for [...] */
/* Frame Queue Descriptor (FQD) field 'CONTEXT_A' uses these constants */
#define QM_STASHING_EXCL_ANNOTATION     0x04
#define QM_STASHING_EXCL_DATA           0x02
#define QM_STASHING_EXCL_CONTEXT        0x01

/* See 1.5.8.4: "FQ State Change Notification" */
/* This struct represents the 32-bit "WR_PARM_[GYR]" parameters in CGR fields
 * and associated commands/responses. The WRED parameters are calculated from
 * these fields as follows;
 *   MaxTH = MA * (2 ^ Mn)
 *   Slope = SA / (2 ^ Sn)
 *    MaxP = 4 * (Pn + 1)
 */
_Packed struct qm_cgr_wr_parm {
    _Packed union {
        volatile uint32_t word;
        _Packed struct {
            volatile uint32_t MA:8;
            volatile uint32_t Mn:5;
            volatile uint32_t SA:7; /* must be between 64-127 */
            volatile uint32_t Sn:6;
            volatile uint32_t Pn:6;
        } _PackedType;
    } _PackedType;
} _PackedType;

/* This struct represents the 13-bit "CS_THRES" CGR field. In the corresponding
 * management commands, this is padded to a 16-bit structure field, so that's
 * how we represent it here. The congestion state threshold is calculated from
 * these fields as follows;
 *   CS threshold = TA * (2 ^ Tn)
 */
_Packed struct qm_cgr_cs_thres {
    volatile uint16_t reserved:3;
    volatile uint16_t TA:8;
    volatile uint16_t Tn:5;
} _PackedType;

/* This identical structure of CGR fields is present in the "Init/Modify CGR"
 * commands and the "Query CGR" result. It's suctioned out here into its own
 * struct. */
_Packed struct __qm_mc_cgr {
    volatile struct qm_cgr_wr_parm wr_parm_g;
    volatile struct qm_cgr_wr_parm wr_parm_y;
    volatile struct qm_cgr_wr_parm wr_parm_r;
    volatile uint8_t wr_en_g;    /* boolean, use QM_CGR_EN */
    volatile uint8_t wr_en_y;    /* boolean, use QM_CGR_EN */
    volatile uint8_t wr_en_r;    /* boolean, use QM_CGR_EN */
    volatile uint8_t cscn_en;    /* boolean, use QM_CGR_EN */
    volatile uint32_t cscn_targ;    /* use QM_CGR_TARG_* */
    volatile uint8_t cstd_en;    /* boolean, use QM_CGR_EN */
    volatile uint8_t cs;        /* boolean, only used in query response */
    volatile struct qm_cgr_cs_thres cs_thres;
    volatile uint8_t frame_mode;  /* boolean, use QM_CGR_EN */
} _PackedType;

#define QM_CGR_EN       0x01 /* For wr_en_*, cscn_en, cstd_en, frame_mode */

/* See 1.5.8.5.1: "Initialize FQ" */
/* See 1.5.8.5.2: "Query FQ" */
/* See 1.5.8.5.3: "Query FQ Non-Programmable Fields" */
/* See 1.5.8.5.4: "Alter FQ State Commands " */
/* See 1.5.8.6.1: "Initialize/Modify CGR" */
/* See 1.5.8.6.2: "Query CGR" */
/* See 1.5.8.6.3: "Query Congestion Group State" */
_Packed struct qm_mc_command {
    volatile uint8_t __dont_write_directly__verb;
    _Packed union {
        _Packed struct qm_mcc_initfq {
            volatile uint8_t reserved1;
            volatile uint16_t we_mask;    /* Write Enable Mask */
            volatile uint32_t fqid;    /* 24-bit */
            volatile uint16_t count;    /* Initialises 'count+1' FQDs */
            volatile struct qm_fqd fqd; /* the FQD fields go here */
            volatile uint8_t reserved3[32];
        } _PackedType initfq;
        _Packed struct qm_mcc_queryfq {
            volatile uint8_t reserved1[3];
            volatile uint32_t fqid;    /* 24-bit */
            volatile uint8_t reserved2[56];
        } _PackedType queryfq;
        _Packed struct qm_mcc_queryfq_np {
            volatile uint8_t reserved1[3];
            volatile uint32_t fqid;    /* 24-bit */
            volatile uint8_t reserved2[56];
        } _PackedType queryfq_np;
        _Packed struct qm_mcc_alterfq {
            volatile uint8_t reserved1[3];
            volatile uint32_t fqid;    /* 24-bit */
            volatile uint8_t reserved2[12];
            volatile uint32_t context_b;
            volatile uint8_t reserved3[40];
        } _PackedType alterfq;
        _Packed struct qm_mcc_initcgr {
            volatile uint8_t reserved1;
            volatile uint16_t we_mask;    /* Write Enable Mask */
            volatile struct __qm_mc_cgr cgr;    /* CGR fields */
            volatile uint8_t reserved2[2];
            volatile uint8_t cgid;
            volatile uint8_t reserved4[32];
        } _PackedType initcgr;
        _Packed struct qm_mcc_querycgr {
            volatile uint8_t reserved1[30];
            volatile uint8_t cgid;
            volatile uint8_t reserved2[32];
        } _PackedType querycgr;
        _Packed struct qm_mcc_querycongestion {
            volatile uint8_t reserved[63];
        } _PackedType querycongestion;
        _Packed struct qm_mcc_querywq {
            volatile uint8_t reserved;
            /* select channel if verb != QUERYWQ_DEDICATED */
            _Packed union {
                volatile uint16_t channel_wq; /* ignores wq (3 lsbits) */
                _Packed struct {
                    volatile uint16_t id:13; /* enum qm_channel */
                    volatile uint16_t reserved1:3;
                } _PackedType channel;
            } _PackedType;
            volatile uint8_t reserved2[60];
        } _PackedType querywq;
    } _PackedType;
} _PackedType;

#define QM_MCC_VERB_VBIT        0x80
#define QM_MCC_VERB_MASK        0x7f    /* where the verb contains; */
#define QM_MCC_VERB_INITFQ_PARKED   0x40
#define QM_MCC_VERB_INITFQ_SCHED    0x41
#define QM_MCC_VERB_QUERYFQ     0x44
#define QM_MCC_VERB_QUERYFQ_NP      0x45    /* "non-programmable" fields */
#define QM_MCC_VERB_QUERYWQ     0x46
#define QM_MCC_VERB_QUERYWQ_DEDICATED   0x47
#define QM_MCC_VERB_ALTER_SCHED     0x48    /* Schedule FQ */
#define QM_MCC_VERB_ALTER_FE        0x49    /* Force Eligible FQ */
#define QM_MCC_VERB_ALTER_RETIRE    0x4a    /* Retire FQ */
#define QM_MCC_VERB_ALTER_OOS       0x4b    /* Take FQ out of service */
#define QM_MCC_VERB_ALTER_RETIRE_CTXB 0x4c  /* Retire FQ with contextB*/
#define QM_MCC_VERB_INITCGR         0x50
#define QM_MCC_VERB_MODIFYCGR       0x51
#define QM_MCC_VERB_QUERYCGR        0x58
#define QM_MCC_VERB_QUERYCONGESTION 0x59
/* INITFQ-specific flags */
#define QM_INITFQ_WE_MASK       0x01ff  /* 'Write Enable' flags; */
#define QM_INITFQ_WE_OAC        0x0100
#define QM_INITFQ_WE_ORPC       0x0080
#define QM_INITFQ_WE_CGID       0x0040
#define QM_INITFQ_WE_FQCTRL     0x0020
#define QM_INITFQ_WE_DESTWQ     0x0010
#define QM_INITFQ_WE_ICSCRED        0x0008
#define QM_INITFQ_WE_TDTHRESH       0x0004
#define QM_INITFQ_WE_CONTEXTB       0x0002
#define QM_INITFQ_WE_CONTEXTA       0x0001
/* INITCGR/MODIFYCGR-specific flags */
#define QM_CGR_WE_MASK          0x07ff  /* 'Write Enable Mask'; */
#define QM_CGR_WE_WR_PARM_G     0x0400
#define QM_CGR_WE_WR_PARM_Y     0x0200
#define QM_CGR_WE_WR_PARM_R     0x0100
#define QM_CGR_WE_WR_EN_G       0x0080
#define QM_CGR_WE_WR_EN_Y       0x0040
#define QM_CGR_WE_WR_EN_R       0x0020
#define QM_CGR_WE_CSCN_EN       0x0010
#define QM_CGR_WE_CSCN_TARG     0x0008
#define QM_CGR_WE_CSTD_EN       0x0004
#define QM_CGR_WE_CS_THRES      0x0002
#define QM_CGR_WE_MODE          0x0001

/* See 1.5.8.5.1: "Initialize FQ" */
/* See 1.5.8.5.2: "Query FQ" */
/* See 1.5.8.5.3: "Query FQ Non-Programmable Fields" */
/* See 1.5.8.5.4: "Alter FQ State Commands " */
/* See 1.5.8.6.1: "Initialize/Modify CGR" */
/* See 1.5.8.6.2: "Query CGR" */
/* See 1.5.8.6.3: "Query Congestion Group State" */
_Packed struct qm_mc_result {
    volatile uint8_t verb;
    volatile uint8_t result;
    _Packed union {
        _Packed struct qm_mcr_initfq {
            volatile uint8_t reserved1[62];
        } _PackedType initfq;
        _Packed struct qm_mcr_queryfq {
            volatile uint8_t reserved1[8];
            volatile struct qm_fqd fqd;    /* the FQD fields are here */
            volatile uint16_t oac;
            volatile uint8_t reserved2[30];
        } _PackedType queryfq;
        _Packed struct qm_mcr_queryfq_np {
            volatile uint8_t reserved1;
            volatile uint8_t state;    /* QM_MCR_NP_STATE_*** */
            volatile uint8_t reserved2;
            volatile uint32_t fqd_link:24;
            volatile uint16_t odp_seq;
            volatile uint16_t orp_nesn;
            volatile uint16_t orp_ea_hseq;
            volatile uint16_t orp_ea_tseq;
            volatile uint8_t reserved3;
            volatile uint32_t orp_ea_hptr:24;
            volatile uint8_t reserved4;
            volatile uint32_t orp_ea_tptr:24;
            volatile uint8_t reserved5;
            volatile uint32_t pfdr_hptr:24;
            volatile uint8_t reserved6;
            volatile uint32_t pfdr_tptr:24;
            volatile uint8_t reserved7[5];
            volatile uint8_t reserved8:7;
            volatile uint8_t is:1;
            volatile uint16_t ics_surp;
            volatile uint32_t byte_cnt;
            volatile uint8_t reserved9;
            volatile uint32_t frm_cnt:24;
            volatile uint32_t reserved10;
            volatile uint16_t ra1_sfdr;    /* QM_MCR_NP_RA1_*** */
            volatile uint16_t ra2_sfdr;    /* QM_MCR_NP_RA2_*** */
            volatile uint16_t reserved11;
            volatile uint16_t od1_sfdr;    /* QM_MCR_NP_OD1_*** */
            volatile uint16_t od2_sfdr;    /* QM_MCR_NP_OD2_*** */
            volatile uint16_t od3_sfdr;    /* QM_MCR_NP_OD3_*** */
        } _PackedType queryfq_np;
        _Packed struct qm_mcr_alterfq {
            volatile uint8_t fqs;        /* Frame Queue Status */
            volatile uint8_t reserved1[61];
        } _PackedType alterfq;
        _Packed struct qm_mcr_initcgr {
            volatile uint8_t reserved1[62];
        } _PackedType initcgr;
        _Packed struct qm_mcr_querycgr {
            volatile uint16_t reserved1;
            volatile struct __qm_mc_cgr cgr; /* CGR fields */
            volatile uint8_t reserved2[3];
            volatile uint32_t reserved3:24;
            volatile uint32_t i_bcnt_hi:8;/* high 8-bits of 40-bit "Instant" */
            volatile uint32_t i_bcnt_lo;    /* low 32-bits of 40-bit */
            volatile uint32_t reserved4:24;
            volatile uint32_t a_bcnt_hi:8;/* high 8-bits of 40-bit "Average" */
            volatile uint32_t a_bcnt_lo;    /* low 32-bits of 40-bit */
            volatile uint32_t lgt;    /* Last Group Tick */
            volatile uint8_t reserved5[12];
        } _PackedType querycgr;
        _Packed struct qm_mcr_querycongestion {
            volatile uint8_t reserved[30];
            /* Access this struct using QM_MCR_QUERYCONGESTION() */
            _Packed struct __qm_mcr_querycongestion {
                volatile uint32_t __state[8];
            } _PackedType state;
        } _PackedType querycongestion;
        _Packed struct qm_mcr_querywq {
            _Packed union {
                volatile uint16_t channel_wq; /* ignores wq (3 lsbits) */
                _Packed struct {
                    volatile uint16_t id:13; /* enum qm_channel */
                    volatile uint16_t reserved:3;
                } _PackedType channel;
            } _PackedType;
            volatile uint8_t reserved[28];
            volatile uint32_t wq_len[8];
        } _PackedType querywq;
    } _PackedType;
} _PackedType;

#define QM_MCR_VERB_RRID        0x80
#define QM_MCR_VERB_MASK        QM_MCC_VERB_MASK
#define QM_MCR_VERB_INITFQ_PARKED   QM_MCC_VERB_INITFQ_PARKED
#define QM_MCR_VERB_INITFQ_SCHED    QM_MCC_VERB_INITFQ_SCHED
#define QM_MCR_VERB_QUERYFQ     QM_MCC_VERB_QUERYFQ
#define QM_MCR_VERB_QUERYFQ_NP      QM_MCC_VERB_QUERYFQ_NP
#define QM_MCR_VERB_QUERYWQ     QM_MCC_VERB_QUERYWQ
#define QM_MCR_VERB_QUERYWQ_DEDICATED   QM_MCC_VERB_QUERYWQ_DEDICATED
#define QM_MCR_VERB_ALTER_SCHED     QM_MCC_VERB_ALTER_SCHED
#define QM_MCR_VERB_ALTER_FE        QM_MCC_VERB_ALTER_FE
#define QM_MCR_VERB_ALTER_RETIRE    QM_MCC_VERB_ALTER_RETIRE
#define QM_MCR_VERB_ALTER_RETIRE_CTXB QM_MCC_VERB_ALTER_RETIRE_CTXB
#define QM_MCR_VERB_ALTER_OOS       QM_MCC_VERB_ALTER_OOS
#define QM_MCR_RESULT_NULL          0x00
#define QM_MCR_RESULT_OK            0xf0
#define QM_MCR_RESULT_ERR_FQID      0xf1
#define QM_MCR_RESULT_ERR_FQSTATE   0xf2
#define QM_MCR_RESULT_ERR_NOTEMPTY  0xf3    /* OOS fails if FQ is !empty */
#define QM_MCR_RESULT_ERR_BADCHANNEL    0xf4
#define QM_MCR_RESULT_PENDING       0xf8
#define QM_MCR_RESULT_ERR_BADCOMMAND    0xff
#define QM_MCR_NP_STATE_FE      0x10
#define QM_MCR_NP_STATE_R       0x08
#define QM_MCR_NP_STATE_MASK        0x07    /* Reads FQD::STATE; */
#define QM_MCR_NP_STATE_OOS     0x00
#define QM_MCR_NP_STATE_RETIRED     0x01
#define QM_MCR_NP_STATE_TEN_SCHED   0x02
#define QM_MCR_NP_STATE_TRU_SCHED   0x03
#define QM_MCR_NP_STATE_PARKED      0x04
#define QM_MCR_NP_STATE_ACTIVE      0x05
#define QM_MCR_NP_PTR_MASK      0x07ff  /* for RA[12] & OD[123] */
#define QM_MCR_NP_RA1_NRA(v)        (((v) >> 14) & 0x3) /* FQD::NRA */
#define QM_MCR_NP_RA2_IT(v)     (((v) >> 14) & 0x1) /* FQD::IT */
#define QM_MCR_NP_OD1_NOD(v)        (((v) >> 14) & 0x3) /* FQD::NOD */
#define QM_MCR_NP_OD3_NPC(v)        (((v) >> 14) & 0x3) /* FQD::NPC */
#define QM_MCR_FQS_ORLPRESENT       0x02    /* ORL fragments to come */
#define QM_MCR_FQS_NOTEMPTY     0x01    /* FQ has enqueued frames */

#define MEM_MAP_END
#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */


/* This extracts the state for congestion group 'n' from a query response.
 * Eg.
 *   uint8_t cgr = [...];
 *   struct qm_mc_result *res = [...];
 *   printf("congestion group %d congestion state: %d\n", cgr,
 *       QM_MCR_QUERYCONGESTION(&res->querycongestion.state, cgr));
 */
#define __CGR_WORD(num)        (num >> 5)
#define __CGR_SHIFT(num)    (num & 0x1f)
static __inline__ int QM_MCR_QUERYCONGESTION(struct __qm_mcr_querycongestion *p,
                    uint8_t cgr)
{
    return (int)(p->__state[__CGR_WORD(cgr)] & (0x80000000 >> __CGR_SHIFT(cgr)));
}


/*********************/
/* Utility interface */
/*********************/

/* Represents an allocator over a range of FQIDs. NB, accesses are not locked,
 * spinlock them yourself if needed. */
struct qman_fqid_pool;

/* Create/destroy a FQID pool, num must be a multiple of 32. NB, _destroy()
 * always succeeds, but returns non-zero if there were "leaked" FQID
 * allocations. */
struct qman_fqid_pool *qman_fqid_pool_create(uint32_t fqid_start, uint32_t num);
int qman_fqid_pool_destroy(struct qman_fqid_pool *pool);
/* Alloc/free a FQID from the range. _alloc() returns zero for success. */
int qman_fqid_pool_alloc(struct qman_fqid_pool *pool, uint32_t *fqid);
void qman_fqid_pool_free(struct qman_fqid_pool *pool, uint32_t fqid);
uint32_t qman_fqid_pool_used(struct qman_fqid_pool *pool);

/*******************************************************************/
/* Managed (aka "shared" or "mux/demux") portal, high-level i/face */
/*******************************************************************/

    /* Congestion Groups */
    /* ----------------- */
/* This wrapper represents a bit-array for the state of the 256 Qman congestion
 * groups. Is also used as a *mask* for congestion groups, eg. so we ignore
 * those that don't concern us. We harness the structure and accessor details
 * already used in the management command to query congestion groups. */
struct qman_cgrs {
    struct __qm_mcr_querycongestion q;
};
static __inline__ void QMAN_CGRS_INIT(struct qman_cgrs *c)
{
    memset(c, 0, sizeof(*c));
}
static __inline__ int QMAN_CGRS_GET(struct qman_cgrs *c, int num)
{
    return QM_MCR_QUERYCONGESTION(&c->q, (uint8_t)num);
}
static __inline__ void QMAN_CGRS_SET(struct qman_cgrs *c, int num)
{
    c->q.__state[__CGR_WORD(num)] |= (0x80000000 >> __CGR_SHIFT(num));
}
static __inline__ void QMAN_CGRS_UNSET(struct qman_cgrs *c, int num)
{
    c->q.__state[__CGR_WORD(num)] &= ~(0x80000000 >> __CGR_SHIFT(num));
}

    /* Portal and Frame Queues */
    /* ----------------------- */

/* This object type represents Qman frame queue descriptors (FQD), and is
 * stored within a cacheline passed to qman_new_fq(). */
struct qman_fq;

/* This enum, and the callback type that returns it, are used when handling
 * dequeued frames via DQRR. Note that for "null" callbacks registered with the
 * portal object (for handling dequeues that do not demux because contextB is
 * NULL), the return value *MUST* be qman_cb_dqrr_consume. */
enum qman_cb_dqrr_result {
    /* DQRR entry can be consumed */
    qman_cb_dqrr_consume,
    /* DQRR entry cannot be consumed now, pause until next poll request */
    qman_cb_dqrr_pause,
    /* Like _consume, but requests parking - FQ must be held-active */
    qman_cb_dqrr_park,
    /* Does not consume, for DCA mode only. This allows out-of-order
     * consumes by explicit calls to qman_dca() and/or the use of implicit
     * DCA via EQCR entries. */
    qman_cb_dqrr_defer
};

/*typedef enum qman_cb_dqrr_result (*qman_cb_dqrr)(t_Handle                   h_Arg,
                                                 t_Handle                   h_QmPortal,
                                                 struct qman_fq             *fq,
                                                 const struct qm_dqrr_entry *dqrr);*/
typedef t_QmReceivedFrameCallback * qman_cb_dqrr;
typedef t_QmReceivedFrameCallback * qman_cb_fqs;
typedef t_QmRejectedFrameCallback * qman_cb_ern;
/* This callback type is used when handling ERNs, FQRNs and FQRLs via MR. They
 * are always consumed after the callback returns. */
typedef void (*qman_cb_mr)(t_Handle                 h_Arg,
                           t_Handle                 h_QmPortal,
                           struct qman_fq           *fq,
                           const struct qm_mr_entry *msg);

struct qman_fq_cb {
    qman_cb_dqrr    dqrr;   /* for dequeued frames */
    qman_cb_ern     ern;   /* for s/w ERNs */
    qman_cb_mr      dc_ern; /* for diverted h/w ERNs */
    qman_cb_mr      fqs;    /* frame-queue state changes*/
};

enum qman_fq_state {
    qman_fq_state_oos,
    qman_fq_state_waiting_parked,
    qman_fq_state_parked,
    qman_fq_state_sched,
    qman_fq_state_retired
};

/* Flags to qman_create_portal() */
#define QMAN_PORTAL_FLAG_IRQ         0x00000001 /* use interrupt handler */
#define QMAN_PORTAL_FLAG_IRQ_FAST    0x00000002 /* ... for fast-path too! */
#define QMAN_PORTAL_FLAG_IRQ_SLOW    0x00000003 /* ... for slow-path too! */
#define QMAN_PORTAL_FLAG_DCA         0x00000004 /* use DCA */
#define QMAN_PORTAL_FLAG_LOCKED      0x00000008 /* multi-core locking */
#define QMAN_PORTAL_FLAG_NOTAFFINE   0x00000010 /* not cpu-default portal */
#define QMAN_PORTAL_FLAG_RSTASH      0x00000020 /* enable DQRR entry stashing */
#define QMAN_PORTAL_FLAG_DSTASH      0x00000040 /* enable data stashing */
#define QMAN_PORTAL_FLAG_RECOVER     0x00000080 /* recovery mode */
#define QMAN_PORTAL_FLAG_WAIT        0x00000100 /* for recovery; can wait */
#define QMAN_PORTAL_FLAG_WAIT_INT    0x00000200 /* for wait; interruptible */
#define QMAN_PORTAL_FLAG_CACHE       0x00000400 /* use cachable area for EQCR/DQRR */

/* Flags to qman_create_fq() */
#define QMAN_FQ_FLAG_NO_ENQUEUE      0x00000001 /* can't enqueue */
#define QMAN_FQ_FLAG_NO_MODIFY       0x00000002 /* can only enqueue */
#define QMAN_FQ_FLAG_TO_DCPORTAL     0x00000004 /* consumed by CAAM/PME/Fman */
#define QMAN_FQ_FLAG_LOCKED          0x00000008 /* multi-core locking */
#define QMAN_FQ_FLAG_RECOVER         0x00000010 /* recovery mode */
#define QMAN_FQ_FLAG_DYNAMIC_FQID    0x00000020 /* (de)allocate fqid */

/* Flags to qman_destroy_fq() */
#define QMAN_FQ_DESTROY_PARKED       0x00000001 /* FQ can be parked or OOS */

/* Flags from qman_fq_state() */
#define QMAN_FQ_STATE_CHANGING       0x80000000 /* 'state' is changing */
#define QMAN_FQ_STATE_NE             0x40000000 /* retired FQ isn't empty */
#define QMAN_FQ_STATE_ORL            0x20000000 /* retired FQ has ORL */
#define QMAN_FQ_STATE_BLOCKOOS       0xe0000000 /* if any are set, no OOS */
#define QMAN_FQ_STATE_CGR_EN         0x10000000 /* CGR enabled */
#define QMAN_FQ_STATE_VDQCR          0x08000000 /* being volatile dequeued */

/* Flags to qman_init_fq() */
#define QMAN_INITFQ_FLAG_SCHED       0x00000001 /* schedule rather than park */
#define QMAN_INITFQ_FLAG_NULL        0x00000002 /* zero 'contextB', no demux */
#define QMAN_INITFQ_FLAG_LOCAL       0x00000004 /* set dest portal */

/* Flags to qman_volatile_dequeue() */
#define QMAN_VOLATILE_FLAG_WAIT_INT  0x00000001 /* if we wait, interruptible? */
#define QMAN_VOLATILE_FLAG_WAIT      0x00000002 /* wait if VDQCR is in use */
#define QMAN_VOLATILE_FLAG_FINISH    0x00000004 /* wait till VDQCR completes */

/* Flags to qman_enqueue(). NB, the strange numbering is to align with
 * hardware, bit-wise. */
#define QMAN_ENQUEUE_FLAG_WAIT       0x00010000 /* wait if EQCR is full */
#define QMAN_ENQUEUE_FLAG_WAIT_INT   0x00020000 /* if wait, interruptible? */
#define QMAN_ENQUEUE_FLAG_WAIT_SYNC  0x00040000 /* if wait, until consumed? */
#define QMAN_ENQUEUE_FLAG_WATCH_CGR  0x00080000 /* watch congestion state */
#define QMAN_ENQUEUE_FLAG_INTERRUPT  0x00000004 /* on command consumption */
#define QMAN_ENQUEUE_FLAG_DCA        0x00008000 /* perform enqueue-DCA */
#define QMAN_ENQUEUE_FLAG_DCA_PARK   0x00004000 /* If DCA, requests park */
#define QMAN_ENQUEUE_FLAG_DCA_PTR(p)        /* If DCA, p is DQRR entry */ \
        (((uint32_t)(p) << 2) & 0x00000f00)
#define QMAN_ENQUEUE_FLAG_C_GREEN    0x00000000 /* choose one C_*** flag */
#define QMAN_ENQUEUE_FLAG_C_YELLOW   0x00000008
#define QMAN_ENQUEUE_FLAG_C_RED      0x00000010
#define QMAN_ENQUEUE_FLAG_C_OVERRIDE 0x00000018
/* For the ORP-specific qman_enqueue_orp() variant, this flag indicates "Not
 * Last In Sequence", ie. a non-terminating fragment. */
#define QMAN_ENQUEUE_FLAG_NLIS       0x01000000
/* - this flag performs no enqueue but fills in an ORP sequence number that
 *   would otherwise block it (eg. if a frame has been dropped). */
#define QMAN_ENQUEUE_FLAG_HOLE       0x02000000
/* - this flag performs no enqueue but advances NESN to the given sequence
 *   number. */
#define QMAN_ENQUEUE_FLAG_NESN       0x04000000

    /* FQ management */
    /* ------------- */
/**
 * qman_free_fq - Deallocates a FQ
 * @fq: the frame queue object to release
 * @flags: bit-mask of QMAN_FQ_FREE_*** options
 *
 * The memory for this frame queue object ('mem' provided in qman_new_fq()) is
 * not deallocated but the caller regains ownership, to do with as desired. The
 * FQ must be in the 'out-of-service' state unless the QMAN_FQ_FREE_PARKED flag
 * is specified, in which case it may also be in the 'parked' state.
 */
void qman_free_fq(struct qman_fq *fq, uint32_t flags);

/**
 * qman_fq_fqid - Queries the frame queue ID of a FQ object
 * @fq: the frame queue object to query
 */
uint32_t qman_fq_fqid(struct qman_fq *fq);

/**
 * qman_fq_state - Queries the state of a FQ object
 * @fq: the frame queue object to query
 * @state: pointer to state enum to return the FQ scheduling state
 * @flags: pointer to state flags to receive QMAN_FQ_STATE_*** bitmask
 *
 * Queries the state of the FQ object, without performing any h/w commands.
 * This captures the state, as seen by the driver, at the time the function
 * executes.
 */
void qman_fq_state(struct qman_fq *fq, enum qman_fq_state *state, uint32_t *flags);

#endif /* __FSL_QMAN_H */
