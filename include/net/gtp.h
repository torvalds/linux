#ifndef _GTP_H_
#define _GTP_H

/* General GTP protocol related definitions. */

#define GTP0_PORT	3386
#define GTP1U_PORT	2152

#define GTP_TPDU	255

struct gtp0_header {	/* According to GSM TS 09.60. */
	__u8	flags;
	__u8	type;
	__be16	length;
	__be16	seq;
	__be16	flow;
	__u8	number;
	__u8	spare[3];
	__be64	tid;
} __attribute__ ((packed));

struct gtp1_header {	/* According to 3GPP TS 29.060. */
	__u8	flags;
	__u8	type;
	__be16	length;
	__be32	tid;
} __attribute__ ((packed));

#define GTP1_F_NPDU	0x01
#define GTP1_F_SEQ	0x02
#define GTP1_F_EXTHDR	0x04
#define GTP1_F_MASK	0x07

#endif
