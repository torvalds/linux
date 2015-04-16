/*
 * Driver for AMD7930 sound chips found on Sparcs.
 * Copyright (C) 2002, 2008 David S. Miller <davem@davemloft.net>
 *
 * Based entirely upon drivers/sbus/audio/amd7930.c which is:
 * Copyright (C) 1996,1997 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 *
 * --- Notes from Thomas's original driver ---
 * This is the lowlevel driver for the AMD7930 audio chip found on all
 * sun4c machines and some sun4m machines.
 *
 * The amd7930 is actually an ISDN chip which has a very simple
 * integrated audio encoder/decoder. When Sun decided on what chip to
 * use for audio, they had the brilliant idea of using the amd7930 and
 * only connecting the audio encoder/decoder pins.
 *
 * Thanks to the AMD engineer who was able to get us the AMD79C30
 * databook which has all the programming information and gain tables.
 *
 * Advanced Micro Devices' Am79C30A is an ISDN/audio chip used in the
 * SparcStation 1+.  The chip provides microphone and speaker interfaces
 * which provide mono-channel audio at 8K samples per second via either
 * 8-bit A-law or 8-bit mu-law encoding.  Also, the chip features an
 * ISDN BRI Line Interface Unit (LIU), I.430 S/T physical interface,
 * which performs basic D channel LAPD processing and provides raw
 * B channel data.  The digital audio channel, the two ISDN B channels,
 * and two 64 Kbps channels to the microprocessor are all interconnected
 * via a multiplexer.
 * --- End of notes from Thoamas's original driver ---
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/initval.h>

#include <asm/irq.h>
#include <asm/prom.h>

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Sun AMD7930 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Sun AMD7930 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Sun AMD7930 soundcard.");
MODULE_AUTHOR("Thomas K. Dyas and David S. Miller");
MODULE_DESCRIPTION("Sun AMD7930");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Sun,AMD7930}}");

/* Device register layout.  */

/* Register interface presented to the CPU by the amd7930. */
#define AMD7930_CR	0x00UL		/* Command Register (W) */
#define AMD7930_IR	AMD7930_CR	/* Interrupt Register (R) */
#define AMD7930_DR	0x01UL		/* Data Register (R/W) */
#define AMD7930_DSR1	0x02UL		/* D-channel Status Register 1 (R) */
#define AMD7930_DER	0x03UL		/* D-channel Error Register (R) */
#define AMD7930_DCTB	0x04UL		/* D-channel Transmit Buffer (W) */
#define AMD7930_DCRB	AMD7930_DCTB	/* D-channel Receive Buffer (R) */
#define AMD7930_BBTB	0x05UL		/* Bb-channel Transmit Buffer (W) */
#define AMD7930_BBRB	AMD7930_BBTB	/* Bb-channel Receive Buffer (R) */
#define AMD7930_BCTB	0x06UL		/* Bc-channel Transmit Buffer (W) */
#define AMD7930_BCRB	AMD7930_BCTB	/* Bc-channel Receive Buffer (R) */
#define AMD7930_DSR2	0x07UL		/* D-channel Status Register 2 (R) */

/* Indirect registers in the Main Audio Processor. */
struct amd7930_map {
	__u16	x[8];
	__u16	r[8];
	__u16	gx;
	__u16	gr;
	__u16	ger;
	__u16	stgr;
	__u16	ftgr;
	__u16	atgr;
	__u8	mmr1;
	__u8	mmr2;
};

/* After an amd7930 interrupt, reading the Interrupt Register (ir)
 * clears the interrupt and returns a bitmask indicating which
 * interrupt source(s) require service.
 */

#define AMR_IR_DTTHRSH			0x01 /* D-channel xmit threshold */
#define AMR_IR_DRTHRSH			0x02 /* D-channel recv threshold */
#define AMR_IR_DSRI			0x04 /* D-channel packet status */
#define AMR_IR_DERI			0x08 /* D-channel error */
#define AMR_IR_BBUF			0x10 /* B-channel data xfer */
#define AMR_IR_LSRI			0x20 /* LIU status */
#define AMR_IR_DSR2I			0x40 /* D-channel buffer status */
#define AMR_IR_MLTFRMI			0x80 /* multiframe or PP */

/* The amd7930 has "indirect registers" which are accessed by writing
 * the register number into the Command Register and then reading or
 * writing values from the Data Register as appropriate. We define the
 * AMR_* macros to be the indirect register numbers and AM_* macros to
 * be bits in whatever register is referred to.
 */

/* Initialization */
#define	AMR_INIT			0x21
#define		AM_INIT_ACTIVE			0x01
#define		AM_INIT_DATAONLY		0x02
#define		AM_INIT_POWERDOWN		0x03
#define		AM_INIT_DISABLE_INTS		0x04
#define AMR_INIT2			0x20
#define		AM_INIT2_ENABLE_POWERDOWN	0x20
#define		AM_INIT2_ENABLE_MULTIFRAME	0x10

