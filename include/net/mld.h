/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MLD_H
#define LINUX_MLD_H

#include <linux/in6.h>
#include <linux/icmpv6.h>

/* MLDv1 Query/Report/Done */
struct mld_msg {
	struct icmp6hdr		mld_hdr;
	struct in6_addr		mld_mca;
};

#define mld_type		mld_hdr.icmp6_type
#define mld_code		mld_hdr.icmp6_code
#define mld_cksum		mld_hdr.icmp6_cksum
#define mld_maxdelay		mld_hdr.icmp6_maxdelay
#define mld_reserved		mld_hdr.icmp6_dataun.un_data16[1]

/* Multicast Listener Discovery version 2 headers */
/* MLDv2 Report */
struct mld2_grec {
	__u8		grec_type;
	__u8		grec_auxwords;
	__be16		grec_nsrcs;
	struct in6_addr	grec_mca;
	struct in6_addr	grec_src[];
};

struct mld2_report {
	struct icmp6hdr		mld2r_hdr;
	struct mld2_grec	mld2r_grec[];
};

#define mld2r_type		mld2r_hdr.icmp6_type
#define mld2r_resv1		mld2r_hdr.icmp6_code
#define mld2r_cksum		mld2r_hdr.icmp6_cksum
#define mld2r_resv2		mld2r_hdr.icmp6_dataun.un_data16[0]
#define mld2r_ngrec		mld2r_hdr.icmp6_dataun.un_data16[1]

/* MLDv2 Query */
struct mld2_query {
	struct icmp6hdr		mld2q_hdr;
	struct in6_addr		mld2q_mca;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u8			mld2q_qrv:3,
				mld2q_suppress:1,
				mld2q_resv2:4;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u8			mld2q_resv2:4,
				mld2q_suppress:1,
				mld2q_qrv:3;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	__u8			mld2q_qqic;
	__be16			mld2q_nsrcs;
	struct in6_addr		mld2q_srcs[];
};

#define mld2q_type		mld2q_hdr.icmp6_type
#define mld2q_code		mld2q_hdr.icmp6_code
#define mld2q_cksum		mld2q_hdr.icmp6_cksum
#define mld2q_mrc		mld2q_hdr.icmp6_maxdelay
#define mld2q_resv1		mld2q_hdr.icmp6_dataun.un_data16[1]

/* RFC3810, 5.1.3. Maximum Response Code:
 *
 * If Maximum Response Code >= 32768, Maximum Response Code represents a
 * floating-point value as follows:
 *
 *  0 1 2 3 4 5 6 7 8 9 A B C D E F
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |1| exp |          mant         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */
#define MLDV2_MRC_EXP(value)	(((value) >> 12) & 0x0007)
#define MLDV2_MRC_MAN(value)	((value) & 0x0fff)

/* RFC3810, 5.1.9. QQIC (Querier's Query Interval Code):
 *
 * If QQIC >= 128, QQIC represents a floating-point value as follows:
 *
 *  0 1 2 3 4 5 6 7
 * +-+-+-+-+-+-+-+-+
 * |1| exp | mant  |
 * +-+-+-+-+-+-+-+-+
 */
#define MLDV2_QQIC_EXP(value)	(((value) >> 4) & 0x07)
#define MLDV2_QQIC_MAN(value)	((value) & 0x0f)

#define MLD_EXP_MIN_LIMIT	32768UL
#define MLDV1_MRD_MAX_COMPAT	(MLD_EXP_MIN_LIMIT - 1)

static inline unsigned long mldv2_mrc(const struct mld2_query *mlh2)
{
	/* RFC3810, 5.1.3. Maximum Response Code */
	unsigned long ret, mc_mrc = ntohs(mlh2->mld2q_mrc);

	if (mc_mrc < MLD_EXP_MIN_LIMIT) {
		ret = mc_mrc;
	} else {
		unsigned long mc_man, mc_exp;

		mc_exp = MLDV2_MRC_EXP(mc_mrc);
		mc_man = MLDV2_MRC_MAN(mc_mrc);

		ret = (mc_man | 0x1000) << (mc_exp + 3);
	}

	return ret;
}

#endif
