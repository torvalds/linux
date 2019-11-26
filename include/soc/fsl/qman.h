/* Copyright 2008 - 2016 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __FSL_QMAN_H
#define __FSL_QMAN_H

#include <linux/bitops.h>
#include <linux/device.h>

/* Hardware constants */
#define QM_CHANNEL_SWPORTAL0 0
#define QMAN_CHANNEL_POOL1 0x21
#define QMAN_CHANNEL_CAAM 0x80
#define QMAN_CHANNEL_POOL1_REV3 0x401
#define QMAN_CHANNEL_CAAM_REV3 0x840
extern u16 qm_channel_pool1;
extern u16 qm_channel_caam;

/* Portal processing (interrupt) sources */
#define QM_PIRQ_CSCI	0x00100000	/* Congestion State Change */
#define QM_PIRQ_EQCI	0x00080000	/* Enqueue Command Committed */
#define QM_PIRQ_EQRI	0x00040000	/* EQCR Ring (below threshold) */
#define QM_PIRQ_DQRI	0x00020000	/* DQRR Ring (non-empty) */
#define QM_PIRQ_MRI	0x00010000	/* MR Ring (non-empty) */
/*
 * This mask contains all the interrupt sources that need handling except DQRI,
 * ie. that if present should trigger slow-path processing.
 */
#define QM_PIRQ_SLOW	(QM_PIRQ_CSCI | QM_PIRQ_EQCI | QM_PIRQ_EQRI | \
			 QM_PIRQ_MRI)

/* For qman_static_dequeue_*** APIs */
#define QM_SDQCR_CHANNELS_POOL_MASK	0x00007fff
/* for n in [1,15] */
#define QM_SDQCR_CHANNELS_POOL(n)	(0x00008000 >> (n))
/* for conversion from n of qm_channel */
static inline u32 QM_SDQCR_CHANNELS_POOL_CONV(u16 channel)
{
	return QM_SDQCR_CHANNELS_POOL(channel + 1 - qm_channel_pool1);
}

/* --- QMan data structures (and associated constants) --- */

/* "Frame Descriptor (FD)" */
struct qm_fd {
	union {
		struct {
			u8 cfg8b_w1;
			u8 bpid;	/* Buffer Pool ID */
			u8 cfg8b_w3;
			u8 addr_hi;	/* high 8-bits of 40-bit address */
			__be32 addr_lo;	/* low 32-bits of 40-bit address */
		} __packed;
		__be64 data;
	};
	__be32 cfg;	/* format, offset, length / congestion */
	union {
		__be32 cmd;
		__be32 status;
	};
} __aligned(8);

#define QM_FD_FORMAT_SG		BIT(31)
#define QM_FD_FORMAT_LONG	BIT(30)
#define QM_FD_FORMAT_COMPOUND	BIT(29)
#define QM_FD_FORMAT_MASK	GENMASK(31, 29)
#define QM_FD_OFF_SHIFT		20
#define QM_FD_OFF_MASK		GENMASK(28, 20)
#define QM_FD_LEN_MASK		GENMASK(19, 0)
#define QM_FD_LEN_BIG_MASK	GENMASK(28, 0)

enum qm_fd_format {
	/*
	 * 'contig' implies a contiguous buffer, whereas 'sg' implies a
	 * scatter-gather table. 'big' implies a 29-bit length with no offset
	 * field, otherwise length is 20-bit and offset is 9-bit. 'compound'
	 * implies a s/g-like table, where each entry itself represents a frame
	 * (contiguous or scatter-gather) and the 29-bit "length" is
	 * interpreted purely for congestion calculations, ie. a "congestion
	 * weight".
	 */
	qm_fd_contig = 0,
	qm_fd_contig_big = QM_FD_FORMAT_LONG,
	qm_fd_sg = QM_FD_FORMAT_SG,
	qm_fd_sg_big = QM_FD_FORMAT_SG | QM_FD_FORMAT_LONG,
	qm_fd_compound = QM_FD_FORMAT_COMPOUND
};

static inline dma_addr_t qm_fd_addr(const struct qm_fd *fd)
{
	return be64_to_cpu(fd->data) & 0xffffffffffLLU;
}

static inline u64 qm_fd_addr_get64(const struct qm_fd *fd)
{
	return be64_to_cpu(fd->data) & 0xffffffffffLLU;
}

static inline void qm_fd_addr_set64(struct qm_fd *fd, u64 addr)
{
	fd->addr_hi = upper_32_bits(addr);
	fd->addr_lo = cpu_to_be32(lower_32_bits(addr));
}

/*
 * The 'format' field indicates the interpretation of the remaining
 * 29 bits of the 32-bit word.
 * If 'format' is _contig or _sg, 20b length and 9b offset.
 * If 'format' is _contig_big or _sg_big, 29b length.
 * If 'format' is _compound, 29b "congestion weight".
 */
static inline enum qm_fd_format qm_fd_get_format(const struct qm_fd *fd)
{
	return be32_to_cpu(fd->cfg) & QM_FD_FORMAT_MASK;
}

static inline int qm_fd_get_offset(const struct qm_fd *fd)
{
	return (be32_to_cpu(fd->cfg) & QM_FD_OFF_MASK) >> QM_FD_OFF_SHIFT;
}

static inline int qm_fd_get_length(const struct qm_fd *fd)
{
	return be32_to_cpu(fd->cfg) & QM_FD_LEN_MASK;
}

static inline int qm_fd_get_len_big(const struct qm_fd *fd)
{
	return be32_to_cpu(fd->cfg) & QM_FD_LEN_BIG_MASK;
}

static inline void qm_fd_set_param(struct qm_fd *fd, enum qm_fd_format fmt,
				   int off, int len)
{
	fd->cfg = cpu_to_be32(fmt | (len & QM_FD_LEN_BIG_MASK) |
			      ((off << QM_FD_OFF_SHIFT) & QM_FD_OFF_MASK));
}

#define qm_fd_set_contig(fd, off, len) \
	qm_fd_set_param(fd, qm_fd_contig, off, len)
#define qm_fd_set_sg(fd, off, len) qm_fd_set_param(fd, qm_fd_sg, off, len)
#define qm_fd_set_contig_big(fd, len) \
	qm_fd_set_param(fd, qm_fd_contig_big, 0, len)
#define qm_fd_set_sg_big(fd, len) qm_fd_set_param(fd, qm_fd_sg_big, 0, len)
#define qm_fd_set_compound(fd, len) qm_fd_set_param(fd, qm_fd_compound, 0, len)

static inline void qm_fd_clear_fd(struct qm_fd *fd)
{
	fd->data = 0;
	fd->cfg = 0;
	fd->cmd = 0;
}