/* Line Interface Unit */
#define	AMR_LIU_LSR			0xA1
#define		AM_LIU_LSR_STATE		0x07
#define		AM_LIU_LSR_F3			0x08
#define		AM_LIU_LSR_F7			0x10
#define		AM_LIU_LSR_F8			0x20
#define		AM_LIU_LSR_HSW			0x40
#define		AM_LIU_LSR_HSW_CHG		0x80
#define	AMR_LIU_LPR			0xA2
#define	AMR_LIU_LMR1			0xA3
#define		AM_LIU_LMR1_B1_ENABL		0x01
#define		AM_LIU_LMR1_B2_ENABL		0x02
#define		AM_LIU_LMR1_F_DISABL		0x04
#define		AM_LIU_LMR1_FA_DISABL		0x08
#define		AM_LIU_LMR1_REQ_ACTIV		0x10
#define		AM_LIU_LMR1_F8_F3		0x20
#define		AM_LIU_LMR1_LIU_ENABL		0x40
#define	AMR_LIU_LMR2			0xA4
#define		AM_LIU_LMR2_DECHO		0x01
#define		AM_LIU_LMR2_DLOOP		0x02
#define		AM_LIU_LMR2_DBACKOFF		0x04
#define		AM_LIU_LMR2_EN_F3_INT		0x08
#define		AM_LIU_LMR2_EN_F8_INT		0x10
#define		AM_LIU_LMR2_EN_HSW_INT		0x20
#define		AM_LIU_LMR2_EN_F7_INT		0x40
#define	AMR_LIU_2_4			0xA5
#define	AMR_LIU_MF			0xA6
#define	AMR_LIU_MFSB			0xA7
#define	AMR_LIU_MFQB			0xA8

/* Multiplexor */
#define	AMR_MUX_MCR1			0x41
#define	AMR_MUX_MCR2			0x42
#define	AMR_MUX_MCR3			0x43
#define		AM_MUX_CHANNEL_B1		0x01
#define		AM_MUX_CHANNEL_B2		0x02
#define		AM_MUX_CHANNEL_Ba		0x03
#define		AM_MUX_CHANNEL_Bb		0x04
#define		AM_MUX_CHANNEL_Bc		0x05
#define		AM_MUX_CHANNEL_Bd		0x06
#define		AM_MUX_CHANNEL_Be		0x07
#define		AM_MUX_CHANNEL_Bf		0x08
#define	AMR_MUX_MCR4			0x44
#define		AM_MUX_MCR4_ENABLE_INTS		0x08
#define		AM_MUX_MCR4_REVERSE_Bb		0x10
#define		AM_MUX_MCR4_REVERSE_Bc		0x20
#define	AMR_MUX_1_4			0x45

/* Main Audio Processor */
#define	AMR_MAP_X			0x61
#define	AMR_MAP_R			0x62
#define	AMR_MAP_GX			0x63
#define	AMR_MAP_GR			0x64
#define	AMR_MAP_GER			0x65
#define	AMR_MAP_STGR			0x66
#define	AMR_MAP_FTGR_1_2		0x67
#define	AMR_MAP_ATGR_1_2		0x68
#define	AMR_MAP_MMR1			0x69
#define		AM_MAP_MMR1_ALAW		0x01
#define		AM_MAP_MMR1_GX			0x02
#define		AM_MAP_MMR1_GR			0x04
#define		AM_MAP_MMR1_GER			0x08
#define		AM_MAP_MMR1_X			0x10
#define		AM_MAP_MMR1_R			0x20
#define		AM_MAP_MMR1_STG			0x40
#define		AM_MAP_MMR1_LOOPBACK		0x80
#define	AMR_MAP_MMR2			0x6A
#define		AM_MAP_MMR2_AINB		0x01
#define		AM_MAP_MMR2_LS			0x02
#define		AM_MAP_MMR2_ENABLE_DTMF		0x04
#define		AM_MAP_MMR2_ENABLE_TONEGEN	0x08
#define		AM_MAP_MMR2_ENABLE_TONERING	0x10
#define		AM_MAP_MMR2_DISABLE_HIGHPASS	0x20
#define		AM_MAP_MMR2_DISABLE_AUTOZERO	0x40
#define	AMR_MAP_1_10			0x6B
#define	AMR_MAP_MMR3			0x6C
#define	AMR_MAP_STRA			0x6D
#define	AMR_MAP_STRF			0x6E
#define	AMR_MAP_PEAKX			0x70
#define	AMR_MAP_PEAKR			0x71
#define	AMR_MAP_15_16			0x72

