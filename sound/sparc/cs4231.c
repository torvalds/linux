/*
 * Driver for CS4231 sound chips found on Sparcs.
 * Copyright (C) 2002 David S. Miller <davem@redhat.com>
 *
 * Based entirely upon drivers/sbus/audio/cs4231.c which is:
 * Copyright (C) 1996, 1997, 1998, 1998 Derrick J Brashear (shadow@andrew.cmu.edu)
 * and also sound/isa/cs423x/cs4231_lib.c which is:
 * Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/control.h>
#include <sound/timer.h>
#include <sound/initval.h>
#include <sound/pcm_params.h>

#include <asm/io.h>
#include <asm/irq.h>

#ifdef CONFIG_SBUS
#define SBUS_SUPPORT
#endif

#ifdef SBUS_SUPPORT
#include <asm/sbus.h>
#endif

#if defined(CONFIG_PCI) && defined(CONFIG_SPARC64)
#define EBUS_SUPPORT
#endif

#ifdef EBUS_SUPPORT
#include <linux/pci.h>
#include <asm/ebus.h>
#endif

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for Sun CS4231 soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for Sun CS4231 soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable Sun CS4231 soundcard.");
MODULE_AUTHOR("Jaroslav Kysela, Derrick J. Brashear and David S. Miller");
MODULE_DESCRIPTION("Sun CS4231");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Sun,CS4231}}");

#ifdef SBUS_SUPPORT
struct sbus_dma_info {
       spinlock_t      lock;
       int             dir;
       void __iomem    *regs;
};
#endif

struct snd_cs4231;
struct cs4231_dma_control {
        void		(*prepare)(struct cs4231_dma_control *dma_cont, int dir);
        void		(*enable)(struct cs4231_dma_control *dma_cont, int on);
        int		(*request)(struct cs4231_dma_control *dma_cont, dma_addr_t bus_addr, size_t len);
        unsigned int	(*address)(struct cs4231_dma_control *dma_cont);
        void		(*reset)(struct snd_cs4231 *chip); 
        void		(*preallocate)(struct snd_cs4231 *chip, struct snd_pcm *pcm); 
#ifdef EBUS_SUPPORT
	struct		ebus_dma_info	ebus_info;
#endif
#ifdef SBUS_SUPPORT
	struct		sbus_dma_info	sbus_info;
#endif
};

struct snd_cs4231 {
	spinlock_t		lock;
	void __iomem		*port;

	struct cs4231_dma_control	p_dma;
	struct cs4231_dma_control	c_dma;

	u32			flags;
#define CS4231_FLAG_EBUS	0x00000001
#define CS4231_FLAG_PLAYBACK	0x00000002
#define CS4231_FLAG_CAPTURE	0x00000004

	struct snd_card		*card;
	struct snd_pcm		*pcm;
	struct snd_pcm_substream	*playback_substream;
	unsigned int		p_periods_sent;
	struct snd_pcm_substream	*capture_substream;
	unsigned int		c_periods_sent;
	struct snd_timer	*timer;

	unsigned short mode;
#define CS4231_MODE_NONE	0x0000
#define CS4231_MODE_PLAY	0x0001
#define CS4231_MODE_RECORD	0x0002
#define CS4231_MODE_TIMER	0x0004
#define CS4231_MODE_OPEN	(CS4231_MODE_PLAY|CS4231_MODE_RECORD|CS4231_MODE_TIMER)

	unsigned char		image[32];	/* registers image */
	int			mce_bit;
	int			calibrate_mute;
	struct mutex		mce_mutex;
	struct mutex		open_mutex;

	union {
#ifdef SBUS_SUPPORT
		struct sbus_dev		*sdev;
#endif
#ifdef EBUS_SUPPORT
		struct pci_dev		*pdev;
#endif
	} dev_u;
	unsigned int		irq[2];
	unsigned int		regs_size;
	struct snd_cs4231	*next;
};

static struct snd_cs4231 *cs4231_list;

/* Eventually we can use sound/isa/cs423x/cs4231_lib.c directly, but for
 * now....  -DaveM
 */

/* IO ports */

#define CS4231P(chip, x)	((chip)->port + c_d_c_CS4231##x)

/* XXX offsets are different than PC ISA chips... */
#define c_d_c_CS4231REGSEL	0x0
#define c_d_c_CS4231REG		0x4
#define c_d_c_CS4231STATUS	0x8
#define c_d_c_CS4231PIO		0xc

/* codec registers */

#define CS4231_LEFT_INPUT	0x00	/* left input control */
#define CS4231_RIGHT_INPUT	0x01	/* right input control */
#define CS4231_AUX1_LEFT_INPUT	0x02	/* left AUX1 input control */
#define CS4231_AUX1_RIGHT_INPUT	0x03	/* right AUX1 input control */
#define CS4231_AUX2_LEFT_INPUT	0x04	/* left AUX2 input control */
#define CS4231_AUX2_RIGHT_INPUT	0x05	/* right AUX2 input control */
#define CS4231_LEFT_OUTPUT	0x06	/* left output control register */
#define CS4231_RIGHT_OUTPUT	0x07	/* right output control register */
#define CS4231_PLAYBK_FORMAT	0x08	/* clock and data format - playback - bits 7-0 MCE */
#define CS4231_IFACE_CTRL	0x09	/* interface control - bits 7-2 MCE */
#define CS4231_PIN_CTRL		0x0a	/* pin control */
#define CS4231_TEST_INIT	0x0b	/* test and initialization */
#define CS4231_MISC_INFO	0x0c	/* miscellaneaous information */
#define CS4231_LOOPBACK		0x0d	/* loopback control */
#define CS4231_PLY_UPR_CNT	0x0e	/* playback upper base count */
#define CS4231_PLY_LWR_CNT	0x0f	/* playback lower base count */
#define CS4231_ALT_FEATURE_1	0x10	/* alternate #1 feature enable */
#define CS4231_ALT_FEATURE_2	0x11	/* alternate #2 feature enable */
#define CS4231_LEFT_LINE_IN	0x12	/* left line input control */
#define CS4231_RIGHT_LINE_IN	0x13	/* right line input control */
#define CS4231_TIMER_LOW	0x14	/* timer low byte */
#define CS4231_TIMER_HIGH	0x15	/* timer high byte */
#define CS4231_LEFT_MIC_INPUT	0x16	/* left MIC input control register (InterWave only) */
#define CS4231_RIGHT_MIC_INPUT	0x17	/* right MIC input control register (InterWave only) */
#define CS4236_EXT_REG		0x17	/* extended register access */
#define CS4231_IRQ_STATUS	0x18	/* irq status register */
#define CS4231_LINE_LEFT_OUTPUT	0x19	/* left line output control register (InterWave only) */
#define CS4231_VERSION		0x19	/* CS4231(A) - version values */
#define CS4231_MONO_CTRL	0x1a	/* mono input/output control */
#define CS4231_LINE_RIGHT_OUTPUT 0x1b	/* right line output control register (InterWave only) */
#define CS4235_LEFT_MASTER	0x1b	/* left master output control */
#define CS4231_REC_FORMAT	0x1c	/* clock and data format - record - bits 7-0 MCE */
#define CS4231_PLY_VAR_FREQ	0x1d	/* playback variable frequency */
#define CS4235_RIGHT_MASTER	0x1d	/* right master output control */
#define CS4231_REC_UPR_CNT	0x1e	/* record upper count */
#define CS4231_REC_LWR_CNT	0x1f	/* record lower count */

/* definitions for codec register select port - CODECP( REGSEL ) */

#define CS4231_INIT		0x80	/* CODEC is initializing */
#define CS4231_MCE		0x40	/* mode change enable */
#define CS4231_TRD		0x20	/* transfer request disable */

/* definitions for codec status register - CODECP( STATUS ) */

#define CS4231_GLOBALIRQ	0x01	/* IRQ is active */

/* definitions for codec irq status - CS4231_IRQ_STATUS	*/

#define CS4231_PLAYBACK_IRQ	0x10
#define CS4231_RECORD_IRQ	0x20
#define CS4231_TIMER_IRQ	0x40
#define CS4231_ALL_IRQS		0x70
#define CS4231_REC_UNDERRUN	0x08
#define CS4231_REC_OVERRUN	0x04
#define CS4231_PLY_OVERRUN	0x02
#define CS4231_PLY_UNDERRUN	0x01

/* definitions for CS4231_LEFT_INPUT and CS4231_RIGHT_INPUT registers */

#define CS4231_ENABLE_MIC_GAIN	0x20

#define CS4231_MIXS_LINE	0x00
#define CS4231_MIXS_AUX1	0x40
#define CS4231_MIXS_MIC		0x80
#define CS4231_MIXS_ALL		0xc0

/* definitions for clock and data format register - CS4231_PLAYBK_FORMAT */

#define CS4231_LINEAR_8		0x00	/* 8-bit unsigned data */
#define CS4231_ALAW_8		0x60	/* 8-bit A-law companded */
#define CS4231_ULAW_8		0x20	/* 8-bit U-law companded */
#define CS4231_LINEAR_16	0x40	/* 16-bit twos complement data - little endian */
#define CS4231_LINEAR_16_BIG	0xc0	/* 16-bit twos complement data - big endian */
#define CS4231_ADPCM_16		0xa0	/* 16-bit ADPCM */
#define CS4231_STEREO		0x10	/* stereo mode */
/* bits 3-1 define frequency divisor */
#define CS4231_XTAL1		0x00	/* 24.576 crystal */
#define CS4231_XTAL2		0x01	/* 16.9344 crystal */

/* definitions for interface control register - CS4231_IFACE_CTRL */

#define CS4231_RECORD_PIO	0x80	/* record PIO enable */
#define CS4231_PLAYBACK_PIO	0x40	/* playback PIO enable */
#define CS4231_CALIB_MODE	0x18	/* calibration mode bits */
#define CS4231_AUTOCALIB	0x08	/* auto calibrate */
#define CS4231_SINGLE_DMA	0x04	/* use single DMA channel */
#define CS4231_RECORD_ENABLE	0x02	/* record enable */
#define CS4231_PLAYBACK_ENABLE	0x01	/* playback enable */

/* definitions for pin control register - CS4231_PIN_CTRL */