/* Scatter/Gather table entry */
struct qm_sg_entry {
	union {
		struct {
			u8 __reserved1[3];
			u8 addr_hi;	/* high 8-bits of 40-bit address */
			__be32 addr_lo;	/* low 32-bits of 40-bit address */
		};
		__be64 data;
	};
	__be32 cfg;	/* E bit, F bit, length */
	u8 __reserved2;
	u8 bpid;
	__be16 offset; /* 13-bit, _res[13-15]*/
} __packed;

#define QM_SG_LEN_MASK	GENMASK(29, 0)
#define QM_SG_OFF_MASK	GENMASK(12, 0)
#define QM_SG_FIN	BIT(30)
#define QM_SG_EXT	BIT(31)

static inline dma_addr_t qm_sg_addr(const struct qm_sg_entry *sg)
{
	return be64_to_cpu(sg->data) & 0xffffffffffLLU;
}

static inline u64 qm_sg_entry_get64(const struct qm_sg_entry *sg)
{
	return be64_to_cpu(sg->data) & 0xffffffffffLLU;
}

static inline void qm_sg_entry_set64(struct qm_sg_entry *sg, u64 addr)
{
	sg->addr_hi = upper_32_bits(addr);
	sg->addr_lo = cpu_to_be32(lower_32_bits(addr));
}

static inline bool qm_sg_entry_is_final(const struct qm_sg_entry *sg)
{
	return be32_to_cpu(sg->cfg) & QM_SG_FIN;
}

static inline bool qm_sg_entry_is_ext(const struct qm_sg_entry *sg)
{
	return be32_to_cpu(sg->cfg) & QM_SG_EXT;
}

static inline int qm_sg_entry_get_len(const struct qm_sg_entry *sg)
{
	return be32_to_cpu(sg->cfg) & QM_SG_LEN_MASK;
}

static inline void qm_sg_entry_set_len(struct qm_sg_entry *sg, int len)
{
	sg->cfg = cpu_to_be32(len & QM_SG_LEN_MASK);
}

static inline void qm_sg_entry_set_f(struct qm_sg_entry *sg, int len)
{
	sg->cfg = cpu_to_be32(QM_SG_FIN | (len & QM_SG_LEN_MASK));
}

static inline int qm_sg_entry_get_off(const struct qm_sg_entry *sg)
{
	return be32_to_cpu(sg->offset) & QM_SG_OFF_MASK;
}

/* "Frame Dequeue Response" */
struct qm_dqrr_entry {
	u8 verb;
	u8 stat;
	__be16 seqnum;	/* 15-bit */
	u8 tok;
	u8 __reserved2[3];
	__be32 fqid;	/* 24-bit */
	__be32 context_b;
	struct qm_fd fd;
	u8 __reserved4[32];
} __packed;
#define QM_DQRR_VERB_VBIT		0x80
#define QM_DQRR_VERB_MASK		0x7f	/* where the verb contains; */
#define QM_DQRR_VERB_FRAME_DEQUEUE	0x60	/* "this format" */
#define QM_DQRR_STAT_FQ_EMPTY		0x80	/* FQ empty */
#define QM_DQRR_STAT_FQ_HELDACTIVE	0x40	/* FQ held active */
#define QM_DQRR_STAT_FQ_FORCEELIGIBLE	0x20	/* FQ was force-eligible'd */
#define QM_DQRR_STAT_FD_VALID		0x10	/* has a non-NULL FD */
#define QM_DQRR_STAT_UNSCHEDULED	0x02	/* Unscheduled dequeue */
#define QM_DQRR_STAT_DQCR_EXPIRED	0x01	/* VDQCR or PDQCR expired*/

/* 'fqid' is a 24-bit field in every h/w descriptor */
#define QM_FQID_MASK	GENMASK(23, 0)
#define qm_fqid_set(p, v) ((p)->fqid = cpu_to_be32((v) & QM_FQID_MASK))
#define qm_fqid_get(p)    (be32_to_cpu((p)->fqid) & QM_FQID_MASK)

/* "ERN Message Response" */
/* "FQ State Change Notification" */
union qm_mr_entry {
	struct {
		u8 verb;
		u8 __reserved[63];
	};
	struct {
		u8 verb;
		u8 dca;
		__be16 seqnum;
		u8 rc;		/* Rej Code: 8-bit */
		u8 __reserved[3];
		__be32 fqid;	/* 24-bit */
		__be32 tag;
		struct qm_fd fd;
		u8 __reserved1[32];
	} __packed ern;
	struct {
		u8 verb;
		u8 fqs;		/* Frame Queue Status */
		u8 __reserved1[6];
		__be32 fqid;	/* 24-bit */
		__be32 context_b;
		u8 __reserved2[48];
	} __packed fq;		/* FQRN/FQRNI/FQRL/FQPN */
};
#define QM_MR_VERB_VBIT			0x80
/*
 * ERNs originating from direct-connect portals ("dcern") use 0x20 as a verb
 * which would be invalid as a s/w enqueue verb. A s/w ERN can be distinguished
 * from the other MR types by noting if the 0x20 bit is unset.
 */
#define QM_MR_VERB_TYPE_MASK		0x27
#define QM_MR_VERB_DC_ERN		0x20
#define QM_MR_VERB_FQRN			0x21
#define QM_MR_VERB_FQRNI		0x22
#define QM_MR_VERB_FQRL			0x23
#define QM_MR_VERB_FQPN			0x24
#define QM_MR_RC_MASK			0xf0	/* contains one of; */
#define QM_MR_RC_CGR_TAILDROP		0x00
#define QM_MR_RC_WRED			0x10
#define QM_MR_RC_ERROR			0x20
#define QM_MR_RC_ORPWINDOW_EARLY	0x30
#define QM_MR_RC_ORPWINDOW_LATE		0x40
#define QM_MR_RC_FQ_TAILDROP		0x50
#define QM_MR_RC_ORPWINDOW_RETIRED	0x60
#define QM_MR_RC_ORP_ZERO		0x70
#define QM_MR_FQS_ORLPRESENT		0x02	/* ORL fragments to come */
#define QM_MR_FQS_NOTEMPTY		0x01	/* FQ has enqueued frames */

/*
 * An identical structure of FQD fields is present in the "Init FQ" command and
 * the "Query FQ" result, it's suctioned out into the "struct qm_fqd" type.
 * Within that, the 'stashing' and 'taildrop' pieces are also factored out, the
 * latter has two inlines to assist with converting to/from the mant+exp
 * representation.
 */
struct qm_fqd_stashing {
	/* See QM_STASHING_EXCL_<...> */
	u8 exclusive;
	/* Numbers of cachelines */
	u8 cl; /* _res[6-7], as[4-5], ds[2-3], cs[0-1] */
};

struct qm_fqd_oac {
	/* "Overhead Accounting Control", see QM_OAC_<...> */
	u8 oac; /* oac[6-7], _res[0-5] */
	/* Two's-complement value (-128 to +127) */
	s8 oal; /* "Overhead Accounting Length" */
};

