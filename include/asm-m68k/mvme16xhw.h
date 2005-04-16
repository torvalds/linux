#ifndef _M68K_MVME16xHW_H_
#define _M68K_MVME16xHW_H_

#include <asm/irq.h>

/* Board ID data structure - pointer to this retrieved from Bug by head.S */

/* Note, bytes 12 and 13 are board no in BCD (0162,0166,0167,0177,etc) */

extern long mvme_bdid_ptr;

typedef struct {
	char	bdid[4];
	u_char	rev, mth, day, yr;
	u_short	size, reserved;
	u_short	brdno;
	char brdsuffix[2];
	u_long	options;
	u_short	clun, dlun, ctype, dnum;
	u_long	option2;
} t_bdid, *p_bdid;


typedef struct {
	u_char	ack_icr,
		flt_icr,
		sel_icr,
		pe_icr,
		bsy_icr,
		spare1,
		isr,
		cr,
		spare2,
		spare3,
		spare4,
		data;
} MVMElp, *MVMElpPtr;

#define MVME_LPR_BASE	0xfff42030

#define mvmelp   ((*(volatile MVMElpPtr)(MVME_LPR_BASE)))

typedef struct {
	unsigned char
		ctrl,
		bcd_sec,
		bcd_min,
		bcd_hr,
		bcd_dow,
		bcd_dom,
		bcd_mth,
		bcd_year;
} MK48T08_t, *MK48T08ptr_t;

#define RTC_WRITE	0x80
#define RTC_READ	0x40
#define RTC_STOP	0x20

#define MVME_RTC_BASE	0xfffc1ff8

#define MVME_I596_BASE	0xfff46000

#define MVME_SCC_A_ADDR	0xfff45005
#define MVME_SCC_B_ADDR	0xfff45001
#define MVME_SCC_PCLK	10000000

#define MVME162_IRQ_TYPE_PRIO	0

#define MVME167_IRQ_PRN		0x54
#define MVME16x_IRQ_I596	0x57
#define MVME16x_IRQ_SCSI	0x55
#define MVME16x_IRQ_FLY		0x7f
#define MVME167_IRQ_SER_ERR	0x5c
#define MVME167_IRQ_SER_MODEM	0x5d
#define MVME167_IRQ_SER_TX	0x5e
#define MVME167_IRQ_SER_RX	0x5f
#define MVME16x_IRQ_TIMER	0x59
#define MVME167_IRQ_ABORT	0x6e
#define MVME162_IRQ_ABORT	0x5e

/* SCC interrupts, for MVME162 */
#define MVME162_IRQ_SCC_BASE		0x40
#define MVME162_IRQ_SCCB_TX		0x40
#define MVME162_IRQ_SCCB_STAT		0x42
#define MVME162_IRQ_SCCB_RX		0x44
#define MVME162_IRQ_SCCB_SPCOND		0x46
#define MVME162_IRQ_SCCA_TX		0x48
#define MVME162_IRQ_SCCA_STAT		0x4a
#define MVME162_IRQ_SCCA_RX		0x4c
#define MVME162_IRQ_SCCA_SPCOND		0x4e

/* MVME162 version register */

#define MVME162_VERSION_REG	0xfff4202e

extern unsigned short mvme16x_config;

/* Lower 8 bits must match the revision register in the MC2 chip */

#define MVME16x_CONFIG_SPEED_32		0x0001
#define MVME16x_CONFIG_NO_VMECHIP2	0x0002
#define MVME16x_CONFIG_NO_SCSICHIP	0x0004
#define MVME16x_CONFIG_NO_ETHERNET	0x0008
#define MVME16x_CONFIG_GOT_FPU		0x0010

#define MVME16x_CONFIG_GOT_LP		0x0100
#define MVME16x_CONFIG_GOT_CD2401	0x0200
#define MVME16x_CONFIG_GOT_SCCA		0x0400
#define MVME16x_CONFIG_GOT_SCCB		0x0800

#endif