#define CS4231_IRQ_ENABLE	0x02	/* enable IRQ */
#define CS4231_XCTL1		0x40	/* external control #1 */
#define CS4231_XCTL0		0x80	/* external control #0 */

/* definitions for test and init register - CS4231_TEST_INIT */

#define CS4231_CALIB_IN_PROGRESS 0x20	/* auto calibrate in progress */
#define CS4231_DMA_REQUEST	0x10	/* DMA request in progress */

/* definitions for misc control register - CS4231_MISC_INFO */

#define CS4231_MODE2		0x40	/* MODE 2 */
#define CS4231_IW_MODE3		0x6c	/* MODE 3 - InterWave enhanced mode */
#define CS4231_4236_MODE3	0xe0	/* MODE 3 - CS4236+ enhanced mode */

/* definitions for alternate feature 1 register - CS4231_ALT_FEATURE_1 */

#define	CS4231_DACZ		0x01	/* zero DAC when underrun */
#define CS4231_TIMER_ENABLE	0x40	/* codec timer enable */
#define CS4231_OLB		0x80	/* output level bit */

/* SBUS DMA register defines.  */

#define APCCSR	0x10UL	/* APC DMA CSR */
#define APCCVA	0x20UL	/* APC Capture DMA Address */
#define APCCC	0x24UL	/* APC Capture Count */
#define APCCNVA	0x28UL	/* APC Capture DMA Next Address */
#define APCCNC	0x2cUL	/* APC Capture Next Count */
#define APCPVA	0x30UL	/* APC Play DMA Address */
#define APCPC	0x34UL	/* APC Play Count */
#define APCPNVA	0x38UL	/* APC Play DMA Next Address */
#define APCPNC	0x3cUL	/* APC Play Next Count */

/* Defines for SBUS DMA-routines */

#define APCVA  0x0UL	/* APC DMA Address */
#define APCC   0x4UL	/* APC Count */
#define APCNVA 0x8UL	/* APC DMA Next Address */
#define APCNC  0xcUL	/* APC Next Count */
#define APC_PLAY 0x30UL	/* Play registers start at 0x30 */
#define APC_RECORD 0x20UL /* Record registers start at 0x20 */

/* APCCSR bits */

#define APC_INT_PENDING 0x800000 /* Interrupt Pending */
#define APC_PLAY_INT    0x400000 /* Playback interrupt */
#define APC_CAPT_INT    0x200000 /* Capture interrupt */
#define APC_GENL_INT    0x100000 /* General interrupt */
#define APC_XINT_ENA    0x80000  /* General ext int. enable */
#define APC_XINT_PLAY   0x40000  /* Playback ext intr */
#define APC_XINT_CAPT   0x20000  /* Capture ext intr */
#define APC_XINT_GENL   0x10000  /* Error ext intr */
#define APC_XINT_EMPT   0x8000   /* Pipe empty interrupt (0 write to pva) */
#define APC_XINT_PEMP   0x4000   /* Play pipe empty (pva and pnva not set) */
#define APC_XINT_PNVA   0x2000   /* Playback NVA dirty */
#define APC_XINT_PENA   0x1000   /* play pipe empty Int enable */
#define APC_XINT_COVF   0x800    /* Cap data dropped on floor */
#define APC_XINT_CNVA   0x400    /* Capture NVA dirty */
#define APC_XINT_CEMP   0x200    /* Capture pipe empty (cva and cnva not set) */
#define APC_XINT_CENA   0x100    /* Cap. pipe empty int enable */
#define APC_PPAUSE      0x80     /* Pause the play DMA */
#define APC_CPAUSE      0x40     /* Pause the capture DMA */
#define APC_CDC_RESET   0x20     /* CODEC RESET */
#define APC_PDMA_READY  0x08     /* Play DMA Go */
#define APC_CDMA_READY  0x04     /* Capture DMA Go */
#define APC_CHIP_RESET  0x01     /* Reset the chip */

/* EBUS DMA register offsets  */

#define EBDMA_CSR	0x00UL	/* Control/Status */
#define EBDMA_ADDR	0x04UL	/* DMA Address */
#define EBDMA_COUNT	0x08UL	/* DMA Count */

/*
 *  Some variables
 */

static unsigned char freq_bits[14] = {
	/* 5510 */	0x00 | CS4231_XTAL2,
	/* 6620 */	0x0E | CS4231_XTAL2,
	/* 8000 */	0x00 | CS4231_XTAL1,
	/* 9600 */	0x0E | CS4231_XTAL1,
	/* 11025 */	0x02 | CS4231_XTAL2,
	/* 16000 */	0x02 | CS4231_XTAL1,
	/* 18900 */	0x04 | CS4231_XTAL2,
	/* 22050 */	0x06 | CS4231_XTAL2,
	/* 27042 */	0x04 | CS4231_XTAL1,
	/* 32000 */	0x06 | CS4231_XTAL1,
	/* 33075 */	0x0C | CS4231_XTAL2,
	/* 37800 */	0x08 | CS4231_XTAL2,
	/* 44100 */	0x0A | CS4231_XTAL2,
	/* 48000 */	0x0C | CS4231_XTAL1
};

static unsigned int rates[14] = {
	5510, 6620, 8000, 9600, 11025, 16000, 18900, 22050,
	27042, 32000, 33075, 37800, 44100, 48000
};

static struct snd_pcm_hw_constraint_list hw_constraints_rates = {
	.count	= 14,
	.list	= rates,
};

static int snd_cs4231_xrate(struct snd_pcm_runtime *runtime)
{
	return snd_pcm_hw_constraint_list(runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &hw_constraints_rates);
}

static unsigned char snd_cs4231_original_image[32] =
{
	0x00,			/* 00/00 - lic */
	0x00,			/* 01/01 - ric */
	0x9f,			/* 02/02 - la1ic */
	0x9f,			/* 03/03 - ra1ic */
	0x9f,			/* 04/04 - la2ic */
	0x9f,			/* 05/05 - ra2ic */
	0xbf,			/* 06/06 - loc */
	0xbf,			/* 07/07 - roc */
	0x20,			/* 08/08 - pdfr */
	CS4231_AUTOCALIB,	/* 09/09 - ic */
	0x00,			/* 0a/10 - pc */
	0x00,			/* 0b/11 - ti */
	CS4231_MODE2,		/* 0c/12 - mi */
	0x00,			/* 0d/13 - lbc */
	0x00,			/* 0e/14 - pbru */
	0x00,			/* 0f/15 - pbrl */
	0x80,			/* 10/16 - afei */
	0x01,			/* 11/17 - afeii */
	0x9f,			/* 12/18 - llic */
	0x9f,			/* 13/19 - rlic */
	0x00,			/* 14/20 - tlb */
	0x00,			/* 15/21 - thb */
	0x00,			/* 16/22 - la3mic/reserved */
	0x00,			/* 17/23 - ra3mic/reserved */
	0x00,			/* 18/24 - afs */
	0x00,			/* 19/25 - lamoc/version */
	0x00,			/* 1a/26 - mioc */
	0x00,			/* 1b/27 - ramoc/reserved */
	0x20,			/* 1c/28 - cdfr */
	0x00,			/* 1d/29 - res4 */
	0x00,			/* 1e/30 - cbru */
	0x00,			/* 1f/31 - cbrl */
};

static u8 __cs4231_readb(struct snd_cs4231 *cp, void __iomem *reg_addr)
{
#ifdef EBUS_SUPPORT
	if (cp->flags & CS4231_FLAG_EBUS) {
		return readb(reg_addr);
	} else {
#endif
#ifdef SBUS_SUPPORT
		return sbus_readb(reg_addr);
#endif
#ifdef EBUS_SUPPORT
	}
#endif
}

static void __cs4231_writeb(struct snd_cs4231 *cp, u8 val, void __iomem *reg_addr)
{
#ifdef EBUS_SUPPORT
	if (cp->flags & CS4231_FLAG_EBUS) {
		return writeb(val, reg_addr);
	} else {
#endif
#ifdef SBUS_SUPPORT
		return sbus_writeb(val, reg_addr);
#endif
#ifdef EBUS_SUPPORT
	}
#endif
}

/*
 *  Basic I/O functions
 */

static void snd_cs4231_outm(struct snd_cs4231 *chip, unsigned char reg,
		     unsigned char mask, unsigned char value)
{
	int timeout;
	unsigned char tmp;

	for (timeout = 250;
	     timeout > 0 && (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT);
	     timeout--)
	     	udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT)
		snd_printdd("outm: auto calibration time out - reg = 0x%x, value = 0x%x\n", reg, value);
#endif
	if (chip->calibrate_mute) {
		chip->image[reg] &= mask;
		chip->image[reg] |= value;
	} else {
		__cs4231_writeb(chip, chip->mce_bit | reg, CS4231P(chip, REGSEL));
		mb();
		tmp = (chip->image[reg] & mask) | value;
		__cs4231_writeb(chip, tmp, CS4231P(chip, REG));
		chip->image[reg] = tmp;
		mb();
	}
}

static void snd_cs4231_dout(struct snd_cs4231 *chip, unsigned char reg, unsigned char value)
{
	int timeout;

	for (timeout = 250;
	     timeout > 0 && (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT);
	     timeout--)
	     	udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT)
		snd_printdd("out: auto calibration time out - reg = 0x%x, value = 0x%x\n", reg, value);
#endif
	__cs4231_writeb(chip, chip->mce_bit | reg, CS4231P(chip, REGSEL));
	__cs4231_writeb(chip, value, CS4231P(chip, REG));
	mb();
}

static void snd_cs4231_out(struct snd_cs4231 *chip, unsigned char reg, unsigned char value)
{
	int timeout;

	for (timeout = 250;
	     timeout > 0 && (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT);
	     timeout--)
	     	udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT)
		snd_printdd("out: auto calibration time out - reg = 0x%x, value = 0x%x\n", reg, value);
#endif
	__cs4231_writeb(chip, chip->mce_bit | reg, CS4231P(chip, REGSEL));
	__cs4231_writeb(chip, value, CS4231P(chip, REG));
	chip->image[reg] = value;
	mb();
}

static unsigned char snd_cs4231_in(struct snd_cs4231 *chip, unsigned char reg)
{
	int timeout;
	unsigned char ret;

	for (timeout = 250;
	     timeout > 0 && (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT);
	     timeout--)
	     	udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT)
		snd_printdd("in: auto calibration time out - reg = 0x%x\n", reg);