struct qm_fqd {
	/* _res[6-7], orprws[3-5], oa[2], olws[0-1] */
	u8 orpc;
	u8 cgid;
	__be16 fq_ctrl;	/* See QM_FQCTRL_<...> */
	__be16 dest_wq;	/* channel[3-15], wq[0-2] */
	__be16 ics_cred; /* 15-bit */
	/*
	 * For "Initialize Frame Queue" commands, the write-enable mask
	 * determines whether 'td' or 'oac_init' is observed. For query
	 * commands, this field is always 'td', and 'oac_query' (below) reflects
	 * the Overhead ACcounting values.
	 */
	union {
		__be16 td; /* "Taildrop": _res[13-15], mant[5-12], exp[0-4] */
		struct qm_fqd_oac oac_init;
	};
	__be32 context_b;
	union {
		/* Treat it as 64-bit opaque */
		__be64 opaque;
		struct {
			__be32 hi;
			__be32 lo;
		};
		/* Treat it as s/w portal stashing config */
		/* see "FQD Context_A field used for [...]" */
		struct {
			struct qm_fqd_stashing stashing;
			/*
			 * 48-bit address of FQ context to
			 * stash, must be cacheline-aligned
			 */
			__be16 context_hi;
			__be32 context_lo;
		} __packed;
	} context_a;
	struct qm_fqd_oac oac_query;
} __packed;

#define QM_FQD_CHAN_OFF		3
#define QM_FQD_WQ_MASK		GENMASK(2, 0)
#define QM_FQD_TD_EXP_MASK	GENMASK(4, 0)
#define QM_FQD_TD_MANT_OFF	5
#define QM_FQD_TD_MANT_MASK	GENMASK(12, 5)
#define QM_FQD_TD_MAX		0xe0000000
#define QM_FQD_TD_MANT_MAX	0xff
#define QM_FQD_OAC_OFF		6
#define QM_FQD_AS_OFF		4
#define QM_FQD_DS_OFF		2
#define QM_FQD_XS_MASK		0x3

/* 64-bit converters for context_hi/lo */
static inline u64 qm_fqd_stashing_get64(const struct qm_fqd *fqd)
{
	return be64_to_cpu(fqd->context_a.opaque) & 0xffffffffffffULL;
}

static inline dma_addr_t qm_fqd_stashing_addr(const struct qm_fqd *fqd)
{
	return be64_to_cpu(fqd->context_a.opaque) & 0xffffffffffffULL;
}

static inline u64 qm_fqd_context_a_get64(const struct qm_fqd *fqd)
{
	return qm_fqd_stashing_get64(fqd);
}

static inline void qm_fqd_stashing_set64(struct qm_fqd *fqd, u64 addr)
{
	fqd->context_a.context_hi = cpu_to_be16(upper_32_bits(addr));
	fqd->context_a.context_lo = cpu_to_be32(lower_32_bits(addr));
}

static inline void qm_fqd_context_a_set64(struct qm_fqd *fqd, u64 addr)
{
	fqd->context_a.hi = cpu_to_be32(upper_32_bits(addr));
	fqd->context_a.lo = cpu_to_be32(lower_32_bits(addr));
}

/* convert a threshold value into mant+exp representation */
static inline int qm_fqd_set_taildrop(struct qm_fqd *fqd, u32 val,
				      int roundup)
{
	u32 e = 0;
	int td, oddbit = 0;

	if (val > QM_FQD_TD_MAX)
		return -ERANGE;

	while (val > QM_FQD_TD_MANT_MAX) {
		oddbit = val & 1;
		val >>= 1;
		e++;
		if (roundup && oddbit)
			val++;
	}

	td = (val << QM_FQD_TD_MANT_OFF) & QM_FQD_TD_MANT_MASK;
	td |= (e & QM_FQD_TD_EXP_MASK);
	fqd->td = cpu_to_be16(td);
	return 0;
}
/* and the other direction */
static inline int qm_fqd_get_taildrop(const struct qm_fqd *fqd)
{
	int td = be16_to_cpu(fqd->td);

	return ((td & QM_FQD_TD_MANT_MASK) >> QM_FQD_TD_MANT_OFF)
		<< (td & QM_FQD_TD_EXP_MASK);
}

static inline void qm_fqd_set_stashing(struct qm_fqd *fqd, u8 as, u8 ds, u8 cs)
{
	struct qm_fqd_stashing *st = &fqd->context_a.stashing;

	st->cl = ((as & QM_FQD_XS_MASK) << QM_FQD_AS_OFF) |
		 ((ds & QM_FQD_XS_MASK) << QM_FQD_DS_OFF) |
		 (cs & QM_FQD_XS_MASK);
}

static inline u8 qm_fqd_get_stashing(const struct qm_fqd *fqd)
{
	return fqd->context_a.stashing.cl;
}

static inline void qm_fqd_set_oac(struct qm_fqd *fqd, u8 val)
{
	fqd->oac_init.oac = val << QM_FQD_OAC_OFF;
}

static inline void qm_fqd_set_oal(struct qm_fqd *fqd, s8 val)
{
	fqd->oac_init.oal = val;
}

static inline void qm_fqd_set_destwq(struct qm_fqd *fqd, int ch, int wq)
{
	fqd->dest_wq = cpu_to_be16((ch << QM_FQD_CHAN_OFF) |
				   (wq & QM_FQD_WQ_MASK));
}

static inline int qm_fqd_get_chan(const struct qm_fqd *fqd)
{
	return be16_to_cpu(fqd->dest_wq) >> QM_FQD_CHAN_OFF;
}

static inline int qm_fqd_get_wq(const struct qm_fqd *fqd)
{
	return be16_to_cpu(fqd->dest_wq) & QM_FQD_WQ_MASK;
}

/* See "Frame Queue Descriptor (FQD)" */
/* Frame Queue Descriptor (FQD) field 'fq_ctrl' uses these constants */
#define QM_FQCTRL_MASK		0x07ff	/* 'fq_ctrl' flags; */
#define QM_FQCTRL_CGE		0x0400	/* Congestion Group Enable */
#define QM_FQCTRL_TDE		0x0200	/* Tail-Drop Enable */
#define QM_FQCTRL_CTXASTASHING	0x0080	/* Context-A stashing */
#define QM_FQCTRL_CPCSTASH	0x0040	/* CPC Stash Enable */
#define QM_FQCTRL_FORCESFDR	0x0008	/* High-priority SFDRs */
#define QM_FQCTRL_AVOIDBLOCK	0x0004	/* Don't block active */
#define QM_FQCTRL_HOLDACTIVE	0x0002	/* Hold active in portal */
#define QM_FQCTRL_PREFERINCACHE	0x0001	/* Aggressively cache FQD */
#define QM_FQCTRL_LOCKINCACHE	QM_FQCTRL_PREFERINCACHE /* older naming */

