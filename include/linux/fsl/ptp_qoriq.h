// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 OMICRON electronics GmbH
 * Copyright 2018 NXP
 */
#ifndef __PTP_QORIQ_H__
#define __PTP_QORIQ_H__

#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/ptp_clock_kernel.h>

/*
 * qoriq ptp registers
 */
struct ctrl_regs {
	u32 tmr_ctrl;     /* Timer control register */
	u32 tmr_tevent;   /* Timestamp event register */
	u32 tmr_temask;   /* Timer event mask register */
	u32 tmr_pevent;   /* Timestamp event register */
	u32 tmr_pemask;   /* Timer event mask register */
	u32 tmr_stat;     /* Timestamp status register */
	u32 tmr_cnt_h;    /* Timer counter high register */
	u32 tmr_cnt_l;    /* Timer counter low register */
	u32 tmr_add;      /* Timer drift compensation addend register */
	u32 tmr_acc;      /* Timer accumulator register */
	u32 tmr_prsc;     /* Timer prescale */
	u8  res1[4];
	u32 tmroff_h;     /* Timer offset high */
	u32 tmroff_l;     /* Timer offset low */
};

struct alarm_regs {
	u32 tmr_alarm1_h; /* Timer alarm 1 high register */
	u32 tmr_alarm1_l; /* Timer alarm 1 high register */
	u32 tmr_alarm2_h; /* Timer alarm 2 high register */
	u32 tmr_alarm2_l; /* Timer alarm 2 high register */
};

struct fiper_regs {
	u32 tmr_fiper1;   /* Timer fixed period interval */
	u32 tmr_fiper2;   /* Timer fixed period interval */
	u32 tmr_fiper3;   /* Timer fixed period interval */
};

struct etts_regs {
	u32 tmr_etts1_h;  /* Timestamp of general purpose external trigger */
	u32 tmr_etts1_l;  /* Timestamp of general purpose external trigger */
	u32 tmr_etts2_h;  /* Timestamp of general purpose external trigger */
	u32 tmr_etts2_l;  /* Timestamp of general purpose external trigger */
};

struct ptp_qoriq_registers {
	struct ctrl_regs __iomem *ctrl_regs;
	struct alarm_regs __iomem *alarm_regs;
	struct fiper_regs __iomem *fiper_regs;
	struct etts_regs __iomem *etts_regs;
};

/* Offset definitions for the four register groups */
#define ETSEC_CTRL_REGS_OFFSET	0x0
#define ETSEC_ALARM_REGS_OFFSET	0x40
#define ETSEC_FIPER_REGS_OFFSET	0x80
#define ETSEC_ETTS_REGS_OFFSET	0xa0

#define CTRL_REGS_OFFSET	0x80
#define ALARM_REGS_OFFSET	0xb8
#define FIPER_REGS_OFFSET	0xd0
#define ETTS_REGS_OFFSET	0xe0


/* Bit definitions for the TMR_CTRL register */
#define ALM1P                 (1<<31) /* Alarm1 output polarity */
#define ALM2P                 (1<<30) /* Alarm2 output polarity */
#define FIPERST               (1<<28) /* FIPER start indication */
#define PP1L                  (1<<27) /* Fiper1 pulse loopback mode enabled. */
#define PP2L                  (1<<26) /* Fiper2 pulse loopback mode enabled. */
#define TCLK_PERIOD_SHIFT     (16) /* 1588 timer reference clock period. */
#define TCLK_PERIOD_MASK      (0x3ff)
#define RTPE                  (1<<15) /* Record Tx Timestamp to PAL Enable. */
#define FRD                   (1<<14) /* FIPER Realignment Disable */
#define ESFDP                 (1<<11) /* External Tx/Rx SFD Polarity. */
#define ESFDE                 (1<<10) /* External Tx/Rx SFD Enable. */
#define ETEP2                 (1<<9) /* External trigger 2 edge polarity */
#define ETEP1                 (1<<8) /* External trigger 1 edge polarity */
#define COPH                  (1<<7) /* Generated clock output phase. */
#define CIPH                  (1<<6) /* External oscillator input clock phase */
#define TMSR                  (1<<5) /* Timer soft reset. */
#define BYP                   (1<<3) /* Bypass drift compensated clock */
#define TE                    (1<<2) /* 1588 timer enable. */
#define CKSEL_SHIFT           (0)    /* 1588 Timer reference clock source */
#define CKSEL_MASK            (0x3)