#endif
	__cs4231_writeb(chip, chip->mce_bit | reg, CS4231P(chip, REGSEL));
	mb();
	ret = __cs4231_readb(chip, CS4231P(chip, REG));
	return ret;
}

/*
 *  CS4231 detection / MCE routines
 */

static void snd_cs4231_busy_wait(struct snd_cs4231 *chip)
{
	int timeout;

	/* huh.. looks like this sequence is proper for CS4231A chip (GUS MAX) */
	for (timeout = 5; timeout > 0; timeout--)
		__cs4231_readb(chip, CS4231P(chip, REGSEL));

	/* end of cleanup sequence */
	for (timeout = 500;
	     timeout > 0 && (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT);
	     timeout--)
	     	udelay(1000);
}

static void snd_cs4231_mce_up(struct snd_cs4231 *chip)
{
	unsigned long flags;
	int timeout;

	spin_lock_irqsave(&chip->lock, flags);
	for (timeout = 250; timeout > 0 && (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT); timeout--)
		udelay(100);
#ifdef CONFIG_SND_DEBUG
	if (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT)
		snd_printdd("mce_up - auto calibration time out (0)\n");
#endif
	chip->mce_bit |= CS4231_MCE;
	timeout = __cs4231_readb(chip, CS4231P(chip, REGSEL));
	if (timeout == 0x80)
		snd_printdd("mce_up [%p]: serious init problem - codec still busy\n", chip->port);
	if (!(timeout & CS4231_MCE))
		__cs4231_writeb(chip, chip->mce_bit | (timeout & 0x1f), CS4231P(chip, REGSEL));
	spin_unlock_irqrestore(&chip->lock, flags);
}

static void snd_cs4231_mce_down(struct snd_cs4231 *chip)
{
	unsigned long flags;
	int timeout;

	spin_lock_irqsave(&chip->lock, flags);
	snd_cs4231_busy_wait(chip);
#ifdef CONFIG_SND_DEBUG
	if (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT)
		snd_printdd("mce_down [%p] - auto calibration time out (0)\n", CS4231P(chip, REGSEL));
#endif
	chip->mce_bit &= ~CS4231_MCE;
	timeout = __cs4231_readb(chip, CS4231P(chip, REGSEL));
	__cs4231_writeb(chip, chip->mce_bit | (timeout & 0x1f), CS4231P(chip, REGSEL));
	if (timeout == 0x80)
		snd_printdd("mce_down [%p]: serious init problem - codec still busy\n", chip->port);
	if ((timeout & CS4231_MCE) == 0) {
		spin_unlock_irqrestore(&chip->lock, flags);
		return;
	}
	snd_cs4231_busy_wait(chip);

	/* calibration process */

	for (timeout = 500; timeout > 0 && (snd_cs4231_in(chip, CS4231_TEST_INIT) & CS4231_CALIB_IN_PROGRESS) == 0; timeout--)
		udelay(100);
	if ((snd_cs4231_in(chip, CS4231_TEST_INIT) & CS4231_CALIB_IN_PROGRESS) == 0) {
		snd_printd("cs4231_mce_down - auto calibration time out (1)\n");
		spin_unlock_irqrestore(&chip->lock, flags);
		return;
	}

	/* in 10ms increments, check condition, up to 250ms */
	timeout = 25;
	while (snd_cs4231_in(chip, CS4231_TEST_INIT) & CS4231_CALIB_IN_PROGRESS) {
		spin_unlock_irqrestore(&chip->lock, flags);
		if (--timeout < 0) {
			snd_printk("mce_down - auto calibration time out (2)\n");
			return;
		}
		msleep(10);
		spin_lock_irqsave(&chip->lock, flags);
	}

	/* in 10ms increments, check condition, up to 100ms */
	timeout = 10;
	while (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT) {
		spin_unlock_irqrestore(&chip->lock, flags);
		if (--timeout < 0) {
			snd_printk("mce_down - auto calibration time out (3)\n");
			return;
		}
		msleep(10);
		spin_lock_irqsave(&chip->lock, flags);
	}
	spin_unlock_irqrestore(&chip->lock, flags);
}

static void snd_cs4231_advance_dma(struct cs4231_dma_control *dma_cont,
				   struct snd_pcm_substream *substream,
				   unsigned int *periods_sent)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	while (1) {
		unsigned int period_size = snd_pcm_lib_period_bytes(substream);
		unsigned int offset = period_size * (*periods_sent);

		BUG_ON(period_size >= (1 << 24));

		if (dma_cont->request(dma_cont, runtime->dma_addr + offset, period_size))
			return;
		(*periods_sent) = ((*periods_sent) + 1) % runtime->periods;
	}
}

static void cs4231_dma_trigger(struct snd_pcm_substream *substream,
			       unsigned int what, int on)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);
	struct cs4231_dma_control *dma_cont;

	if (what & CS4231_PLAYBACK_ENABLE) {
		dma_cont = &chip->p_dma;
		if (on) {
			dma_cont->prepare(dma_cont, 0);
			dma_cont->enable(dma_cont, 1);
			snd_cs4231_advance_dma(dma_cont,
				chip->playback_substream,
				&chip->p_periods_sent);
		} else {
			dma_cont->enable(dma_cont, 0);
		}
	}
	if (what & CS4231_RECORD_ENABLE) {
		dma_cont = &chip->c_dma;
		if (on) {
			dma_cont->prepare(dma_cont, 1);
			dma_cont->enable(dma_cont, 1);
			snd_cs4231_advance_dma(dma_cont,
				chip->capture_substream,
				&chip->c_periods_sent);
		} else {
			dma_cont->enable(dma_cont, 0);
		}
	}
}

static int snd_cs4231_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);
	int result = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_STOP:
	{
		unsigned int what = 0;
		struct snd_pcm_substream *s;
		unsigned long flags;

		snd_pcm_group_for_each_entry(s, substream) {
			if (s == chip->playback_substream) {
				what |= CS4231_PLAYBACK_ENABLE;
				snd_pcm_trigger_done(s, substream);
			} else if (s == chip->capture_substream) {
				what |= CS4231_RECORD_ENABLE;
				snd_pcm_trigger_done(s, substream);
			}
		}

		spin_lock_irqsave(&chip->lock, flags);
		if (cmd == SNDRV_PCM_TRIGGER_START) {
			cs4231_dma_trigger(substream, what, 1);
			chip->image[CS4231_IFACE_CTRL] |= what;
		} else {
			cs4231_dma_trigger(substream, what, 0);
			chip->image[CS4231_IFACE_CTRL] &= ~what;
		}
		snd_cs4231_out(chip, CS4231_IFACE_CTRL,
			       chip->image[CS4231_IFACE_CTRL]);
		spin_unlock_irqrestore(&chip->lock, flags);
		break;
	}
	default:
		result = -EINVAL;
		break;
	}

	return result;
}

/*
 *  CODEC I/O
 */

static unsigned char snd_cs4231_get_rate(unsigned int rate)
{
	int i;

	for (i = 0; i < 14; i++)
		if (rate == rates[i])
			return freq_bits[i];
	// snd_BUG();
	return freq_bits[13];
}

static unsigned char snd_cs4231_get_format(struct snd_cs4231 *chip, int format, int channels)
{
	unsigned char rformat;

	rformat = CS4231_LINEAR_8;
	switch (format) {
	case SNDRV_PCM_FORMAT_MU_LAW:	rformat = CS4231_ULAW_8; break;
	case SNDRV_PCM_FORMAT_A_LAW:	rformat = CS4231_ALAW_8; break;
	case SNDRV_PCM_FORMAT_S16_LE:	rformat = CS4231_LINEAR_16; break;
	case SNDRV_PCM_FORMAT_S16_BE:	rformat = CS4231_LINEAR_16_BIG; break;
	case SNDRV_PCM_FORMAT_IMA_ADPCM:	rformat = CS4231_ADPCM_16; break;
	}
	if (channels > 1)
		rformat |= CS4231_STEREO;
	return rformat;
}

static void snd_cs4231_calibrate_mute(struct snd_cs4231 *chip, int mute)
{
	unsigned long flags;

	mute = mute ? 1 : 0;
	spin_lock_irqsave(&chip->lock, flags);
	if (chip->calibrate_mute == mute) {
		spin_unlock_irqrestore(&chip->lock, flags);
		return;
	}
	if (!mute) {
		snd_cs4231_dout(chip, CS4231_LEFT_INPUT,
				chip->image[CS4231_LEFT_INPUT]);
		snd_cs4231_dout(chip, CS4231_RIGHT_INPUT,
				chip->image[CS4231_RIGHT_INPUT]);
		snd_cs4231_dout(chip, CS4231_LOOPBACK,
				chip->image[CS4231_LOOPBACK]);
	}
	snd_cs4231_dout(chip, CS4231_AUX1_LEFT_INPUT,
			mute ? 0x80 : chip->image[CS4231_AUX1_LEFT_INPUT]);
	snd_cs4231_dout(chip, CS4231_AUX1_RIGHT_INPUT,
			mute ? 0x80 : chip->image[CS4231_AUX1_RIGHT_INPUT]);
	snd_cs4231_dout(chip, CS4231_AUX2_LEFT_INPUT,
			mute ? 0x80 : chip->image[CS4231_AUX2_LEFT_INPUT]);
	snd_cs4231_dout(chip, CS4231_AUX2_RIGHT_INPUT,
			mute ? 0x80 : chip->image[CS4231_AUX2_RIGHT_INPUT]);
	snd_cs4231_dout(chip, CS4231_LEFT_OUTPUT,
			mute ? 0x80 : chip->image[CS4231_LEFT_OUTPUT]);
	snd_cs4231_dout(chip, CS4231_RIGHT_OUTPUT,
			mute ? 0x80 : chip->image[CS4231_RIGHT_OUTPUT]);
	snd_cs4231_dout(chip, CS4231_LEFT_LINE_IN,
			mute ? 0x80 : chip->image[CS4231_LEFT_LINE_IN]);
	snd_cs4231_dout(chip, CS4231_RIGHT_LINE_IN,
			mute ? 0x80 : chip->image[CS4231_RIGHT_LINE_IN]);
	snd_cs4231_dout(chip, CS4231_MONO_CTRL,
			mute ? 0xc0 : chip->image[CS4231_MONO_CTRL]);
	chip->calibrate_mute = mute;
	spin_unlock_irqrestore(&chip->lock, flags);
}

