/* SPDX-License-Identifier: GPL-2.0 */
/*
 * OMAP Multi-Channel Buffered Serial Port
 *
 * Contact: Jarkko Nikula <jarkko.nikula@bitmer.com>
 *          Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#ifndef __OMAP_MCBSP_PRIV_H__
#define __OMAP_MCBSP_PRIV_H__

#include <linux/platform_data/asoc-ti-mcbsp.h>

#ifdef CONFIG_ARCH_OMAP1
#define mcbsp_omap1()	1
#else
#define mcbsp_omap1()	0
#endif

/* McBSP register numbers. Register address offset = num * reg_step */
enum {
	/* Common registers */
	OMAP_MCBSP_REG_SPCR2 = 4,
	OMAP_MCBSP_REG_SPCR1,
	OMAP_MCBSP_REG_RCR2,
	OMAP_MCBSP_REG_RCR1,
	OMAP_MCBSP_REG_XCR2,
	OMAP_MCBSP_REG_XCR1,
	OMAP_MCBSP_REG_SRGR2,
	OMAP_MCBSP_REG_SRGR1,
	OMAP_MCBSP_REG_MCR2,
	OMAP_MCBSP_REG_MCR1,
	OMAP_MCBSP_REG_RCERA,
	OMAP_MCBSP_REG_RCERB,
	OMAP_MCBSP_REG_XCERA,
	OMAP_MCBSP_REG_XCERB,
	OMAP_MCBSP_REG_PCR0,
	OMAP_MCBSP_REG_RCERC,
	OMAP_MCBSP_REG_RCERD,
	OMAP_MCBSP_REG_XCERC,
	OMAP_MCBSP_REG_XCERD,
	OMAP_MCBSP_REG_RCERE,
	OMAP_MCBSP_REG_RCERF,
	OMAP_MCBSP_REG_XCERE,
	OMAP_MCBSP_REG_XCERF,
	OMAP_MCBSP_REG_RCERG,
	OMAP_MCBSP_REG_RCERH,
	OMAP_MCBSP_REG_XCERG,
	OMAP_MCBSP_REG_XCERH,

	/* OMAP1-OMAP2420 registers */
	OMAP_MCBSP_REG_DRR2 = 0,
	OMAP_MCBSP_REG_DRR1,
	OMAP_MCBSP_REG_DXR2,
	OMAP_MCBSP_REG_DXR1,

	/* OMAP2430 and onwards */
	OMAP_MCBSP_REG_DRR = 0,
	OMAP_MCBSP_REG_DXR = 2,
	OMAP_MCBSP_REG_SYSCON =	35,
	OMAP_MCBSP_REG_THRSH2,
	OMAP_MCBSP_REG_THRSH1,
	OMAP_MCBSP_REG_IRQST = 40,
	OMAP_MCBSP_REG_IRQEN,
	OMAP_MCBSP_REG_WAKEUPEN,
	OMAP_MCBSP_REG_XCCR,
	OMAP_MCBSP_REG_RCCR,
	OMAP_MCBSP_REG_XBUFFSTAT,
	OMAP_MCBSP_REG_RBUFFSTAT,
	OMAP_MCBSP_REG_SSELCR,
};

/************************** McBSP SPCR1 bit definitions ***********************/
#define RRST			BIT(0)
#define RRDY			BIT(1)
#define RFULL			BIT(2)
#define RSYNC_ERR		BIT(3)
#define RINTM(value)		(((value) & 0x3) << 4)	/* bits 4:5 */
#define ABIS			BIT(6)
#define DXENA			BIT(7)
#define CLKSTP(value)		(((value) & 0x3) << 11)	/* bits 11:12 */
#define RJUST(value)		(((value) & 0x3) << 13)	/* bits 13:14 */
#define ALB			BIT(15)
#define DLB			BIT(15)

/************************** McBSP SPCR2 bit definitions ***********************/
#define XRST			BIT(0)
#define XRDY			BIT(1)
#define XEMPTY			BIT(2)
#define XSYNC_ERR		BIT(3)
#define XINTM(value)		(((value) & 0x3) << 4)	/* bits 4:5 */
#define GRST			BIT(6)
#define FRST			BIT(7)
#define SOFT			BIT(8)
#define FREE			BIT(9)

/************************** McBSP PCR bit definitions *************************/
#define CLKRP			BIT(0)
#define CLKXP			BIT(1)
#define FSRP			BIT(2)
#define FSXP			BIT(3)
#define DR_STAT			BIT(4)
#define DX_STAT			BIT(5)
#define CLKS_STAT		BIT(6)
#define SCLKME			BIT(7)
#define CLKRM			BIT(8)
#define CLKXM			BIT(9)
#define FSRM			BIT(10)
#define FSXM			BIT(11)
#define RIOEN			BIT(12)
#define XIOEN			BIT(13)
#define IDLE_EN			BIT(14)