/* Data Link Controller */
#define	AMR_DLC_FRAR_1_2_3		0x81
#define	AMR_DLC_SRAR_1_2_3		0x82
#define	AMR_DLC_TAR			0x83
#define	AMR_DLC_DRLR			0x84
#define	AMR_DLC_DTCR			0x85
#define	AMR_DLC_DMR1			0x86
#define		AMR_DLC_DMR1_DTTHRSH_INT	0x01
#define		AMR_DLC_DMR1_DRTHRSH_INT	0x02
#define		AMR_DLC_DMR1_TAR_ENABL		0x04
#define		AMR_DLC_DMR1_EORP_INT		0x08
#define		AMR_DLC_DMR1_EN_ADDR1		0x10
#define		AMR_DLC_DMR1_EN_ADDR2		0x20
#define		AMR_DLC_DMR1_EN_ADDR3		0x40
#define		AMR_DLC_DMR1_EN_ADDR4		0x80
#define		AMR_DLC_DMR1_EN_ADDRS		0xf0
#define	AMR_DLC_DMR2			0x87
#define		AMR_DLC_DMR2_RABRT_INT		0x01
#define		AMR_DLC_DMR2_RESID_INT		0x02
#define		AMR_DLC_DMR2_COLL_INT		0x04
#define		AMR_DLC_DMR2_FCS_INT		0x08
#define		AMR_DLC_DMR2_OVFL_INT		0x10
#define		AMR_DLC_DMR2_UNFL_INT		0x20
#define		AMR_DLC_DMR2_OVRN_INT		0x40
#define		AMR_DLC_DMR2_UNRN_INT		0x80
#define	AMR_DLC_1_7			0x88
#define	AMR_DLC_DRCR			0x89
#define	AMR_DLC_RNGR1			0x8A
#define	AMR_DLC_RNGR2			0x8B
#define	AMR_DLC_FRAR4			0x8C
#define	AMR_DLC_SRAR4			0x8D
#define	AMR_DLC_DMR3			0x8E
#define		AMR_DLC_DMR3_VA_INT		0x01
#define		AMR_DLC_DMR3_EOTP_INT		0x02
#define		AMR_DLC_DMR3_LBRP_INT		0x04
#define		AMR_DLC_DMR3_RBA_INT		0x08
#define		AMR_DLC_DMR3_LBT_INT		0x10
#define		AMR_DLC_DMR3_TBE_INT		0x20
#define		AMR_DLC_DMR3_RPLOST_INT		0x40
#define		AMR_DLC_DMR3_KEEP_FCS		0x80
#define	AMR_DLC_DMR4			0x8F
#define		AMR_DLC_DMR4_RCV_1		0x00
#define		AMR_DLC_DMR4_RCV_2		0x01
#define		AMR_DLC_DMR4_RCV_4		0x02
#define		AMR_DLC_DMR4_RCV_8		0x03
#define		AMR_DLC_DMR4_RCV_16		0x01
#define		AMR_DLC_DMR4_RCV_24		0x02
#define		AMR_DLC_DMR4_RCV_30		0x03
#define		AMR_DLC_DMR4_XMT_1		0x00
#define		AMR_DLC_DMR4_XMT_2		0x04
#define		AMR_DLC_DMR4_XMT_4		0x08
#define		AMR_DLC_DMR4_XMT_8		0x0c
#define		AMR_DLC_DMR4_XMT_10		0x08
#define		AMR_DLC_DMR4_XMT_14		0x0c
#define		AMR_DLC_DMR4_IDLE_MARK		0x00
#define		AMR_DLC_DMR4_IDLE_FLAG		0x10
#define		AMR_DLC_DMR4_ADDR_BOTH		0x00
#define		AMR_DLC_DMR4_ADDR_1ST		0x20
#define		AMR_DLC_DMR4_ADDR_2ND		0xa0
#define		AMR_DLC_DMR4_CR_ENABLE		0x40
#define	AMR_DLC_12_15			0x90
#define	AMR_DLC_ASR			0x91
#define	AMR_DLC_EFCR			0x92
#define		AMR_DLC_EFCR_EXTEND_FIFO	0x01
#define		AMR_DLC_EFCR_SEC_PKT_INT	0x02

#define AMR_DSR1_VADDR			0x01
#define AMR_DSR1_EORP			0x02
#define AMR_DSR1_PKT_IP			0x04
#define AMR_DSR1_DECHO_ON		0x08
#define AMR_DSR1_DLOOP_ON		0x10
#define AMR_DSR1_DBACK_OFF		0x20
#define AMR_DSR1_EOTP			0x40
#define AMR_DSR1_CXMT_ABRT		0x80

#define AMR_DSR2_LBRP			0x01
#define AMR_DSR2_RBA			0x02
#define AMR_DSR2_RPLOST			0x04
#define AMR_DSR2_LAST_BYTE		0x08
#define AMR_DSR2_TBE			0x10
#define AMR_DSR2_MARK_IDLE		0x20
#define AMR_DSR2_FLAG_IDLE		0x40
#define AMR_DSR2_SECOND_PKT		0x80

#define AMR_DER_RABRT			0x01
#define AMR_DER_RFRAME			0x02
#define AMR_DER_COLLISION		0x04
#define AMR_DER_FCS			0x08
#define AMR_DER_OVFL			0x10
#define AMR_DER_UNFL			0x20
#define AMR_DER_OVRN			0x40
#define AMR_DER_UNRN			0x80

/* Peripheral Port */
#define	AMR_PP_PPCR1			0xC0
#define	AMR_PP_PPSR			0xC1
#define	AMR_PP_PPIER			0xC2
#define	AMR_PP_MTDR			0xC3
#define	AMR_PP_MRDR			0xC3
#define	AMR_PP_CITDR0			0xC4
#define	AMR_PP_CIRDR0			0xC4
#define	AMR_PP_CITDR1			0xC5
#define	AMR_PP_CIRDR1			0xC5
#define	AMR_PP_PPCR2			0xC8
#define	AMR_PP_PPCR3			0xC9

struct snd_amd7930 {
	spinlock_t		lock;
	void __iomem		*regs;
	u32			flags;
#define AMD7930_FLAG_PLAYBACK	0x00000001
#define AMD7930_FLAG_CAPTURE	0x00000002

	struct amd7930_map	map;

	struct snd_card		*card;
	struct snd_pcm		*pcm;
	struct snd_pcm_substream	*playback_substream;
	struct snd_pcm_substream	*capture_substream;

	/* Playback/Capture buffer state. */
	unsigned char		*p_orig, *p_cur;
	int			p_left;
	unsigned char		*c_orig, *c_cur;
	int			c_left;