static void snd_cs4231_playback_format(struct snd_cs4231 *chip, struct snd_pcm_hw_params *params,
				       unsigned char pdfr)
{
	unsigned long flags;

	mutex_lock(&chip->mce_mutex);
	snd_cs4231_calibrate_mute(chip, 1);

	snd_cs4231_mce_up(chip);

	spin_lock_irqsave(&chip->lock, flags);
	snd_cs4231_out(chip, CS4231_PLAYBK_FORMAT,
		       (chip->image[CS4231_IFACE_CTRL] & CS4231_RECORD_ENABLE) ?
		       (pdfr & 0xf0) | (chip->image[CS4231_REC_FORMAT] & 0x0f) :
		       pdfr);
	spin_unlock_irqrestore(&chip->lock, flags);

	snd_cs4231_mce_down(chip);

	snd_cs4231_calibrate_mute(chip, 0);
	mutex_unlock(&chip->mce_mutex);
}

static void snd_cs4231_capture_format(struct snd_cs4231 *chip, struct snd_pcm_hw_params *params,
                                      unsigned char cdfr)
{
	unsigned long flags;

	mutex_lock(&chip->mce_mutex);
	snd_cs4231_calibrate_mute(chip, 1);

	snd_cs4231_mce_up(chip);

	spin_lock_irqsave(&chip->lock, flags);
	if (!(chip->image[CS4231_IFACE_CTRL] & CS4231_PLAYBACK_ENABLE)) {
		snd_cs4231_out(chip, CS4231_PLAYBK_FORMAT,
			       ((chip->image[CS4231_PLAYBK_FORMAT]) & 0xf0) |
			       (cdfr & 0x0f));
		spin_unlock_irqrestore(&chip->lock, flags);
		snd_cs4231_mce_down(chip);
		snd_cs4231_mce_up(chip);
		spin_lock_irqsave(&chip->lock, flags);
	}
	snd_cs4231_out(chip, CS4231_REC_FORMAT, cdfr);
	spin_unlock_irqrestore(&chip->lock, flags);

	snd_cs4231_mce_down(chip);

	snd_cs4231_calibrate_mute(chip, 0);
	mutex_unlock(&chip->mce_mutex);
}

/*
 *  Timer interface
 */

static unsigned long snd_cs4231_timer_resolution(struct snd_timer *timer)
{
	struct snd_cs4231 *chip = snd_timer_chip(timer);

	return chip->image[CS4231_PLAYBK_FORMAT] & 1 ? 9969 : 9920;
}

static int snd_cs4231_timer_start(struct snd_timer *timer)
{
	unsigned long flags;
	unsigned int ticks;
	struct snd_cs4231 *chip = snd_timer_chip(timer);

	spin_lock_irqsave(&chip->lock, flags);
	ticks = timer->sticks;
	if ((chip->image[CS4231_ALT_FEATURE_1] & CS4231_TIMER_ENABLE) == 0 ||
	    (unsigned char)(ticks >> 8) != chip->image[CS4231_TIMER_HIGH] ||
	    (unsigned char)ticks != chip->image[CS4231_TIMER_LOW]) {
		snd_cs4231_out(chip, CS4231_TIMER_HIGH,
			       chip->image[CS4231_TIMER_HIGH] =
			       (unsigned char) (ticks >> 8));
		snd_cs4231_out(chip, CS4231_TIMER_LOW,
			       chip->image[CS4231_TIMER_LOW] =
			       (unsigned char) ticks);
		snd_cs4231_out(chip, CS4231_ALT_FEATURE_1,
			       chip->image[CS4231_ALT_FEATURE_1] | CS4231_TIMER_ENABLE);
	}
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int snd_cs4231_timer_stop(struct snd_timer *timer)
{
	unsigned long flags;
	struct snd_cs4231 *chip = snd_timer_chip(timer);

	spin_lock_irqsave(&chip->lock, flags);
	snd_cs4231_out(chip, CS4231_ALT_FEATURE_1,
		       chip->image[CS4231_ALT_FEATURE_1] &= ~CS4231_TIMER_ENABLE);
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static void __init snd_cs4231_init(struct snd_cs4231 *chip)
{
	unsigned long flags;

	snd_cs4231_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printdd("init: (1)\n");
#endif
	snd_cs4231_mce_up(chip);
	spin_lock_irqsave(&chip->lock, flags);
	chip->image[CS4231_IFACE_CTRL] &= ~(CS4231_PLAYBACK_ENABLE | CS4231_PLAYBACK_PIO |
					    CS4231_RECORD_ENABLE | CS4231_RECORD_PIO |
					    CS4231_CALIB_MODE);
	chip->image[CS4231_IFACE_CTRL] |= CS4231_AUTOCALIB;
	snd_cs4231_out(chip, CS4231_IFACE_CTRL, chip->image[CS4231_IFACE_CTRL]);
	spin_unlock_irqrestore(&chip->lock, flags);
	snd_cs4231_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printdd("init: (2)\n");
#endif

	snd_cs4231_mce_up(chip);
	spin_lock_irqsave(&chip->lock, flags);
	snd_cs4231_out(chip, CS4231_ALT_FEATURE_1, chip->image[CS4231_ALT_FEATURE_1]);
	spin_unlock_irqrestore(&chip->lock, flags);
	snd_cs4231_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printdd("init: (3) - afei = 0x%x\n", chip->image[CS4231_ALT_FEATURE_1]);
#endif

	spin_lock_irqsave(&chip->lock, flags);
	snd_cs4231_out(chip, CS4231_ALT_FEATURE_2, chip->image[CS4231_ALT_FEATURE_2]);
	spin_unlock_irqrestore(&chip->lock, flags);

	snd_cs4231_mce_up(chip);
	spin_lock_irqsave(&chip->lock, flags);
	snd_cs4231_out(chip, CS4231_PLAYBK_FORMAT, chip->image[CS4231_PLAYBK_FORMAT]);
	spin_unlock_irqrestore(&chip->lock, flags);
	snd_cs4231_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printdd("init: (4)\n");
#endif

	snd_cs4231_mce_up(chip);
	spin_lock_irqsave(&chip->lock, flags);
	snd_cs4231_out(chip, CS4231_REC_FORMAT, chip->image[CS4231_REC_FORMAT]);
	spin_unlock_irqrestore(&chip->lock, flags);
	snd_cs4231_mce_down(chip);

#ifdef SNDRV_DEBUG_MCE
	snd_printdd("init: (5)\n");
#endif
}

static int snd_cs4231_open(struct snd_cs4231 *chip, unsigned int mode)
{
	unsigned long flags;

	mutex_lock(&chip->open_mutex);
	if ((chip->mode & mode)) {
		mutex_unlock(&chip->open_mutex);
		return -EAGAIN;
	}
	if (chip->mode & CS4231_MODE_OPEN) {
		chip->mode |= mode;
		mutex_unlock(&chip->open_mutex);
		return 0;
	}
	/* ok. now enable and ack CODEC IRQ */
	spin_lock_irqsave(&chip->lock, flags);
	snd_cs4231_out(chip, CS4231_IRQ_STATUS, CS4231_PLAYBACK_IRQ |
		       CS4231_RECORD_IRQ |
		       CS4231_TIMER_IRQ);
	snd_cs4231_out(chip, CS4231_IRQ_STATUS, 0);
	__cs4231_writeb(chip, 0, CS4231P(chip, STATUS));	/* clear IRQ */
	__cs4231_writeb(chip, 0, CS4231P(chip, STATUS));	/* clear IRQ */

	snd_cs4231_out(chip, CS4231_IRQ_STATUS, CS4231_PLAYBACK_IRQ |
		       CS4231_RECORD_IRQ |
		       CS4231_TIMER_IRQ);
	snd_cs4231_out(chip, CS4231_IRQ_STATUS, 0);

	spin_unlock_irqrestore(&chip->lock, flags);

	chip->mode = mode;
	mutex_unlock(&chip->open_mutex);
	return 0;
}

static void snd_cs4231_close(struct snd_cs4231 *chip, unsigned int mode)
{
	unsigned long flags;

	mutex_lock(&chip->open_mutex);
	chip->mode &= ~mode;
	if (chip->mode & CS4231_MODE_OPEN) {
		mutex_unlock(&chip->open_mutex);
		return;
	}
	snd_cs4231_calibrate_mute(chip, 1);

	/* disable IRQ */
	spin_lock_irqsave(&chip->lock, flags);
	snd_cs4231_out(chip, CS4231_IRQ_STATUS, 0);
	__cs4231_writeb(chip, 0, CS4231P(chip, STATUS));	/* clear IRQ */
	__cs4231_writeb(chip, 0, CS4231P(chip, STATUS));	/* clear IRQ */

	/* now disable record & playback */

	if (chip->image[CS4231_IFACE_CTRL] &
	    (CS4231_PLAYBACK_ENABLE | CS4231_PLAYBACK_PIO |
	     CS4231_RECORD_ENABLE | CS4231_RECORD_PIO)) {
		spin_unlock_irqrestore(&chip->lock, flags);
		snd_cs4231_mce_up(chip);
		spin_lock_irqsave(&chip->lock, flags);
		chip->image[CS4231_IFACE_CTRL] &=
			~(CS4231_PLAYBACK_ENABLE | CS4231_PLAYBACK_PIO |
			  CS4231_RECORD_ENABLE | CS4231_RECORD_PIO);
		snd_cs4231_out(chip, CS4231_IFACE_CTRL, chip->image[CS4231_IFACE_CTRL]);
		spin_unlock_irqrestore(&chip->lock, flags);
		snd_cs4231_mce_down(chip);
		spin_lock_irqsave(&chip->lock, flags);
	}

	/* clear IRQ again */
	snd_cs4231_out(chip, CS4231_IRQ_STATUS, 0);
	__cs4231_writeb(chip, 0, CS4231P(chip, STATUS));	/* clear IRQ */
	__cs4231_writeb(chip, 0, CS4231P(chip, STATUS));	/* clear IRQ */
	spin_unlock_irqrestore(&chip->lock, flags);

	snd_cs4231_calibrate_mute(chip, 0);

	chip->mode = 0;
	mutex_unlock(&chip->open_mutex);
}

/*
 *  timer open/close
 */

static int snd_cs4231_timer_open(struct snd_timer *timer)
{
	struct snd_cs4231 *chip = snd_timer_chip(timer);
	snd_cs4231_open(chip, CS4231_MODE_TIMER);
	return 0;
}

static int snd_cs4231_timer_close(struct snd_timer * timer)
{
	struct snd_cs4231 *chip = snd_timer_chip(timer);
	snd_cs4231_close(chip, CS4231_MODE_TIMER);
	return 0;
}

static struct snd_timer_hardware snd_cs4231_timer_table =
{
	.flags		=	SNDRV_TIMER_HW_AUTO,
	.resolution	=	9945,
	.ticks		=	65535,
	.open		=	snd_cs4231_timer_open,
	.close		=	snd_cs4231_timer_close,
	.c_resolution	=	snd_cs4231_timer_resolution,
	.start		=	snd_cs4231_timer_start,
	.stop		=	snd_cs4231_timer_stop,
};

/*
 *  ok.. exported functions..
 */

static int snd_cs4231_playback_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *hw_params)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);
	unsigned char new_pdfr;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream,
					    params_buffer_bytes(hw_params))) < 0)
		return err;
	new_pdfr = snd_cs4231_get_format(chip, params_format(hw_params),
					 params_channels(hw_params)) |
		snd_cs4231_get_rate(params_rate(hw_params));
	snd_cs4231_playback_format(chip, hw_params, new_pdfr);

	return 0;
}

