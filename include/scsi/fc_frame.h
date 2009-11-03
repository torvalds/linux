/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
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
#define fr_max_payload(fp)	(fr_cb(fp)->fr_max_payload)
#define fr_fsp(fp)	(fr_cb(fp)->fr_fsp)
#define fr_crc(fp)	(fr_cb(fp)->fr_crc)

struct fc_frame {
	struct sk_buff skb;
};

struct fcoe_rcv_info {
	struct packet_type  *ptype;
	struct fc_lport	*fr_dev;	/* transport layer private pointer */
	struct fc_seq	*fr_seq;	/* for use with exchange manager */
	struct fc_fcp_pkt *fr_fsp;	/* for the corresponding fcp I/O */
	u32		fr_crc;
	u16		fr_max_payload;	/* max FC payload */
	enum fc_sof	fr_sof;		/* start of frame delimiter */
	enum fc_eof	fr_eof;		/* end of frame delimiter */
	u8		fr_flags;	/* flags - see below */
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
 * This hides a cast and provides a place to add some checking.
 */
static inline
struct fc_frame_header *fc_frame_header_get(const struct fc_frame *fp)
{
	WARN_ON(fr_len(fp) < sizeof(struct fc_frame_header));
	return (struct fc_frame_header *) fr_hdr(fp);
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

#endif /* _FC_FRAME_H_ */
