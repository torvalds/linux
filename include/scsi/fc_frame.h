/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * Maintained at www.Open-FCoE.org
 */

#ifndef _FC_FRAME_H_
#define _FC_FRAME_H_

#include <linux/scatterlist.h>
#include <linux/skbuff.h>
#include <scsi/scsi_cmnd.h>

#include <scsi/fc/fc_fs.h>
#include <scsi/fc/fc_fcp.h>
#include <scsi/fc/fc_encaps.h>

#include <linux/if_ether.h>

/* some helpful macros */

#define ntohll(x) be64_to_cpu(x)
#define htonll(x) cpu_to_be64(x)

static inline u32 ntoh24(const u8 *p)
{
	return (p[0] << 16) | (p[1] << 8) | p[2];
}

static inline void hton24(u8 *p, u32 v)
{
	p[0] = (v >> 16) & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = v & 0xff;
}

/*
 * The fc_frame interface is used to pass frame data between functions.
 * The frame includes the data buffer, length, and SOF / EOF delimiter types.
 * A pointer to the port structure of the receiving port is also includeded.
 */

#define	FC_FRAME_HEADROOM	32	/* headroom for VLAN + FCoE headers */
#define	FC_FRAME_TAILROOM	8	/* trailer space for FCoE */

/* Max number of skb frags allowed, reserving one for fcoe_crc_eof page */
#define FC_FRAME_SG_LEN		(MAX_SKB_FRAGS - 1)

#define fp_skb(fp)	(&((fp)->skb))
#define fr_hdr(fp)	((fp)->skb.data)
#define fr_len(fp)	((fp)->skb.len)
#define fr_cb(fp)	((struct fcoe_rcv_info *)&((fp)->skb.cb[0]))
#define fr_dev(fp)	(fr_cb(fp)->fr_dev)
#define fr_seq(fp)	(fr_cb(fp)->fr_seq)
#define fr_sof(fp)	(fr_cb(fp)->fr_sof)
#define fr_eof(fp)	(fr_cb(fp)->fr_eof)
#define fr_flags(fp)	(fr_cb(fp)->fr_flags)
#define fr_encaps(fp)	(fr_cb(fp)->fr_encaps)
#define fr_max_payload(fp)	(fr_cb(fp)->fr_max_payload)
#define fr_fsp(fp)	(fr_cb(fp)->fr_fsp)
#define fr_crc(fp)	(fr_cb(fp)->fr_crc)

struct fc_frame {
	struct sk_buff skb;
};

struct fcoe_rcv_info {
	struct fc_lport	*fr_dev;	/* transport layer private pointer */
	struct fc_seq	*fr_seq;	/* for use with exchange manager */
	struct fc_fcp_pkt *fr_fsp;	/* for the corresponding fcp I/O */
	u32		fr_crc;
	u16		fr_max_payload;	/* max FC payload */
	u8		fr_sof;		/* start of frame delimiter */
	u8		fr_eof;		/* end of frame delimiter */
	u8		fr_flags;	/* flags - see below */
	u8		fr_encaps;	/* LLD encapsulation info (e.g. FIP) */
	u8		granted_mac[ETH_ALEN]; /* FCoE MAC address */
};


/*
 * Get fc_frame pointer for an skb that's already been imported.
 */
static inline struct fcoe_rcv_info *fcoe_dev_from_skb(const struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct fcoe_rcv_info) > sizeof(skb->cb));
	return (struct fcoe_rcv_info *) skb->cb;
}

/*
 * fr_flags.
 */
#define	FCPHF_CRC_UNCHECKED	0x01	/* CRC not computed, still appended */

/*
 * Initialize a frame.
 * We don't do a complete memset here for performance reasons.
 * The caller must set fr_free, fr_hdr, fr_len, fr_sof, and fr_eof eventually.
 */
static inline void fc_frame_init(struct fc_frame *fp)
{
	fr_dev(fp) = NULL;
	fr_seq(fp) = NULL;
	fr_flags(fp) = 0;
	fr_encaps(fp) = 0;
}

struct fc_frame *fc_frame_alloc_fill(struct fc_lport *, size_t payload_len);
struct fc_frame *_fc_frame_alloc(size_t payload_len);

/*
 * Allocate fc_frame structure and buffer.  Set the initial length to
 * payload_size + sizeof (struct fc_frame_header).
 */
static inline struct fc_frame *fc_frame_alloc(struct fc_lport *dev, size_t len)
{
	struct fc_frame *fp;

	/*
	 * Note: Since len will often be a constant multiple of 4,
	 * this check will usually be evaluated and eliminated at compile time.
	 */
	if (len && len % 4)
		fp = fc_frame_alloc_fill(dev, len);
	else
		fp = _fc_frame_alloc(len);
	return fp;
}