static int snd_cs4231_playback_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_cs4231_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);

	chip->image[CS4231_IFACE_CTRL] &= ~(CS4231_PLAYBACK_ENABLE |
					    CS4231_PLAYBACK_PIO);

	BUG_ON(runtime->period_size > 0xffff + 1);

	chip->p_periods_sent = 0;
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int snd_cs4231_capture_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *hw_params)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);
	unsigned char new_cdfr;
	int err;

	if ((err = snd_pcm_lib_malloc_pages(substream,
					    params_buffer_bytes(hw_params))) < 0)
		return err;
	new_cdfr = snd_cs4231_get_format(chip, params_format(hw_params),
					 params_channels(hw_params)) |
		snd_cs4231_get_rate(params_rate(hw_params));
	snd_cs4231_capture_format(chip, hw_params, new_cdfr);

	return 0;
}

static int snd_cs4231_capture_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_cs4231_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);
	unsigned long flags;

	spin_lock_irqsave(&chip->lock, flags);
	chip->image[CS4231_IFACE_CTRL] &= ~(CS4231_RECORD_ENABLE |
					    CS4231_RECORD_PIO);


	chip->c_periods_sent = 0;
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static void snd_cs4231_overrange(struct snd_cs4231 *chip)
{
	unsigned long flags;
	unsigned char res;

	spin_lock_irqsave(&chip->lock, flags);
	res = snd_cs4231_in(chip, CS4231_TEST_INIT);
	spin_unlock_irqrestore(&chip->lock, flags);

	if (res & (0x08 | 0x02))	/* detect overrange only above 0dB; may be user selectable? */
		chip->capture_substream->runtime->overrange++;
}

static void snd_cs4231_play_callback(struct snd_cs4231 *chip)
{
	if (chip->image[CS4231_IFACE_CTRL] & CS4231_PLAYBACK_ENABLE) {
		snd_pcm_period_elapsed(chip->playback_substream);
		snd_cs4231_advance_dma(&chip->p_dma, chip->playback_substream,
					    &chip->p_periods_sent);
	}
}

static void snd_cs4231_capture_callback(struct snd_cs4231 *chip)
{
	if (chip->image[CS4231_IFACE_CTRL] & CS4231_RECORD_ENABLE) {
		snd_pcm_period_elapsed(chip->capture_substream);
		snd_cs4231_advance_dma(&chip->c_dma, chip->capture_substream,
					    &chip->c_periods_sent);
	}
}

static snd_pcm_uframes_t snd_cs4231_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);
	struct cs4231_dma_control *dma_cont = &chip->p_dma;
	size_t ptr;
	
	if (!(chip->image[CS4231_IFACE_CTRL] & CS4231_PLAYBACK_ENABLE))
		return 0;
	ptr = dma_cont->address(dma_cont);
	if (ptr != 0)
		ptr -= substream->runtime->dma_addr;
	
	return bytes_to_frames(substream->runtime, ptr);
}

static snd_pcm_uframes_t snd_cs4231_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);
	struct cs4231_dma_control *dma_cont = &chip->c_dma;
	size_t ptr;
	
	if (!(chip->image[CS4231_IFACE_CTRL] & CS4231_RECORD_ENABLE))
		return 0;
	ptr = dma_cont->address(dma_cont);
	if (ptr != 0)
		ptr -= substream->runtime->dma_addr;
	
	return bytes_to_frames(substream->runtime, ptr);
}

/*

 */

static int __init snd_cs4231_probe(struct snd_cs4231 *chip)
{
	unsigned long flags;
	int i, id, vers;
	unsigned char *ptr;

	id = vers = 0;
	for (i = 0; i < 50; i++) {
		mb();
		if (__cs4231_readb(chip, CS4231P(chip, REGSEL)) & CS4231_INIT)
			udelay(2000);
		else {
			spin_lock_irqsave(&chip->lock, flags);
			snd_cs4231_out(chip, CS4231_MISC_INFO, CS4231_MODE2);
			id = snd_cs4231_in(chip, CS4231_MISC_INFO) & 0x0f;
			vers = snd_cs4231_in(chip, CS4231_VERSION);
			spin_unlock_irqrestore(&chip->lock, flags);
			if (id == 0x0a)
				break;	/* this is valid value */
		}
	}
	snd_printdd("cs4231: port = %p, id = 0x%x\n", chip->port, id);
	if (id != 0x0a)
		return -ENODEV;	/* no valid device found */

	spin_lock_irqsave(&chip->lock, flags);


	/* Reset DMA engine (sbus only).  */
	chip->p_dma.reset(chip);

	__cs4231_readb(chip, CS4231P(chip, STATUS));	/* clear any pendings IRQ */
	__cs4231_writeb(chip, 0, CS4231P(chip, STATUS));
	mb();

	spin_unlock_irqrestore(&chip->lock, flags);

	chip->image[CS4231_MISC_INFO] = CS4231_MODE2;
	chip->image[CS4231_IFACE_CTRL] =
		chip->image[CS4231_IFACE_CTRL] & ~CS4231_SINGLE_DMA;
	chip->image[CS4231_ALT_FEATURE_1] = 0x80;
	chip->image[CS4231_ALT_FEATURE_2] = 0x01;
	if (vers & 0x20)
		chip->image[CS4231_ALT_FEATURE_2] |= 0x02;

	ptr = (unsigned char *) &chip->image;

	snd_cs4231_mce_down(chip);

	spin_lock_irqsave(&chip->lock, flags);

	for (i = 0; i < 32; i++)	/* ok.. fill all CS4231 registers */
		snd_cs4231_out(chip, i, *ptr++);

	spin_unlock_irqrestore(&chip->lock, flags);

	snd_cs4231_mce_up(chip);

	snd_cs4231_mce_down(chip);

	mdelay(2);

	return 0;		/* all things are ok.. */
}

static struct snd_pcm_hardware snd_cs4231_playback =
{
	.info			= (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
	.formats		= (SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW |
				 SNDRV_PCM_FMTBIT_IMA_ADPCM |
				 SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S16_BE),
	.rates			= SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_48000,
	.rate_min		= 5510,
	.rate_max		= 48000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= (32*1024),
	.period_bytes_min	= 64,
	.period_bytes_max	= (32*1024),
	.periods_min		= 1,
	.periods_max		= 1024,
};

static struct snd_pcm_hardware snd_cs4231_capture =
{
	.info			= (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_SYNC_START),
	.formats		= (SNDRV_PCM_FMTBIT_MU_LAW | SNDRV_PCM_FMTBIT_A_LAW |
				 SNDRV_PCM_FMTBIT_IMA_ADPCM |
				 SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE |
				 SNDRV_PCM_FMTBIT_S16_BE),
	.rates			= SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_8000_48000,
	.rate_min		= 5510,
	.rate_max		= 48000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= (32*1024),
	.period_bytes_min	= 64,
	.period_bytes_max	= (32*1024),
	.periods_min		= 1,
	.periods_max		= 1024,
};

static int snd_cs4231_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	runtime->hw = snd_cs4231_playback;

	if ((err = snd_cs4231_open(chip, CS4231_MODE_PLAY)) < 0) {
		snd_free_pages(runtime->dma_area, runtime->dma_bytes);
		return err;
	}
	chip->playback_substream = substream;
	chip->p_periods_sent = 0;
	snd_pcm_set_sync(substream);
	snd_cs4231_xrate(runtime);

	return 0;
}

static int snd_cs4231_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	runtime->hw = snd_cs4231_capture;

	if ((err = snd_cs4231_open(chip, CS4231_MODE_RECORD)) < 0) {
		snd_free_pages(runtime->dma_area, runtime->dma_bytes);
		return err;
	}
	chip->capture_substream = substream;
	chip->c_periods_sent = 0;
	snd_pcm_set_sync(substream);
	snd_cs4231_xrate(runtime);

	return 0;
}

static int snd_cs4231_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);

	snd_cs4231_close(chip, CS4231_MODE_PLAY);
	chip->playback_substream = NULL;

	return 0;
}

static int snd_cs4231_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_cs4231 *chip = snd_pcm_substream_chip(substream);

	snd_cs4231_close(chip, CS4231_MODE_RECORD);
	chip->capture_substream = NULL;

	return 0;
}

/* XXX We can do some power-management, in particular on EBUS using
 * XXX the audio AUXIO register...
 */

static struct snd_pcm_ops snd_cs4231_playback_ops = {
	.open		=	snd_cs4231_playback_open,
	.close		=	snd_cs4231_playback_close,
	.ioctl		=	snd_pcm_lib_ioctl,
	.hw_params	=	snd_cs4231_playback_hw_params,
	.hw_free	=	snd_cs4231_playback_hw_free,
	.prepare	=	snd_cs4231_playback_prepare,
	.trigger	=	snd_cs4231_trigger,
	.pointer	=	snd_cs4231_playback_pointer,
};