/* See "FQD Context_A field used for [...] */
/* Frame Queue Descriptor (FQD) field 'CONTEXT_A' uses these constants */
#define QM_STASHING_EXCL_ANNOTATION	0x04
#define QM_STASHING_EXCL_DATA		0x02
#define QM_STASHING_EXCL_CTX		0x01

/* See "Intra Class Scheduling" */
/* FQD field 'OAC' (Overhead ACcounting) uses these constants */
#define QM_OAC_ICS		0x2 /* Accounting for Intra-Class Scheduling */
#define QM_OAC_CG		0x1 /* Accounting for Congestion Groups */

/*
 * This struct represents the 32-bit "WR_PARM_[GYR]" parameters in CGR fields
 * and associated commands/responses. The WRED parameters are calculated from
 * these fields as follows;
 *   MaxTH = MA * (2 ^ Mn)
 *   Slope = SA / (2 ^ Sn)
 *    MaxP = 4 * (Pn + 1)
 */
struct qm_cgr_wr_parm {
	/* MA[24-31], Mn[19-23], SA[12-18], Sn[6-11], Pn[0-5] */
	__be32 word;
};
/*
 * This struct represents the 13-bit "CS_THRES" CGR field. In the corresponding
 * management commands, this is padded to a 16-bit structure field, so that's
 * how we represent it here. The congestion state threshold is calculated from
 * these fields as follows;
 *   CS threshold = TA * (2 ^ Tn)
 */
struct qm_cgr_cs_thres {
	/* _res[13-15], TA[5-12], Tn[0-4] */
	__be16 word;
};
/*
 * This identical structure of CGR fields is present in the "Init/Modify CGR"
 * commands and the "Query CGR" result. It's suctioned out here into its own
 * struct.
 */
struct __qm_mc_cgr {
	struct qm_cgr_wr_parm wr_parm_g;
	struct qm_cgr_wr_parm wr_parm_y;
	struct qm_cgr_wr_parm wr_parm_r;
	u8 wr_en_g;	/* boolean, use QM_CGR_EN */
	u8 wr_en_y;	/* boolean, use QM_CGR_EN */
	u8 wr_en_r;	/* boolean, use QM_CGR_EN */
	u8 cscn_en;	/* boolean, use QM_CGR_EN */
	union {
		struct {
			__be16 cscn_targ_upd_ctrl; /* use QM_CGR_TARG_UDP_* */
			__be16 cscn_targ_dcp_low;
		};
		__be32 cscn_targ;	/* use QM_CGR_TARG_* */
	};
	u8 cstd_en;	/* boolean, use QM_CGR_EN */
	u8 cs;		/* boolean, only used in query response */
	struct qm_cgr_cs_thres cs_thres; /* use qm_cgr_cs_thres_set64() */
	u8 mode;	/* QMAN_CGR_MODE_FRAME not supported in rev1.0 */
} __packed;
#define QM_CGR_EN		0x01 /* For wr_en_*, cscn_en, cstd_en */
#define QM_CGR_TARG_UDP_CTRL_WRITE_BIT	0x8000 /* value written to portal bit*/
#define QM_CGR_TARG_UDP_CTRL_DCP	0x4000 /* 0: SWP, 1: DCP */
#define QM_CGR_TARG_PORTAL(n)	(0x80000000 >> (n)) /* s/w portal, 0-9 */
#define QM_CGR_TARG_FMAN0	0x00200000 /* direct-connect portal: fman0 */
#define QM_CGR_TARG_FMAN1	0x00100000 /*			   : fman1 */
/* Convert CGR thresholds to/from "cs_thres" format */
static inline u64 qm_cgr_cs_thres_get64(const struct qm_cgr_cs_thres *th)
{
	int thres = be16_to_cpu(th->word);

	return ((thres >> 5) & 0xff) << (thres & 0x1f);
}

static inline int qm_cgr_cs_thres_set64(struct qm_cgr_cs_thres *th, u64 val,
					int roundup)
{
	u32 e = 0;
	int oddbit = 0;

	while (val > 0xff) {
		oddbit = val & 1;
		val >>= 1;
		e++;
		if (roundup && oddbit)
			val++;
	}
	th->word = cpu_to_be16(((val & 0xff) << 5) | (e & 0x1f));
	return 0;
}

/* "Initialize FQ" */
struct qm_mcc_initfq {
	u8 __reserved1[2];
	__be16 we_mask;	/* Write Enable Mask */
	__be32 fqid;	/* 24-bit */
	__be16 count;	/* Initialises 'count+1' FQDs */
	struct qm_fqd fqd; /* the FQD fields go here */
	u8 __reserved2[30];
} __packed;
/* "Initialize/Modify CGR" */
struct qm_mcc_initcgr {
	u8 __reserve1[2];
	__be16 we_mask;	/* Write Enable Mask */
	struct __qm_mc_cgr cgr;	/* CGR fields */
	u8 __reserved2[2];
	u8 cgid;
	u8 __reserved3[32];
} __packed;

/* INITFQ-specific flags */
#define QM_INITFQ_WE_MASK		0x01ff	/* 'Write Enable' flags; */
#define QM_INITFQ_WE_OAC		0x0100
#define QM_INITFQ_WE_ORPC		0x0080
#define QM_INITFQ_WE_CGID		0x0040
#define QM_INITFQ_WE_FQCTRL		0x0020
#define QM_INITFQ_WE_DESTWQ		0x0010
#define QM_INITFQ_WE_ICSCRED		0x0008
#define QM_INITFQ_WE_TDTHRESH		0x0004
#define QM_INITFQ_WE_CONTEXTB		0x0002
#define QM_INITFQ_WE_CONTEXTA		0x0001
/* INITCGR/MODIFYCGR-specific flags */
#define QM_CGR_WE_MASK			0x07ff	/* 'Write Enable Mask'; */
#define QM_CGR_WE_WR_PARM_G		0x0400
#define QM_CGR_WE_WR_PARM_Y		0x0200
#define QM_CGR_WE_WR_PARM_R		0x0100
#define QM_CGR_WE_WR_EN_G		0x0080
#define QM_CGR_WE_WR_EN_Y		0x0040
#define QM_CGR_WE_WR_EN_R		0x0020
#define QM_CGR_WE_CSCN_EN		0x0010
#define QM_CGR_WE_CSCN_TARG		0x0008
#define QM_CGR_WE_CSTD_EN		0x0004
#define QM_CGR_WE_CS_THRES		0x0002
#define QM_CGR_WE_MODE			0x0001

#define QMAN_CGR_FLAG_USE_INIT	     0x00000001
#define QMAN_CGR_MODE_FRAME          0x00000001

	/* Portal and Frame Queues */
/* Represents a managed portal */
struct qman_portal;