	int			rgain;
	int			pgain;
	int			mgain;

	struct platform_device	*op;
	unsigned int		irq;
	struct snd_amd7930	*next;
};

static struct snd_amd7930 *amd7930_list;

/* Idle the AMD7930 chip.  The amd->lock is not held.  */
static __inline__ void amd7930_idle(struct snd_amd7930 *amd)
{
	unsigned long flags;

	spin_lock_irqsave(&amd->lock, flags);
	sbus_writeb(AMR_INIT, amd->regs + AMD7930_CR);
	sbus_writeb(0, amd->regs + AMD7930_DR);
	spin_unlock_irqrestore(&amd->lock, flags);
}

/* Enable chip interrupts.  The amd->lock is not held.  */
static __inline__ void amd7930_enable_ints(struct snd_amd7930 *amd)
{
	unsigned long flags;

	spin_lock_irqsave(&amd->lock, flags);
	sbus_writeb(AMR_INIT, amd->regs + AMD7930_CR);
	sbus_writeb(AM_INIT_ACTIVE, amd->regs + AMD7930_DR);
	spin_unlock_irqrestore(&amd->lock, flags);
}

/* Disable chip interrupts.  The amd->lock is not held.  */
static __inline__ void amd7930_disable_ints(struct snd_amd7930 *amd)
{
	unsigned long flags;

	spin_lock_irqsave(&amd->lock, flags);
	sbus_writeb(AMR_INIT, amd->regs + AMD7930_CR);
	sbus_writeb(AM_INIT_ACTIVE | AM_INIT_DISABLE_INTS, amd->regs + AMD7930_DR);
	spin_unlock_irqrestore(&amd->lock, flags);
}

/* Commit amd7930_map settings to the hardware.
 * The amd->lock is held and local interrupts are disabled.
 */
static void __amd7930_write_map(struct snd_amd7930 *amd)
{
	struct amd7930_map *map = &amd->map;

	sbus_writeb(AMR_MAP_GX, amd->regs + AMD7930_CR);
	sbus_writeb(((map->gx >> 0) & 0xff), amd->regs + AMD7930_DR);
	sbus_writeb(((map->gx >> 8) & 0xff), amd->regs + AMD7930_DR);

	sbus_writeb(AMR_MAP_GR, amd->regs + AMD7930_CR);
	sbus_writeb(((map->gr >> 0) & 0xff), amd->regs + AMD7930_DR);
	sbus_writeb(((map->gr >> 8) & 0xff), amd->regs + AMD7930_DR);

	sbus_writeb(AMR_MAP_STGR, amd->regs + AMD7930_CR);
	sbus_writeb(((map->stgr >> 0) & 0xff), amd->regs + AMD7930_DR);
	sbus_writeb(((map->stgr >> 8) & 0xff), amd->regs + AMD7930_DR);

	sbus_writeb(AMR_MAP_GER, amd->regs + AMD7930_CR);
	sbus_writeb(((map->ger >> 0) & 0xff), amd->regs + AMD7930_DR);
	sbus_writeb(((map->ger >> 8) & 0xff), amd->regs + AMD7930_DR);

	sbus_writeb(AMR_MAP_MMR1, amd->regs + AMD7930_CR);
	sbus_writeb(map->mmr1, amd->regs + AMD7930_DR);

	sbus_writeb(AMR_MAP_MMR2, amd->regs + AMD7930_CR);
	sbus_writeb(map->mmr2, amd->regs + AMD7930_DR);
}

/* gx, gr & stg gains.  this table must contain 256 elements with
 * the 0th being "infinity" (the magic value 9008).  The remaining
 * elements match sun's gain curve (but with higher resolution):
 * -18 to 0dB in .16dB steps then 0 to 12dB in .08dB steps.
 */