static struct snd_pcm_ops snd_cs4231_capture_ops = {
	.open		=	snd_cs4231_capture_open,
	.close		=	snd_cs4231_capture_close,
	.ioctl		=	snd_pcm_lib_ioctl,
	.hw_params	=	snd_cs4231_capture_hw_params,
	.hw_free	=	snd_cs4231_capture_hw_free,
	.prepare	=	snd_cs4231_capture_prepare,
	.trigger	=	snd_cs4231_trigger,
	.pointer	=	snd_cs4231_capture_pointer,
};

static int __init snd_cs4231_pcm(struct snd_cs4231 *chip)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(chip->card, "CS4231", 0, 1, 1, &pcm)) < 0)
		return err;

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_cs4231_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_cs4231_capture_ops);
	
	/* global setup */
	pcm->private_data = chip;
	pcm->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;
	strcpy(pcm->name, "CS4231");

	chip->p_dma.preallocate(chip, pcm);

	chip->pcm = pcm;

	return 0;
}

static int __init snd_cs4231_timer(struct snd_cs4231 *chip)
{
	struct snd_timer *timer;
	struct snd_timer_id tid;
	int err;

	/* Timer initialization */
	tid.dev_class = SNDRV_TIMER_CLASS_CARD;
	tid.dev_sclass = SNDRV_TIMER_SCLASS_NONE;
	tid.card = chip->card->number;
	tid.device = 0;
	tid.subdevice = 0;
	if ((err = snd_timer_new(chip->card, "CS4231", &tid, &timer)) < 0)
		return err;
	strcpy(timer->name, "CS4231");
	timer->private_data = chip;
	timer->hw = snd_cs4231_timer_table;
	chip->timer = timer;

	return 0;
}
	
/*
 *  MIXER part
 */

static int snd_cs4231_info_mux(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	static char *texts[4] = {
		"Line", "CD", "Mic", "Mix"
	};
	struct snd_cs4231 *chip = snd_kcontrol_chip(kcontrol);

	snd_assert(chip->card != NULL, return -EINVAL);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 2;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item > 3)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);

	return 0;
}

static int snd_cs4231_get_mux(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs4231 *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&chip->lock, flags);
	ucontrol->value.enumerated.item[0] =
		(chip->image[CS4231_LEFT_INPUT] & CS4231_MIXS_ALL) >> 6;
	ucontrol->value.enumerated.item[1] =
		(chip->image[CS4231_RIGHT_INPUT] & CS4231_MIXS_ALL) >> 6;
	spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int snd_cs4231_put_mux(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs4231 *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned short left, right;
	int change;
	
	if (ucontrol->value.enumerated.item[0] > 3 ||
	    ucontrol->value.enumerated.item[1] > 3)
		return -EINVAL;
	left = ucontrol->value.enumerated.item[0] << 6;
	right = ucontrol->value.enumerated.item[1] << 6;

	spin_lock_irqsave(&chip->lock, flags);

	left = (chip->image[CS4231_LEFT_INPUT] & ~CS4231_MIXS_ALL) | left;
	right = (chip->image[CS4231_RIGHT_INPUT] & ~CS4231_MIXS_ALL) | right;
	change = left != chip->image[CS4231_LEFT_INPUT] ||
	         right != chip->image[CS4231_RIGHT_INPUT];
	snd_cs4231_out(chip, CS4231_LEFT_INPUT, left);
	snd_cs4231_out(chip, CS4231_RIGHT_INPUT, right);

	spin_unlock_irqrestore(&chip->lock, flags);

	return change;
}

static int snd_cs4231_info_single(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 16) & 0xff;

	uinfo->type = (mask == 1) ?
		SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;

	return 0;
}

static int snd_cs4231_get_single(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs4231 *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	
	spin_lock_irqsave(&chip->lock, flags);

	ucontrol->value.integer.value[0] = (chip->image[reg] >> shift) & mask;

	spin_unlock_irqrestore(&chip->lock, flags);

	if (invert)
		ucontrol->value.integer.value[0] =
			(mask - ucontrol->value.integer.value[0]);

	return 0;
}

static int snd_cs4231_put_single(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs4231 *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int mask = (kcontrol->private_value >> 16) & 0xff;
	int invert = (kcontrol->private_value >> 24) & 0xff;
	int change;
	unsigned short val;
	
	val = (ucontrol->value.integer.value[0] & mask);
	if (invert)
		val = mask - val;
	val <<= shift;

	spin_lock_irqsave(&chip->lock, flags);

	val = (chip->image[reg] & ~(mask << shift)) | val;
	change = val != chip->image[reg];
	snd_cs4231_out(chip, reg, val);

	spin_unlock_irqrestore(&chip->lock, flags);

	return change;
}

static int snd_cs4231_info_double(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ?
		SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;

	return 0;
}

static int snd_cs4231_get_double(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs4231 *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	
	spin_lock_irqsave(&chip->lock, flags);

	ucontrol->value.integer.value[0] = (chip->image[left_reg] >> shift_left) & mask;
	ucontrol->value.integer.value[1] = (chip->image[right_reg] >> shift_right) & mask;

	spin_unlock_irqrestore(&chip->lock, flags);

	if (invert) {
		ucontrol->value.integer.value[0] =
			(mask - ucontrol->value.integer.value[0]);
		ucontrol->value.integer.value[1] =
			(mask - ucontrol->value.integer.value[1]);
	}

	return 0;
}

static int snd_cs4231_put_double(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_cs4231 *chip = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int shift_left = (kcontrol->private_value >> 16) & 0x07;
	int shift_right = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int invert = (kcontrol->private_value >> 22) & 1;
	int change;
	unsigned short val1, val2;
	
	val1 = ucontrol->value.integer.value[0] & mask;
	val2 = ucontrol->value.integer.value[1] & mask;
	if (invert) {
		val1 = mask - val1;
		val2 = mask - val2;
	}
	val1 <<= shift_left;
	val2 <<= shift_right;

	spin_lock_irqsave(&chip->lock, flags);

	val1 = (chip->image[left_reg] & ~(mask << shift_left)) | val1;
	val2 = (chip->image[right_reg] & ~(mask << shift_right)) | val2;
	change = val1 != chip->image[left_reg] || val2 != chip->image[right_reg];
	snd_cs4231_out(chip, left_reg, val1);
	snd_cs4231_out(chip, right_reg, val2);

	spin_unlock_irqrestore(&chip->lock, flags);

	return change;
}

#define CS4231_SINGLE(xname, xindex, reg, shift, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_cs4231_info_single, \
  .get = snd_cs4231_get_single, .put = snd_cs4231_put_single, \
  .private_value = reg | (shift << 8) | (mask << 16) | (invert << 24) }

#define CS4231_DOUBLE(xname, xindex, left_reg, right_reg, shift_left, shift_right, mask, invert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_cs4231_info_double, \
  .get = snd_cs4231_get_double, .put = snd_cs4231_put_double, \
  .private_value = left_reg | (right_reg << 8) | (shift_left << 16) | (shift_right << 19) | (mask << 24) | (invert << 22) }

static struct snd_kcontrol_new snd_cs4231_controls[] __initdata = {
CS4231_DOUBLE("PCM Playback Switch", 0, CS4231_LEFT_OUTPUT, CS4231_RIGHT_OUTPUT, 7, 7, 1, 1),
CS4231_DOUBLE("PCM Playback Volume", 0, CS4231_LEFT_OUTPUT, CS4231_RIGHT_OUTPUT, 0, 0, 63, 1),
CS4231_DOUBLE("Line Playback Switch", 0, CS4231_LEFT_LINE_IN, CS4231_RIGHT_LINE_IN, 7, 7, 1, 1),
CS4231_DOUBLE("Line Playback Volume", 0, CS4231_LEFT_LINE_IN, CS4231_RIGHT_LINE_IN, 0, 0, 31, 1),
CS4231_DOUBLE("Aux Playback Switch", 0, CS4231_AUX1_LEFT_INPUT, CS4231_AUX1_RIGHT_INPUT, 7, 7, 1, 1),
CS4231_DOUBLE("Aux Playback Volume", 0, CS4231_AUX1_LEFT_INPUT, CS4231_AUX1_RIGHT_INPUT, 0, 0, 31, 1),
CS4231_DOUBLE("Aux Playback Switch", 1, CS4231_AUX2_LEFT_INPUT, CS4231_AUX2_RIGHT_INPUT, 7, 7, 1, 1),
CS4231_DOUBLE("Aux Playback Volume", 1, CS4231_AUX2_LEFT_INPUT, CS4231_AUX2_RIGHT_INPUT, 0, 0, 31, 1),
CS4231_SINGLE("Mono Playback Switch", 0, CS4231_MONO_CTRL, 7, 1, 1),
CS4231_SINGLE("Mono Playback Volume", 0, CS4231_MONO_CTRL, 0, 15, 1),
CS4231_SINGLE("Mono Output Playback Switch", 0, CS4231_MONO_CTRL, 6, 1, 1),
CS4231_SINGLE("Mono Output Playback Bypass", 0, CS4231_MONO_CTRL, 5, 1, 0),
CS4231_DOUBLE("Capture Volume", 0, CS4231_LEFT_INPUT, CS4231_RIGHT_INPUT, 0, 0, 15, 0),
{
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
	.name	= "Capture Source",
	.info	= snd_cs4231_info_mux,
	.get	= snd_cs4231_get_mux,
	.put	= snd_cs4231_put_mux,
},
CS4231_DOUBLE("Mic Boost", 0, CS4231_LEFT_INPUT, CS4231_RIGHT_INPUT, 5, 5, 1, 0),
CS4231_SINGLE("Loopback Capture Switch", 0, CS4231_LOOPBACK, 0, 1, 0),
CS4231_SINGLE("Loopback Capture Volume", 0, CS4231_LOOPBACK, 2, 63, 1),
/* SPARC specific uses of XCTL{0,1} general purpose outputs.  */
CS4231_SINGLE("Line Out Switch", 0, CS4231_PIN_CTRL, 6, 1, 1),
CS4231_SINGLE("Headphone Out Switch", 0, CS4231_PIN_CTRL, 7, 1, 1)
};
                                        