/*
 * This object type represents QMan frame queue descriptors (FQD), it is
 * cacheline-aligned, and initialised by qman_create_fq(). The structure is
 * defined further down.
 */
struct qman_fq;

/*
 * This object type represents a QMan congestion group, it is defined further
 * down.
 */
struct qman_cgr;

/*
 * This enum, and the callback type that returns it, are used when handling
 * dequeued frames via DQRR. Note that for "null" callbacks registered with the
 * portal object (for handling dequeues that do not demux because context_b is
 * NULL), the return value *MUST* be qman_cb_dqrr_consume.
 */
enum qman_cb_dqrr_result {
	/* DQRR entry can be consumed */
	qman_cb_dqrr_consume,
	/* Like _consume, but requests parking - FQ must be held-active */
	qman_cb_dqrr_park,
	/* Does not consume, for DCA mode only. */
	qman_cb_dqrr_defer,
	/*
	 * Stop processing without consuming this ring entry. Exits the current
	 * qman_p_poll_dqrr() or interrupt-handling, as appropriate. If within
	 * an interrupt handler, the callback would typically call
	 * qman_irqsource_remove(QM_PIRQ_DQRI) before returning this value,
	 * otherwise the interrupt will reassert immediately.
	 */
	qman_cb_dqrr_stop,
	/* Like qman_cb_dqrr_stop, but consumes the current entry. */
	qman_cb_dqrr_consume_stop
};
typedef enum qman_cb_dqrr_result (*qman_cb_dqrr)(struct qman_portal *qm,
					struct qman_fq *fq,
					const struct qm_dqrr_entry *dqrr);

/*
 * This callback type is used when handling ERNs, FQRNs and FQRLs via MR. They
 * are always consumed after the callback returns.
 */
typedef void (*qman_cb_mr)(struct qman_portal *qm, struct qman_fq *fq,
			   const union qm_mr_entry *msg);

/*
 * s/w-visible states. Ie. tentatively scheduled + truly scheduled + active +
 * held-active + held-suspended are just "sched". Things like "retired" will not
 * be assumed until it is complete (ie. QMAN_FQ_STATE_CHANGING is set until
 * then, to indicate it's completing and to gate attempts to retry the retire
 * command). Note, park commands do not set QMAN_FQ_STATE_CHANGING because it's
 * technically impossible in the case of enqueue DCAs (which refer to DQRR ring
 * index rather than the FQ that ring entry corresponds to), so repeated park
 * commands are allowed (if you're silly enough to try) but won't change FQ
 * state, and the resulting park notifications move FQs from "sched" to
 * "parked".
 */
enum qman_fq_state {
	qman_fq_state_oos,
	qman_fq_state_parked,
	qman_fq_state_sched,
	qman_fq_state_retired
};

#define QMAN_FQ_STATE_CHANGING	     0x80000000 /* 'state' is changing */
#define QMAN_FQ_STATE_NE	     0x40000000 /* retired FQ isn't empty */
#define QMAN_FQ_STATE_ORL	     0x20000000 /* retired FQ has ORL */
#define QMAN_FQ_STATE_BLOCKOOS	     0xe0000000 /* if any are set, no OOS */
#define QMAN_FQ_STATE_CGR_EN	     0x10000000 /* CGR enabled */
#define QMAN_FQ_STATE_VDQCR	     0x08000000 /* being volatile dequeued */

/*
 * Frame queue objects (struct qman_fq) are stored within memory passed to
 * qman_create_fq(), as this allows stashing of caller-provided demux callback
 * pointers at no extra cost to stashing of (driver-internal) FQ state. If the
 * caller wishes to add per-FQ state and have it benefit from dequeue-stashing,
 * they should;
 *
 * (a) extend the qman_fq structure with their state; eg.
 *
 *     // myfq is allocated and driver_fq callbacks filled in;
 *     struct my_fq {
 *	   struct qman_fq base;
 *	   int an_extra_field;
 *	   [ ... add other fields to be associated with each FQ ...]
 *     } *myfq = some_my_fq_allocator();
 *     struct qman_fq *fq = qman_create_fq(fqid, flags, &myfq->base);
 *
 *     // in a dequeue callback, access extra fields from 'fq' via a cast;
 *     struct my_fq *myfq = (struct my_fq *)fq;
 *     do_something_with(myfq->an_extra_field);
 *     [...]
 *
 * (b) when and if configuring the FQ for context stashing, specify how ever
 *     many cachelines are required to stash 'struct my_fq', to accelerate not
 *     only the QMan driver but the callback as well.
 */

struct qman_fq_cb {
	qman_cb_dqrr dqrr;	/* for dequeued frames */
	qman_cb_mr ern;		/* for s/w ERNs */
	qman_cb_mr fqs;		/* frame-queue state changes*/
};

struct qman_fq {
	/* Caller of qman_create_fq() provides these demux callbacks */
	struct qman_fq_cb cb;
	/*
	 * These are internal to the driver, don't touch. In particular, they
	 * may change, be removed, or extended (so you shouldn't rely on
	 * sizeof(qman_fq) being a constant).
	 */
	u32 fqid, idx;
	unsigned long flags;
	enum qman_fq_state state;
	int cgr_groupid;
};

/*
 * This callback type is used when handling congestion group entry/exit.
 * 'congested' is non-zero on congestion-entry, and zero on congestion-exit.
 */
typedef void (*qman_cb_cgr)(struct qman_portal *qm,
			    struct qman_cgr *cgr, int congested);

struct qman_cgr {
	/* Set these prior to qman_create_cgr() */
	u32 cgrid; /* 0..255, but u32 to allow specials like -1, 256, etc.*/
	qman_cb_cgr cb;
	/* These are private to the driver */
	u16 chan; /* portal channel this object is created on */
	struct list_head node;
};

/* Flags to qman_create_fq() */
#define QMAN_FQ_FLAG_NO_ENQUEUE	     0x00000001 /* can't enqueue */
#define QMAN_FQ_FLAG_NO_MODIFY	     0x00000002 /* can only enqueue */
#define QMAN_FQ_FLAG_TO_DCPORTAL     0x00000004 /* consumed by CAAM/PME/Fman */
#define QMAN_FQ_FLAG_DYNAMIC_FQID    0x00000020 /* (de)allocate fqid */

/* Flags to qman_init_fq() */
#define QMAN_INITFQ_FLAG_SCHED	     0x00000001 /* schedule rather than park */
#define QMAN_INITFQ_FLAG_LOCAL	     0x00000004 /* set dest portal */

/*
 * For qman_volatile_dequeue(); Choose one PRECEDENCE. EXACT is optional. Use
 * NUMFRAMES(n) (6-bit) or NUMFRAMES_TILLEMPTY to fill in the frame-count. Use
 * FQID(n) to fill in the frame queue ID.
 */