/************************** McBSP RCR1 bit definitions ************************/
#define RWDLEN1(value)		(((value) & 0x7) << 5)	/* Bits 5:7 */
#define RFRLEN1(value)		(((value) & 0x7f) << 8)	/* Bits 8:14 */

/************************** McBSP XCR1 bit definitions ************************/
#define XWDLEN1(value)		(((value) & 0x7) << 5)	/* Bits 5:7 */
#define XFRLEN1(value)		(((value) & 0x7f) << 8)	/* Bits 8:14 */

/*************************** McBSP RCR2 bit definitions ***********************/
#define RDATDLY(value)		((value) & 0x3)		/* Bits 0:1 */
#define RFIG			BIT(2)
#define RCOMPAND(value)		(((value) & 0x3) << 3)	/* Bits 3:4 */
#define RWDLEN2(value)		(((value) & 0x7) << 5)	/* Bits 5:7 */
#define RFRLEN2(value)		(((value) & 0x7f) << 8)	/* Bits 8:14 */
#define RPHASE			BIT(15)

/*************************** McBSP XCR2 bit definitions ***********************/
#define XDATDLY(value)		((value) & 0x3)		/* Bits 0:1 */
#define XFIG			BIT(2)
#define XCOMPAND(value)		(((value) & 0x3) << 3)	/* Bits 3:4 */
#define XWDLEN2(value)		(((value) & 0x7) << 5)	/* Bits 5:7 */
#define XFRLEN2(value)		(((value) & 0x7f) << 8)	/* Bits 8:14 */
#define XPHASE			BIT(15)

/************************* McBSP SRGR1 bit definitions ************************/
#define CLKGDV(value)		((value) & 0x7f)		/* Bits 0:7 */
#define FWID(value)		(((value) & 0xff) << 8)	/* Bits 8:15 */

/************************* McBSP SRGR2 bit definitions ************************/
#define FPER(value)		((value) & 0x0fff)	/* Bits 0:11 */
#define FSGM			BIT(12)
#define CLKSM			BIT(13)
#define CLKSP			BIT(14)
#define GSYNC			BIT(15)

/************************* McBSP MCR1 bit definitions *************************/
#define RMCM			BIT(0)
#define RCBLK(value)		(((value) & 0x7) << 2)	/* Bits 2:4 */
#define RPABLK(value)		(((value) & 0x3) << 5)	/* Bits 5:6 */
#define RPBBLK(value)		(((value) & 0x3) << 7)	/* Bits 7:8 */

/************************* McBSP MCR2 bit definitions *************************/
#define XMCM(value)		((value) & 0x3)		/* Bits 0:1 */
#define XCBLK(value)		(((value) & 0x7) << 2)	/* Bits 2:4 */
#define XPABLK(value)		(((value) & 0x3) << 5)	/* Bits 5:6 */
#define XPBBLK(value)		(((value) & 0x3) << 7)	/* Bits 7:8 */

/*********************** McBSP XCCR bit definitions *************************/
#define XDISABLE		BIT(0)
#define XDMAEN			BIT(3)
#define DILB			BIT(5)
#define XFULL_CYCLE		BIT(11)
#define DXENDLY(value)		(((value) & 0x3) << 12)	/* Bits 12:13 */
#define PPCONNECT		BIT(14)
#define EXTCLKGATE		BIT(15)

/********************** McBSP RCCR bit definitions *************************/
#define RDISABLE		BIT(0)
#define RDMAEN			BIT(3)
#define RFULL_CYCLE		BIT(11)

/********************** McBSP SYSCONFIG bit definitions ********************/
#define SOFTRST			BIT(1)
#define ENAWAKEUP		BIT(2)
#define SIDLEMODE(value)	(((value) & 0x3) << 3)
#define CLOCKACTIVITY(value)	(((value) & 0x3) << 8)

/********************** McBSP DMA operating modes **************************/
#define MCBSP_DMA_MODE_ELEMENT		0
#define MCBSP_DMA_MODE_THRESHOLD	1

