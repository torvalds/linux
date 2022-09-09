/* SPDX-License-Identifier: GPL-2.0 */
/*
 * HD-audio controller (Azalia) registers and helpers
 *
 * For traditional reasons, we still use azx_ prefix here
 */

#ifndef __SOUND_HDA_REGISTER_H
#define __SOUND_HDA_REGISTER_H

#include <linux/io.h>
#include <sound/hdaudio.h>

#define AZX_REG_GCAP			0x00
#define   AZX_GCAP_64OK		(1 << 0)   /* 64bit address support */
#define   AZX_GCAP_NSDO		(3 << 1)   /* # of serial data out signals */
#define   AZX_GCAP_BSS		(31 << 3)  /* # of bidirectional streams */
#define   AZX_GCAP_ISS		(15 << 8)  /* # of input streams */
#define   AZX_GCAP_OSS		(15 << 12) /* # of output streams */
#define AZX_REG_VMIN			0x02
#define AZX_REG_VMAJ			0x03
#define AZX_REG_OUTPAY			0x04
#define AZX_REG_INPAY			0x06
#define AZX_REG_GCTL			0x08
#define   AZX_GCTL_RESET	(1 << 0)   /* controller reset */
#define   AZX_GCTL_FCNTRL	(1 << 1)   /* flush control */
#define   AZX_GCTL_UNSOL	(1 << 8)   /* accept unsol. response enable */
#define AZX_REG_WAKEEN			0x0c
#define AZX_REG_STATESTS		0x0e
#define AZX_REG_GSTS			0x10
#define   AZX_GSTS_FSTS		(1 << 1)   /* flush status */
#define AZX_REG_GCAP2			0x12
#define AZX_REG_LLCH			0x14
#define AZX_REG_OUTSTRMPAY		0x18
#define AZX_REG_INSTRMPAY		0x1A
#define AZX_REG_INTCTL			0x20
#define AZX_REG_INTSTS			0x24
#define AZX_REG_WALLCLK			0x30	/* 24Mhz source */
#define AZX_REG_OLD_SSYNC		0x34	/* SSYNC for old ICH */
#define AZX_REG_SSYNC			0x38
#define AZX_REG_CORBLBASE		0x40
#define AZX_REG_CORBUBASE		0x44
#define AZX_REG_CORBWP			0x48
#define AZX_REG_CORBRP			0x4a
#define   AZX_CORBRP_RST	(1 << 15)  /* read pointer reset */
#define AZX_REG_CORBCTL			0x4c
#define   AZX_CORBCTL_RUN	(1 << 1)   /* enable DMA */
#define   AZX_CORBCTL_CMEIE	(1 << 0)   /* enable memory error irq */
#define AZX_REG_CORBSTS			0x4d
#define   AZX_CORBSTS_CMEI	(1 << 0)   /* memory error indication */
#define AZX_REG_CORBSIZE		0x4e

#define AZX_REG_RIRBLBASE		0x50
#define AZX_REG_RIRBUBASE		0x54
#define AZX_REG_RIRBWP			0x58
#define   AZX_RIRBWP_RST	(1 << 15)  /* write pointer reset */
#define AZX_REG_RINTCNT			0x5a
#define AZX_REG_RIRBCTL			0x5c
#define   AZX_RBCTL_IRQ_EN	(1 << 0)   /* enable IRQ */
#define   AZX_RBCTL_DMA_EN	(1 << 1)   /* enable DMA */
#define   AZX_RBCTL_OVERRUN_EN	(1 << 2)   /* enable overrun irq */
#define AZX_REG_RIRBSTS			0x5d
#define   AZX_RBSTS_IRQ		(1 << 0)   /* response irq */
#define   AZX_RBSTS_OVERRUN	(1 << 2)   /* overrun irq */
#define AZX_REG_RIRBSIZE		0x5e

#define AZX_REG_IC			0x60
#define AZX_REG_IR			0x64
#define AZX_REG_IRS			0x68
#define   AZX_IRS_VALID		(1<<1)
#define   AZX_IRS_BUSY		(1<<0)

