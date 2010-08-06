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
	struct in6_addr	grec_src[0];
};

struct mld2_report {
	struct icmp6hdr		mld2r_hdr;
	struct mld2_grec	mld2r_grec[0];
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
	struct in6_addr		mld2q_srcs[0];
};

#define mld2q_type		mld2q_hdr.icmp6_type
#define mld2q_code		mld2q_hdr.icmp6_code
#define mld2q_cksum		mld2q_hdr.icmp6_cksum
#define mld2q_mrc		mld2q_hdr.icmp6_maxdelay
#define mld2q_resv1		mld2q_hdr.icmp6_dataun.un_data16[1]

/* Max Response Code */
#define MLDV2_MASK(value, nb) ((nb)>=32 ? (value) : ((1<<(nb))-1) & (value))
#define MLDV2_EXP(thresh, nbmant, nbexp, value) \
	((value) < (thresh) ? (value) : \
	((MLDV2_MASK(value, nbmant) | (1<<(nbmant))) << \
	(MLDV2_MASK((value) >> (nbmant), nbexp) + (nbexp))))

#define MLDV2_MRC(value) MLDV2_EXP(0x8000, 12, 3, value)

#endif
