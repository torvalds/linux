/*
 *  include/asm-s390/etr.h
 *
 *  Copyright IBM Corp. 2006
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */
#ifndef __S390_ETR_H
#define __S390_ETR_H

/* ETR attachment control register */
struct etr_eacr {
	unsigned int e0		: 1;	/* port 0 stepping control */
	unsigned int e1		: 1;	/* port 1 stepping control */
	unsigned int _pad0	: 5;	/* must be 00100 */
	unsigned int dp		: 1;	/* data port control */
	unsigned int p0		: 1;	/* port 0 change recognition control */
	unsigned int p1		: 1;	/* port 1 change recognition control */
	unsigned int _pad1	: 3;	/* must be 000 */
	unsigned int ea		: 1;	/* ETR alert control */
	unsigned int es		: 1;	/* ETR sync check control */
	unsigned int sl		: 1;	/* switch to local control */
} __attribute__ ((packed));

/* Port state returned by steai */
enum etr_psc {
	etr_psc_operational = 0,
	etr_psc_semi_operational = 1,
	etr_psc_protocol_error =  4,
	etr_psc_no_symbols = 8,
	etr_psc_no_signal = 12,
	etr_psc_pps_mode = 13
};

/* Logical port state returned by stetr */
enum etr_lpsc {
	etr_lpsc_operational_step = 0,
	etr_lpsc_operational_alt = 1,
	etr_lpsc_semi_operational = 2,
	etr_lpsc_protocol_error =  4,
	etr_lpsc_no_symbol_sync = 8,
	etr_lpsc_no_signal = 12,
	etr_lpsc_pps_mode = 13
};

/* ETR status words */
struct etr_esw {
	struct etr_eacr eacr;		/* attachment control register */
	unsigned int y		: 1;	/* stepping mode */
	unsigned int _pad0	: 5;	/* must be 00000 */
	unsigned int p		: 1;	/* stepping port number */
	unsigned int q		: 1;	/* data port number */
	unsigned int psc0	: 4;	/* port 0 state code */
	unsigned int psc1	: 4;	/* port 1 state code */
} __attribute__ ((packed));

/* Second level data register status word */
struct etr_slsw {
	unsigned int vv1	: 1;	/* copy of validity bit data frame 1 */
	unsigned int vv2	: 1;	/* copy of validity bit data frame 2 */
	unsigned int vv3	: 1;	/* copy of validity bit data frame 3 */
	unsigned int vv4	: 1;	/* copy of validity bit data frame 4 */
	unsigned int _pad0	: 19;	/* must by all zeroes */
	unsigned int n		: 1;	/* EAF port number */
	unsigned int v1		: 1;	/* validity bit ETR data frame 1 */
	unsigned int v2		: 1;	/* validity bit ETR data frame 2 */
	unsigned int v3		: 1;	/* validity bit ETR data frame 3 */
	unsigned int v4		: 1;	/* validity bit ETR data frame 4 */
	unsigned int _pad1	: 4;	/* must be 0000 */
} __attribute__ ((packed));

/* ETR data frames */
struct etr_edf1 {
	unsigned int u		: 1;	/* untuned bit */
	unsigned int _pad0	: 1;	/* must be 0 */
	unsigned int r		: 1;	/* service request bit */
	unsigned int _pad1	: 4;	/* must be 0000 */
	unsigned int a		: 1;	/* time adjustment bit */
	unsigned int net_id	: 8;	/* ETR network id */
	unsigned int etr_id	: 8;	/* id of ETR which sends data frames */
	unsigned int etr_pn	: 8;	/* port number of ETR output port */
} __attribute__ ((packed));

struct etr_edf2 {
	unsigned int etv	: 32;	/* Upper 32 bits of TOD. */
} __attribute__ ((packed));

struct etr_edf3 {
	unsigned int rc		: 8;	/* failure reason code */
	unsigned int _pad0	: 3;	/* must be 000 */
	unsigned int c		: 1;	/* ETR coupled bit */
	unsigned int tc		: 4;	/* ETR type code */
	unsigned int blto	: 8;	/* biased local time offset */
					/* (blto - 128) * 15 = minutes */
	unsigned int buo	: 8;	/* biased utc offset */
					/* (buo - 128) = leap seconds */
} __attribute__ ((packed));