#define QM_VDQCR_PRECEDENCE_VDQCR	0x0
#define QM_VDQCR_PRECEDENCE_SDQCR	0x80000000
#define QM_VDQCR_EXACT			0x40000000
#define QM_VDQCR_NUMFRAMES_MASK		0x3f000000
#define QM_VDQCR_NUMFRAMES_SET(n)	(((n) & 0x3f) << 24)
#define QM_VDQCR_NUMFRAMES_GET(n)	(((n) >> 24) & 0x3f)
#define QM_VDQCR_NUMFRAMES_TILLEMPTY	QM_VDQCR_NUMFRAMES_SET(0)

#define QMAN_VOLATILE_FLAG_WAIT	     0x00000001 /* wait if VDQCR is in use */
#define QMAN_VOLATILE_FLAG_WAIT_INT  0x00000002 /* if wait, interruptible? */
#define QMAN_VOLATILE_FLAG_FINISH    0x00000004 /* wait till VDQCR completes */

/* "Query FQ Non-Programmable Fields" */
struct qm_mcr_queryfq_np {
	u8 verb;
	u8 result;
	u8 __reserved1;
	u8 state;		/* QM_MCR_NP_STATE_*** */
	u32 fqd_link;		/* 24-bit, _res2[24-31] */
	u16 odp_seq;		/* 14-bit, _res3[14-15] */
	u16 orp_nesn;		/* 14-bit, _res4[14-15] */
	u16 orp_ea_hseq;	/* 15-bit, _res5[15] */
	u16 orp_ea_tseq;	/* 15-bit, _res6[15] */
	u32 orp_ea_hptr;	/* 24-bit, _res7[24-31] */
	u32 orp_ea_tptr;	/* 24-bit, _res8[24-31] */
	u32 pfdr_hptr;		/* 24-bit, _res9[24-31] */
	u32 pfdr_tptr;		/* 24-bit, _res10[24-31] */
	u8 __reserved2[5];
	u8 is;			/* 1-bit, _res12[1-7] */
	u16 ics_surp;
	u32 byte_cnt;
	u32 frm_cnt;		/* 24-bit, _res13[24-31] */
	u32 __reserved3;
	u16 ra1_sfdr;		/* QM_MCR_NP_RA1_*** */
	u16 ra2_sfdr;		/* QM_MCR_NP_RA2_*** */
	u16 __reserved4;
	u16 od1_sfdr;		/* QM_MCR_NP_OD1_*** */
	u16 od2_sfdr;		/* QM_MCR_NP_OD2_*** */
	u16 od3_sfdr;		/* QM_MCR_NP_OD3_*** */
} __packed;

#define QM_MCR_NP_STATE_FE		0x10
#define QM_MCR_NP_STATE_R		0x08
#define QM_MCR_NP_STATE_MASK		0x07	/* Reads FQD::STATE; */
#define QM_MCR_NP_STATE_OOS		0x00
#define QM_MCR_NP_STATE_RETIRED		0x01
#define QM_MCR_NP_STATE_TEN_SCHED	0x02
#define QM_MCR_NP_STATE_TRU_SCHED	0x03
#define QM_MCR_NP_STATE_PARKED		0x04
#define QM_MCR_NP_STATE_ACTIVE		0x05
#define QM_MCR_NP_PTR_MASK		0x07ff	/* for RA[12] & OD[123] */
#define QM_MCR_NP_RA1_NRA(v)		(((v) >> 14) & 0x3)	/* FQD::NRA */
#define QM_MCR_NP_RA2_IT(v)		(((v) >> 14) & 0x1)	/* FQD::IT */
#define QM_MCR_NP_OD1_NOD(v)		(((v) >> 14) & 0x3)	/* FQD::NOD */
#define QM_MCR_NP_OD3_NPC(v)		(((v) >> 14) & 0x3)	/* FQD::NPC */

enum qm_mcr_queryfq_np_masks {
	qm_mcr_fqd_link_mask = BIT(24) - 1,
	qm_mcr_odp_seq_mask = BIT(14) - 1,
	qm_mcr_orp_nesn_mask = BIT(14) - 1,
	qm_mcr_orp_ea_hseq_mask = BIT(15) - 1,
	qm_mcr_orp_ea_tseq_mask = BIT(15) - 1,
	qm_mcr_orp_ea_hptr_mask = BIT(24) - 1,
	qm_mcr_orp_ea_tptr_mask = BIT(24) - 1,
	qm_mcr_pfdr_hptr_mask = BIT(24) - 1,
	qm_mcr_pfdr_tptr_mask = BIT(24) - 1,
	qm_mcr_is_mask = BIT(1) - 1,
	qm_mcr_frm_cnt_mask = BIT(24) - 1,
};