/*
 * Free the fc_frame structure and buffer.
 */
static inline void fc_frame_free(struct fc_frame *fp)
{
	kfree_skb(fp_skb(fp));
}

static inline int fc_frame_is_linear(struct fc_frame *fp)
{
	return !skb_is_nonlinear(fp_skb(fp));
}

/*
 * Get frame header from message in fc_frame structure.
 * This version doesn't do a length check.
 */
static inline
struct fc_frame_header *__fc_frame_header_get(const struct fc_frame *fp)
{
	return (struct fc_frame_header *)fr_hdr(fp);
}

/*
 * Get frame header from message in fc_frame structure.
 * This hides a cast and provides a place to add some checking.
 */
static inline
struct fc_frame_header *fc_frame_header_get(const struct fc_frame *fp)
{
	WARN_ON(fr_len(fp) < sizeof(struct fc_frame_header));
	return __fc_frame_header_get(fp);
}

/*
 * Get source FC_ID (S_ID) from frame header in message.
 */
static inline u32 fc_frame_sid(const struct fc_frame *fp)
{
	return ntoh24(__fc_frame_header_get(fp)->fh_s_id);
}

/*
 * Get destination FC_ID (D_ID) from frame header in message.
 */
static inline u32 fc_frame_did(const struct fc_frame *fp)
{
	return ntoh24(__fc_frame_header_get(fp)->fh_d_id);
}

/*
 * Get frame payload from message in fc_frame structure.
 * This hides a cast and provides a place to add some checking.
 * The len parameter is the minimum length for the payload portion.
 * Returns NULL if the frame is too short.
 *
 * This assumes the interesting part of the payload is in the first part
 * of the buffer for received data.  This may not be appropriate to use for
 * buffers being transmitted.
 */
static inline void *fc_frame_payload_get(const struct fc_frame *fp,
					 size_t len)
{
	void *pp = NULL;

	if (fr_len(fp) >= sizeof(struct fc_frame_header) + len)
		pp = fc_frame_header_get(fp) + 1;
	return pp;
}

/*
 * Get frame payload opcode (first byte) from message in fc_frame structure.
 * This hides a cast and provides a place to add some checking. Return 0
 * if the frame has no payload.
 */
static inline u8 fc_frame_payload_op(const struct fc_frame *fp)
{
	u8 *cp;

	cp = fc_frame_payload_get(fp, sizeof(u8));
	if (!cp)
		return 0;
	return *cp;

}

/*
 * Get FC class from frame.
 */
static inline enum fc_class fc_frame_class(const struct fc_frame *fp)
{
	return fc_sof_class(fr_sof(fp));
}

/*
 * Check the CRC in a frame.
 * The CRC immediately follows the last data item *AFTER* the length.
 * The return value is zero if the CRC matches.
 */
u32 fc_frame_crc_check(struct fc_frame *);

static inline u8 fc_frame_rctl(const struct fc_frame *fp)
{
	return fc_frame_header_get(fp)->fh_r_ctl;
}

static inline bool fc_frame_is_cmd(const struct fc_frame *fp)
{
	return fc_frame_rctl(fp) == FC_RCTL_DD_UNSOL_CMD;
}

/*
 * Check for leaks.
 * Print the frame header of any currently allocated frame, assuming there
 * should be none at this point.
 */
void fc_frame_leak_check(void);

static inline void __fc_fill_fc_hdr(struct fc_frame_header *fh,
				    enum fc_rctl r_ctl,
				    u32 did, u32 sid, enum fc_fh_type type,
				    u32 f_ctl, u32 parm_offset)
{
	WARN_ON(r_ctl == 0);
	fh->fh_r_ctl = r_ctl;
	hton24(fh->fh_d_id, did);
	hton24(fh->fh_s_id, sid);
	fh->fh_type = type;
	hton24(fh->fh_f_ctl, f_ctl);
	fh->fh_cs_ctl = 0;
	fh->fh_df_ctl = 0;
	fh->fh_parm_offset = htonl(parm_offset);
}

/**
 * fill FC header fields in specified fc_frame
 */
static inline void fc_fill_fc_hdr(struct fc_frame *fp, enum fc_rctl r_ctl,
				  u32 did, u32 sid, enum fc_fh_type type,
				  u32 f_ctl, u32 parm_offset)
{
	struct fc_frame_header *fh;

	fh = fc_frame_header_get(fp);
	__fc_fill_fc_hdr(fh, r_ctl, did, sid, type, f_ctl, parm_offset);
}


#endif /* _FC_FRAME_H_ */