static __const__ __u16 gx_coeff[256] = {
	0x9008, 0x8b7c, 0x8b51, 0x8b45, 0x8b42, 0x8b3b, 0x8b36, 0x8b33,
	0x8b32, 0x8b2a, 0x8b2b, 0x8b2c, 0x8b25, 0x8b23, 0x8b22, 0x8b22,
	0x9122, 0x8b1a, 0x8aa3, 0x8aa3, 0x8b1c, 0x8aa6, 0x912d, 0x912b,
	0x8aab, 0x8b12, 0x8aaa, 0x8ab2, 0x9132, 0x8ab4, 0x913c, 0x8abb,
	0x9142, 0x9144, 0x9151, 0x8ad5, 0x8aeb, 0x8a79, 0x8a5a, 0x8a4a,
	0x8b03, 0x91c2, 0x91bb, 0x8a3f, 0x8a33, 0x91b2, 0x9212, 0x9213,
	0x8a2c, 0x921d, 0x8a23, 0x921a, 0x9222, 0x9223, 0x922d, 0x9231,
	0x9234, 0x9242, 0x925b, 0x92dd, 0x92c1, 0x92b3, 0x92ab, 0x92a4,
	0x92a2, 0x932b, 0x9341, 0x93d3, 0x93b2, 0x93a2, 0x943c, 0x94b2,
	0x953a, 0x9653, 0x9782, 0x9e21, 0x9d23, 0x9cd2, 0x9c23, 0x9baa,
	0x9bde, 0x9b33, 0x9b22, 0x9b1d, 0x9ab2, 0xa142, 0xa1e5, 0x9a3b,
	0xa213, 0xa1a2, 0xa231, 0xa2eb, 0xa313, 0xa334, 0xa421, 0xa54b,
	0xada4, 0xac23, 0xab3b, 0xaaab, 0xaa5c, 0xb1a3, 0xb2ca, 0xb3bd,
	0xbe24, 0xbb2b, 0xba33, 0xc32b, 0xcb5a, 0xd2a2, 0xe31d, 0x0808,
	0x72ba, 0x62c2, 0x5c32, 0x52db, 0x513e, 0x4cce, 0x43b2, 0x4243,
	0x41b4, 0x3b12, 0x3bc3, 0x3df2, 0x34bd, 0x3334, 0x32c2, 0x3224,
	0x31aa, 0x2a7b, 0x2aaa, 0x2b23, 0x2bba, 0x2c42, 0x2e23, 0x25bb,
	0x242b, 0x240f, 0x231a, 0x22bb, 0x2241, 0x2223, 0x221f, 0x1a33,
	0x1a4a, 0x1acd, 0x2132, 0x1b1b, 0x1b2c, 0x1b62, 0x1c12, 0x1c32,
	0x1d1b, 0x1e71, 0x16b1, 0x1522, 0x1434, 0x1412, 0x1352, 0x1323,
	0x1315, 0x12bc, 0x127a, 0x1235, 0x1226, 0x11a2, 0x1216, 0x0a2a,
	0x11bc, 0x11d1, 0x1163, 0x0ac2, 0x0ab2, 0x0aab, 0x0b1b, 0x0b23,
	0x0b33, 0x0c0f, 0x0bb3, 0x0c1b, 0x0c3e, 0x0cb1, 0x0d4c, 0x0ec1,
	0x079a, 0x0614, 0x0521, 0x047c, 0x0422, 0x03b1, 0x03e3, 0x0333,
	0x0322, 0x031c, 0x02aa, 0x02ba, 0x02f2, 0x0242, 0x0232, 0x0227,
	0x0222, 0x021b, 0x01ad, 0x0212, 0x01b2, 0x01bb, 0x01cb, 0x01f6,
	0x0152, 0x013a, 0x0133, 0x0131, 0x012c, 0x0123, 0x0122, 0x00a2,
	0x011b, 0x011e, 0x0114, 0x00b1, 0x00aa, 0x00b3, 0x00bd, 0x00ba,
	0x00c5, 0x00d3, 0x00f3, 0x0062, 0x0051, 0x0042, 0x003b, 0x0033,
	0x0032, 0x002a, 0x002c, 0x0025, 0x0023, 0x0022, 0x001a, 0x0021,
	0x001b, 0x001b, 0x001d, 0x0015, 0x0013, 0x0013, 0x0012, 0x0012,
	0x000a, 0x000a, 0x0011, 0x0011, 0x000b, 0x000b, 0x000c, 0x000e,
};

static __const__ __u16 ger_coeff[] = {
	0x431f, /* 5. dB */
	0x331f, /* 5.5 dB */
	0x40dd, /* 6. dB */
	0x11dd, /* 6.5 dB */
	0x440f, /* 7. dB */
	0x411f, /* 7.5 dB */
	0x311f, /* 8. dB */
	0x5520, /* 8.5 dB */
	0x10dd, /* 9. dB */
	0x4211, /* 9.5 dB */
	0x410f, /* 10. dB */
	0x111f, /* 10.5 dB */
	0x600b, /* 11. dB */
	0x00dd, /* 11.5 dB */
	0x4210, /* 12. dB */
	0x110f, /* 13. dB */
	0x7200, /* 14. dB */
	0x2110, /* 15. dB */
	0x2200, /* 15.9 dB */
	0x000b, /* 16.9 dB */
	0x000f  /* 18. dB */
};

/* Update amd7930_map settings and program them into the hardware.
 * The amd->lock is held and local interrupts are disabled.
 */
static void __amd7930_update_map(struct snd_amd7930 *amd)
{
	struct amd7930_map *map = &amd->map;
	int level;

	map->gx = gx_coeff[amd->rgain];
	map->stgr = gx_coeff[amd->mgain];
	level = (amd->pgain * (256 + ARRAY_SIZE(ger_coeff))) >> 8;
	if (level >= 256) {
		map->ger = ger_coeff[level - 256];
		map->gr = gx_coeff[255];
	} else {
		map->ger = ger_coeff[0];
		map->gr = gx_coeff[level];
	}
	__amd7930_write_map(amd);
}

static irqreturn_t snd_amd7930_interrupt(int irq, void *dev_id)
{
	struct snd_amd7930 *amd = dev_id;
	unsigned int elapsed;
	u8 ir;

	spin_lock(&amd->lock);

	elapsed = 0;

	ir = sbus_readb(amd->regs + AMD7930_IR);
	if (ir & AMR_IR_BBUF) {
		u8 byte;

		if (amd->flags & AMD7930_FLAG_PLAYBACK) {
			if (amd->p_left > 0) {
				byte = *(amd->p_cur++);
				amd->p_left--;
				sbus_writeb(byte, amd->regs + AMD7930_BBTB);
				if (amd->p_left == 0)
					elapsed |= AMD7930_FLAG_PLAYBACK;
			} else
				sbus_writeb(0, amd->regs + AMD7930_BBTB);
		} else if (amd->flags & AMD7930_FLAG_CAPTURE) {
			byte = sbus_readb(amd->regs + AMD7930_BBRB);
			if (amd->c_left > 0) {
				*(amd->c_cur++) = byte;
				amd->c_left--;
				if (amd->c_left == 0)
					elapsed |= AMD7930_FLAG_CAPTURE;
			}
		}
	}
	spin_unlock(&amd->lock);

	if (elapsed & AMD7930_FLAG_PLAYBACK)
		snd_pcm_period_elapsed(amd->playback_substream);
	else
		snd_pcm_period_elapsed(amd->capture_substream);

	return IRQ_HANDLED;
}

