/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DCCP_H
#define _LINUX_DCCP_H

#include <uapi/linux/dccp.h>

static inline struct dccp_hdr_ext *dccp_hdrx(const struct dccp_hdr *dh)
{
	return (struct dccp_hdr_ext *)((unsigned char *)dh + sizeof(*dh));
}

static inline unsigned int __dccp_basic_hdr_len(const struct dccp_hdr *dh)
{
	return sizeof(*dh) + (dh->dccph_x ? sizeof(struct dccp_hdr_ext) : 0);
}

static inline __u64 dccp_hdr_seq(const struct dccp_hdr *dh)
{
	__u64 seq_nr =  ntohs(dh->dccph_seq);

	if (dh->dccph_x != 0)
		seq_nr = (seq_nr << 32) + ntohl(dccp_hdrx(dh)->dccph_seq_low);
	else
		seq_nr += (u32)dh->dccph_seq2 << 16;

	return seq_nr;
}

static inline unsigned int __dccp_hdr_len(const struct dccp_hdr *dh)
{
	return __dccp_basic_hdr_len(dh) +
	       dccp_packet_hdr_len(dh->dccph_type);
}

#endif /* _LINUX_DCCP_H */