#define AZX_REG_DPLBASE			0x70
#define AZX_REG_DPUBASE			0x74
#define   AZX_DPLBASE_ENABLE	0x1	/* Enable position buffer */

/* SD offset: SDI0=0x80, SDI1=0xa0, ... SDO3=0x160 */
enum { SDI0, SDI1, SDI2, SDI3, SDO0, SDO1, SDO2, SDO3 };

/* stream register offsets from stream base */
#define AZX_REG_SD_CTL			0x00
#define AZX_REG_SD_CTL_3B		0x02 /* 3rd byte of SD_CTL register */
#define AZX_REG_SD_STS			0x03
#define AZX_REG_SD_LPIB			0x04
#define AZX_REG_SD_CBL			0x08
#define AZX_REG_SD_LVI			0x0c
#define AZX_REG_SD_FIFOW		0x0e
#define AZX_REG_SD_FIFOSIZE		0x10
#define AZX_REG_SD_FORMAT		0x12
#define AZX_REG_SD_FIFOL		0x14
#define AZX_REG_SD_BDLPL		0x18
#define AZX_REG_SD_BDLPU		0x1c

/* GTS registers */
#define AZX_REG_LLCH			0x14

#define AZX_REG_GTS_BASE		0x520

#define AZX_REG_GTSCC	(AZX_REG_GTS_BASE + 0x00)
#define AZX_REG_WALFCC	(AZX_REG_GTS_BASE + 0x04)
#define AZX_REG_TSCCL	(AZX_REG_GTS_BASE + 0x08)
#define AZX_REG_TSCCU	(AZX_REG_GTS_BASE + 0x0C)
#define AZX_REG_LLPFOC	(AZX_REG_GTS_BASE + 0x14)
#define AZX_REG_LLPCL	(AZX_REG_GTS_BASE + 0x18)
#define AZX_REG_LLPCU	(AZX_REG_GTS_BASE + 0x1C)

/* Haswell/Broadwell display HD-A controller Extended Mode registers */
#define AZX_REG_HSW_EM4			0x100c
#define AZX_REG_HSW_EM5			0x1010

/* Skylake/Broxton vendor-specific registers */
#define AZX_REG_VS_EM1			0x1000
#define AZX_REG_VS_INRC			0x1004
#define AZX_REG_VS_OUTRC		0x1008
#define AZX_REG_VS_FIFOTRK		0x100C
#define AZX_REG_VS_FIFOTRK2		0x1010
#define AZX_REG_VS_EM2			0x1030
#define AZX_REG_VS_EM3L			0x1038
#define AZX_REG_VS_EM3U			0x103C
#define AZX_REG_VS_EM4L			0x1040
#define AZX_REG_VS_EM4U			0x1044
#define AZX_REG_VS_LTRP			0x1048
#define AZX_REG_VS_D0I3C		0x104A
#define AZX_REG_VS_PCE			0x104B
#define AZX_REG_VS_L2MAGC		0x1050
#define AZX_REG_VS_L2LAHPT		0x1054
#define AZX_REG_VS_SDXDPIB_XBASE	0x1084
#define AZX_REG_VS_SDXDPIB_XINTERVAL	0x20
#define AZX_REG_VS_SDXEFIFOS_XBASE	0x1094
#define AZX_REG_VS_SDXEFIFOS_XINTERVAL	0x20

/* PCI space */
#define AZX_PCIREG_TCSEL		0x44

/*
 * other constants
 */

/* max number of fragments - we may use more if allocating more pages for BDL */
#define BDL_SIZE		4096
#define AZX_MAX_BDL_ENTRIES	(BDL_SIZE / 16)
#define AZX_MAX_FRAG		32
/*
 * max buffer size - artificial 4MB limit per stream to avoid big allocations
 * In theory it can be really big, but as it is per stream on systems with many streams memory could
 * be quickly saturated if userspace requests maximum buffer size for each of them.
 */
#define AZX_MAX_BUF_SIZE	(4*1024*1024)

/* RIRB int mask: overrun[2], response[0] */
#define RIRB_INT_RESPONSE	0x01
#define RIRB_INT_OVERRUN	0x04
#define RIRB_INT_MASK		0x05