static int snd_amd7930_trigger(struct snd_amd7930 *amd, unsigned int flag, int cmd)
{
	unsigned long flags;
	int result = 0;

	spin_lock_irqsave(&amd->lock, flags);
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		if (!(amd->flags & flag)) {
			amd->flags |= flag;

			/* Enable B channel interrupts.  */
			sbus_writeb(AMR_MUX_MCR4, amd->regs + AMD7930_CR);
			sbus_writeb(AM_MUX_MCR4_ENABLE_INTS, amd->regs + AMD7930_DR);
		}
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		if (amd->flags & flag) {
			amd->flags &= ~flag;

			/* Disable B channel interrupts.  */
			sbus_writeb(AMR_MUX_MCR4, amd->regs + AMD7930_CR);
			sbus_writeb(0, amd->regs + AMD7930_DR);
		}
	} else {
		result = -EINVAL;
	}
	spin_unlock_irqrestore(&amd->lock, flags);

	return result;
}

static int snd_amd7930_playback_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	struct snd_amd7930 *amd = snd_pcm_substream_chip(substream);
	return snd_amd7930_trigger(amd, AMD7930_FLAG_PLAYBACK, cmd);
}

static int snd_amd7930_capture_trigger(struct snd_pcm_substream *substream,
				       int cmd)
{
	struct snd_amd7930 *amd = snd_pcm_substream_chip(substream);
	return snd_amd7930_trigger(amd, AMD7930_FLAG_CAPTURE, cmd);
}

static int snd_amd7930_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_amd7930 *amd = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned long flags;
	u8 new_mmr1;

	spin_lock_irqsave(&amd->lock, flags);

	amd->flags |= AMD7930_FLAG_PLAYBACK;

	/* Setup the pseudo-dma transfer pointers.  */
	amd->p_orig = amd->p_cur = runtime->dma_area;
	amd->p_left = size;

	/* Put the chip into the correct encoding format.  */
	new_mmr1 = amd->map.mmr1;
	if (runtime->format == SNDRV_PCM_FORMAT_A_LAW)
		new_mmr1 |= AM_MAP_MMR1_ALAW;
	else
		new_mmr1 &= ~AM_MAP_MMR1_ALAW;
	if (new_mmr1 != amd->map.mmr1) {
		amd->map.mmr1 = new_mmr1;
		__amd7930_update_map(amd);
	}

	spin_unlock_irqrestore(&amd->lock, flags);

	return 0;
}

static int snd_amd7930_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_amd7930 *amd = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int size = snd_pcm_lib_buffer_bytes(substream);
	unsigned long flags;
	u8 new_mmr1;

	spin_lock_irqsave(&amd->lock, flags);

	amd->flags |= AMD7930_FLAG_CAPTURE;

	/* Setup the pseudo-dma transfer pointers.  */
	amd->c_orig = amd->c_cur = runtime->dma_area;
	amd->c_left = size;

	/* Put the chip into the correct encoding format.  */
	new_mmr1 = amd->map.mmr1;
	if (runtime->format == SNDRV_PCM_FORMAT_A_LAW)
		new_mmr1 |= AM_MAP_MMR1_ALAW;
	else
		new_mmr1 &= ~AM_MAP_MMR1_ALAW;
	if (new_mmr1 != amd->map.mmr1) {
		amd->map.mmr1 = new_mmr1;
		__amd7930_update_map(amd);
	}

	spin_unlock_irqrestore(&amd->lock, flags);

	return 0;
}

static snd_pcm_uframes_t snd_amd7930_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_amd7930 *amd = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(amd->flags & AMD7930_FLAG_PLAYBACK))
		return 0;
	ptr = amd->p_cur - amd->p_orig;
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_amd7930_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_amd7930 *amd = snd_pcm_substream_chip(substream);
	size_t ptr;

	if (!(amd->flags & AMD7930_FLAG_CAPTURE))
		return 0;

	ptr = amd->c_cur - amd->c_orig;
	return bytes_to_frames(substream->runtime, ptr);
}

/* Playback and capture have identical properties.  */
static struct snd_pcm_hardware snd_amd7930_pcm_hw =
{
	.info			= (SNDRV_PCM_INFO_MMAP |
				   SNDRV_PCM_INFO_MMAP_VALID |
				   SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_HALF_DUPLEX),
	.formats		= SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW,
	.rates			= SNDRV_PCM_RATE_8000,
	.rate_min		= 8000,
	.rate_max		= 8000,
	.channels_min		= 1,
	.channels_max		= 1,
	.buffer_bytes_max	= (64*1024),
	.period_bytes_min	= 1,
	.period_bytes_max	= (64*1024),
	.periods_min		= 1,
	.periods_max		= 1024,
};