/* Bit definitions for the TMR_TEVENT register */
#define ETS2                  (1<<25) /* External trigger 2 timestamp sampled */
#define ETS1                  (1<<24) /* External trigger 1 timestamp sampled */
#define ALM2                  (1<<17) /* Current time = alarm time register 2 */
#define ALM1                  (1<<16) /* Current time = alarm time register 1 */
#define PP1                   (1<<7)  /* periodic pulse generated on FIPER1 */
#define PP2                   (1<<6)  /* periodic pulse generated on FIPER2 */
#define PP3                   (1<<5)  /* periodic pulse generated on FIPER3 */

/* Bit definitions for the TMR_TEMASK register */
#define ETS2EN                (1<<25) /* External trigger 2 timestamp enable */
#define ETS1EN                (1<<24) /* External trigger 1 timestamp enable */
#define ALM2EN                (1<<17) /* Timer ALM2 event enable */
#define ALM1EN                (1<<16) /* Timer ALM1 event enable */
#define PP1EN                 (1<<7) /* Periodic pulse event 1 enable */
#define PP2EN                 (1<<6) /* Periodic pulse event 2 enable */

/* Bit definitions for the TMR_PEVENT register */
#define TXP2                  (1<<9) /* PTP transmitted timestamp im TXTS2 */
#define TXP1                  (1<<8) /* PTP transmitted timestamp in TXTS1 */
#define RXP                   (1<<0) /* PTP frame has been received */

/* Bit definitions for the TMR_PEMASK register */
#define TXP2EN                (1<<9) /* Transmit PTP packet event 2 enable */
#define TXP1EN                (1<<8) /* Transmit PTP packet event 1 enable */
#define RXPEN                 (1<<0) /* Receive PTP packet event enable */

/* Bit definitions for the TMR_STAT register */
#define STAT_VEC_SHIFT        (0) /* Timer general purpose status vector */
#define STAT_VEC_MASK         (0x3f)
#define ETS1_VLD              (1<<24)
#define ETS2_VLD              (1<<25)

/* Bit definitions for the TMR_PRSC register */
#define PRSC_OCK_SHIFT        (0) /* Output clock division/prescale factor. */
#define PRSC_OCK_MASK         (0xffff)


#define DRIVER		"ptp_qoriq"
#define N_EXT_TS	2

#define DEFAULT_CKSEL		1
#define DEFAULT_TMR_PRSC	2
#define DEFAULT_FIPER1_PERIOD	1000000000
#define DEFAULT_FIPER2_PERIOD	100000

struct ptp_qoriq {
	void __iomem *base;
	struct ptp_qoriq_registers regs;
	spinlock_t lock; /* protects regs */
	struct ptp_clock *clock;
	struct ptp_clock_info caps;
	struct resource *rsrc;
	struct dentry *debugfs_root;
	struct device *dev;
	bool extts_fifo_support;
	int irq;
	int phc_index;
	u64 alarm_interval; /* for periodic alarm */
	u64 alarm_value;
	u32 tclk_period;  /* nanoseconds */
	u32 tmr_prsc;
	u32 tmr_add;
	u32 cksel;
	u32 tmr_fiper1;
	u32 tmr_fiper2;
	u32 (*read)(unsigned __iomem *addr);
	void (*write)(unsigned __iomem *addr, u32 val);
};

static inline u32 qoriq_read_be(unsigned __iomem *addr)
{
	return ioread32be(addr);
}

static inline void qoriq_write_be(unsigned __iomem *addr, u32 val)
{
	iowrite32be(val, addr);
}

static inline u32 qoriq_read_le(unsigned __iomem *addr)
{
	return ioread32(addr);
}

static inline void qoriq_write_le(unsigned __iomem *addr, u32 val)
{
	iowrite32(val, addr);
}

irqreturn_t ptp_qoriq_isr(int irq, void *priv);
int ptp_qoriq_init(struct ptp_qoriq *ptp_qoriq, void __iomem *base,
		   const struct ptp_clock_info *caps);
void ptp_qoriq_free(struct ptp_qoriq *ptp_qoriq);
int ptp_qoriq_adjfine(struct ptp_clock_info *ptp, long scaled_ppm);
int ptp_qoriq_adjtime(struct ptp_clock_info *ptp, s64 delta);
int ptp_qoriq_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts);
int ptp_qoriq_settime(struct ptp_clock_info *ptp,
		      const struct timespec64 *ts);
int ptp_qoriq_enable(struct ptp_clock_info *ptp,
		     struct ptp_clock_request *rq, int on);
#ifdef CONFIG_DEBUG_FS
void ptp_qoriq_create_debugfs(struct ptp_qoriq *ptp_qoriq);
void ptp_qoriq_remove_debugfs(struct ptp_qoriq *ptp_qoriq);
#else
static inline void ptp_qoriq_create_debugfs(struct ptp_qoriq *ptp_qoriq)
{ }
static inline void ptp_qoriq_remove_debugfs(struct ptp_qoriq *ptp_qoriq)
{ }
#endif

#endif