/********************** McBSP WAKEUPEN/IRQST/IRQEN bit definitions *********/
#define RSYNCERREN		BIT(0)
#define RFSREN			BIT(1)
#define REOFEN			BIT(2)
#define RRDYEN			BIT(3)
#define RUNDFLEN		BIT(4)
#define ROVFLEN			BIT(5)
#define XSYNCERREN		BIT(7)
#define XFSXEN			BIT(8)
#define XEOFEN			BIT(9)
#define XRDYEN			BIT(10)
#define XUNDFLEN		BIT(11)
#define XOVFLEN			BIT(12)
#define XEMPTYEOFEN		BIT(14)

/* Clock signal muxing options */
#define CLKR_SRC_CLKR		0 /* CLKR signal is from the CLKR pin */
#define CLKR_SRC_CLKX		1 /* CLKR signal is from the CLKX pin */
#define FSR_SRC_FSR		2 /* FSR signal is from the FSR pin */
#define FSR_SRC_FSX		3 /* FSR signal is from the FSX pin */

/* McBSP functional clock sources */
#define MCBSP_CLKS_PRCM_SRC	0
#define MCBSP_CLKS_PAD_SRC	1

/* we don't do multichannel for now */
struct omap_mcbsp_reg_cfg {
	u16 spcr2;
	u16 spcr1;
	u16 rcr2;
	u16 rcr1;
	u16 xcr2;
	u16 xcr1;
	u16 srgr2;
	u16 srgr1;
	u16 mcr2;
	u16 mcr1;
	u16 pcr0;
	u16 rcerc;
	u16 rcerd;
	u16 xcerc;
	u16 xcerd;
	u16 rcere;
	u16 rcerf;
	u16 xcere;
	u16 xcerf;
	u16 rcerg;
	u16 rcerh;
	u16 xcerg;
	u16 xcerh;
	u16 xccr;
	u16 rccr;
};

struct omap_mcbsp_st_data;

struct omap_mcbsp {
	struct device *dev;
	struct clk *fclk;
	spinlock_t lock;
	unsigned long phys_base;
	unsigned long phys_dma_base;
	void __iomem *io_base;
	u8 id;
	/*
	 * Flags indicating is the bus already activated and configured by
	 * another substream
	 */
	int active;
	int configured;
	u8 free;

	int irq;
	int rx_irq;
	int tx_irq;

	/* Protect the field .free, while checking if the mcbsp is in use */
	struct omap_mcbsp_platform_data *pdata;
	struct omap_mcbsp_st_data *st_data;
	struct omap_mcbsp_reg_cfg cfg_regs;
	struct snd_dmaengine_dai_dma_data dma_data[2];
	unsigned int dma_req[2];
	int dma_op_mode;
	u16 max_tx_thres;
	u16 max_rx_thres;
	void *reg_cache;
	int reg_cache_size;

	unsigned int fmt;
	unsigned int in_freq;
	unsigned int latency[2];
	int clk_div;
	int wlen;

	struct pm_qos_request pm_qos_req;
};

static inline void omap_mcbsp_write(struct omap_mcbsp *mcbsp, u16 reg, u32 val)
{
	void __iomem *addr = mcbsp->io_base + reg * mcbsp->pdata->reg_step;

	if (mcbsp->pdata->reg_size == 2) {
		((u16 *)mcbsp->reg_cache)[reg] = (u16)val;
		writew_relaxed((u16)val, addr);
	} else {
		((u32 *)mcbsp->reg_cache)[reg] = val;
		writel_relaxed(val, addr);
	}
}

static inline int omap_mcbsp_read(struct omap_mcbsp *mcbsp, u16 reg,
				  bool from_cache)
{
	void __iomem *addr = mcbsp->io_base + reg * mcbsp->pdata->reg_step;

	if (mcbsp->pdata->reg_size == 2) {
		return !from_cache ? readw_relaxed(addr) :
				     ((u16 *)mcbsp->reg_cache)[reg];
	} else {
		return !from_cache ? readl_relaxed(addr) :
				     ((u32 *)mcbsp->reg_cache)[reg];
	}
}

#define MCBSP_READ(mcbsp, reg) \
		omap_mcbsp_read(mcbsp, OMAP_MCBSP_REG_##reg, 0)
#define MCBSP_WRITE(mcbsp, reg, val) \
		omap_mcbsp_write(mcbsp, OMAP_MCBSP_REG_##reg, val)
#define MCBSP_READ_CACHE(mcbsp, reg) \
		omap_mcbsp_read(mcbsp, OMAP_MCBSP_REG_##reg, 1)


/* Sidetone specific API */
int omap_mcbsp_st_init(struct platform_device *pdev);
int omap_mcbsp_st_start(struct omap_mcbsp *mcbsp);
int omap_mcbsp_st_stop(struct omap_mcbsp *mcbsp);

#endif /* __OMAP_MCBSP_PRIV_H__ */
