/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2007 Intel Corporation. All rights reserved.
 *
 * Maintained at www.Open-FCoE.org
 */
#ifndef _FC_ENCAPS_H_
#define _FC_ENCAPS_H_

/*
 * Protocol definitions from RFC 3643 - Fibre Channel Frame Encapsulation.
 *
 * Note:  The frame length field is the number of 32-bit words in
 * the encapsulation including the fcip_encaps_header, CRC and EOF words.
 * The minimum frame length value in bytes is (32 + 24 + 4 + 4) * 4 = 64.
 * The maximum frame length value in bytes is (32 + 24 + 2112 + 4 + 4) = 2172.
 */
#define FC_ENCAPS_MIN_FRAME_LEN 64	/* min frame len (bytes) (see above) */
#define FC_ENCAPS_MAX_FRAME_LEN (FC_ENCAPS_MIN_FRAME_LEN + FC_MAX_PAYLOAD)

#define FC_ENCAPS_VER       1           /* current version number */

struct fc_encaps_hdr {
	__u8	fc_proto;	/* protocol number */
	__u8	fc_ver;		/* version of encapsulation */
	__u8	fc_proto_n;	/* ones complement of protocol */
	__u8	fc_ver_n;	/* ones complement of version */

	unsigned char fc_proto_data[8]; /* protocol specific data */

	__be16	fc_len_flags;	/* 10-bit length/4 w/ 6 flag bits */
	__be16	fc_len_flags_n;	/* ones complement of length / flags */

	/*
	 * Offset 0x10
	 */
	__be32	fc_time[2];	/* time stamp: seconds and fraction */
	__be32	fc_crc;		/* CRC */
	__be32	fc_sof;		/* start of frame (see FC_SOF below) */

	/* 0x20 - FC frame content followed by EOF word */
};

#define FCIP_ENCAPS_HDR_LEN 0x20	/* expected length for asserts */

/*
 * Macro's for making redundant copies of EOF and SOF.
 */
#define FC_XY(x, y)		((((x) & 0xff) << 8) | ((y) & 0xff))
#define FC_XYXY(x, y)		((FCIP_XY(x, y) << 16) | FCIP_XY(x, y))
#define FC_XYNN(x, y)		(FCIP_XYXY(x, y) ^ 0xffff)

#define FC_SOF_ENCODE(n)	FC_XYNN(n, n)
#define FC_EOF_ENCODE(n)	FC_XYNN(n, n)

/*
 * SOF / EOF bytes.
 */
enum fc_sof {
	FC_SOF_F =	0x28,	/* fabric */
	FC_SOF_I4 =	0x29,	/* initiate class 4 */
	FC_SOF_I2 =	0x2d,	/* initiate class 2 */
	FC_SOF_I3 =	0x2e,	/* initiate class 3 */
	FC_SOF_N4 =	0x31,	/* normal class 4 */
	FC_SOF_N2 =	0x35,	/* normal class 2 */
	FC_SOF_N3 =	0x36,	/* normal class 3 */
	FC_SOF_C4 =	0x39,	/* activate class 4 */
} __attribute__((packed));

enum fc_eof {
	FC_EOF_N =	0x41,	/* normal (not last frame of seq) */
	FC_EOF_T =	0x42,	/* terminate (last frame of sequence) */
	FC_EOF_RT =	0x44,
	FC_EOF_DT =	0x46,	/* disconnect-terminate class-1 */
	FC_EOF_NI =	0x49,	/* normal-invalid */
	FC_EOF_DTI =	0x4e,	/* disconnect-terminate-invalid */
	FC_EOF_RTI =	0x4f,
	FC_EOF_A =	0x50,	/* abort */
} __attribute__((packed));

#define FC_SOF_CLASS_MASK 0x06	/* mask for class of service in SOF */

/*
 * Define classes in terms of the SOF code (initial).
 */
enum fc_class {
	FC_CLASS_NONE = 0,	/* software value indicating no class */
	FC_CLASS_2 =	FC_SOF_I2,
	FC_CLASS_3 =	FC_SOF_I3,
	FC_CLASS_4 =	FC_SOF_I4,
	FC_CLASS_F =	FC_SOF_F,
};

/*
 * Determine whether SOF code indicates the need for a BLS ACK.
 */
static inline int fc_sof_needs_ack(enum fc_sof sof)
{
	return (~sof) & 0x02;	/* true for class 1, 2, 4, 6, or F */
}

/*
 * Given an fc_class, return the normal (non-initial) SOF value.
 */
static inline enum fc_sof fc_sof_normal(enum fc_class class)
{
	return class + FC_SOF_N3 - FC_SOF_I3;	/* diff is always 8 */
}

/*
 * Compute class from SOF value.
 */
static inline enum fc_class fc_sof_class(enum fc_sof sof)
{
	return (sof & 0x7) | FC_SOF_F;
}

/*
 * Determine whether SOF is for the initial frame of a sequence.
 */
static inline int fc_sof_is_init(enum fc_sof sof)
{
	return sof < 0x30;
}

#endif /* _FC_ENCAPS_H_ */
