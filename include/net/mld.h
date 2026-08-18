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

/* MLDv2 QQIC floating-point exponential field min threshold */
#define MLD_QQIC_MIN_THRESHOLD	128
/* MLDv2 QQIC FP max threshold (mant = 0xF, exp = 7) -> 31744 */
#define MLD_QQIC_MAX_THRESHOLD	31744
/* MLDv2 MRC floating-point exponential field min threshold */
#define MLD_MRC_MIN_THRESHOLD	32768UL
/* MLDv2 MRC FP max threshold (mant = 0xFFF, exp = 7) -> 8387584 */
#define MLD_MRC_MAX_THRESHOLD	8387584
#define MLDV1_MRD_MAX_COMPAT	(MLD_MRC_MIN_THRESHOLD - 1)

#define MLD_MAX_QUEUE		8
#define MLD_MAX_SKBS		32

/* V2 exponential field encoding */

/*
 * Calculate Maximum Response Code from Maximum Response Delay
 *
 * MRC represents the 16-bit encoded form of Maximum Response Delay (MRD);
 * once decoded, the resulting value is in milliseconds.
 *
 * RFC3810, 5.1.3. defines only the decoding formula:
 *     Maximum Response Delay = (mant | 0x1000) << (exp + 3)
 *
 * but does NOT define the encoding procedure. To derive exponent:
 *
 * For the 16-bit MRC, the "hidden bit" (0x1000) is left shifted by 12 to
 * sit above the 12-bit mantissa. The RFC then shifts this entire block
 * left by (exp + 3) to reconstruct the value. So, 'hidden bit' is the
 * MSB which is shifted by (12 + exp + 3).
 *
 * Total left shift of the hidden bit = 12 + (exp + 3) = exp + 15.
 * This is the MSB at the 0-based bit position: (exp + 15).
 * Since fls() is 1-based, fls(value) - 1 = exp + 15.
 *
 * Therefore:
 *     exp  = fls(value) - 16
 *     mant = (value >> (exp + 3)) & 0x0FFF
 *
 * Final encoding formula:
 *     0x8000 | (exp << 12) | mant
 *
 * Example (value = 1311744):
 *  0               1               2               3
 *  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |0 0 0 0 0 0 0 0 0 0 0 1 0 1 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0| 1311744
 * |                      ^-^--------mant---------^ ^...(exp+3)...^| exp=5
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Encoded:
 *   0x8000 | (5 << 12) | 0x404 = 0xD404
 */
static inline u16 mldv2_mrc(unsigned long mrd)
{
	u16 mc_man, mc_exp;

	/* MRC < 32768 is literal */
	if (mrd < MLD_MRC_MIN_THRESHOLD)
		return mrd;

	/* Saturate at max representable (mant = 0xFFF, exp = 7) -> 8387584 */
	if (mrd >= MLD_MRC_MAX_THRESHOLD)
		return 0xFFFF;

	mc_exp = fls(mrd) - 16;
	mc_man = (mrd >> (mc_exp + 3)) & 0x0FFF;

	return 0x8000 | (mc_exp << 12) | mc_man;
}

/*
 * Calculate Querier's Query Interval Code from Querier's Query Interval
 *
 * QQIC represents the 8-bit encoded form of Querier's Query Interval (QQI);
 * once decoded, the resulting value is in seconds.
 *
 * RFC3810, 5.1.9. defines only the decoding formula:
 *     QQI = (mant | 0x10) << (exp + 3)
 *
 * but does NOT define the encoding procedure. To derive exponent:
 *
 * For any value of mantissa and exponent, the decoding formula indicates
 * that the "hidden bit" (0x10) is shifted 4 bits left to sit above the
 * 4-bit mantissa. The RFC again shifts this entire block left by (exp + 3)
 * to reconstruct the value. So, 'hidden bit' is the MSB which is shifted
 * by (4 + exp + 3).
 *
 * Total left shift of the 'hidden bit' = 4 + (exp + 3) = exp + 7.
 * This is the MSB at the 0-based bit position: (exp + 7).
 * Since fls() is 1-based, fls(value) - 1 = exp + 7.
 *
 * Therefore:
 *     exp  = fls(value) - 8
 *     mant = (value >> (exp + 3)) & 0x0F
 *
 * Final encoding formula:
 *     0x80 | (exp << 4) | mant
 *
 * Example (value = 3200):
 *  0               1
 *  0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |0 0 0 0 1 1 0 0 1 0 0 0 0 0 0 0| (value = 3200)
 * |        ^-^-mant^ ^..(exp+3)..^| exp = 4, mant = 9
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Encoded:
 *   0x80 | (4 << 4) | 9 = 0xC9
 */
static inline u8 mldv2_qqic(unsigned long value)
{
	u8 mc_man, mc_exp;

	/* QQIC < 128 is literal */
	if (value < MLD_QQIC_MIN_THRESHOLD)
		return value;

	/* Saturate at max representable (mant = 0xF, exp = 7) -> 31744 */
	if (value >= MLD_QQIC_MAX_THRESHOLD)
		return 0xFF;

	mc_exp  = fls(value) - 8;
	mc_man = (value >> (mc_exp + 3)) & 0x0F;

	return 0x80 | (mc_exp << 4) | mc_man;
}

/* V2 exponential field decoding */

/* Calculate Maximum Response Delay from Maximum Response Code
 *
 * RFC3810, relevant sections:
 *  - 5.1.3. Maximum Response Code defines the decoding formula:
 *      0 1 2 3 4 5 6 7 8 9 A B C D E F
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |1| exp |          mant         |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    Maximum Response Delay = (mant | 0x1000) << (exp+3)
 *  - 9.3. Query Response Interval
 *
 * After decode, MRC represents the Maximum Response Delay (MRD) in
 * units of milliseconds.
 */
static inline unsigned long mldv2_mrd(const struct mld2_query *mlh2)
{
	unsigned long mc_mrc = ntohs(mlh2->mld2q_mrc);

	if (mc_mrc < MLD_MRC_MIN_THRESHOLD) {
		return mc_mrc;
	} else {
		unsigned long mc_man, mc_exp;

		mc_exp = MLDV2_MRC_EXP(mc_mrc);
		mc_man = MLDV2_MRC_MAN(mc_mrc);

		return (mc_man | 0x1000) << (mc_exp + 3);
	}
}

/* Calculate Querier's Query Interval from Querier's Query Interval Code
 *
 * RFC3810, relevant sections:
 *  - 5.1.9. QQIC (Querier's Query Interval Code) defines the decoding formula:
 *      0 1 2 3 4 5 6 7
 *     +-+-+-+-+-+-+-+-+
 *     |1| exp | mant  |
 *     +-+-+-+-+-+-+-+-+
 *    QQI = (mant | 0x10) << (exp + 3)
 *  - 9.2. Query Interval
 *  - 9.12. Older Version Querier Present Timeout
 *    (the [Query Interval] in the last Query received)
 *
 * After decode, QQIC represents the Querier's Query Interval in units
 * of seconds.
 */
static inline unsigned long mldv2_qqi(const struct mld2_query *mlh2)
{
	unsigned long qqic = mlh2->mld2q_qqic;

	if (qqic < MLD_QQIC_MIN_THRESHOLD) {
		return qqic;
	} else {
		unsigned long mc_man, mc_exp;

		mc_exp = MLDV2_QQIC_EXP(qqic);
		mc_man = MLDV2_QQIC_MAN(qqic);

		return (mc_man | 0x10) << (mc_exp + 3);
	}
}

#endif