#define qm_mcr_np_get(np, field) \
	((np)->field & (qm_mcr_##field##_mask))

	/* Portal Management */
/**
 * qman_p_irqsource_add - add processing sources to be interrupt-driven
 * @bits: bitmask of QM_PIRQ_**I processing sources
 *
 * Adds processing sources that should be interrupt-driven (rather than
 * processed via qman_poll_***() functions).
 */
void qman_p_irqsource_add(struct qman_portal *p, u32 bits);

/**
 * qman_p_irqsource_remove - remove processing sources from being int-driven
 * @bits: bitmask of QM_PIRQ_**I processing sources
 *
 * Removes processing sources from being interrupt-driven, so that they will
 * instead be processed via qman_poll_***() functions.
 */
void qman_p_irqsource_remove(struct qman_portal *p, u32 bits);

/**
 * qman_affine_cpus - return a mask of cpus that have affine portals
 */
const cpumask_t *qman_affine_cpus(void);

/**
 * qman_affine_channel - return the channel ID of an portal
 * @cpu: the cpu whose affine portal is the subject of the query
 *
 * If @cpu is -1, the affine portal for the current CPU will be used. It is a
 * bug to call this function for any value of @cpu (other than -1) that is not a
 * member of the mask returned from qman_affine_cpus().
 */
u16 qman_affine_channel(int cpu);

/**
 * qman_get_affine_portal - return the portal pointer affine to cpu
 * @cpu: the cpu whose affine portal is the subject of the query
 */
struct qman_portal *qman_get_affine_portal(int cpu);

/**
 * qman_start_using_portal - register a device link for the portal user
 * @p: the portal that will be in use
 * @dev: the device that will use the portal
 *
 * Makes sure that the devices that use the portal are unbound when the
 * portal is unbound
 */
int qman_start_using_portal(struct qman_portal *p, struct device *dev);

/**
 * qman_p_poll_dqrr - process DQRR (fast-path) entries
 * @limit: the maximum number of DQRR entries to process
 *
 * Use of this function requires that DQRR processing not be interrupt-driven.
 * The return value represents the number of DQRR entries processed.
 */
int qman_p_poll_dqrr(struct qman_portal *p, unsigned int limit);

/**
 * qman_p_static_dequeue_add - Add pool channels to the portal SDQCR
 * @pools: bit-mask of pool channels, using QM_SDQCR_CHANNELS_POOL(n)
 *
 * Adds a set of pool channels to the portal's static dequeue command register
 * (SDQCR). The requested pools are limited to those the portal has dequeue
 * access to.
 */
void qman_p_static_dequeue_add(struct qman_portal *p, u32 pools);

	/* FQ management */
/**
 * qman_create_fq - Allocates a FQ
 * @fqid: the index of the FQD to encapsulate, must be "Out of Service"
 * @flags: bit-mask of QMAN_FQ_FLAG_*** options
 * @fq: memory for storing the 'fq', with callbacks filled in
 *
 * Creates a frame queue object for the given @fqid, unless the
 * QMAN_FQ_FLAG_DYNAMIC_FQID flag is set in @flags, in which case a FQID is
 * dynamically allocated (or the function fails if none are available). Once
 * created, the caller should not touch the memory at 'fq' except as extended to
 * adjacent memory for user-defined fields (see the definition of "struct
 * qman_fq" for more info). NO_MODIFY is only intended for enqueuing to
 * pre-existing frame-queues that aren't to be otherwise interfered with, it
 * prevents all other modifications to the frame queue. The TO_DCPORTAL flag
 * causes the driver to honour any context_b modifications requested in the
 * qm_init_fq() API, as this indicates the frame queue will be consumed by a
 * direct-connect portal (PME, CAAM, or Fman). When frame queues are consumed by
 * software portals, the context_b field is controlled by the driver and can't
 * be modified by the caller.
 */
int qman_create_fq(u32 fqid, u32 flags, struct qman_fq *fq);

/**
 * qman_destroy_fq - Deallocates a FQ
 * @fq: the frame queue object to release
 *
 * The memory for this frame queue object ('fq' provided in qman_create_fq()) is
 * not deallocated but the caller regains ownership, to do with as desired. The
 * FQ must be in the 'out-of-service' or in the 'parked' state.
 */
void qman_destroy_fq(struct qman_fq *fq);

/**
 * qman_fq_fqid - Queries the frame queue ID of a FQ object
 * @fq: the frame queue object to query
 */
u32 qman_fq_fqid(struct qman_fq *fq);

/**
 * qman_init_fq - Initialises FQ fields, leaves the FQ "parked" or "scheduled"
 * @fq: the frame queue object to modify, must be 'parked' or new.
 * @flags: bit-mask of QMAN_INITFQ_FLAG_*** options
 * @opts: the FQ-modification settings, as defined in the low-level API
 *
 * The @opts parameter comes from the low-level portal API. Select
 * QMAN_INITFQ_FLAG_SCHED in @flags to cause the frame queue to be scheduled
 * rather than parked. NB, @opts can be NULL.
 *
 * Note that some fields and options within @opts may be ignored or overwritten
 * by the driver;
 * 1. the 'count' and 'fqid' fields are always ignored (this operation only
 * affects one frame queue: @fq).
 * 2. the QM_INITFQ_WE_CONTEXTB option of the 'we_mask' field and the associated
 * 'fqd' structure's 'context_b' field are sometimes overwritten;
 *   - if @fq was not created with QMAN_FQ_FLAG_TO_DCPORTAL, then context_b is
 *     initialised to a value used by the driver for demux.
 *   - if context_b is initialised for demux, so is context_a in case stashing
 *     is requested (see item 4).
 * (So caller control of context_b is only possible for TO_DCPORTAL frame queue
 * objects.)
 * 3. if @flags contains QMAN_INITFQ_FLAG_LOCAL, the 'fqd' structure's
 * 'dest::channel' field will be overwritten to match the portal used to issue
 * the command. If the WE_DESTWQ write-enable bit had already been set by the
 * caller, the channel workqueue will be left as-is, otherwise the write-enable
 * bit is set and the workqueue is set to a default of 4. If the "LOCAL" flag
 * isn't set, the destination channel/workqueue fields and the write-enable bit
 * are left as-is.
 * 4. if the driver overwrites context_a/b for demux, then if
 * QM_INITFQ_WE_CONTEXTA is set, the driver will only overwrite
 * context_a.address fields and will leave the stashing fields provided by the
 * user alone, otherwise it will zero out the context_a.stashing fields.
 */
int qman_init_fq(struct qman_fq *fq, u32 flags, struct qm_mcc_initfq *opts);

/**
 * qman_schedule_fq - Schedules a FQ
 * @fq: the frame queue object to schedule, must be 'parked'
 *
 * Schedules the frame queue, which must be Parked, which takes it to
 * Tentatively-Scheduled or Truly-Scheduled depending on its fill-level.
 */
int qman_schedule_fq(struct qman_fq *fq);

/**
 * qman_retire_fq - Retires a FQ
 * @fq: the frame queue object to retire
 * @flags: FQ flags (QMAN_FQ_STATE*) if retirement completes immediately
 *
 * Retires the frame queue. This returns zero if it succeeds immediately, +1 if
 * the retirement was started asynchronously, otherwise it returns negative for
 * failure. When this function returns zero, @flags is set to indicate whether
 * the retired FQ is empty and/or whether it has any ORL fragments (to show up
 * as ERNs). Otherwise the corresponding flags will be known when a subsequent
 * FQRN message shows up on the portal's message ring.
 *
 * NB, if the retirement is asynchronous (the FQ was in the Truly Scheduled or
 * Active state), the completion will be via the message ring as a FQRN - but
 * the corresponding callback may occur before this function returns!! Ie. the
 * caller should be prepared to accept the callback as the function is called,
 * not only once it has returned.
 */
int qman_retire_fq(struct qman_fq *fq, u32 *flags);

/**
 * qman_oos_fq - Puts a FQ "out of service"
 * @fq: the frame queue object to be put out-of-service, must be 'retired'
 *
 * The frame queue must be retired and empty, and if any order restoration list
 * was released as ERNs at the time of retirement, they must all be consumed.
 */
int qman_oos_fq(struct qman_fq *fq);

/*
 * qman_volatile_dequeue - Issue a volatile dequeue command
 * @fq: the frame queue object to dequeue from
 * @flags: a bit-mask of QMAN_VOLATILE_FLAG_*** options
 * @vdqcr: bit mask of QM_VDQCR_*** options, as per qm_dqrr_vdqcr_set()
 *
 * Attempts to lock access to the portal's VDQCR volatile dequeue functionality.
 * The function will block and sleep if QMAN_VOLATILE_FLAG_WAIT is specified and
 * the VDQCR is already in use, otherwise returns non-zero for failure. If
 * QMAN_VOLATILE_FLAG_FINISH is specified, the function will only return once
 * the VDQCR command has finished executing (ie. once the callback for the last
 * DQRR entry resulting from the VDQCR command has been called). If not using
 * the FINISH flag, completion can be determined either by detecting the
 * presence of the QM_DQRR_STAT_UNSCHEDULED and QM_DQRR_STAT_DQCR_EXPIRED bits
 * in the "stat" parameter passed to the FQ's dequeue callback, or by waiting
 * for the QMAN_FQ_STATE_VDQCR bit to disappear.
 */
int qman_volatile_dequeue(struct qman_fq *fq, u32 flags, u32 vdqcr);

/**
 * qman_enqueue - Enqueue a frame to a frame queue
 * @fq: the frame queue object to enqueue to
 * @fd: a descriptor of the frame to be enqueued
 *
 * Fills an entry in the EQCR of portal @qm to enqueue the frame described by
 * @fd. The descriptor details are copied from @fd to the EQCR entry, the 'pid'
 * field is ignored. The return value is non-zero on error, such as ring full.
 */
int qman_enqueue(struct qman_fq *fq, const struct qm_fd *fd);

/**
 * qman_alloc_fqid_range - Allocate a contiguous range of FQIDs
 * @result: is set by the API to the base FQID of the allocated range
 * @count: the number of FQIDs required
 *
 * Returns 0 on success, or a negative error code.
 */
int qman_alloc_fqid_range(u32 *result, u32 count);
#define qman_alloc_fqid(result) qman_alloc_fqid_range(result, 1)

/**
 * qman_release_fqid - Release the specified frame queue ID
 * @fqid: the FQID to be released back to the resource pool
 *
 * This function can also be used to seed the allocator with
 * FQID ranges that it can subsequently allocate from.
 * Returns 0 on success, or a negative error code.
 */
int qman_release_fqid(u32 fqid);

/**
 * qman_query_fq_np - Queries non-programmable FQD fields
 * @fq: the frame queue object to be queried
 * @np: storage for the queried FQD fields
 */
int qman_query_fq_np(struct qman_fq *fq, struct qm_mcr_queryfq_np *np);

	/* Pool-channel management */
/**
 * qman_alloc_pool_range - Allocate a contiguous range of pool-channel IDs
 * @result: is set by the API to the base pool-channel ID of the allocated range
 * @count: the number of pool-channel IDs required
 *
 * Returns 0 on success, or a negative error code.
 */
int qman_alloc_pool_range(u32 *result, u32 count);
#define qman_alloc_pool(result) qman_alloc_pool_range(result, 1)

/**
 * qman_release_pool - Release the specified pool-channel ID
 * @id: the pool-chan ID to be released back to the resource pool
 *
 * This function can also be used to seed the allocator with
 * pool-channel ID ranges that it can subsequently allocate from.
 * Returns 0 on success, or a negative error code.
 */
int qman_release_pool(u32 id);

	/* CGR management */
/**
 * qman_create_cgr - Register a congestion group object
 * @cgr: the 'cgr' object, with fields filled in
 * @flags: QMAN_CGR_FLAG_* values
 * @opts: optional state of CGR settings
 *
 * Registers this object to receiving congestion entry/exit callbacks on the
 * portal affine to the cpu portal on which this API is executed. If opts is
 * NULL then only the callback (cgr->cb) function is registered. If @flags
 * contains QMAN_CGR_FLAG_USE_INIT, then an init hw command (which will reset
 * any unspecified parameters) will be used rather than a modify hw hardware
 * (which only modifies the specified parameters).
 */
int qman_create_cgr(struct qman_cgr *cgr, u32 flags,
		    struct qm_mcc_initcgr *opts);

/**
 * qman_delete_cgr - Deregisters a congestion group object
 * @cgr: the 'cgr' object to deregister
 *
 * "Unplugs" this CGR object from the portal affine to the cpu on which this API
 * is executed. This must be excuted on the same affine portal on which it was
 * created.
 */
int qman_delete_cgr(struct qman_cgr *cgr);

/**
 * qman_delete_cgr_safe - Deregisters a congestion group object from any CPU
 * @cgr: the 'cgr' object to deregister
 *
 * This will select the proper CPU and run there qman_delete_cgr().
 */
void qman_delete_cgr_safe(struct qman_cgr *cgr);

/**
 * qman_query_cgr_congested - Queries CGR's congestion status
 * @cgr: the 'cgr' object to query
 * @result: returns 'cgr's congestion status, 1 (true) if congested
 */
int qman_query_cgr_congested(struct qman_cgr *cgr, bool *result);

/**
 * qman_alloc_cgrid_range - Allocate a contiguous range of CGR IDs
 * @result: is set by the API to the base CGR ID of the allocated range
 * @count: the number of CGR IDs required
 *
 * Returns 0 on success, or a negative error code.
 */
int qman_alloc_cgrid_range(u32 *result, u32 count);
#define qman_alloc_cgrid(result) qman_alloc_cgrid_range(result, 1)

/**
 * qman_release_cgrid - Release the specified CGR ID
 * @id: the CGR ID to be released back to the resource pool
 *
 * This function can also be used to seed the allocator with
 * CGR ID ranges that it can subsequently allocate from.
 * Returns 0 on success, or a negative error code.
 */
int qman_release_cgrid(u32 id);

/**
 * qman_is_probed - Check if qman is probed
 *
 * Returns 1 if the qman driver successfully probed, -1 if the qman driver
 * failed to probe or 0 if the qman driver did not probed yet.
 */
int qman_is_probed(void);

/**
 * qman_portals_probed - Check if all cpu bound qman portals are probed
 *
 * Returns 1 if all the required cpu bound qman portals successfully probed,
 * -1 if probe errors appeared or 0 if the qman portals did not yet finished
 * probing.
 */
int qman_portals_probed(void);

/**
 * qman_dqrr_get_ithresh - Get coalesce interrupt threshold
 * @portal: portal to get the value for
 * @ithresh: threshold pointer
 */
void qman_dqrr_get_ithresh(struct qman_portal *portal, u8 *ithresh);

/**
 * qman_dqrr_set_ithresh - Set coalesce interrupt threshold
 * @portal: portal to set the new value on
 * @ithresh: new threshold value
 *
 * Returns 0 on success, or a negative error code.
 */
int qman_dqrr_set_ithresh(struct qman_portal *portal, u8 ithresh);

/**
 * qman_dqrr_get_iperiod - Get coalesce interrupt period
 * @portal: portal to get the value for
 * @iperiod: period pointer
 */
void qman_portal_get_iperiod(struct qman_portal *portal, u32 *iperiod);

/**
 * qman_dqrr_set_iperiod - Set coalesce interrupt period
 * @portal: portal to set the new value on
 * @ithresh: new period value
 *
 * Returns 0 on success, or a negative error code.
 */
int qman_portal_set_iperiod(struct qman_portal *portal, u32 iperiod);

#endif	/* __FSL_QMAN_H */