/* STATESTS int mask: S3,SD2,SD1,SD0 */
#define STATESTS_INT_MASK	((1 << HDA_MAX_CODECS) - 1)

/* SD_CTL bits */
#define SD_CTL_STREAM_RESET	0x01	/* stream reset bit */
#define SD_CTL_DMA_START	0x02	/* stream DMA start bit */
#define SD_CTL_STRIPE		(3 << 16)	/* stripe control */
#define SD_CTL_TRAFFIC_PRIO	(1 << 18)	/* traffic priority */
#define SD_CTL_DIR		(1 << 19)	/* bi-directional stream */
#define SD_CTL_STREAM_TAG_MASK	(0xf << 20)
#define SD_CTL_STREAM_TAG_SHIFT	20

/* SD_CTL and SD_STS */
#define SD_INT_DESC_ERR		0x10	/* descriptor error interrupt */
#define SD_INT_FIFO_ERR		0x08	/* FIFO error interrupt */
#define SD_INT_COMPLETE		0x04	/* completion interrupt */
#define SD_INT_MASK		(SD_INT_DESC_ERR|SD_INT_FIFO_ERR|\
				 SD_INT_COMPLETE)
#define SD_CTL_STRIPE_MASK	0x3	/* stripe control mask */

/* SD_STS */
#define SD_STS_FIFO_READY	0x20	/* FIFO ready */

/* INTCTL and INTSTS */
#define AZX_INT_ALL_STREAM	0xff	   /* all stream interrupts */
#define AZX_INT_CTRL_EN	0x40000000 /* controller interrupt enable bit */
#define AZX_INT_GLOBAL_EN	0x80000000 /* global interrupt enable bit */

/* below are so far hardcoded - should read registers in future */
#define AZX_MAX_CORB_ENTRIES	256
#define AZX_MAX_RIRB_ENTRIES	256

/* Capability header  Structure */
#define AZX_REG_CAP_HDR			0x0
#define AZX_CAP_HDR_VER_OFF		28
#define AZX_CAP_HDR_VER_MASK		(0xF << AZX_CAP_HDR_VER_OFF)
#define AZX_CAP_HDR_ID_OFF		16
#define AZX_CAP_HDR_ID_MASK		(0xFFF << AZX_CAP_HDR_ID_OFF)
#define AZX_CAP_HDR_NXT_PTR_MASK	0xFFFF

/* registers of Software Position Based FIFO Capability Structure */
#define AZX_SPB_CAP_ID			0x4
#define AZX_REG_SPB_BASE_ADDR		0x700
#define AZX_REG_SPB_SPBFCH		0x00
#define AZX_REG_SPB_SPBFCCTL		0x04
/* Base used to calculate the iterating register offset */
#define AZX_SPB_BASE			0x08
/* Interval used to calculate the iterating register offset */
#define AZX_SPB_INTERVAL		0x08
/* SPIB base */
#define AZX_SPB_SPIB			0x00
/* SPIB MAXFIFO base*/
#define AZX_SPB_MAXFIFO			0x04

/* registers of Global Time Synchronization Capability Structure */
#define AZX_GTS_CAP_ID			0x1
#define AZX_REG_GTS_GTSCH		0x00
#define AZX_REG_GTS_GTSCD		0x04
#define AZX_REG_GTS_GTSCTLAC		0x0C
#define AZX_GTS_BASE			0x20
#define AZX_GTS_INTERVAL		0x20

/* registers for Processing Pipe Capability Structure */
#define AZX_PP_CAP_ID			0x3
#define AZX_REG_PP_PPCH			0x10
#define AZX_REG_PP_PPCTL		0x04
#define AZX_PPCTL_PIE			(1<<31)
#define AZX_PPCTL_GPROCEN		(1<<30)
/* _X_ = dma engine # and cannot * exceed 29 (per spec max 30 dma engines) */
#define AZX_PPCTL_PROCEN(_X_)		(1<<(_X_))

#define AZX_REG_PP_PPSTS		0x08

#define AZX_PPHC_BASE			0x10
#define AZX_PPHC_INTERVAL		0x10