static int __init snd_cs4231_mixer(struct snd_cs4231 *chip)
{
	struct snd_card *card;
	int err, idx;

	snd_assert(chip != NULL && chip->pcm != NULL, return -EINVAL);

	card = chip->card;

	strcpy(card->mixername, chip->pcm->name);

	for (idx = 0; idx < ARRAY_SIZE(snd_cs4231_controls); idx++) {
		if ((err = snd_ctl_add(card,
				       snd_ctl_new1(&snd_cs4231_controls[idx],
						    chip))) < 0)
			return err;
	}
	return 0;
}

static int dev;

static int __init cs4231_attach_begin(struct snd_card **rcard)
{
	struct snd_card *card;

	*rcard = NULL;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	card = snd_card_new(index[dev], id[dev], THIS_MODULE, 0);
	if (card == NULL)
		return -ENOMEM;

	strcpy(card->driver, "CS4231");
	strcpy(card->shortname, "Sun CS4231");

	*rcard = card;
	return 0;
}

static int __init cs4231_attach_finish(struct snd_card *card, struct snd_cs4231 *chip)
{
	int err;

	if ((err = snd_cs4231_pcm(chip)) < 0)
		goto out_err;

	if ((err = snd_cs4231_mixer(chip)) < 0)
		goto out_err;

	if ((err = snd_cs4231_timer(chip)) < 0)
		goto out_err;

	if ((err = snd_card_register(card)) < 0)
		goto out_err;

	chip->next = cs4231_list;
	cs4231_list = chip;

	dev++;
	return 0;

out_err:
	snd_card_free(card);
	return err;
}

#ifdef SBUS_SUPPORT

static irqreturn_t snd_cs4231_sbus_interrupt(int irq, void *dev_id)
{
	unsigned long flags;
	unsigned char status;
	u32 csr;
	struct snd_cs4231 *chip = dev_id;

	/*This is IRQ is not raised by the cs4231*/
	if (!(__cs4231_readb(chip, CS4231P(chip, STATUS)) & CS4231_GLOBALIRQ))
		return IRQ_NONE;

	/* ACK the APC interrupt. */
	csr = sbus_readl(chip->port + APCCSR);

	sbus_writel(csr, chip->port + APCCSR);

	if ((csr & APC_PDMA_READY) && 
 	    (csr & APC_PLAY_INT) &&
	    (csr & APC_XINT_PNVA) &&
	    !(csr & APC_XINT_EMPT))
			snd_cs4231_play_callback(chip);

	if ((csr & APC_CDMA_READY) && 
  	    (csr & APC_CAPT_INT) &&
	    (csr & APC_XINT_CNVA) &&
	    !(csr & APC_XINT_EMPT))
			snd_cs4231_capture_callback(chip);
	
	status = snd_cs4231_in(chip, CS4231_IRQ_STATUS);

	if (status & CS4231_TIMER_IRQ) {
		if (chip->timer)
			snd_timer_interrupt(chip->timer, chip->timer->sticks);
	}		

	if ((status & CS4231_RECORD_IRQ) && (csr & APC_CDMA_READY))
		snd_cs4231_overrange(chip);

	/* ACK the CS4231 interrupt. */
	spin_lock_irqsave(&chip->lock, flags);
	snd_cs4231_outm(chip, CS4231_IRQ_STATUS, ~CS4231_ALL_IRQS | ~status, 0);
	spin_unlock_irqrestore(&chip->lock, flags);

	return IRQ_HANDLED;
}

/*
 * SBUS DMA routines
 */

static int sbus_dma_request(struct cs4231_dma_control *dma_cont, dma_addr_t bus_addr, size_t len)
{
	unsigned long flags;
	u32 test, csr;
	int err;
	struct sbus_dma_info *base = &dma_cont->sbus_info;
	
	if (len >= (1 << 24))
		return -EINVAL;
	spin_lock_irqsave(&base->lock, flags);
	csr = sbus_readl(base->regs + APCCSR);
	err = -EINVAL;
	test = APC_CDMA_READY;
	if ( base->dir == APC_PLAY )
		test = APC_PDMA_READY;
	if (!(csr & test))
		goto out;
	err = -EBUSY;
	test = APC_XINT_CNVA;
	if ( base->dir == APC_PLAY )
		test = APC_XINT_PNVA;
	if (!(csr & test))
		goto out;
	err = 0;
	sbus_writel(bus_addr, base->regs + base->dir + APCNVA);
	sbus_writel(len, base->regs + base->dir + APCNC);
out:
	spin_unlock_irqrestore(&base->lock, flags);
	return err;
}

static void sbus_dma_prepare(struct cs4231_dma_control *dma_cont, int d)
{
	unsigned long flags;
	u32 csr, test;
	struct sbus_dma_info *base = &dma_cont->sbus_info;

	spin_lock_irqsave(&base->lock, flags);
	csr = sbus_readl(base->regs + APCCSR);
	test =  APC_GENL_INT | APC_PLAY_INT | APC_XINT_ENA |
		APC_XINT_PLAY | APC_XINT_PEMP | APC_XINT_GENL |
		 APC_XINT_PENA;
	if ( base->dir == APC_RECORD )
		test = APC_GENL_INT | APC_CAPT_INT | APC_XINT_ENA |
			APC_XINT_CAPT | APC_XINT_CEMP | APC_XINT_GENL;
	csr |= test;
	sbus_writel(csr, base->regs + APCCSR);
	spin_unlock_irqrestore(&base->lock, flags);
}

static void sbus_dma_enable(struct cs4231_dma_control *dma_cont, int on)
{
	unsigned long flags;
	u32 csr, shift;
	struct sbus_dma_info *base = &dma_cont->sbus_info;

	spin_lock_irqsave(&base->lock, flags);
	if (!on) {
		sbus_writel(0, base->regs + base->dir + APCNC);
		sbus_writel(0, base->regs + base->dir + APCNVA);
		sbus_writel(0, base->regs + base->dir + APCC);
		sbus_writel(0, base->regs + base->dir + APCVA);

		/* ACK any APC interrupts. */
		csr = sbus_readl(base->regs + APCCSR);
		sbus_writel(csr, base->regs + APCCSR);
	} 
	udelay(1000);
	csr = sbus_readl(base->regs + APCCSR);
	shift = 0;
	if ( base->dir == APC_PLAY )
		shift = 1;
	if (on)
		csr &= ~(APC_CPAUSE << shift);
	else
		csr |= (APC_CPAUSE << shift); 
	sbus_writel(csr, base->regs + APCCSR);
	if (on)
		csr |= (APC_CDMA_READY << shift);
	else
		csr &= ~(APC_CDMA_READY << shift);
	sbus_writel(csr, base->regs + APCCSR);
	
	spin_unlock_irqrestore(&base->lock, flags);
}

static unsigned int sbus_dma_addr(struct cs4231_dma_control *dma_cont)
{
	struct sbus_dma_info *base = &dma_cont->sbus_info;

        return sbus_readl(base->regs + base->dir + APCVA);
}

static void sbus_dma_reset(struct snd_cs4231 *chip)
{
        sbus_writel(APC_CHIP_RESET, chip->port + APCCSR);
        sbus_writel(0x00, chip->port + APCCSR);
        sbus_writel(sbus_readl(chip->port + APCCSR) | APC_CDC_RESET,
		    chip->port + APCCSR);
  
        udelay(20);
  
        sbus_writel(sbus_readl(chip->port + APCCSR) & ~APC_CDC_RESET,
		    chip->port + APCCSR);
        sbus_writel(sbus_readl(chip->port + APCCSR) | (APC_XINT_ENA |
		       APC_XINT_PENA |
		       APC_XINT_CENA),
	               chip->port + APCCSR);
}

static void sbus_dma_preallocate(struct snd_cs4231 *chip, struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_SBUS,
					      snd_dma_sbus_data(chip->dev_u.sdev),
					      64*1024, 128*1024);
}

/*
 * Init and exit routines
 */

static int snd_cs4231_sbus_free(struct snd_cs4231 *chip)
{
	if (chip->irq[0])
		free_irq(chip->irq[0], chip);

	if (chip->port)
		sbus_iounmap(chip->port, chip->regs_size);

	kfree(chip);

	return 0;
}

static int snd_cs4231_sbus_dev_free(struct snd_device *device)
{
	struct snd_cs4231 *cp = device->device_data;

	return snd_cs4231_sbus_free(cp);
}

static struct snd_device_ops snd_cs4231_sbus_dev_ops = {
	.dev_free	=	snd_cs4231_sbus_dev_free,
};

static int __init snd_cs4231_sbus_create(struct snd_card *card,
					 struct sbus_dev *sdev,
					 int dev,
					 struct snd_cs4231 **rchip)
{
	struct snd_cs4231 *chip;
	int err;

	*rchip = NULL;
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	spin_lock_init(&chip->lock);
	spin_lock_init(&chip->c_dma.sbus_info.lock);
	spin_lock_init(&chip->p_dma.sbus_info.lock);
	mutex_init(&chip->mce_mutex);
	mutex_init(&chip->open_mutex);
	chip->card = card;
	chip->dev_u.sdev = sdev;
	chip->regs_size = sdev->reg_addrs[0].reg_size;
	memcpy(&chip->image, &snd_cs4231_original_image,
	       sizeof(snd_cs4231_original_image));

	chip->port = sbus_ioremap(&sdev->resource[0], 0,
				  chip->regs_size, "cs4231");
	if (!chip->port) {
		snd_printdd("cs4231-%d: Unable to map chip registers.\n", dev);
		return -EIO;
	}

	chip->c_dma.sbus_info.regs = chip->port;
	chip->p_dma.sbus_info.regs = chip->port;
	chip->c_dma.sbus_info.dir = APC_RECORD;
	chip->p_dma.sbus_info.dir = APC_PLAY;

	chip->p_dma.prepare = sbus_dma_prepare;
	chip->p_dma.enable = sbus_dma_enable;
	chip->p_dma.request = sbus_dma_request;
	chip->p_dma.address = sbus_dma_addr;
	chip->p_dma.reset = sbus_dma_reset;
	chip->p_dma.preallocate = sbus_dma_preallocate;

	chip->c_dma.prepare = sbus_dma_prepare;
	chip->c_dma.enable = sbus_dma_enable;
	chip->c_dma.request = sbus_dma_request;
	chip->c_dma.address = sbus_dma_addr;
	chip->c_dma.reset = sbus_dma_reset;
	chip->c_dma.preallocate = sbus_dma_preallocate;