struct etr_edf4 {
	unsigned int ed		: 8;	/* ETS device dependent data */
	unsigned int _pad0	: 1;	/* must be 0 */
	unsigned int buc	: 5;	/* biased ut1 correction */
					/* (buc - 16) * 0.1 seconds */
	unsigned int em		: 6;	/* ETS error magnitude */
	unsigned int dc		: 6;	/* ETS drift code */
	unsigned int sc		: 6;	/* ETS steering code */
} __attribute__ ((packed));

/*
 * ETR attachment information block, two formats
 * format 1 has 4 reserved words with a size of 64 bytes
 * format 2 has 16 reserved words with a size of 96 bytes
 */
struct etr_aib {
	struct etr_esw esw;
	struct etr_slsw slsw;
	unsigned long long tsp;
	struct etr_edf1 edf1;
	struct etr_edf2 edf2;
	struct etr_edf3 edf3;
	struct etr_edf4 edf4;
	unsigned int reserved[16];
} __attribute__ ((packed,aligned(8)));

/* ETR interruption parameter */
struct etr_interruption_parameter {
	unsigned int _pad0	: 8;
	unsigned int pc0	: 1;	/* port 0 state change */
	unsigned int pc1	: 1;	/* port 1 state change */
	unsigned int _pad1	: 3;
	unsigned int eai	: 1;	/* ETR alert indication */
	unsigned int _pad2	: 18;
} __attribute__ ((packed));

/* Query TOD offset result */
struct etr_ptff_qto {
	unsigned long long physical_clock;
	unsigned long long tod_offset;
	unsigned long long logical_tod_offset;
	unsigned long long tod_epoch_difference;
} __attribute__ ((packed));

/* Inline assembly helper functions */
static inline int etr_setr(struct etr_eacr *ctrl)
{
	int rc = -ENOSYS;

	asm volatile(
		"	.insn	s,0xb2160000,0(%2)\n"
		"0:	la	%0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (rc) : "m" (*ctrl), "a" (ctrl));
	return rc;
}

/* Stores a format 1 aib with 64 bytes */
static inline int etr_stetr(struct etr_aib *aib)
{
	int rc = -ENOSYS;

	asm volatile(
		"	.insn	s,0xb2170000,0(%2)\n"
		"0:	la	%0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (rc) : "m" (*aib), "a" (aib));
	return rc;
}

/* Stores a format 2 aib with 96 bytes for specified port */
static inline int etr_steai(struct etr_aib *aib, unsigned int func)
{
	register unsigned int reg0 asm("0") = func;
	int rc = -ENOSYS;

	asm volatile(
		"	.insn	s,0xb2b30000,0(%2)\n"
		"0:	la	%0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (rc) : "m" (*aib), "a" (aib), "d" (reg0));
	return rc;
}

/* Function codes for the steai instruction. */
#define ETR_STEAI_STEPPING_PORT		0x10
#define ETR_STEAI_ALTERNATE_PORT	0x11
#define ETR_STEAI_PORT_0		0x12
#define ETR_STEAI_PORT_1		0x13

static inline int etr_ptff(void *ptff_block, unsigned int func)
{
	register unsigned int reg0 asm("0") = func;
	register unsigned long reg1 asm("1") = (unsigned long) ptff_block;
	int rc = -ENOSYS;

	asm volatile(
		"	.word	0x0104\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (rc), "=m" (ptff_block)
		: "d" (reg0), "d" (reg1), "m" (ptff_block) : "cc");
	return rc;
}

/* Function codes for the ptff instruction. */
#define ETR_PTFF_QAF	0x00	/* query available functions */
#define ETR_PTFF_QTO	0x01	/* query tod offset */
#define ETR_PTFF_QSI	0x02	/* query steering information */
#define ETR_PTFF_ATO	0x40	/* adjust tod offset */
#define ETR_PTFF_STO	0x41	/* set tod offset */
#define ETR_PTFF_SFS	0x42	/* set fine steering rate */
#define ETR_PTFF_SGS	0x43	/* set gross steering rate */

/* Functions needed by the machine check handler */
extern void etr_switch_to_local(void);
extern void etr_sync_check(void);

#endif /* __S390_ETR_H */