static int snd_amd7930_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_amd7930 *amd = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	amd->playback_substream = substream;
	runtime->hw = snd_amd7930_pcm_hw;
	return 0;
}

static int snd_amd7930_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_amd7930 *amd = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	amd->capture_substream = substream;
	runtime->hw = snd_amd7930_pcm_hw;
	return 0;
}

static int snd_amd7930_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_amd7930 *amd = snd_pcm_substream_chip(substream);

	amd->playback_substream = NULL;
	return 0;
}

static int snd_amd7930_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_amd7930 *amd = snd_pcm_substream_chip(substream);

	amd->capture_substream = NULL;
	return 0;
}

static int snd_amd7930_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *hw_params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_amd7930_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static struct snd_pcm_ops snd_amd7930_playback_ops = {
	.open		=	snd_amd7930_playback_open,
	.close		=	snd_amd7930_playback_close,
	.ioctl		=	snd_pcm_lib_ioctl,
	.hw_params	=	snd_amd7930_hw_params,
	.hw_free	=	snd_amd7930_hw_free,
	.prepare	=	snd_amd7930_playback_prepare,
	.trigger	=	snd_amd7930_playback_trigger,
	.pointer	=	snd_amd7930_playback_pointer,
};

static struct snd_pcm_ops snd_amd7930_capture_ops = {
	.open		=	snd_amd7930_capture_open,
	.close		=	snd_amd7930_capture_close,
	.ioctl		=	snd_pcm_lib_ioctl,
	.hw_params	=	snd_amd7930_hw_params,
	.hw_free	=	snd_amd7930_hw_free,
	.prepare	=	snd_amd7930_capture_prepare,
	.trigger	=	snd_amd7930_capture_trigger,
	.pointer	=	snd_amd7930_capture_pointer,
};

static int snd_amd7930_pcm(struct snd_amd7930 *amd)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(amd->card,
			       /* ID */             "sun_amd7930",
			       /* device */         0,
			       /* playback count */ 1,
			       /* capture count */  1, &pcm)) < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_amd7930_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_amd7930_capture_ops);

	pcm->private_data = amd;
	pcm->info_flags = 0;
	strcpy(pcm->name, amd->card->shortname);
	amd->pcm = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
					      snd_dma_continuous_data(GFP_KERNEL),
					      64*1024, 64*1024);

	return 0;
}

#define VOLUME_MONITOR	0
#define VOLUME_CAPTURE	1
#define VOLUME_PLAYBACK	2

static int snd_amd7930_info_volume(struct snd_kcontrol *kctl, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 255;

	return 0;
}

static int snd_amd7930_get_volume(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_amd7930 *amd = snd_kcontrol_chip(kctl);
	int type = kctl->private_value;
	int *swval;

	switch (type) {
	case VOLUME_MONITOR:
		swval = &amd->mgain;
		break;
	case VOLUME_CAPTURE:
		swval = &amd->rgain;
		break;
	case VOLUME_PLAYBACK:
	default:
		swval = &amd->pgain;
		break;
	}

	ucontrol->value.integer.value[0] = *swval;

	return 0;
}

static int snd_amd7930_put_volume(struct snd_kcontrol *kctl, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_amd7930 *amd = snd_kcontrol_chip(kctl);
	unsigned long flags;
	int type = kctl->private_value;
	int *swval, change;

	switch (type) {
	case VOLUME_MONITOR:
		swval = &amd->mgain;
		break;
	case VOLUME_CAPTURE:
		swval = &amd->rgain;
		break;
	case VOLUME_PLAYBACK:
	default:
		swval = &amd->pgain;
		break;
	}

	spin_lock_irqsave(&amd->lock, flags);

	if (*swval != ucontrol->value.integer.value[0]) {
		*swval = ucontrol->value.integer.value[0] & 0xff;
		__amd7930_update_map(amd);
		change = 1;
	} else
		change = 0;

	spin_unlock_irqrestore(&amd->lock, flags);

	return change;
}

static struct snd_kcontrol_new amd7930_controls[] = {
	{
		.iface		=	SNDRV_CTL_ELEM_IFACE_MIXER,
		.name		=	"Monitor Volume",
		.index		=	0,
		.info		=	snd_amd7930_info_volume,
		.get		=	snd_amd7930_get_volume,
		.put		=	snd_amd7930_put_volume,
		.private_value	=	VOLUME_MONITOR,
	},
	{
		.iface		=	SNDRV_CTL_ELEM_IFACE_MIXER,
		.name		=	"Capture Volume",
		.index		=	0,
		.info		=	snd_amd7930_info_volume,
		.get		=	snd_amd7930_get_volume,
		.put		=	snd_amd7930_put_volume,
		.private_value	=	VOLUME_CAPTURE,
	},
	{
		.iface		=	SNDRV_CTL_ELEM_IFACE_MIXER,
		.name		=	"Playback Volume",
		.index		=	0,
		.info		=	snd_amd7930_info_volume,
		.get		=	snd_amd7930_get_volume,
		.put		=	snd_amd7930_put_volume,
		.private_value	=	VOLUME_PLAYBACK,
	},
};