	if (request_irq(sdev->irqs[0], snd_cs4231_sbus_interrupt,
			IRQF_SHARED, "cs4231", chip)) {
		snd_printdd("cs4231-%d: Unable to grab SBUS IRQ %d\n",
			    dev, sdev->irqs[0]);
		snd_cs4231_sbus_free(chip);
		return -EBUSY;
	}
	chip->irq[0] = sdev->irqs[0];

	if (snd_cs4231_probe(chip) < 0) {
		snd_cs4231_sbus_free(chip);
		return -ENODEV;
	}
	snd_cs4231_init(chip);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL,
				  chip, &snd_cs4231_sbus_dev_ops)) < 0) {
		snd_cs4231_sbus_free(chip);
		return err;
	}

	*rchip = chip;
	return 0;
}

static int __init cs4231_sbus_attach(struct sbus_dev *sdev)
{
	struct resource *rp = &sdev->resource[0];
	struct snd_cs4231 *cp;
	struct snd_card *card;
	int err;

	err = cs4231_attach_begin(&card);
	if (err)
		return err;

	sprintf(card->longname, "%s at 0x%02lx:0x%016Lx, irq %d",
		card->shortname,
		rp->flags & 0xffL,
		(unsigned long long)rp->start,
		sdev->irqs[0]);

	if ((err = snd_cs4231_sbus_create(card, sdev, dev, &cp)) < 0) {
		snd_card_free(card);
		return err;
	}

	return cs4231_attach_finish(card, cp);
}
#endif

#ifdef EBUS_SUPPORT

static void snd_cs4231_ebus_play_callback(struct ebus_dma_info *p, int event, void *cookie)
{
	struct snd_cs4231 *chip = cookie;
	
	snd_cs4231_play_callback(chip);
}

static void snd_cs4231_ebus_capture_callback(struct ebus_dma_info *p, int event, void *cookie)
{
	struct snd_cs4231 *chip = cookie;

	snd_cs4231_capture_callback(chip);
}

/*
 * EBUS DMA wrappers
 */

static int _ebus_dma_request(struct cs4231_dma_control *dma_cont, dma_addr_t bus_addr, size_t len)
{
	return ebus_dma_request(&dma_cont->ebus_info, bus_addr, len);
}

static void _ebus_dma_enable(struct cs4231_dma_control *dma_cont, int on)
{
	ebus_dma_enable(&dma_cont->ebus_info, on);
}

static void _ebus_dma_prepare(struct cs4231_dma_control *dma_cont, int dir)
{
	ebus_dma_prepare(&dma_cont->ebus_info, dir);
}

static unsigned int _ebus_dma_addr(struct cs4231_dma_control *dma_cont)
{
	return ebus_dma_addr(&dma_cont->ebus_info);
}

static void _ebus_dma_reset(struct snd_cs4231 *chip)
{
	return;
}

static void _ebus_dma_preallocate(struct snd_cs4231 *chip, struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
				      snd_dma_pci_data(chip->dev_u.pdev),
				      64*1024, 128*1024);
}

/*
 * Init and exit routines
 */

static int snd_cs4231_ebus_free(struct snd_cs4231 *chip)
{
	if (chip->c_dma.ebus_info.regs) {
		ebus_dma_unregister(&chip->c_dma.ebus_info);
		iounmap(chip->c_dma.ebus_info.regs);
	}
	if (chip->p_dma.ebus_info.regs) {
		ebus_dma_unregister(&chip->p_dma.ebus_info);
		iounmap(chip->p_dma.ebus_info.regs);
	}

	if (chip->port)
		iounmap(chip->port);

	kfree(chip);

	return 0;
}

static int snd_cs4231_ebus_dev_free(struct snd_device *device)
{
	struct snd_cs4231 *cp = device->device_data;

	return snd_cs4231_ebus_free(cp);
}

static struct snd_device_ops snd_cs4231_ebus_dev_ops = {
	.dev_free	=	snd_cs4231_ebus_dev_free,
};

static int __init snd_cs4231_ebus_create(struct snd_card *card,
					 struct linux_ebus_device *edev,
					 int dev,
					 struct snd_cs4231 **rchip)
{
	struct snd_cs4231 *chip;
	int err;

	*rchip = NULL;
	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	spin_lock_init(&chip->lock);
	spin_lock_init(&chip->c_dma.ebus_info.lock);
	spin_lock_init(&chip->p_dma.ebus_info.lock);
	mutex_init(&chip->mce_mutex);
	mutex_init(&chip->open_mutex);
	chip->flags |= CS4231_FLAG_EBUS;
	chip->card = card;
	chip->dev_u.pdev = edev->bus->self;
	memcpy(&chip->image, &snd_cs4231_original_image,
	       sizeof(snd_cs4231_original_image));
	strcpy(chip->c_dma.ebus_info.name, "cs4231(capture)");
	chip->c_dma.ebus_info.flags = EBUS_DMA_FLAG_USE_EBDMA_HANDLER;
	chip->c_dma.ebus_info.callback = snd_cs4231_ebus_capture_callback;
	chip->c_dma.ebus_info.client_cookie = chip;
	chip->c_dma.ebus_info.irq = edev->irqs[0];
	strcpy(chip->p_dma.ebus_info.name, "cs4231(play)");
	chip->p_dma.ebus_info.flags = EBUS_DMA_FLAG_USE_EBDMA_HANDLER;
	chip->p_dma.ebus_info.callback = snd_cs4231_ebus_play_callback;
	chip->p_dma.ebus_info.client_cookie = chip;
	chip->p_dma.ebus_info.irq = edev->irqs[1];

	chip->p_dma.prepare = _ebus_dma_prepare;
	chip->p_dma.enable = _ebus_dma_enable;
	chip->p_dma.request = _ebus_dma_request;
	chip->p_dma.address = _ebus_dma_addr;
	chip->p_dma.reset = _ebus_dma_reset;
	chip->p_dma.preallocate = _ebus_dma_preallocate;

	chip->c_dma.prepare = _ebus_dma_prepare;
	chip->c_dma.enable = _ebus_dma_enable;
	chip->c_dma.request = _ebus_dma_request;
	chip->c_dma.address = _ebus_dma_addr;
	chip->c_dma.reset = _ebus_dma_reset;
	chip->c_dma.preallocate = _ebus_dma_preallocate;

	chip->port = ioremap(edev->resource[0].start, 0x10);
	chip->p_dma.ebus_info.regs = ioremap(edev->resource[1].start, 0x10);
	chip->c_dma.ebus_info.regs = ioremap(edev->resource[2].start, 0x10);
	if (!chip->port || !chip->p_dma.ebus_info.regs || !chip->c_dma.ebus_info.regs) {
		snd_cs4231_ebus_free(chip);
		snd_printdd("cs4231-%d: Unable to map chip registers.\n", dev);
		return -EIO;
	}

	if (ebus_dma_register(&chip->c_dma.ebus_info)) {
		snd_cs4231_ebus_free(chip);
		snd_printdd("cs4231-%d: Unable to register EBUS capture DMA\n", dev);
		return -EBUSY;
	}
	if (ebus_dma_irq_enable(&chip->c_dma.ebus_info, 1)) {
		snd_cs4231_ebus_free(chip);
		snd_printdd("cs4231-%d: Unable to enable EBUS capture IRQ\n", dev);
		return -EBUSY;
	}

	if (ebus_dma_register(&chip->p_dma.ebus_info)) {
		snd_cs4231_ebus_free(chip);
		snd_printdd("cs4231-%d: Unable to register EBUS play DMA\n", dev);
		return -EBUSY;
	}
	if (ebus_dma_irq_enable(&chip->p_dma.ebus_info, 1)) {
		snd_cs4231_ebus_free(chip);
		snd_printdd("cs4231-%d: Unable to enable EBUS play IRQ\n", dev);
		return -EBUSY;
	}

	if (snd_cs4231_probe(chip) < 0) {
		snd_cs4231_ebus_free(chip);
		return -ENODEV;
	}
	snd_cs4231_init(chip);

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL,
				  chip, &snd_cs4231_ebus_dev_ops)) < 0) {
		snd_cs4231_ebus_free(chip);
		return err;
	}

	*rchip = chip;
	return 0;
}

static int __init cs4231_ebus_attach(struct linux_ebus_device *edev)
{
	struct snd_card *card;
	struct snd_cs4231 *chip;
	int err;

	err = cs4231_attach_begin(&card);
	if (err)
		return err;

	sprintf(card->longname, "%s at 0x%lx, irq %d",
		card->shortname,
		edev->resource[0].start,
		edev->irqs[0]);

	if ((err = snd_cs4231_ebus_create(card, edev, dev, &chip)) < 0) {
		snd_card_free(card);
		return err;
	}

	return cs4231_attach_finish(card, chip);
}
#endif

static int __init cs4231_init(void)
{
#ifdef SBUS_SUPPORT
	struct sbus_bus *sbus;
	struct sbus_dev *sdev;
#endif
#ifdef EBUS_SUPPORT
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev;
#endif
	int found;

	found = 0;

#ifdef SBUS_SUPPORT
	for_all_sbusdev(sdev, sbus) {
		if (!strcmp(sdev->prom_name, "SUNW,CS4231")) {
			if (cs4231_sbus_attach(sdev) == 0)
				found++;
		}
	}
#endif
#ifdef EBUS_SUPPORT
	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			int match = 0;

			if (!strcmp(edev->prom_node->name, "SUNW,CS4231")) {
				match = 1;
			} else if (!strcmp(edev->prom_node->name, "audio")) {
				const char *compat;

				compat = of_get_property(edev->prom_node,
							 "compatible", NULL);
				if (compat && !strcmp(compat, "SUNW,CS4231"))
					match = 1;
			}

			if (match &&
			    cs4231_ebus_attach(edev) == 0)
				found++;
		}
	}
#endif


	return (found > 0) ? 0 : -EIO;
}

static void __exit cs4231_exit(void)
{
	struct snd_cs4231 *p = cs4231_list;

	while (p != NULL) {
		struct snd_cs4231 *next = p->next;

		snd_card_free(p->card);

		p = next;
	}

	cs4231_list = NULL;
}

module_init(cs4231_init);
module_exit(cs4231_exit);