#define AZX_REG_PPHCLLPL		0x0
#define AZX_REG_PPHCLLPU		0x4
#define AZX_REG_PPHCLDPL		0x8
#define AZX_REG_PPHCLDPU		0xC

#define AZX_PPLC_BASE			0x10
#define AZX_PPLC_MULTI			0x10
#define AZX_PPLC_INTERVAL		0x10

#define AZX_REG_PPLCCTL			0x0
#define AZX_PPLCCTL_STRM_BITS		4
#define AZX_PPLCCTL_STRM_SHIFT		20
#define AZX_REG_MASK(bit_num, offset) \
	(((1 << (bit_num)) - 1) << (offset))
#define AZX_PPLCCTL_STRM_MASK \
	AZX_REG_MASK(AZX_PPLCCTL_STRM_BITS, AZX_PPLCCTL_STRM_SHIFT)
#define AZX_PPLCCTL_RUN			(1<<1)
#define AZX_PPLCCTL_STRST		(1<<0)

#define AZX_REG_PPLCFMT			0x4
#define AZX_REG_PPLCLLPL		0x8
#define AZX_REG_PPLCLLPU		0xC

/* registers for Multiple Links Capability Structure */
#define AZX_ML_CAP_ID			0x2
#define AZX_REG_ML_MLCH			0x00
#define AZX_REG_ML_MLCD			0x04
#define AZX_ML_BASE			0x40
#define AZX_ML_INTERVAL			0x40

#define AZX_REG_ML_LCAP			0x00
#define AZX_REG_ML_LCTL			0x04
#define AZX_REG_ML_LOSIDV		0x08
#define AZX_REG_ML_LSDIID		0x0C
#define AZX_REG_ML_LPSOO		0x10
#define AZX_REG_ML_LPSIO		0x12
#define AZX_REG_ML_LWALFC		0x18
#define AZX_REG_ML_LOUTPAY		0x20
#define AZX_REG_ML_LINPAY		0x30

/* bit0 is reserved, with BIT(1) mapping to stream1 */
#define ML_LOSIDV_STREAM_MASK		0xFFFE

#define ML_LCTL_SCF_MASK			0xF
#define AZX_MLCTL_SPA				(0x1 << 16)
#define AZX_MLCTL_CPA				(0x1 << 23)
#define AZX_MLCTL_SPA_SHIFT			16
#define AZX_MLCTL_CPA_SHIFT			23

/* registers for DMA Resume Capability Structure */
#define AZX_DRSM_CAP_ID			0x5
#define AZX_REG_DRSM_CTL		0x4
/* Base used to calculate the iterating register offset */
#define AZX_DRSM_BASE			0x08
/* Interval used to calculate the iterating register offset */
#define AZX_DRSM_INTERVAL		0x08

/* Global time synchronization registers */
#define GTSCC_TSCCD_MASK		0x80000000
#define GTSCC_TSCCD_SHIFT		BIT(31)
#define GTSCC_TSCCI_MASK		0x20
#define GTSCC_CDMAS_DMA_DIR_SHIFT	4

#define WALFCC_CIF_MASK			0x1FF
#define WALFCC_FN_SHIFT			9
#define HDA_CLK_CYCLES_PER_FRAME	512

/*
 * An error occurs near frame "rollover". The clocks in frame value indicates
 * whether this error may have occurred. Here we use the value of 10. Please
 * see the errata for the right number [<10]
 */
#define HDA_MAX_CYCLE_VALUE		499
#define HDA_MAX_CYCLE_OFFSET		10
#define HDA_MAX_CYCLE_READ_RETRY	10

#define TSCCU_CCU_SHIFT			32
#define LLPC_CCU_SHIFT			32


/*
 * helpers to read the stream position
 */
static inline unsigned int
snd_hdac_stream_get_pos_lpib(struct hdac_stream *stream)
{
	return snd_hdac_stream_readl(stream, SD_LPIB);
}

static inline unsigned int
snd_hdac_stream_get_pos_posbuf(struct hdac_stream *stream)
{
	return le32_to_cpu(*stream->posbuf);
}

#endif /* __SOUND_HDA_REGISTER_H */
