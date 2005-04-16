/*
 *  linux/include/asm/traps.h
 *
 *  Copyright (C) 1993        Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#ifndef _M68KNOMMU_TRAPS_H
#define _M68KNOMMU_TRAPS_H

#ifndef __ASSEMBLY__

typedef void (*e_vector)(void);

extern e_vector vectors[];

#endif

#define VEC_BUSERR  (2)
#define VEC_ADDRERR (3)
#define VEC_ILLEGAL (4)
#define VEC_ZERODIV (5)
#define VEC_CHK     (6)
#define VEC_TRAP    (7)
#define VEC_PRIV    (8)
#define VEC_TRACE   (9)
#define VEC_LINE10  (10)
#define VEC_LINE11  (11)
#define VEC_RESV1   (12)
#define VEC_COPROC  (13)
#define VEC_FORMAT  (14)
#define VEC_UNINT   (15)
#define VEC_SPUR    (24)
#define VEC_INT1    (25)
#define VEC_INT2    (26)
#define VEC_INT3    (27)
#define VEC_INT4    (28)
#define VEC_INT5    (29)
#define VEC_INT6    (30)
#define VEC_INT7    (31)
#define VEC_SYS     (32)
#define VEC_TRAP1   (33)
#define VEC_TRAP2   (34)
#define VEC_TRAP3   (35)
#define VEC_TRAP4   (36)
#define VEC_TRAP5   (37)
#define VEC_TRAP6   (38)
#define VEC_TRAP7   (39)
#define VEC_TRAP8   (40)
#define VEC_TRAP9   (41)
#define VEC_TRAP10  (42)
#define VEC_TRAP11  (43)
#define VEC_TRAP12  (44)
#define VEC_TRAP13  (45)
#define VEC_TRAP14  (46)
#define VEC_TRAP15  (47)
#define VEC_FPBRUC  (48)
#define VEC_FPIR    (49)
#define VEC_FPDIVZ  (50)
#define VEC_FPUNDER (51)
#define VEC_FPOE    (52)
#define VEC_FPOVER  (53)
#define VEC_FPNAN   (54)
#define VEC_FPUNSUP (55)
#define	VEC_UNIMPEA (60)
#define	VEC_UNIMPII (61)
#define VEC_USER    (64)

#define VECOFF(vec) ((vec)<<2)

#ifndef __ASSEMBLY__

/* Status register bits */
#define PS_T  (0x8000)
#define PS_S  (0x2000)
#define PS_M  (0x1000)
#define PS_C  (0x0001)

/* structure for stack frames */

struct frame {
    struct pt_regs ptregs;
    union {
	    struct {
		    unsigned long  iaddr;    /* instruction address */
	    } fmt2;
	    struct {
		    unsigned long  effaddr;  /* effective address */
	    } fmt3;
	    struct {
		    unsigned long  effaddr;  /* effective address */
		    unsigned long  pc;	     /* pc of faulted instr */
	    } fmt4;
	    struct {
		    unsigned long  effaddr;  /* effective address */
		    unsigned short ssw;      /* special status word */
		    unsigned short wb3s;     /* write back 3 status */
		    unsigned short wb2s;     /* write back 2 status */
		    unsigned short wb1s;     /* write back 1 status */
		    unsigned long  faddr;    /* fault address */
		    unsigned long  wb3a;     /* write back 3 address */
		    unsigned long  wb3d;     /* write back 3 data */
		    unsigned long  wb2a;     /* write back 2 address */
		    unsigned long  wb2d;     /* write back 2 data */
		    unsigned long  wb1a;     /* write back 1 address */
		    unsigned long  wb1dpd0;  /* write back 1 data/push data 0*/
		    unsigned long  pd1;      /* push data 1*/
		    unsigned long  pd2;      /* push data 2*/
		    unsigned long  pd3;      /* push data 3*/
	    } fmt7;
	    struct {
		    unsigned long  iaddr;    /* instruction address */
		    unsigned short int1[4];  /* internal registers */
	    } fmt9;
	    struct {
		    unsigned short int1;
		    unsigned short ssw;      /* special status word */
		    unsigned short isc;      /* instruction stage c */
		    unsigned short isb;      /* instruction stage b */
		    unsigned long  daddr;    /* data cycle fault address */
		    unsigned short int2[2];
		    unsigned long  dobuf;    /* data cycle output buffer */
		    unsigned short int3[2];
	    } fmta;
	    struct {
		    unsigned short int1;
		    unsigned short ssw;     /* special status word */
		    unsigned short isc;     /* instruction stage c */
		    unsigned short isb;     /* instruction stage b */
		    unsigned long  daddr;   /* data cycle fault address */
		    unsigned short int2[2];
		    unsigned long  dobuf;   /* data cycle output buffer */
		    unsigned short int3[4];
		    unsigned long  baddr;   /* stage B address */
		    unsigned short int4[2];
		    unsigned long  dibuf;   /* data cycle input buffer */
		    unsigned short int5[3];
		    unsigned	   ver : 4; /* stack frame version # */
		    unsigned	   int6:12;
		    unsigned short int7[18];
	    } fmtb;
    } un;
};

#endif /* __ASSEMBLY__ */

#endif /* _M68KNOMMU_TRAPS_H */