static int snd_amd7930_mixer(struct snd_amd7930 *amd)
{
	struct snd_card *card;
	int idx, err;

	if (snd_BUG_ON(!amd || !amd->card))
		return -EINVAL;

	card = amd->card;
	strcpy(card->mixername, card->shortname);

	for (idx = 0; idx < ARRAY_SIZE(amd7930_controls); idx++) {
		if ((err = snd_ctl_add(card,
				       snd_ctl_new1(&amd7930_controls[idx], amd))) < 0)
			return err;
	}

	return 0;
}

static int snd_amd7930_free(struct snd_amd7930 *amd)
{
	struct platform_device *op = amd->op;

	amd7930_idle(amd);

	if (amd->irq)
		free_irq(amd->irq, amd);

	if (amd->regs)
		of_iounmap(&op->resource[0], amd->regs,
			   resource_size(&op->resource[0]));

	kfree(amd);

	return 0;
}

static int snd_amd7930_dev_free(struct snd_device *device)
{
	struct snd_amd7930 *amd = device->device_data;

	return snd_amd7930_free(amd);
}

static struct snd_device_ops snd_amd7930_dev_ops = {
	.dev_free	=	snd_amd7930_dev_free,
};

static int snd_amd7930_create(struct snd_card *card,
			      struct platform_device *op,
			      int irq, int dev,
			      struct snd_amd7930 **ramd)
{
	struct snd_amd7930 *amd;
	unsigned long flags;
	int err;

	*ramd = NULL;
	amd = kzalloc(sizeof(*amd), GFP_KERNEL);
	if (amd == NULL)
		return -ENOMEM;

	spin_lock_init(&amd->lock);
	amd->card = card;
	amd->op = op;

	amd->regs = of_ioremap(&op->resource[0], 0,
			       resource_size(&op->resource[0]), "amd7930");
	if (!amd->regs) {
		snd_printk(KERN_ERR
			   "amd7930-%d: Unable to map chip registers.\n", dev);
		return -EIO;
	}

	amd7930_idle(amd);

	if (request_irq(irq, snd_amd7930_interrupt,
			IRQF_SHARED, "amd7930", amd)) {
		snd_printk(KERN_ERR "amd7930-%d: Unable to grab IRQ %d\n",
			   dev, irq);
		snd_amd7930_free(amd);
		return -EBUSY;
	}
	amd->irq = irq;

	amd7930_enable_ints(amd);

	spin_lock_irqsave(&amd->lock, flags);

	amd->rgain = 128;
	amd->pgain = 200;
	amd->mgain = 0;

	memset(&amd->map, 0, sizeof(amd->map));
	amd->map.mmr1 = (AM_MAP_MMR1_GX | AM_MAP_MMR1_GER |
			 AM_MAP_MMR1_GR | AM_MAP_MMR1_STG);
	amd->map.mmr2 = (AM_MAP_MMR2_LS | AM_MAP_MMR2_AINB);

	__amd7930_update_map(amd);

	/* Always MUX audio (Ba) to channel Bb. */
	sbus_writeb(AMR_MUX_MCR1, amd->regs + AMD7930_CR);
	sbus_writeb(AM_MUX_CHANNEL_Ba | (AM_MUX_CHANNEL_Bb << 4),
		    amd->regs + AMD7930_DR);

	spin_unlock_irqrestore(&amd->lock, flags);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL,
				  amd, &snd_amd7930_dev_ops)) < 0) {
		snd_amd7930_free(amd);
		return err;
	}

	*ramd = amd;
	return 0;
}

static int amd7930_sbus_probe(struct platform_device *op)
{
	struct resource *rp = &op->resource[0];
	static int dev_num;
	struct snd_card *card;
	struct snd_amd7930 *amd;
	int err, irq;

	irq = op->archdata.irqs[0];

	if (dev_num >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev_num]) {
		dev_num++;
		return -ENOENT;
	}

	err = snd_card_new(&op->dev, index[dev_num], id[dev_num],
			   THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	strcpy(card->driver, "AMD7930");
	strcpy(card->shortname, "Sun AMD7930");
	sprintf(card->longname, "%s at 0x%02lx:0x%08Lx, irq %d",
		card->shortname,
		rp->flags & 0xffL,
		(unsigned long long)rp->start,
		irq);

	if ((err = snd_amd7930_create(card, op,
				      irq, dev_num, &amd)) < 0)
		goto out_err;

	if ((err = snd_amd7930_pcm(amd)) < 0)
		goto out_err;

	if ((err = snd_amd7930_mixer(amd)) < 0)
		goto out_err;

	if ((err = snd_card_register(card)) < 0)
		goto out_err;

	amd->next = amd7930_list;
	amd7930_list = amd;

	dev_num++;

	return 0;

out_err:
	snd_card_free(card);
	return err;
}

static const struct of_device_id amd7930_match[] = {
	{
		.name = "audio",
	},
	{},
};

static struct platform_driver amd7930_sbus_driver = {
	.driver = {
		.name = "audio",
		.of_match_table = amd7930_match,
	},
	.probe		= amd7930_sbus_probe,
};

static int __init amd7930_init(void)
{
	return platform_driver_register(&amd7930_sbus_driver);
}

static void __exit amd7930_exit(void)
{
	struct snd_amd7930 *p = amd7930_list;

	while (p != NULL) {
		struct snd_amd7930 *next = p->next;

		snd_card_free(p->card);

		p = next;
	}

	amd7930_list = NULL;

	platform_driver_unregister(&amd7930_sbus_driver);
}

module_init(amd7930_init);
module_exit(amd7930_exit);
