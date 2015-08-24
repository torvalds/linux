/*
 *   ALSA driver for Intel ICH (i8x0) chipsets
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@perex.cz>
 *
 *
 *   This code also contains alpha support for SiS 735 chipsets provided
 *   by Mike Pieper <mptei@users.sourceforge.net>. We have no datasheet
 *   for SiS735, so the code is not fully functional.
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

 *
 */      

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/info.h>
#include <sound/initval.h>
/* for 440MX workaround */
#include <asm/pgtable.h>
#include <asm/cacheflush.h>

#ifdef CONFIG_KVM_GUEST
#include <linux/kvm_para.h>
#else
#define kvm_para_available() (0)
#endif

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Intel 82801AA,82901AB,i810,i820,i830,i840,i845,MX440; SiS 7012; Ali 5455");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{Intel,82801AA-ICH},"
		"{Intel,82901AB-ICH0},"
		"{Intel,82801BA-ICH2},"
		"{Intel,82801CA-ICH3},"
		"{Intel,82801DB-ICH4},"
		"{Intel,ICH5},"
		"{Intel,ICH6},"
		"{Intel,ICH7},"
		"{Intel,6300ESB},"
		"{Intel,ESB2},"
		"{Intel,MX440},"
		"{SiS,SI7012},"
		"{NVidia,nForce Audio},"
		"{NVidia,nForce2 Audio},"
		"{NVidia,nForce3 Audio},"
		"{NVidia,MCP04},"
		"{NVidia,MCP501},"
		"{NVidia,CK804},"
		"{NVidia,CK8},"
		"{NVidia,CK8S},"
		"{AMD,AMD768},"
		"{AMD,AMD8111},"
	        "{ALI,M5455}}");

static int index = SNDRV_DEFAULT_IDX1;	/* Index 0-MAX */
static char *id = SNDRV_DEFAULT_STR1;	/* ID for this card */
static int ac97_clock;
static char *ac97_quirk;
static bool buggy_semaphore;
static int buggy_irq = -1; /* auto-check */
static bool xbox;
static int spdif_aclink = -1;
static int inside_vm = -1;

module_param(index, int, 0444);
MODULE_PARM_DESC(index, "Index value for Intel i8x0 soundcard.");
module_param(id, charp, 0444);
MODULE_PARM_DESC(id, "ID string for Intel i8x0 soundcard.");
module_param(ac97_clock, int, 0444);
MODULE_PARM_DESC(ac97_clock, "AC'97 codec clock (0 = whitelist + auto-detect, 1 = force autodetect).");
module_param(ac97_quirk, charp, 0444);
MODULE_PARM_DESC(ac97_quirk, "AC'97 workaround for strange hardware.");
module_param(buggy_semaphore, bool, 0444);
MODULE_PARM_DESC(buggy_semaphore, "Enable workaround for hardwares with problematic codec semaphores.");
module_param(buggy_irq, bint, 0444);
MODULE_PARM_DESC(buggy_irq, "Enable workaround for buggy interrupts on some motherboards.");
module_param(xbox, bool, 0444);
MODULE_PARM_DESC(xbox, "Set to 1 for Xbox, if you have problems with the AC'97 codec detection.");
module_param(spdif_aclink, int, 0444);
MODULE_PARM_DESC(spdif_aclink, "S/PDIF over AC-link.");
module_param(inside_vm, bint, 0444);
MODULE_PARM_DESC(inside_vm, "KVM/Parallels optimization.");

/* just for backward compatibility */
static bool enable;
module_param(enable, bool, 0444);
static int joystick;
module_param(joystick, int, 0444);

/*
 *  Direct registers
 */
enum { DEVICE_INTEL, DEVICE_INTEL_ICH4, DEVICE_SIS, DEVICE_ALI, DEVICE_NFORCE };

#define ICHREG(x) ICH_REG_##x

#define DEFINE_REGSET(name,base) \
enum { \
	ICH_REG_##name##_BDBAR	= base + 0x0,	/* dword - buffer descriptor list base address */ \
	ICH_REG_##name##_CIV	= base + 0x04,	/* byte - current index value */ \
	ICH_REG_##name##_LVI	= base + 0x05,	/* byte - last valid index */ \
	ICH_REG_##name##_SR	= base + 0x06,	/* byte - status register */ \
	ICH_REG_##name##_PICB	= base + 0x08,	/* word - position in current buffer */ \
	ICH_REG_##name##_PIV	= base + 0x0a,	/* byte - prefetched index value */ \
	ICH_REG_##name##_CR	= base + 0x0b,	/* byte - control register */ \
};

/* busmaster blocks */
DEFINE_REGSET(OFF, 0);		/* offset */
DEFINE_REGSET(PI, 0x00);	/* PCM in */
DEFINE_REGSET(PO, 0x10);	/* PCM out */
DEFINE_REGSET(MC, 0x20);	/* Mic in */

/* ICH4 busmaster blocks */
DEFINE_REGSET(MC2, 0x40);	/* Mic in 2 */
DEFINE_REGSET(PI2, 0x50);	/* PCM in 2 */
DEFINE_REGSET(SP, 0x60);	/* SPDIF out */

/* values for each busmaster block */

/* LVI */
#define ICH_REG_LVI_MASK		0x1f

/* SR */
#define ICH_FIFOE			0x10	/* FIFO error */
#define ICH_BCIS			0x08	/* buffer completion interrupt status */
#define ICH_LVBCI			0x04	/* last valid buffer completion interrupt */
#define ICH_CELV			0x02	/* current equals last valid */
#define ICH_DCH				0x01	/* DMA controller halted */

/* PIV */
#define ICH_REG_PIV_MASK		0x1f	/* mask */

/* CR */
#define ICH_IOCE			0x10	/* interrupt on completion enable */
#define ICH_FEIE			0x08	/* fifo error interrupt enable */
#define ICH_LVBIE			0x04	/* last valid buffer interrupt enable */
#define ICH_RESETREGS			0x02	/* reset busmaster registers */
#define ICH_STARTBM			0x01	/* start busmaster operation */


/* global block */
#define ICH_REG_GLOB_CNT		0x2c	/* dword - global control */
#define   ICH_PCM_SPDIF_MASK	0xc0000000	/* s/pdif pcm slot mask (ICH4) */
#define   ICH_PCM_SPDIF_NONE	0x00000000	/* reserved - undefined */
#define   ICH_PCM_SPDIF_78	0x40000000	/* s/pdif pcm on slots 7&8 */
#define   ICH_PCM_SPDIF_69	0x80000000	/* s/pdif pcm on slots 6&9 */
#define   ICH_PCM_SPDIF_1011	0xc0000000	/* s/pdif pcm on slots 10&11 */
#define   ICH_PCM_20BIT		0x00400000	/* 20-bit samples (ICH4) */
#define   ICH_PCM_246_MASK	0x00300000	/* chan mask (not all chips) */
#define   ICH_PCM_8		0x00300000      /* 8 channels (not all chips) */
#define   ICH_PCM_6		0x00200000	/* 6 channels (not all chips) */
#define   ICH_PCM_4		0x00100000	/* 4 channels (not all chips) */
#define   ICH_PCM_2		0x00000000	/* 2 channels (stereo) */
#define   ICH_SIS_PCM_246_MASK	0x000000c0	/* 6 channels (SIS7012) */
#define   ICH_SIS_PCM_6		0x00000080	/* 6 channels (SIS7012) */
#define   ICH_SIS_PCM_4		0x00000040	/* 4 channels (SIS7012) */
#define   ICH_SIS_PCM_2		0x00000000	/* 2 channels (SIS7012) */
#define   ICH_TRIE		0x00000040	/* tertiary resume interrupt enable */
#define   ICH_SRIE		0x00000020	/* secondary resume interrupt enable */
#define   ICH_PRIE		0x00000010	/* primary resume interrupt enable */
#define   ICH_ACLINK		0x00000008	/* AClink shut off */
#define   ICH_AC97WARM		0x00000004	/* AC'97 warm reset */
#define   ICH_AC97COLD		0x00000002	/* AC'97 cold reset */
#define   ICH_GIE		0x00000001	/* GPI interrupt enable */
#define ICH_REG_GLOB_STA		0x30	/* dword - global status */
#define   ICH_TRI		0x20000000	/* ICH4: tertiary (AC_SDIN2) resume interrupt */
#define   ICH_TCR		0x10000000	/* ICH4: tertiary (AC_SDIN2) codec ready */
#define   ICH_BCS		0x08000000	/* ICH4: bit clock stopped */
#define   ICH_SPINT		0x04000000	/* ICH4: S/PDIF interrupt */
#define   ICH_P2INT		0x02000000	/* ICH4: PCM2-In interrupt */
#define   ICH_M2INT		0x01000000	/* ICH4: Mic2-In interrupt */
#define   ICH_SAMPLE_CAP	0x00c00000	/* ICH4: sample capability bits (RO) */
#define   ICH_SAMPLE_16_20	0x00400000	/* ICH4: 16- and 20-bit samples */
#define   ICH_MULTICHAN_CAP	0x00300000	/* ICH4: multi-channel capability bits (RO) */
#define   ICH_SIS_TRI		0x00080000	/* SIS: tertiary resume irq */
#define   ICH_SIS_TCR		0x00040000	/* SIS: tertiary codec ready */
#define   ICH_MD3		0x00020000	/* modem power down semaphore */
#define   ICH_AD3		0x00010000	/* audio power down semaphore */
#define   ICH_RCS		0x00008000	/* read completion status */
#define   ICH_BIT3		0x00004000	/* bit 3 slot 12 */
#define   ICH_BIT2		0x00002000	/* bit 2 slot 12 */
#define   ICH_BIT1		0x00001000	/* bit 1 slot 12 */
#define   ICH_SRI		0x00000800	/* secondary (AC_SDIN1) resume interrupt */
#define   ICH_PRI		0x00000400	/* primary (AC_SDIN0) resume interrupt */
#define   ICH_SCR		0x00000200	/* secondary (AC_SDIN1) codec ready */
#define   ICH_PCR		0x00000100	/* primary (AC_SDIN0) codec ready */
#define   ICH_MCINT		0x00000080	/* MIC capture interrupt */
#define   ICH_POINT		0x00000040	/* playback interrupt */
#define   ICH_PIINT		0x00000020	/* capture interrupt */
#define   ICH_NVSPINT		0x00000010	/* nforce spdif interrupt */
#define   ICH_MOINT		0x00000004	/* modem playback interrupt */
#define   ICH_MIINT		0x00000002	/* modem capture interrupt */
#define   ICH_GSCI		0x00000001	/* GPI status change interrupt */
#define ICH_REG_ACC_SEMA		0x34	/* byte - codec write semaphore */
#define   ICH_CAS		0x01		/* codec access semaphore */
#define ICH_REG_SDM		0x80
#define   ICH_DI2L_MASK		0x000000c0	/* PCM In 2, Mic In 2 data in line */
#define   ICH_DI2L_SHIFT	6
#define   ICH_DI1L_MASK		0x00000030	/* PCM In 1, Mic In 1 data in line */
#define   ICH_DI1L_SHIFT	4
#define   ICH_SE		0x00000008	/* steer enable */
#define   ICH_LDI_MASK		0x00000003	/* last codec read data input */

#define ICH_MAX_FRAGS		32		/* max hw frags */


/*
 * registers for Ali5455
 */

/* ALi 5455 busmaster blocks */
DEFINE_REGSET(AL_PI, 0x40);	/* ALi PCM in */
DEFINE_REGSET(AL_PO, 0x50);	/* Ali PCM out */
DEFINE_REGSET(AL_MC, 0x60);	/* Ali Mic in */
DEFINE_REGSET(AL_CDC_SPO, 0x70);	/* Ali Codec SPDIF out */
DEFINE_REGSET(AL_CENTER, 0x80);		/* Ali center out */
DEFINE_REGSET(AL_LFE, 0x90);		/* Ali center out */
DEFINE_REGSET(AL_CLR_SPI, 0xa0);	/* Ali Controller SPDIF in */
DEFINE_REGSET(AL_CLR_SPO, 0xb0);	/* Ali Controller SPDIF out */
DEFINE_REGSET(AL_I2S, 0xc0);	/* Ali I2S in */
DEFINE_REGSET(AL_PI2, 0xd0);	/* Ali PCM2 in */
DEFINE_REGSET(AL_MC2, 0xe0);	/* Ali Mic2 in */

enum {
	ICH_REG_ALI_SCR = 0x00,		/* System Control Register */
	ICH_REG_ALI_SSR = 0x04,		/* System Status Register  */
	ICH_REG_ALI_DMACR = 0x08,	/* DMA Control Register    */
	ICH_REG_ALI_FIFOCR1 = 0x0c,	/* FIFO Control Register 1  */
	ICH_REG_ALI_INTERFACECR = 0x10,	/* Interface Control Register */
	ICH_REG_ALI_INTERRUPTCR = 0x14,	/* Interrupt control Register */
	ICH_REG_ALI_INTERRUPTSR = 0x18,	/* Interrupt  Status Register */
	ICH_REG_ALI_FIFOCR2 = 0x1c,	/* FIFO Control Register 2   */
	ICH_REG_ALI_CPR = 0x20,		/* Command Port Register     */
	ICH_REG_ALI_CPR_ADDR = 0x22,	/* ac97 addr write */
	ICH_REG_ALI_SPR = 0x24,		/* Status Port Register      */
	ICH_REG_ALI_SPR_ADDR = 0x26,	/* ac97 addr read */
	ICH_REG_ALI_FIFOCR3 = 0x2c,	/* FIFO Control Register 3  */
	ICH_REG_ALI_TTSR = 0x30,	/* Transmit Tag Slot Register */
	ICH_REG_ALI_RTSR = 0x34,	/* Receive Tag Slot  Register */
	ICH_REG_ALI_CSPSR = 0x38,	/* Command/Status Port Status Register */
	ICH_REG_ALI_CAS = 0x3c,		/* Codec Write Semaphore Register */
	ICH_REG_ALI_HWVOL = 0xf0,	/* hardware volume control/status */
	ICH_REG_ALI_I2SCR = 0xf4,	/* I2S control/status */
	ICH_REG_ALI_SPDIFCSR = 0xf8,	/* spdif channel status register  */
	ICH_REG_ALI_SPDIFICS = 0xfc,	/* spdif interface control/status  */
};

#define ALI_CAS_SEM_BUSY	0x80000000
#define ALI_CPR_ADDR_SECONDARY	0x100
#define ALI_CPR_ADDR_READ	0x80
#define ALI_CSPSR_CODEC_READY	0x08
#define ALI_CSPSR_READ_OK	0x02
#define ALI_CSPSR_WRITE_OK	0x01

/* interrupts for the whole chip by interrupt status register finish */
 
#define ALI_INT_MICIN2		(1<<26)
#define ALI_INT_PCMIN2		(1<<25)
#define ALI_INT_I2SIN		(1<<24)
#define ALI_INT_SPDIFOUT	(1<<23)	/* controller spdif out INTERRUPT */
#define ALI_INT_SPDIFIN		(1<<22)
#define ALI_INT_LFEOUT		(1<<21)
#define ALI_INT_CENTEROUT	(1<<20)
#define ALI_INT_CODECSPDIFOUT	(1<<19)
#define ALI_INT_MICIN		(1<<18)
#define ALI_INT_PCMOUT		(1<<17)
#define ALI_INT_PCMIN		(1<<16)
#define ALI_INT_CPRAIS		(1<<7)	/* command port available */
#define ALI_INT_SPRAIS		(1<<5)	/* status port available */
#define ALI_INT_GPIO		(1<<1)
#define ALI_INT_MASK		(ALI_INT_SPDIFOUT|ALI_INT_CODECSPDIFOUT|\
				 ALI_INT_MICIN|ALI_INT_PCMOUT|ALI_INT_PCMIN)

#define ICH_ALI_SC_RESET	(1<<31)	/* master reset */
#define ICH_ALI_SC_AC97_DBL	(1<<30)
#define ICH_ALI_SC_CODEC_SPDF	(3<<20)	/* 1=7/8, 2=6/9, 3=10/11 */
#define ICH_ALI_SC_IN_BITS	(3<<18)
#define ICH_ALI_SC_OUT_BITS	(3<<16)
#define ICH_ALI_SC_6CH_CFG	(3<<14)
#define ICH_ALI_SC_PCM_4	(1<<8)
#define ICH_ALI_SC_PCM_6	(2<<8)
#define ICH_ALI_SC_PCM_246_MASK	(3<<8)

#define ICH_ALI_SS_SEC_ID	(3<<5)
#define ICH_ALI_SS_PRI_ID	(3<<3)

#define ICH_ALI_IF_AC97SP	(1<<21)
#define ICH_ALI_IF_MC		(1<<20)
#define ICH_ALI_IF_PI		(1<<19)
#define ICH_ALI_IF_MC2		(1<<18)
#define ICH_ALI_IF_PI2		(1<<17)
#define ICH_ALI_IF_LINE_SRC	(1<<15)	/* 0/1 = slot 3/6 */
#define ICH_ALI_IF_MIC_SRC	(1<<14)	/* 0/1 = slot 3/6 */
#define ICH_ALI_IF_SPDF_SRC	(3<<12)	/* 00 = PCM, 01 = AC97-in, 10 = spdif-in, 11 = i2s */
#define ICH_ALI_IF_AC97_OUT	(3<<8)	/* 00 = PCM, 10 = spdif-in, 11 = i2s */
#define ICH_ALI_IF_PO_SPDF	(1<<3)
#define ICH_ALI_IF_PO		(1<<1)

/*
 *  
 */

enum {
	ICHD_PCMIN,
	ICHD_PCMOUT,
	ICHD_MIC,
	ICHD_MIC2,
	ICHD_PCM2IN,
	ICHD_SPBAR,
	ICHD_LAST = ICHD_SPBAR
};
enum {
	NVD_PCMIN,
	NVD_PCMOUT,
	NVD_MIC,
	NVD_SPBAR,
	NVD_LAST = NVD_SPBAR
};
enum {
	ALID_PCMIN,
	ALID_PCMOUT,
	ALID_MIC,
	ALID_AC97SPDIFOUT,
	ALID_SPDIFIN,
	ALID_SPDIFOUT,
	ALID_LAST = ALID_SPDIFOUT
};

#define get_ichdev(substream) (substream->runtime->private_data)

struct ichdev {
	unsigned int ichd;			/* ich device number */
	unsigned long reg_offset;		/* offset to bmaddr */
	u32 *bdbar;				/* CPU address (32bit) */
	unsigned int bdbar_addr;		/* PCI bus address (32bit) */
	struct snd_pcm_substream *substream;
	unsigned int physbuf;			/* physical address (32bit) */
        unsigned int size;
        unsigned int fragsize;
        unsigned int fragsize1;
        unsigned int position;
	unsigned int pos_shift;
	unsigned int last_pos;
        int frags;
        int lvi;
        int lvi_frag;
	int civ;
	int ack;
	int ack_reload;
	unsigned int ack_bit;
	unsigned int roff_sr;
	unsigned int roff_picb;
	unsigned int int_sta_mask;		/* interrupt status mask */
	unsigned int ali_slot;			/* ALI DMA slot */
	struct ac97_pcm *pcm;
	int pcm_open_flag;
	unsigned int page_attr_changed: 1;
	unsigned int suspended: 1;
};

struct intel8x0 {
	unsigned int device_type;

	int irq;

	void __iomem *addr;
	void __iomem *bmaddr;

	struct pci_dev *pci;
	struct snd_card *card;

	int pcm_devs;
	struct snd_pcm *pcm[6];
	struct ichdev ichd[6];

	unsigned multi4: 1,
		 multi6: 1,
		 multi8 :1,
		 dra: 1,
		 smp20bit: 1;
	unsigned in_ac97_init: 1,
		 in_sdin_init: 1;
	unsigned in_measurement: 1;	/* during ac97 clock measurement */
	unsigned fix_nocache: 1; 	/* workaround for 440MX */
	unsigned buggy_irq: 1;		/* workaround for buggy mobos */
	unsigned xbox: 1;		/* workaround for Xbox AC'97 detection */
	unsigned buggy_semaphore: 1;	/* workaround for buggy codec semaphore */
	unsigned inside_vm: 1;		/* enable VM optimization */

	int spdif_idx;	/* SPDIF BAR index; *_SPBAR or -1 if use PCMOUT */
	unsigned int sdm_saved;	/* SDM reg value */

	struct snd_ac97_bus *ac97_bus;
	struct snd_ac97 *ac97[3];
	unsigned int ac97_sdin[3];
	unsigned int max_codecs, ncodecs;
	unsigned int *codec_bit;
	unsigned int codec_isr_bits;
	unsigned int codec_ready_bits;

	spinlock_t reg_lock;
	
	u32 bdbars_count;
	struct snd_dma_buffer bdbars;
	u32 int_sta_reg;		/* interrupt status register */
	u32 int_sta_mask;		/* interrupt status mask */
};

static const struct pci_device_id snd_intel8x0_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x2415), DEVICE_INTEL },	/* 82801AA */
	{ PCI_VDEVICE(INTEL, 0x2425), DEVICE_INTEL },	/* 82901AB */
	{ PCI_VDEVICE(INTEL, 0x2445), DEVICE_INTEL },	/* 82801BA */
	{ PCI_VDEVICE(INTEL, 0x2485), DEVICE_INTEL },	/* ICH3 */
	{ PCI_VDEVICE(INTEL, 0x24c5), DEVICE_INTEL_ICH4 }, /* ICH4 */
	{ PCI_VDEVICE(INTEL, 0x24d5), DEVICE_INTEL_ICH4 }, /* ICH5 */
	{ PCI_VDEVICE(INTEL, 0x25a6), DEVICE_INTEL_ICH4 }, /* ESB */
	{ PCI_VDEVICE(INTEL, 0x266e), DEVICE_INTEL_ICH4 }, /* ICH6 */
	{ PCI_VDEVICE(INTEL, 0x27de), DEVICE_INTEL_ICH4 }, /* ICH7 */
	{ PCI_VDEVICE(INTEL, 0x2698), DEVICE_INTEL_ICH4 }, /* ESB2 */
	{ PCI_VDEVICE(INTEL, 0x7195), DEVICE_INTEL },	/* 440MX */
	{ PCI_VDEVICE(SI, 0x7012), DEVICE_SIS },	/* SI7012 */
	{ PCI_VDEVICE(NVIDIA, 0x01b1), DEVICE_NFORCE },	/* NFORCE */
	{ PCI_VDEVICE(NVIDIA, 0x003a), DEVICE_NFORCE },	/* MCP04 */
	{ PCI_VDEVICE(NVIDIA, 0x006a), DEVICE_NFORCE },	/* NFORCE2 */
	{ PCI_VDEVICE(NVIDIA, 0x0059), DEVICE_NFORCE },	/* CK804 */
	{ PCI_VDEVICE(NVIDIA, 0x008a), DEVICE_NFORCE },	/* CK8 */
	{ PCI_VDEVICE(NVIDIA, 0x00da), DEVICE_NFORCE },	/* NFORCE3 */
	{ PCI_VDEVICE(NVIDIA, 0x00ea), DEVICE_NFORCE },	/* CK8S */
	{ PCI_VDEVICE(NVIDIA, 0x026b), DEVICE_NFORCE },	/* MCP51 */
	{ PCI_VDEVICE(AMD, 0x746d), DEVICE_INTEL },	/* AMD8111 */
	{ PCI_VDEVICE(AMD, 0x7445), DEVICE_INTEL },	/* AMD768 */
	{ PCI_VDEVICE(AL, 0x5455), DEVICE_ALI },   /* Ali5455 */
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, snd_intel8x0_ids);

/*
 *  Lowlevel I/O - busmaster
 */

static inline u8 igetbyte(struct intel8x0 *chip, u32 offset)
{
	return ioread8(chip->bmaddr + offset);
}

static inline u16 igetword(struct intel8x0 *chip, u32 offset)
{
	return ioread16(chip->bmaddr + offset);
}

static inline u32 igetdword(struct intel8x0 *chip, u32 offset)
{
	return ioread32(chip->bmaddr + offset);
}

static inline void iputbyte(struct intel8x0 *chip, u32 offset, u8 val)
{
	iowrite8(val, chip->bmaddr + offset);
}

static inline void iputword(struct intel8x0 *chip, u32 offset, u16 val)
{
	iowrite16(val, chip->bmaddr + offset);
}

static inline void iputdword(struct intel8x0 *chip, u32 offset, u32 val)
{
	iowrite32(val, chip->bmaddr + offset);
}

/*
 *  Lowlevel I/O - AC'97 registers
 */

static inline u16 iagetword(struct intel8x0 *chip, u32 offset)
{
	return ioread16(chip->addr + offset);
}

static inline void iaputword(struct intel8x0 *chip, u32 offset, u16 val)
{
	iowrite16(val, chip->addr + offset);
}

/*
 *  Basic I/O
 */

/*
 * access to AC97 codec via normal i/o (for ICH and SIS7012)
 */

static int snd_intel8x0_codec_semaphore(struct intel8x0 *chip, unsigned int codec)
{
	int time;
	
	if (codec > 2)
		return -EIO;
	if (chip->in_sdin_init) {
		/* we don't know the ready bit assignment at the moment */
		/* so we check any */
		codec = chip->codec_isr_bits;
	} else {
		codec = chip->codec_bit[chip->ac97_sdin[codec]];
	}

	/* codec ready ? */
	if ((igetdword(chip, ICHREG(GLOB_STA)) & codec) == 0)
		return -EIO;

	if (chip->buggy_semaphore)
		return 0; /* just ignore ... */

	/* Anyone holding a semaphore for 1 msec should be shot... */
	time = 100;
      	do {
      		if (!(igetbyte(chip, ICHREG(ACC_SEMA)) & ICH_CAS))
      			return 0;
		udelay(10);
	} while (time--);

	/* access to some forbidden (non existent) ac97 registers will not
	 * reset the semaphore. So even if you don't get the semaphore, still
	 * continue the access. We don't need the semaphore anyway. */
	dev_err(chip->card->dev,
		"codec_semaphore: semaphore is not ready [0x%x][0x%x]\n",
			igetbyte(chip, ICHREG(ACC_SEMA)), igetdword(chip, ICHREG(GLOB_STA)));
	iagetword(chip, 0);	/* clear semaphore flag */
	/* I don't care about the semaphore */
	return -EBUSY;
}
 
static void snd_intel8x0_codec_write(struct snd_ac97 *ac97,
				     unsigned short reg,
				     unsigned short val)
{
	struct intel8x0 *chip = ac97->private_data;
	
	if (snd_intel8x0_codec_semaphore(chip, ac97->num) < 0) {
		if (! chip->in_ac97_init)
			dev_err(chip->card->dev,
				"codec_write %d: semaphore is not ready for register 0x%x\n",
				ac97->num, reg);
	}
	iaputword(chip, reg + ac97->num * 0x80, val);
}

static unsigned short snd_intel8x0_codec_read(struct snd_ac97 *ac97,
					      unsigned short reg)
{
	struct intel8x0 *chip = ac97->private_data;
	unsigned short res;
	unsigned int tmp;

	if (snd_intel8x0_codec_semaphore(chip, ac97->num) < 0) {
		if (! chip->in_ac97_init)
			dev_err(chip->card->dev,
				"codec_read %d: semaphore is not ready for register 0x%x\n",
				ac97->num, reg);
		res = 0xffff;
	} else {
		res = iagetword(chip, reg + ac97->num * 0x80);
		if ((tmp = igetdword(chip, ICHREG(GLOB_STA))) & ICH_RCS) {
			/* reset RCS and preserve other R/WC bits */
			iputdword(chip, ICHREG(GLOB_STA), tmp &
				  ~(chip->codec_ready_bits | ICH_GSCI));
			if (! chip->in_ac97_init)
				dev_err(chip->card->dev,
					"codec_read %d: read timeout for register 0x%x\n",
					ac97->num, reg);
			res = 0xffff;
		}
	}
	return res;
}

static void snd_intel8x0_codec_read_test(struct intel8x0 *chip,
					 unsigned int codec)
{
	unsigned int tmp;

	if (snd_intel8x0_codec_semaphore(chip, codec) >= 0) {
		iagetword(chip, codec * 0x80);
		if ((tmp = igetdword(chip, ICHREG(GLOB_STA))) & ICH_RCS) {
			/* reset RCS and preserve other R/WC bits */
			iputdword(chip, ICHREG(GLOB_STA), tmp &
				  ~(chip->codec_ready_bits | ICH_GSCI));
		}
	}
}

/*
 * access to AC97 for Ali5455
 */
static int snd_intel8x0_ali_codec_ready(struct intel8x0 *chip, int mask)
{
	int count = 0;
	for (count = 0; count < 0x7f; count++) {
		int val = igetbyte(chip, ICHREG(ALI_CSPSR));
		if (val & mask)
			return 0;
	}
	if (! chip->in_ac97_init)
		dev_warn(chip->card->dev, "AC97 codec ready timeout.\n");
	return -EBUSY;
}

static int snd_intel8x0_ali_codec_semaphore(struct intel8x0 *chip)
{
	int time = 100;
	if (chip->buggy_semaphore)
		return 0; /* just ignore ... */
	while (--time && (igetdword(chip, ICHREG(ALI_CAS)) & ALI_CAS_SEM_BUSY))
		udelay(1);
	if (! time && ! chip->in_ac97_init)
		dev_warn(chip->card->dev, "ali_codec_semaphore timeout\n");
	return snd_intel8x0_ali_codec_ready(chip, ALI_CSPSR_CODEC_READY);
}

static unsigned short snd_intel8x0_ali_codec_read(struct snd_ac97 *ac97, unsigned short reg)
{
	struct intel8x0 *chip = ac97->private_data;
	unsigned short data = 0xffff;

	if (snd_intel8x0_ali_codec_semaphore(chip))
		goto __err;
	reg |= ALI_CPR_ADDR_READ;
	if (ac97->num)
		reg |= ALI_CPR_ADDR_SECONDARY;
	iputword(chip, ICHREG(ALI_CPR_ADDR), reg);
	if (snd_intel8x0_ali_codec_ready(chip, ALI_CSPSR_READ_OK))
		goto __err;
	data = igetword(chip, ICHREG(ALI_SPR));
 __err:
	return data;
}

static void snd_intel8x0_ali_codec_write(struct snd_ac97 *ac97, unsigned short reg,
					 unsigned short val)
{
	struct intel8x0 *chip = ac97->private_data;

	if (snd_intel8x0_ali_codec_semaphore(chip))
		return;
	iputword(chip, ICHREG(ALI_CPR), val);
	if (ac97->num)
		reg |= ALI_CPR_ADDR_SECONDARY;
	iputword(chip, ICHREG(ALI_CPR_ADDR), reg);
	snd_intel8x0_ali_codec_ready(chip, ALI_CSPSR_WRITE_OK);
}


/*
 * DMA I/O
 */
static void snd_intel8x0_setup_periods(struct intel8x0 *chip, struct ichdev *ichdev) 
{
	int idx;
	u32 *bdbar = ichdev->bdbar;
	unsigned long port = ichdev->reg_offset;

	iputdword(chip, port + ICH_REG_OFF_BDBAR, ichdev->bdbar_addr);
	if (ichdev->size == ichdev->fragsize) {
		ichdev->ack_reload = ichdev->ack = 2;
		ichdev->fragsize1 = ichdev->fragsize >> 1;
		for (idx = 0; idx < (ICH_REG_LVI_MASK + 1) * 2; idx += 4) {
			bdbar[idx + 0] = cpu_to_le32(ichdev->physbuf);
			bdbar[idx + 1] = cpu_to_le32(0x80000000 | /* interrupt on completion */
						     ichdev->fragsize1 >> ichdev->pos_shift);
			bdbar[idx + 2] = cpu_to_le32(ichdev->physbuf + (ichdev->size >> 1));
			bdbar[idx + 3] = cpu_to_le32(0x80000000 | /* interrupt on completion */
						     ichdev->fragsize1 >> ichdev->pos_shift);
		}
		ichdev->frags = 2;
	} else {
		ichdev->ack_reload = ichdev->ack = 1;
		ichdev->fragsize1 = ichdev->fragsize;
		for (idx = 0; idx < (ICH_REG_LVI_MASK + 1) * 2; idx += 2) {
			bdbar[idx + 0] = cpu_to_le32(ichdev->physbuf +
						     (((idx >> 1) * ichdev->fragsize) %
						      ichdev->size));
			bdbar[idx + 1] = cpu_to_le32(0x80000000 | /* interrupt on completion */
						     ichdev->fragsize >> ichdev->pos_shift);
#if 0
			dev_dbg(chip->card->dev, "bdbar[%i] = 0x%x [0x%x]\n",
			       idx + 0, bdbar[idx + 0], bdbar[idx + 1]);
#endif
		}
		ichdev->frags = ichdev->size / ichdev->fragsize;
	}
	iputbyte(chip, port + ICH_REG_OFF_LVI, ichdev->lvi = ICH_REG_LVI_MASK);
	ichdev->civ = 0;
	iputbyte(chip, port + ICH_REG_OFF_CIV, 0);
	ichdev->lvi_frag = ICH_REG_LVI_MASK % ichdev->frags;
	ichdev->position = 0;
#if 0
	dev_dbg(chip->card->dev,
		"lvi_frag = %i, frags = %i, period_size = 0x%x, period_size1 = 0x%x\n",
	       ichdev->lvi_frag, ichdev->frags, ichdev->fragsize,
	       ichdev->fragsize1);
#endif
	/* clear interrupts */
	iputbyte(chip, port + ichdev->roff_sr, ICH_FIFOE | ICH_BCIS | ICH_LVBCI);
}

#ifdef __i386__
/*
 * Intel 82443MX running a 100MHz processor system bus has a hardware bug,
 * which aborts PCI busmaster for audio transfer.  A workaround is to set
 * the pages as non-cached.  For details, see the errata in
 *	http://download.intel.com/design/chipsets/specupdt/24505108.pdf
 */
static void fill_nocache(void *buf, int size, int nocache)
{
	size = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (nocache)
		set_pages_uc(virt_to_page(buf), size);
	else
		set_pages_wb(virt_to_page(buf), size);
}
#else
#define fill_nocache(buf, size, nocache) do { ; } while (0)
#endif

/*
 *  Interrupt handler
 */

static inline void snd_intel8x0_update(struct intel8x0 *chip, struct ichdev *ichdev)
{
	unsigned long port = ichdev->reg_offset;
	unsigned long flags;
	int status, civ, i, step;
	int ack = 0;

	spin_lock_irqsave(&chip->reg_lock, flags);
	status = igetbyte(chip, port + ichdev->roff_sr);
	civ = igetbyte(chip, port + ICH_REG_OFF_CIV);
	if (!(status & ICH_BCIS)) {
		step = 0;
	} else if (civ == ichdev->civ) {
		// snd_printd("civ same %d\n", civ);
		step = 1;
		ichdev->civ++;
		ichdev->civ &= ICH_REG_LVI_MASK;
	} else {
		step = civ - ichdev->civ;
		if (step < 0)
			step += ICH_REG_LVI_MASK + 1;
		// if (step != 1)
		//	snd_printd("step = %d, %d -> %d\n", step, ichdev->civ, civ);
		ichdev->civ = civ;
	}

	ichdev->position += step * ichdev->fragsize1;
	if (! chip->in_measurement)
		ichdev->position %= ichdev->size;
	ichdev->lvi += step;
	ichdev->lvi &= ICH_REG_LVI_MASK;
	iputbyte(chip, port + ICH_REG_OFF_LVI, ichdev->lvi);
	for (i = 0; i < step; i++) {
		ichdev->lvi_frag++;
		ichdev->lvi_frag %= ichdev->frags;
		ichdev->bdbar[ichdev->lvi * 2] = cpu_to_le32(ichdev->physbuf + ichdev->lvi_frag * ichdev->fragsize1);
#if 0
	dev_dbg(chip->card->dev,
		"new: bdbar[%i] = 0x%x [0x%x], prefetch = %i, all = 0x%x, 0x%x\n",
	       ichdev->lvi * 2, ichdev->bdbar[ichdev->lvi * 2],
	       ichdev->bdbar[ichdev->lvi * 2 + 1], inb(ICH_REG_OFF_PIV + port),
	       inl(port + 4), inb(port + ICH_REG_OFF_CR));
#endif
		if (--ichdev->ack == 0) {
			ichdev->ack = ichdev->ack_reload;
			ack = 1;
		}
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (ack && ichdev->substream) {
		snd_pcm_period_elapsed(ichdev->substream);
	}
	iputbyte(chip, port + ichdev->roff_sr,
		 status & (ICH_FIFOE | ICH_BCIS | ICH_LVBCI));
}

static irqreturn_t snd_intel8x0_interrupt(int irq, void *dev_id)
{
	struct intel8x0 *chip = dev_id;
	struct ichdev *ichdev;
	unsigned int status;
	unsigned int i;

	status = igetdword(chip, chip->int_sta_reg);
	if (status == 0xffffffff)	/* we are not yet resumed */
		return IRQ_NONE;

	if ((status & chip->int_sta_mask) == 0) {
		if (status) {
			/* ack */
			iputdword(chip, chip->int_sta_reg, status);
			if (! chip->buggy_irq)
				status = 0;
		}
		return IRQ_RETVAL(status);
	}

	for (i = 0; i < chip->bdbars_count; i++) {
		ichdev = &chip->ichd[i];
		if (status & ichdev->int_sta_mask)
			snd_intel8x0_update(chip, ichdev);
	}

	/* ack them */
	iputdword(chip, chip->int_sta_reg, status & chip->int_sta_mask);
	
	return IRQ_HANDLED;
}

/*
 *  PCM part
 */

static int snd_intel8x0_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	struct ichdev *ichdev = get_ichdev(substream);
	unsigned char val = 0;
	unsigned long port = ichdev->reg_offset;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
		ichdev->suspended = 0;
		/* fallthru */
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		val = ICH_IOCE | ICH_STARTBM;
		ichdev->last_pos = ichdev->position;
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		ichdev->suspended = 1;
		/* fallthru */
	case SNDRV_PCM_TRIGGER_STOP:
		val = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		val = ICH_IOCE;
		break;
	default:
		return -EINVAL;
	}
	iputbyte(chip, port + ICH_REG_OFF_CR, val);
	if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		/* wait until DMA stopped */
		while (!(igetbyte(chip, port + ichdev->roff_sr) & ICH_DCH)) ;
		/* reset whole DMA things */
		iputbyte(chip, port + ICH_REG_OFF_CR, ICH_RESETREGS);
	}
	return 0;
}

static int snd_intel8x0_ali_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	struct ichdev *ichdev = get_ichdev(substream);
	unsigned long port = ichdev->reg_offset;
	static int fiforeg[] = {
		ICHREG(ALI_FIFOCR1), ICHREG(ALI_FIFOCR2), ICHREG(ALI_FIFOCR3)
	};
	unsigned int val, fifo;

	val = igetdword(chip, ICHREG(ALI_DMACR));
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_RESUME:
		ichdev->suspended = 0;
		/* fallthru */
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			/* clear FIFO for synchronization of channels */
			fifo = igetdword(chip, fiforeg[ichdev->ali_slot / 4]);
			fifo &= ~(0xff << (ichdev->ali_slot % 4));  
			fifo |= 0x83 << (ichdev->ali_slot % 4); 
			iputdword(chip, fiforeg[ichdev->ali_slot / 4], fifo);
		}
		iputbyte(chip, port + ICH_REG_OFF_CR, ICH_IOCE);
		val &= ~(1 << (ichdev->ali_slot + 16)); /* clear PAUSE flag */
		/* start DMA */
		iputdword(chip, ICHREG(ALI_DMACR), val | (1 << ichdev->ali_slot));
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		ichdev->suspended = 1;
		/* fallthru */
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		/* pause */
		iputdword(chip, ICHREG(ALI_DMACR), val | (1 << (ichdev->ali_slot + 16)));
		iputbyte(chip, port + ICH_REG_OFF_CR, 0);
		while (igetbyte(chip, port + ICH_REG_OFF_CR))
			;
		if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH)
			break;
		/* reset whole DMA things */
		iputbyte(chip, port + ICH_REG_OFF_CR, ICH_RESETREGS);
		/* clear interrupts */
		iputbyte(chip, port + ICH_REG_OFF_SR,
			 igetbyte(chip, port + ICH_REG_OFF_SR) | 0x1e);
		iputdword(chip, ICHREG(ALI_INTERRUPTSR),
			  igetdword(chip, ICHREG(ALI_INTERRUPTSR)) & ichdev->int_sta_mask);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int snd_intel8x0_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *hw_params)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	struct ichdev *ichdev = get_ichdev(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int dbl = params_rate(hw_params) > 48000;
	int err;

	if (chip->fix_nocache && ichdev->page_attr_changed) {
		fill_nocache(runtime->dma_area, runtime->dma_bytes, 0); /* clear */
		ichdev->page_attr_changed = 0;
	}
	err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	if (err < 0)
		return err;
	if (chip->fix_nocache) {
		if (runtime->dma_area && ! ichdev->page_attr_changed) {
			fill_nocache(runtime->dma_area, runtime->dma_bytes, 1);
			ichdev->page_attr_changed = 1;
		}
	}
	if (ichdev->pcm_open_flag) {
		snd_ac97_pcm_close(ichdev->pcm);
		ichdev->pcm_open_flag = 0;
	}
	err = snd_ac97_pcm_open(ichdev->pcm, params_rate(hw_params),
				params_channels(hw_params),
				ichdev->pcm->r[dbl].slots);
	if (err >= 0) {
		ichdev->pcm_open_flag = 1;
		/* Force SPDIF setting */
		if (ichdev->ichd == ICHD_PCMOUT && chip->spdif_idx < 0)
			snd_ac97_set_rate(ichdev->pcm->r[0].codec[0], AC97_SPDIF,
					  params_rate(hw_params));
	}
	return err;
}

static int snd_intel8x0_hw_free(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	struct ichdev *ichdev = get_ichdev(substream);

	if (ichdev->pcm_open_flag) {
		snd_ac97_pcm_close(ichdev->pcm);
		ichdev->pcm_open_flag = 0;
	}
	if (chip->fix_nocache && ichdev->page_attr_changed) {
		fill_nocache(substream->runtime->dma_area, substream->runtime->dma_bytes, 0);
		ichdev->page_attr_changed = 0;
	}
	return snd_pcm_lib_free_pages(substream);
}

static void snd_intel8x0_setup_pcm_out(struct intel8x0 *chip,
				       struct snd_pcm_runtime *runtime)
{
	unsigned int cnt;
	int dbl = runtime->rate > 48000;

	spin_lock_irq(&chip->reg_lock);
	switch (chip->device_type) {
	case DEVICE_ALI:
		cnt = igetdword(chip, ICHREG(ALI_SCR));
		cnt &= ~ICH_ALI_SC_PCM_246_MASK;
		if (runtime->channels == 4 || dbl)
			cnt |= ICH_ALI_SC_PCM_4;
		else if (runtime->channels == 6)
			cnt |= ICH_ALI_SC_PCM_6;
		iputdword(chip, ICHREG(ALI_SCR), cnt);
		break;
	case DEVICE_SIS:
		cnt = igetdword(chip, ICHREG(GLOB_CNT));
		cnt &= ~ICH_SIS_PCM_246_MASK;
		if (runtime->channels == 4 || dbl)
			cnt |= ICH_SIS_PCM_4;
		else if (runtime->channels == 6)
			cnt |= ICH_SIS_PCM_6;
		iputdword(chip, ICHREG(GLOB_CNT), cnt);
		break;
	default:
		cnt = igetdword(chip, ICHREG(GLOB_CNT));
		cnt &= ~(ICH_PCM_246_MASK | ICH_PCM_20BIT);
		if (runtime->channels == 4 || dbl)
			cnt |= ICH_PCM_4;
		else if (runtime->channels == 6)
			cnt |= ICH_PCM_6;
		else if (runtime->channels == 8)
			cnt |= ICH_PCM_8;
		if (chip->device_type == DEVICE_NFORCE) {
			/* reset to 2ch once to keep the 6 channel data in alignment,
			 * to start from Front Left always
			 */
			if (cnt & ICH_PCM_246_MASK) {
				iputdword(chip, ICHREG(GLOB_CNT), cnt & ~ICH_PCM_246_MASK);
				spin_unlock_irq(&chip->reg_lock);
				msleep(50); /* grrr... */
				spin_lock_irq(&chip->reg_lock);
			}
		} else if (chip->device_type == DEVICE_INTEL_ICH4) {
			if (runtime->sample_bits > 16)
				cnt |= ICH_PCM_20BIT;
		}
		iputdword(chip, ICHREG(GLOB_CNT), cnt);
		break;
	}
	spin_unlock_irq(&chip->reg_lock);
}

static int snd_intel8x0_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ichdev *ichdev = get_ichdev(substream);

	ichdev->physbuf = runtime->dma_addr;
	ichdev->size = snd_pcm_lib_buffer_bytes(substream);
	ichdev->fragsize = snd_pcm_lib_period_bytes(substream);
	if (ichdev->ichd == ICHD_PCMOUT) {
		snd_intel8x0_setup_pcm_out(chip, runtime);
		if (chip->device_type == DEVICE_INTEL_ICH4)
			ichdev->pos_shift = (runtime->sample_bits > 16) ? 2 : 1;
	}
	snd_intel8x0_setup_periods(chip, ichdev);
	return 0;
}

static snd_pcm_uframes_t snd_intel8x0_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	struct ichdev *ichdev = get_ichdev(substream);
	size_t ptr1, ptr;
	int civ, timeout = 10;
	unsigned int position;

	spin_lock(&chip->reg_lock);
	do {
		civ = igetbyte(chip, ichdev->reg_offset + ICH_REG_OFF_CIV);
		ptr1 = igetword(chip, ichdev->reg_offset + ichdev->roff_picb);
		position = ichdev->position;
		if (ptr1 == 0) {
			udelay(10);
			continue;
		}
		if (civ != igetbyte(chip, ichdev->reg_offset + ICH_REG_OFF_CIV))
			continue;

		/* IO read operation is very expensive inside virtual machine
		 * as it is emulated. The probability that subsequent PICB read
		 * will return different result is high enough to loop till
		 * timeout here.
		 * Same CIV is strict enough condition to be sure that PICB
		 * is valid inside VM on emulated card. */
		if (chip->inside_vm)
			break;
		if (ptr1 == igetword(chip, ichdev->reg_offset + ichdev->roff_picb))
			break;
	} while (timeout--);
	ptr = ichdev->last_pos;
	if (ptr1 != 0) {
		ptr1 <<= ichdev->pos_shift;
		ptr = ichdev->fragsize1 - ptr1;
		ptr += position;
		if (ptr < ichdev->last_pos) {
			unsigned int pos_base, last_base;
			pos_base = position / ichdev->fragsize1;
			last_base = ichdev->last_pos / ichdev->fragsize1;
			/* another sanity check; ptr1 can go back to full
			 * before the base position is updated
			 */
			if (pos_base == last_base)
				ptr = ichdev->last_pos;
		}
	}
	ichdev->last_pos = ptr;
	spin_unlock(&chip->reg_lock);
	if (ptr >= ichdev->size)
		return 0;
	return bytes_to_frames(substream->runtime, ptr);
}

static struct snd_pcm_hardware snd_intel8x0_stream =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_BLOCK_TRANSFER |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_PAUSE |
				 SNDRV_PCM_INFO_RESUME),
	.formats =		SNDRV_PCM_FMTBIT_S16_LE,
	.rates =		SNDRV_PCM_RATE_48000,
	.rate_min =		48000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	128 * 1024,
	.period_bytes_min =	32,
	.period_bytes_max =	128 * 1024,
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static unsigned int channels4[] = {
	2, 4,
};

static struct snd_pcm_hw_constraint_list hw_constraints_channels4 = {
	.count = ARRAY_SIZE(channels4),
	.list = channels4,
	.mask = 0,
};

static unsigned int channels6[] = {
	2, 4, 6,
};

static struct snd_pcm_hw_constraint_list hw_constraints_channels6 = {
	.count = ARRAY_SIZE(channels6),
	.list = channels6,
	.mask = 0,
};

static unsigned int channels8[] = {
	2, 4, 6, 8,
};

static struct snd_pcm_hw_constraint_list hw_constraints_channels8 = {
	.count = ARRAY_SIZE(channels8),
	.list = channels8,
	.mask = 0,
};

static int snd_intel8x0_pcm_open(struct snd_pcm_substream *substream, struct ichdev *ichdev)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	ichdev->substream = substream;
	runtime->hw = snd_intel8x0_stream;
	runtime->hw.rates = ichdev->pcm->rates;
	snd_pcm_limit_hw_rates(runtime);
	if (chip->device_type == DEVICE_SIS) {
		runtime->hw.buffer_bytes_max = 64*1024;
		runtime->hw.period_bytes_max = 64*1024;
	}
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	runtime->private_data = ichdev;
	return 0;
}

static int snd_intel8x0_playback_open(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	err = snd_intel8x0_pcm_open(substream, &chip->ichd[ICHD_PCMOUT]);
	if (err < 0)
		return err;

	if (chip->multi8) {
		runtime->hw.channels_max = 8;
		snd_pcm_hw_constraint_list(runtime, 0,
						SNDRV_PCM_HW_PARAM_CHANNELS,
						&hw_constraints_channels8);
	} else if (chip->multi6) {
		runtime->hw.channels_max = 6;
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
					   &hw_constraints_channels6);
	} else if (chip->multi4) {
		runtime->hw.channels_max = 4;
		snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
					   &hw_constraints_channels4);
	}
	if (chip->dra) {
		snd_ac97_pcm_double_rate_rules(runtime);
	}
	if (chip->smp20bit) {
		runtime->hw.formats |= SNDRV_PCM_FMTBIT_S32_LE;
		snd_pcm_hw_constraint_msbits(runtime, 0, 32, 20);
	}
	return 0;
}

static int snd_intel8x0_playback_close(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_PCMOUT].substream = NULL;
	return 0;
}

static int snd_intel8x0_capture_open(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ICHD_PCMIN]);
}

static int snd_intel8x0_capture_close(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_PCMIN].substream = NULL;
	return 0;
}

static int snd_intel8x0_mic_open(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ICHD_MIC]);
}

static int snd_intel8x0_mic_close(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_MIC].substream = NULL;
	return 0;
}

static int snd_intel8x0_mic2_open(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ICHD_MIC2]);
}

static int snd_intel8x0_mic2_close(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_MIC2].substream = NULL;
	return 0;
}

static int snd_intel8x0_capture2_open(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ICHD_PCM2IN]);
}

static int snd_intel8x0_capture2_close(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ICHD_PCM2IN].substream = NULL;
	return 0;
}

static int snd_intel8x0_spdif_open(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	int idx = chip->device_type == DEVICE_NFORCE ? NVD_SPBAR : ICHD_SPBAR;

	return snd_intel8x0_pcm_open(substream, &chip->ichd[idx]);
}

static int snd_intel8x0_spdif_close(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	int idx = chip->device_type == DEVICE_NFORCE ? NVD_SPBAR : ICHD_SPBAR;

	chip->ichd[idx].substream = NULL;
	return 0;
}

static int snd_intel8x0_ali_ac97spdifout_open(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	unsigned int val;

	spin_lock_irq(&chip->reg_lock);
	val = igetdword(chip, ICHREG(ALI_INTERFACECR));
	val |= ICH_ALI_IF_AC97SP;
	iputdword(chip, ICHREG(ALI_INTERFACECR), val);
	/* also needs to set ALI_SC_CODEC_SPDF correctly */
	spin_unlock_irq(&chip->reg_lock);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ALID_AC97SPDIFOUT]);
}

static int snd_intel8x0_ali_ac97spdifout_close(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);
	unsigned int val;

	chip->ichd[ALID_AC97SPDIFOUT].substream = NULL;
	spin_lock_irq(&chip->reg_lock);
	val = igetdword(chip, ICHREG(ALI_INTERFACECR));
	val &= ~ICH_ALI_IF_AC97SP;
	iputdword(chip, ICHREG(ALI_INTERFACECR), val);
	spin_unlock_irq(&chip->reg_lock);

	return 0;
}

#if 0 // NYI
static int snd_intel8x0_ali_spdifin_open(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ALID_SPDIFIN]);
}

static int snd_intel8x0_ali_spdifin_close(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ALID_SPDIFIN].substream = NULL;
	return 0;
}

static int snd_intel8x0_ali_spdifout_open(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	return snd_intel8x0_pcm_open(substream, &chip->ichd[ALID_SPDIFOUT]);
}

static int snd_intel8x0_ali_spdifout_close(struct snd_pcm_substream *substream)
{
	struct intel8x0 *chip = snd_pcm_substream_chip(substream);

	chip->ichd[ALID_SPDIFOUT].substream = NULL;
	return 0;
}
#endif

static struct snd_pcm_ops snd_intel8x0_playback_ops = {
	.open =		snd_intel8x0_playback_open,
	.close =	snd_intel8x0_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static struct snd_pcm_ops snd_intel8x0_capture_ops = {
	.open =		snd_intel8x0_capture_open,
	.close =	snd_intel8x0_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static struct snd_pcm_ops snd_intel8x0_capture_mic_ops = {
	.open =		snd_intel8x0_mic_open,
	.close =	snd_intel8x0_mic_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static struct snd_pcm_ops snd_intel8x0_capture_mic2_ops = {
	.open =		snd_intel8x0_mic2_open,
	.close =	snd_intel8x0_mic2_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static struct snd_pcm_ops snd_intel8x0_capture2_ops = {
	.open =		snd_intel8x0_capture2_open,
	.close =	snd_intel8x0_capture2_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static struct snd_pcm_ops snd_intel8x0_spdif_ops = {
	.open =		snd_intel8x0_spdif_open,
	.close =	snd_intel8x0_spdif_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static struct snd_pcm_ops snd_intel8x0_ali_playback_ops = {
	.open =		snd_intel8x0_playback_open,
	.close =	snd_intel8x0_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_ali_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static struct snd_pcm_ops snd_intel8x0_ali_capture_ops = {
	.open =		snd_intel8x0_capture_open,
	.close =	snd_intel8x0_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_ali_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static struct snd_pcm_ops snd_intel8x0_ali_capture_mic_ops = {
	.open =		snd_intel8x0_mic_open,
	.close =	snd_intel8x0_mic_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_ali_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static struct snd_pcm_ops snd_intel8x0_ali_ac97spdifout_ops = {
	.open =		snd_intel8x0_ali_ac97spdifout_open,
	.close =	snd_intel8x0_ali_ac97spdifout_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_ali_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

#if 0 // NYI
static struct snd_pcm_ops snd_intel8x0_ali_spdifin_ops = {
	.open =		snd_intel8x0_ali_spdifin_open,
	.close =	snd_intel8x0_ali_spdifin_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};

static struct snd_pcm_ops snd_intel8x0_ali_spdifout_ops = {
	.open =		snd_intel8x0_ali_spdifout_open,
	.close =	snd_intel8x0_ali_spdifout_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_intel8x0_hw_params,
	.hw_free =	snd_intel8x0_hw_free,
	.prepare =	snd_intel8x0_pcm_prepare,
	.trigger =	snd_intel8x0_pcm_trigger,
	.pointer =	snd_intel8x0_pcm_pointer,
};
#endif // NYI

struct ich_pcm_table {
	char *suffix;
	struct snd_pcm_ops *playback_ops;
	struct snd_pcm_ops *capture_ops;
	size_t prealloc_size;
	size_t prealloc_max_size;
	int ac97_idx;
};

static int snd_intel8x0_pcm1(struct intel8x0 *chip, int device,
			     struct ich_pcm_table *rec)
{
	struct snd_pcm *pcm;
	int err;
	char name[32];

	if (rec->suffix)
		sprintf(name, "Intel ICH - %s", rec->suffix);
	else
		strcpy(name, "Intel ICH");
	err = snd_pcm_new(chip->card, name, device,
			  rec->playback_ops ? 1 : 0,
			  rec->capture_ops ? 1 : 0, &pcm);
	if (err < 0)
		return err;

	if (rec->playback_ops)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, rec->playback_ops);
	if (rec->capture_ops)
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, rec->capture_ops);

	pcm->private_data = chip;
	pcm->info_flags = 0;
	if (rec->suffix)
		sprintf(pcm->name, "%s - %s", chip->card->shortname, rec->suffix);
	else
		strcpy(pcm->name, chip->card->shortname);
	chip->pcm[device] = pcm;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_pci_data(chip->pci),
					      rec->prealloc_size, rec->prealloc_max_size);

	if (rec->playback_ops &&
	    rec->playback_ops->open == snd_intel8x0_playback_open) {
		struct snd_pcm_chmap *chmap;
		int chs = 2;
		if (chip->multi8)
			chs = 8;
		else if (chip->multi6)
			chs = 6;
		else if (chip->multi4)
			chs = 4;
		err = snd_pcm_add_chmap_ctls(pcm, SNDRV_PCM_STREAM_PLAYBACK,
					     snd_pcm_alt_chmaps, chs, 0,
					     &chmap);
		if (err < 0)
			return err;
		chmap->channel_mask = SND_PCM_CHMAP_MASK_2468;
		chip->ac97[0]->chmaps[SNDRV_PCM_STREAM_PLAYBACK] = chmap;
	}

	return 0;
}

static struct ich_pcm_table intel_pcms[] = {
	{
		.playback_ops = &snd_intel8x0_playback_ops,
		.capture_ops = &snd_intel8x0_capture_ops,
		.prealloc_size = 64 * 1024,
		.prealloc_max_size = 128 * 1024,
	},
	{
		.suffix = "MIC ADC",
		.capture_ops = &snd_intel8x0_capture_mic_ops,
		.prealloc_size = 0,
		.prealloc_max_size = 128 * 1024,
		.ac97_idx = ICHD_MIC,
	},
	{
		.suffix = "MIC2 ADC",
		.capture_ops = &snd_intel8x0_capture_mic2_ops,
		.prealloc_size = 0,
		.prealloc_max_size = 128 * 1024,
		.ac97_idx = ICHD_MIC2,
	},
	{
		.suffix = "ADC2",
		.capture_ops = &snd_intel8x0_capture2_ops,
		.prealloc_size = 0,
		.prealloc_max_size = 128 * 1024,
		.ac97_idx = ICHD_PCM2IN,
	},
	{
		.suffix = "IEC958",
		.playback_ops = &snd_intel8x0_spdif_ops,
		.prealloc_size = 64 * 1024,
		.prealloc_max_size = 128 * 1024,
		.ac97_idx = ICHD_SPBAR,
	},
};

static struct ich_pcm_table nforce_pcms[] = {
	{
		.playback_ops = &snd_intel8x0_playback_ops,
		.capture_ops = &snd_intel8x0_capture_ops,
		.prealloc_size = 64 * 1024,
		.prealloc_max_size = 128 * 1024,
	},
	{
		.suffix = "MIC ADC",
		.capture_ops = &snd_intel8x0_capture_mic_ops,
		.prealloc_size = 0,
		.prealloc_max_size = 128 * 1024,
		.ac97_idx = NVD_MIC,
	},
	{
		.suffix = "IEC958",
		.playback_ops = &snd_intel8x0_spdif_ops,
		.prealloc_size = 64 * 1024,
		.prealloc_max_size = 128 * 1024,
		.ac97_idx = NVD_SPBAR,
	},
};

static struct ich_pcm_table ali_pcms[] = {
	{
		.playback_ops = &snd_intel8x0_ali_playback_ops,
		.capture_ops = &snd_intel8x0_ali_capture_ops,
		.prealloc_size = 64 * 1024,
		.prealloc_max_size = 128 * 1024,
	},
	{
		.suffix = "MIC ADC",
		.capture_ops = &snd_intel8x0_ali_capture_mic_ops,
		.prealloc_size = 0,
		.prealloc_max_size = 128 * 1024,
		.ac97_idx = ALID_MIC,
	},
	{
		.suffix = "IEC958",
		.playback_ops = &snd_intel8x0_ali_ac97spdifout_ops,
		/* .capture_ops = &snd_intel8x0_ali_spdifin_ops, */
		.prealloc_size = 64 * 1024,
		.prealloc_max_size = 128 * 1024,
		.ac97_idx = ALID_AC97SPDIFOUT,
	},
#if 0 // NYI
	{
		.suffix = "HW IEC958",
		.playback_ops = &snd_intel8x0_ali_spdifout_ops,
		.prealloc_size = 64 * 1024,
		.prealloc_max_size = 128 * 1024,
	},
#endif
};

static int snd_intel8x0_pcm(struct intel8x0 *chip)
{
	int i, tblsize, device, err;
	struct ich_pcm_table *tbl, *rec;

	switch (chip->device_type) {
	case DEVICE_INTEL_ICH4:
		tbl = intel_pcms;
		tblsize = ARRAY_SIZE(intel_pcms);
		if (spdif_aclink)
			tblsize--;
		break;
	case DEVICE_NFORCE:
		tbl = nforce_pcms;
		tblsize = ARRAY_SIZE(nforce_pcms);
		if (spdif_aclink)
			tblsize--;
		break;
	case DEVICE_ALI:
		tbl = ali_pcms;
		tblsize = ARRAY_SIZE(ali_pcms);
		break;
	default:
		tbl = intel_pcms;
		tblsize = 2;
		break;
	}

	device = 0;
	for (i = 0; i < tblsize; i++) {
		rec = tbl + i;
		if (i > 0 && rec->ac97_idx) {
			/* activate PCM only when associated AC'97 codec */
			if (! chip->ichd[rec->ac97_idx].pcm)
				continue;
		}
		err = snd_intel8x0_pcm1(chip, device, rec);
		if (err < 0)
			return err;
		device++;
	}

	chip->pcm_devs = device;
	return 0;
}
	

/*
 *  Mixer part
 */

static void snd_intel8x0_mixer_free_ac97_bus(struct snd_ac97_bus *bus)
{
	struct intel8x0 *chip = bus->private_data;
	chip->ac97_bus = NULL;
}

static void snd_intel8x0_mixer_free_ac97(struct snd_ac97 *ac97)
{
	struct intel8x0 *chip = ac97->private_data;
	chip->ac97[ac97->num] = NULL;
}

static struct ac97_pcm ac97_pcm_defs[] = {
	/* front PCM */
	{
		.exclusive = 1,
		.r = {	{
				.slots = (1 << AC97_SLOT_PCM_LEFT) |
					 (1 << AC97_SLOT_PCM_RIGHT) |
					 (1 << AC97_SLOT_PCM_CENTER) |
					 (1 << AC97_SLOT_PCM_SLEFT) |
					 (1 << AC97_SLOT_PCM_SRIGHT) |
					 (1 << AC97_SLOT_LFE)
			},
			{
				.slots = (1 << AC97_SLOT_PCM_LEFT) |
					 (1 << AC97_SLOT_PCM_RIGHT) |
					 (1 << AC97_SLOT_PCM_LEFT_0) |
					 (1 << AC97_SLOT_PCM_RIGHT_0)
			}
		}
	},
	/* PCM IN #1 */
	{
		.stream = 1,
		.exclusive = 1,
		.r = {	{
				.slots = (1 << AC97_SLOT_PCM_LEFT) |
					 (1 << AC97_SLOT_PCM_RIGHT)
			}
		}
	},
	/* MIC IN #1 */
	{
		.stream = 1,
		.exclusive = 1,
		.r = {	{
				.slots = (1 << AC97_SLOT_MIC)
			}
		}
	},
	/* S/PDIF PCM */
	{
		.exclusive = 1,
		.spdif = 1,
		.r = {	{
				.slots = (1 << AC97_SLOT_SPDIF_LEFT2) |
					 (1 << AC97_SLOT_SPDIF_RIGHT2)
			}
		}
	},
	/* PCM IN #2 */
	{
		.stream = 1,
		.exclusive = 1,
		.r = {	{
				.slots = (1 << AC97_SLOT_PCM_LEFT) |
					 (1 << AC97_SLOT_PCM_RIGHT)
			}
		}
	},
	/* MIC IN #2 */
	{
		.stream = 1,
		.exclusive = 1,
		.r = {	{
				.slots = (1 << AC97_SLOT_MIC)
			}
		}
	},
};

static const struct ac97_quirk ac97_quirks[] = {
        {
		.subvendor = 0x0e11,
		.subdevice = 0x000e,
		.name = "Compaq Deskpro EN",	/* AD1885 */
		.type = AC97_TUNE_HP_ONLY
        },
	{
		.subvendor = 0x0e11,
		.subdevice = 0x008a,
		.name = "Compaq Evo W4000",	/* AD1885 */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x0e11,
		.subdevice = 0x00b8,
		.name = "Compaq Evo D510C",
		.type = AC97_TUNE_HP_ONLY
	},
        {
		.subvendor = 0x0e11,
		.subdevice = 0x0860,
		.name = "HP/Compaq nx7010",
		.type = AC97_TUNE_MUTE_LED
        },
	{
		.subvendor = 0x1014,
		.subdevice = 0x0534,
		.name = "ThinkPad X31",
		.type = AC97_TUNE_INV_EAPD
	},
	{
		.subvendor = 0x1014,
		.subdevice = 0x1f00,
		.name = "MS-9128",
		.type = AC97_TUNE_ALC_JACK
	},
	{
		.subvendor = 0x1014,
		.subdevice = 0x0267,
		.name = "IBM NetVista A30p",	/* AD1981B */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1025,
		.subdevice = 0x0082,
		.name = "Acer Travelmate 2310",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1025,
		.subdevice = 0x0083,
		.name = "Acer Aspire 3003LCi",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x00d8,
		.name = "Dell Precision 530",	/* AD1885 */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x010d,
		.name = "Dell",	/* which model?  AD1885 */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x0126,
		.name = "Dell Optiplex GX260",	/* AD1981A */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x012c,
		.name = "Dell Precision 650",	/* AD1981A */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x012d,
		.name = "Dell Precision 450",	/* AD1981B*/
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x0147,
		.name = "Dell",	/* which model?  AD1981B*/
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x0151,
		.name = "Dell Optiplex GX270",  /* AD1981B */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x014e,
		.name = "Dell D800", /* STAC9750/51 */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x0163,
		.name = "Dell Unknown",	/* STAC9750/51 */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x016a,
		.name = "Dell Inspiron 8600",	/* STAC9750/51 */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x0182,
		.name = "Dell Latitude D610",	/* STAC9750/51 */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x0186,
		.name = "Dell Latitude D810", /* cf. Malone #41015 */
		.type = AC97_TUNE_HP_MUTE_LED
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x0188,
		.name = "Dell Inspiron 6000",
		.type = AC97_TUNE_HP_MUTE_LED /* cf. Malone #41015 */
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x0189,
		.name = "Dell Inspiron 9300",
		.type = AC97_TUNE_HP_MUTE_LED
	},
	{
		.subvendor = 0x1028,
		.subdevice = 0x0191,
		.name = "Dell Inspiron 8600",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x103c,
		.subdevice = 0x006d,
		.name = "HP zv5000",
		.type = AC97_TUNE_MUTE_LED	/*AD1981B*/
	},
	{	/* FIXME: which codec? */
		.subvendor = 0x103c,
		.subdevice = 0x00c3,
		.name = "HP xw6000",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x103c,
		.subdevice = 0x088c,
		.name = "HP nc8000",
		.type = AC97_TUNE_HP_MUTE_LED
	},
	{
		.subvendor = 0x103c,
		.subdevice = 0x0890,
		.name = "HP nc6000",
		.type = AC97_TUNE_MUTE_LED
	},
	{
		.subvendor = 0x103c,
		.subdevice = 0x129d,
		.name = "HP xw8000",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x103c,
		.subdevice = 0x0938,
		.name = "HP nc4200",
		.type = AC97_TUNE_HP_MUTE_LED
	},
	{
		.subvendor = 0x103c,
		.subdevice = 0x099c,
		.name = "HP nx6110/nc6120",
		.type = AC97_TUNE_HP_MUTE_LED
	},
	{
		.subvendor = 0x103c,
		.subdevice = 0x0944,
		.name = "HP nc6220",
		.type = AC97_TUNE_HP_MUTE_LED
	},
	{
		.subvendor = 0x103c,
		.subdevice = 0x0934,
		.name = "HP nc8220",
		.type = AC97_TUNE_HP_MUTE_LED
	},
	{
		.subvendor = 0x103c,
		.subdevice = 0x12f1,
		.name = "HP xw8200",	/* AD1981B*/
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x103c,
		.subdevice = 0x12f2,
		.name = "HP xw6200",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x103c,
		.subdevice = 0x3008,
		.name = "HP xw4200",	/* AD1981B*/
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x104d,
		.subdevice = 0x8144,
		.name = "Sony",
		.type = AC97_TUNE_INV_EAPD
	},
	{
		.subvendor = 0x104d,
		.subdevice = 0x8197,
		.name = "Sony S1XP",
		.type = AC97_TUNE_INV_EAPD
	},
	{
		.subvendor = 0x104d,
		.subdevice = 0x81c0,
		.name = "Sony VAIO VGN-T350P", /*AD1981B*/
		.type = AC97_TUNE_INV_EAPD
	},
	{
		.subvendor = 0x104d,
		.subdevice = 0x81c5,
		.name = "Sony VAIO VGN-B1VP", /*AD1981B*/
		.type = AC97_TUNE_INV_EAPD
	},
 	{
		.subvendor = 0x1043,
		.subdevice = 0x80f3,
		.name = "ASUS ICH5/AD1985",
		.type = AC97_TUNE_AD_SHARING
	},
	{
		.subvendor = 0x10cf,
		.subdevice = 0x11c3,
		.name = "Fujitsu-Siemens E4010",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x10cf,
		.subdevice = 0x1225,
		.name = "Fujitsu-Siemens T3010",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x10cf,
		.subdevice = 0x1253,
		.name = "Fujitsu S6210",	/* STAC9750/51 */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x10cf,
		.subdevice = 0x127d,
		.name = "Fujitsu Lifebook P7010",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x10cf,
		.subdevice = 0x127e,
		.name = "Fujitsu Lifebook C1211D",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x10cf,
		.subdevice = 0x12ec,
		.name = "Fujitsu-Siemens 4010",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x10cf,
		.subdevice = 0x12f2,
		.name = "Fujitsu-Siemens Celsius H320",
		.type = AC97_TUNE_SWAP_HP
	},
	{
		.subvendor = 0x10f1,
		.subdevice = 0x2665,
		.name = "Fujitsu-Siemens Celsius",	/* AD1981? */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x10f1,
		.subdevice = 0x2885,
		.name = "AMD64 Mobo",	/* ALC650 */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x10f1,
		.subdevice = 0x2895,
		.name = "Tyan Thunder K8WE",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x10f7,
		.subdevice = 0x834c,
		.name = "Panasonic CF-R4",
		.type = AC97_TUNE_HP_ONLY,
	},
	{
		.subvendor = 0x110a,
		.subdevice = 0x0056,
		.name = "Fujitsu-Siemens Scenic",	/* AD1981? */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x11d4,
		.subdevice = 0x5375,
		.name = "ADI AD1985 (discrete)",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x1462,
		.subdevice = 0x5470,
		.name = "MSI P4 ATX 645 Ultra",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x161f,
		.subdevice = 0x202f,
		.name = "Gateway M520",
		.type = AC97_TUNE_INV_EAPD
	},
	{
		.subvendor = 0x161f,
		.subdevice = 0x203a,
		.name = "Gateway 4525GZ",		/* AD1981B */
		.type = AC97_TUNE_INV_EAPD
	},
	{
		.subvendor = 0x1734,
		.subdevice = 0x0088,
		.name = "Fujitsu-Siemens D1522",	/* AD1981 */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x8086,
		.subdevice = 0x2000,
		.mask = 0xfff0,
		.name = "Intel ICH5/AD1985",
		.type = AC97_TUNE_AD_SHARING
	},
	{
		.subvendor = 0x8086,
		.subdevice = 0x4000,
		.mask = 0xfff0,
		.name = "Intel ICH5/AD1985",
		.type = AC97_TUNE_AD_SHARING
	},
	{
		.subvendor = 0x8086,
		.subdevice = 0x4856,
		.name = "Intel D845WN (82801BA)",
		.type = AC97_TUNE_SWAP_HP
	},
	{
		.subvendor = 0x8086,
		.subdevice = 0x4d44,
		.name = "Intel D850EMV2",	/* AD1885 */
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x8086,
		.subdevice = 0x4d56,
		.name = "Intel ICH/AD1885",
		.type = AC97_TUNE_HP_ONLY
	},
	{
		.subvendor = 0x8086,
		.subdevice = 0x6000,
		.mask = 0xfff0,
		.name = "Intel ICH5/AD1985",
		.type = AC97_TUNE_AD_SHARING
	},
	{
		.subvendor = 0x8086,
		.subdevice = 0xe000,
		.mask = 0xfff0,
		.name = "Intel ICH5/AD1985",
		.type = AC97_TUNE_AD_SHARING
	},
#if 0 /* FIXME: this seems wrong on most boards */
	{
		.subvendor = 0x8086,
		.subdevice = 0xa000,
		.mask = 0xfff0,
		.name = "Intel ICH5/AD1985",
		.type = AC97_TUNE_HP_ONLY
	},
#endif
	{ } /* terminator */
};

static int snd_intel8x0_mixer(struct intel8x0 *chip, int ac97_clock,
			      const char *quirk_override)
{
	struct snd_ac97_bus *pbus;
	struct snd_ac97_template ac97;
	int err;
	unsigned int i, codecs;
	unsigned int glob_sta = 0;
	struct snd_ac97_bus_ops *ops;
	static struct snd_ac97_bus_ops standard_bus_ops = {
		.write = snd_intel8x0_codec_write,
		.read = snd_intel8x0_codec_read,
	};
	static struct snd_ac97_bus_ops ali_bus_ops = {
		.write = snd_intel8x0_ali_codec_write,
		.read = snd_intel8x0_ali_codec_read,
	};

	chip->spdif_idx = -1; /* use PCMOUT (or disabled) */
	if (!spdif_aclink) {
		switch (chip->device_type) {
		case DEVICE_NFORCE:
			chip->spdif_idx = NVD_SPBAR;
			break;
		case DEVICE_ALI:
			chip->spdif_idx = ALID_AC97SPDIFOUT;
			break;
		case DEVICE_INTEL_ICH4:
			chip->spdif_idx = ICHD_SPBAR;
			break;
		}
	}

	chip->in_ac97_init = 1;
	
	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = chip;
	ac97.private_free = snd_intel8x0_mixer_free_ac97;
	ac97.scaps = AC97_SCAP_SKIP_MODEM | AC97_SCAP_POWER_SAVE;
	if (chip->xbox)
		ac97.scaps |= AC97_SCAP_DETECT_BY_VENDOR;
	if (chip->device_type != DEVICE_ALI) {
		glob_sta = igetdword(chip, ICHREG(GLOB_STA));
		ops = &standard_bus_ops;
		chip->in_sdin_init = 1;
		codecs = 0;
		for (i = 0; i < chip->max_codecs; i++) {
			if (! (glob_sta & chip->codec_bit[i]))
				continue;
			if (chip->device_type == DEVICE_INTEL_ICH4) {
				snd_intel8x0_codec_read_test(chip, codecs);
				chip->ac97_sdin[codecs] =
					igetbyte(chip, ICHREG(SDM)) & ICH_LDI_MASK;
				if (snd_BUG_ON(chip->ac97_sdin[codecs] >= 3))
					chip->ac97_sdin[codecs] = 0;
			} else
				chip->ac97_sdin[codecs] = i;
			codecs++;
		}
		chip->in_sdin_init = 0;
		if (! codecs)
			codecs = 1;
	} else {
		ops = &ali_bus_ops;
		codecs = 1;
		/* detect the secondary codec */
		for (i = 0; i < 100; i++) {
			unsigned int reg = igetdword(chip, ICHREG(ALI_RTSR));
			if (reg & 0x40) {
				codecs = 2;
				break;
			}
			iputdword(chip, ICHREG(ALI_RTSR), reg | 0x40);
			udelay(1);
		}
	}
	if ((err = snd_ac97_bus(chip->card, 0, ops, chip, &pbus)) < 0)
		goto __err;
	pbus->private_free = snd_intel8x0_mixer_free_ac97_bus;
	if (ac97_clock >= 8000 && ac97_clock <= 48000)
		pbus->clock = ac97_clock;
	/* FIXME: my test board doesn't work well with VRA... */
	if (chip->device_type == DEVICE_ALI)
		pbus->no_vra = 1;
	else
		pbus->dra = 1;
	chip->ac97_bus = pbus;
	chip->ncodecs = codecs;

	ac97.pci = chip->pci;
	for (i = 0; i < codecs; i++) {
		ac97.num = i;
		if ((err = snd_ac97_mixer(pbus, &ac97, &chip->ac97[i])) < 0) {
			if (err != -EACCES)
				dev_err(chip->card->dev,
					"Unable to initialize codec #%d\n", i);
			if (i == 0)
				goto __err;
		}
	}
	/* tune up the primary codec */
	snd_ac97_tune_hardware(chip->ac97[0], ac97_quirks, quirk_override);
	/* enable separate SDINs for ICH4 */
	if (chip->device_type == DEVICE_INTEL_ICH4)
		pbus->isdin = 1;
	/* find the available PCM streams */
	i = ARRAY_SIZE(ac97_pcm_defs);
	if (chip->device_type != DEVICE_INTEL_ICH4)
		i -= 2;		/* do not allocate PCM2IN and MIC2 */
	if (chip->spdif_idx < 0)
		i--;		/* do not allocate S/PDIF */
	err = snd_ac97_pcm_assign(pbus, i, ac97_pcm_defs);
	if (err < 0)
		goto __err;
	chip->ichd[ICHD_PCMOUT].pcm = &pbus->pcms[0];
	chip->ichd[ICHD_PCMIN].pcm = &pbus->pcms[1];
	chip->ichd[ICHD_MIC].pcm = &pbus->pcms[2];
	if (chip->spdif_idx >= 0)
		chip->ichd[chip->spdif_idx].pcm = &pbus->pcms[3];
	if (chip->device_type == DEVICE_INTEL_ICH4) {
		chip->ichd[ICHD_PCM2IN].pcm = &pbus->pcms[4];
		chip->ichd[ICHD_MIC2].pcm = &pbus->pcms[5];
	}
	/* enable separate SDINs for ICH4 */
	if (chip->device_type == DEVICE_INTEL_ICH4) {
		struct ac97_pcm *pcm = chip->ichd[ICHD_PCM2IN].pcm;
		u8 tmp = igetbyte(chip, ICHREG(SDM));
		tmp &= ~(ICH_DI2L_MASK|ICH_DI1L_MASK);
		if (pcm) {
			tmp |= ICH_SE;	/* steer enable for multiple SDINs */
			tmp |= chip->ac97_sdin[0] << ICH_DI1L_SHIFT;
			for (i = 1; i < 4; i++) {
				if (pcm->r[0].codec[i]) {
					tmp |= chip->ac97_sdin[pcm->r[0].codec[1]->num] << ICH_DI2L_SHIFT;
					break;
				}
			}
		} else {
			tmp &= ~ICH_SE; /* steer disable */
		}
		iputbyte(chip, ICHREG(SDM), tmp);
	}
	if (pbus->pcms[0].r[0].slots & (1 << AC97_SLOT_PCM_SLEFT)) {
		chip->multi4 = 1;
		if (pbus->pcms[0].r[0].slots & (1 << AC97_SLOT_LFE)) {
			chip->multi6 = 1;
			if (chip->ac97[0]->flags & AC97_HAS_8CH)
				chip->multi8 = 1;
		}
	}
	if (pbus->pcms[0].r[1].rslots[0]) {
		chip->dra = 1;
	}
	if (chip->device_type == DEVICE_INTEL_ICH4) {
		if ((igetdword(chip, ICHREG(GLOB_STA)) & ICH_SAMPLE_CAP) == ICH_SAMPLE_16_20)
			chip->smp20bit = 1;
	}
	if (chip->device_type == DEVICE_NFORCE && !spdif_aclink) {
		/* 48kHz only */
		chip->ichd[chip->spdif_idx].pcm->rates = SNDRV_PCM_RATE_48000;
	}
	if (chip->device_type == DEVICE_INTEL_ICH4 && !spdif_aclink) {
		/* use slot 10/11 for SPDIF */
		u32 val;
		val = igetdword(chip, ICHREG(GLOB_CNT)) & ~ICH_PCM_SPDIF_MASK;
		val |= ICH_PCM_SPDIF_1011;
		iputdword(chip, ICHREG(GLOB_CNT), val);
		snd_ac97_update_bits(chip->ac97[0], AC97_EXTENDED_STATUS, 0x03 << 4, 0x03 << 4);
	}
	chip->in_ac97_init = 0;
	return 0;

 __err:
	/* clear the cold-reset bit for the next chance */
	if (chip->device_type != DEVICE_ALI)
		iputdword(chip, ICHREG(GLOB_CNT),
			  igetdword(chip, ICHREG(GLOB_CNT)) & ~ICH_AC97COLD);
	return err;
}


/*
 *
 */

static void do_ali_reset(struct intel8x0 *chip)
{
	iputdword(chip, ICHREG(ALI_SCR), ICH_ALI_SC_RESET);
	iputdword(chip, ICHREG(ALI_FIFOCR1), 0x83838383);
	iputdword(chip, ICHREG(ALI_FIFOCR2), 0x83838383);
	iputdword(chip, ICHREG(ALI_FIFOCR3), 0x83838383);
	iputdword(chip, ICHREG(ALI_INTERFACECR),
		  ICH_ALI_IF_PI|ICH_ALI_IF_PO);
	iputdword(chip, ICHREG(ALI_INTERRUPTCR), 0x00000000);
	iputdword(chip, ICHREG(ALI_INTERRUPTSR), 0x00000000);
}

#ifdef CONFIG_SND_AC97_POWER_SAVE
static struct snd_pci_quirk ich_chip_reset_mode[] = {
	SND_PCI_QUIRK(0x1014, 0x051f, "Thinkpad R32", 1),
	{ } /* end */
};

static int snd_intel8x0_ich_chip_cold_reset(struct intel8x0 *chip)
{
	unsigned int cnt;
	/* ACLink on, 2 channels */

	if (snd_pci_quirk_lookup(chip->pci, ich_chip_reset_mode))
		return -EIO;

	cnt = igetdword(chip, ICHREG(GLOB_CNT));
	cnt &= ~(ICH_ACLINK | ICH_PCM_246_MASK);

	/* do cold reset - the full ac97 powerdown may leave the controller
	 * in a warm state but actually it cannot communicate with the codec.
	 */
	iputdword(chip, ICHREG(GLOB_CNT), cnt & ~ICH_AC97COLD);
	cnt = igetdword(chip, ICHREG(GLOB_CNT));
	udelay(10);
	iputdword(chip, ICHREG(GLOB_CNT), cnt | ICH_AC97COLD);
	msleep(1);
	return 0;
}
#define snd_intel8x0_ich_chip_can_cold_reset(chip) \
	(!snd_pci_quirk_lookup(chip->pci, ich_chip_reset_mode))
#else
#define snd_intel8x0_ich_chip_cold_reset(chip)	0
#define snd_intel8x0_ich_chip_can_cold_reset(chip) (0)
#endif

static int snd_intel8x0_ich_chip_reset(struct intel8x0 *chip)
{
	unsigned long end_time;
	unsigned int cnt;
	/* ACLink on, 2 channels */
	cnt = igetdword(chip, ICHREG(GLOB_CNT));
	cnt &= ~(ICH_ACLINK | ICH_PCM_246_MASK);
	/* finish cold or do warm reset */
	cnt |= (cnt & ICH_AC97COLD) == 0 ? ICH_AC97COLD : ICH_AC97WARM;
	iputdword(chip, ICHREG(GLOB_CNT), cnt);
	end_time = (jiffies + (HZ / 4)) + 1;
	do {
		if ((igetdword(chip, ICHREG(GLOB_CNT)) & ICH_AC97WARM) == 0)
			return 0;
		schedule_timeout_uninterruptible(1);
	} while (time_after_eq(end_time, jiffies));
	dev_err(chip->card->dev, "AC'97 warm reset still in progress? [0x%x]\n",
		   igetdword(chip, ICHREG(GLOB_CNT)));
	return -EIO;
}

static int snd_intel8x0_ich_chip_init(struct intel8x0 *chip, int probing)
{
	unsigned long end_time;
	unsigned int status, nstatus;
	unsigned int cnt;
	int err;

	/* put logic to right state */
	/* first clear status bits */
	status = ICH_RCS | ICH_MCINT | ICH_POINT | ICH_PIINT;
	if (chip->device_type == DEVICE_NFORCE)
		status |= ICH_NVSPINT;
	cnt = igetdword(chip, ICHREG(GLOB_STA));
	iputdword(chip, ICHREG(GLOB_STA), cnt & status);

	if (snd_intel8x0_ich_chip_can_cold_reset(chip))
		err = snd_intel8x0_ich_chip_cold_reset(chip);
	else
		err = snd_intel8x0_ich_chip_reset(chip);
	if (err < 0)
		return err;

	if (probing) {
		/* wait for any codec ready status.
		 * Once it becomes ready it should remain ready
		 * as long as we do not disable the ac97 link.
		 */
		end_time = jiffies + HZ;
		do {
			status = igetdword(chip, ICHREG(GLOB_STA)) &
				chip->codec_isr_bits;
			if (status)
				break;
			schedule_timeout_uninterruptible(1);
		} while (time_after_eq(end_time, jiffies));
		if (! status) {
			/* no codec is found */
			dev_err(chip->card->dev,
				"codec_ready: codec is not ready [0x%x]\n",
				   igetdword(chip, ICHREG(GLOB_STA)));
			return -EIO;
		}

		/* wait for other codecs ready status. */
		end_time = jiffies + HZ / 4;
		while (status != chip->codec_isr_bits &&
		       time_after_eq(end_time, jiffies)) {
			schedule_timeout_uninterruptible(1);
			status |= igetdword(chip, ICHREG(GLOB_STA)) &
				chip->codec_isr_bits;
		}

	} else {
		/* resume phase */
		int i;
		status = 0;
		for (i = 0; i < chip->ncodecs; i++)
			if (chip->ac97[i])
				status |= chip->codec_bit[chip->ac97_sdin[i]];
		/* wait until all the probed codecs are ready */
		end_time = jiffies + HZ;
		do {
			nstatus = igetdword(chip, ICHREG(GLOB_STA)) &
				chip->codec_isr_bits;
			if (status == nstatus)
				break;
			schedule_timeout_uninterruptible(1);
		} while (time_after_eq(end_time, jiffies));
	}

	if (chip->device_type == DEVICE_SIS) {
		/* unmute the output on SIS7012 */
		iputword(chip, 0x4c, igetword(chip, 0x4c) | 1);
	}
	if (chip->device_type == DEVICE_NFORCE && !spdif_aclink) {
		/* enable SPDIF interrupt */
		unsigned int val;
		pci_read_config_dword(chip->pci, 0x4c, &val);
		val |= 0x1000000;
		pci_write_config_dword(chip->pci, 0x4c, val);
	}
      	return 0;
}

static int snd_intel8x0_ali_chip_init(struct intel8x0 *chip, int probing)
{
	u32 reg;
	int i = 0;

	reg = igetdword(chip, ICHREG(ALI_SCR));
	if ((reg & 2) == 0)	/* Cold required */
		reg |= 2;
	else
		reg |= 1;	/* Warm */
	reg &= ~0x80000000;	/* ACLink on */
	iputdword(chip, ICHREG(ALI_SCR), reg);

	for (i = 0; i < HZ / 2; i++) {
		if (! (igetdword(chip, ICHREG(ALI_INTERRUPTSR)) & ALI_INT_GPIO))
			goto __ok;
		schedule_timeout_uninterruptible(1);
	}
	dev_err(chip->card->dev, "AC'97 reset failed.\n");
	if (probing)
		return -EIO;

 __ok:
	for (i = 0; i < HZ / 2; i++) {
		reg = igetdword(chip, ICHREG(ALI_RTSR));
		if (reg & 0x80) /* primary codec */
			break;
		iputdword(chip, ICHREG(ALI_RTSR), reg | 0x80);
		schedule_timeout_uninterruptible(1);
	}

	do_ali_reset(chip);
	return 0;
}

static int snd_intel8x0_chip_init(struct intel8x0 *chip, int probing)
{
	unsigned int i, timeout;
	int err;
	
	if (chip->device_type != DEVICE_ALI) {
		if ((err = snd_intel8x0_ich_chip_init(chip, probing)) < 0)
			return err;
		iagetword(chip, 0);	/* clear semaphore flag */
	} else {
		if ((err = snd_intel8x0_ali_chip_init(chip, probing)) < 0)
			return err;
	}

	/* disable interrupts */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, 0x00);
	/* reset channels */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, ICH_RESETREGS);
	for (i = 0; i < chip->bdbars_count; i++) {
	        timeout = 100000;
	        while (--timeout != 0) {
        		if ((igetbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset) & ICH_RESETREGS) == 0)
        		        break;
                }
                if (timeout == 0)
			dev_err(chip->card->dev, "reset of registers failed?\n");
        }
	/* initialize Buffer Descriptor Lists */
	for (i = 0; i < chip->bdbars_count; i++)
		iputdword(chip, ICH_REG_OFF_BDBAR + chip->ichd[i].reg_offset,
			  chip->ichd[i].bdbar_addr);
	return 0;
}

static int snd_intel8x0_free(struct intel8x0 *chip)
{
	unsigned int i;

	if (chip->irq < 0)
		goto __hw_end;
	/* disable interrupts */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, 0x00);
	/* reset channels */
	for (i = 0; i < chip->bdbars_count; i++)
		iputbyte(chip, ICH_REG_OFF_CR + chip->ichd[i].reg_offset, ICH_RESETREGS);
	if (chip->device_type == DEVICE_NFORCE && !spdif_aclink) {
		/* stop the spdif interrupt */
		unsigned int val;
		pci_read_config_dword(chip->pci, 0x4c, &val);
		val &= ~0x1000000;
		pci_write_config_dword(chip->pci, 0x4c, val);
	}
	/* --- */

      __hw_end:
	if (chip->irq >= 0)
		free_irq(chip->irq, chip);
	if (chip->bdbars.area) {
		if (chip->fix_nocache)
			fill_nocache(chip->bdbars.area, chip->bdbars.bytes, 0);
		snd_dma_free_pages(&chip->bdbars);
	}
	if (chip->addr)
		pci_iounmap(chip->pci, chip->addr);
	if (chip->bmaddr)
		pci_iounmap(chip->pci, chip->bmaddr);
	pci_release_regions(chip->pci);
	pci_disable_device(chip->pci);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * power management
 */
static int intel8x0_suspend(struct device *dev)
{
	struct snd_card *card = dev_get_drvdata(dev);
	struct intel8x0 *chip = card->private_data;
	int i;

	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);
	for (i = 0; i < chip->pcm_devs; i++)
		snd_pcm_suspend_all(chip->pcm[i]);
	/* clear nocache */
	if (chip->fix_nocache) {
		for (i = 0; i < chip->bdbars_count; i++) {
			struct ichdev *ichdev = &chip->ichd[i];
			if (ichdev->substream && ichdev->page_attr_changed) {
				struct snd_pcm_runtime *runtime = ichdev->substream->runtime;
				if (runtime->dma_area)
					fill_nocache(runtime->dma_area, runtime->dma_bytes, 0);
			}
		}
	}
	for (i = 0; i < chip->ncodecs; i++)
		snd_ac97_suspend(chip->ac97[i]);
	if (chip->device_type == DEVICE_INTEL_ICH4)
		chip->sdm_saved = igetbyte(chip, ICHREG(SDM));

	if (chip->irq >= 0) {
		free_irq(chip->irq, chip);
		chip->irq = -1;
	}
	return 0;
}

static int intel8x0_resume(struct device *dev)
{
	struct pci_dev *pci = to_pci_dev(dev);
	struct snd_card *card = dev_get_drvdata(dev);
	struct intel8x0 *chip = card->private_data;
	int i;

	snd_intel8x0_chip_init(chip, 0);
	if (request_irq(pci->irq, snd_intel8x0_interrupt,
			IRQF_SHARED, KBUILD_MODNAME, chip)) {
		dev_err(dev, "unable to grab IRQ %d, disabling device\n",
			pci->irq);
		snd_card_disconnect(card);
		return -EIO;
	}
	chip->irq = pci->irq;
	synchronize_irq(chip->irq);

	/* re-initialize mixer stuff */
	if (chip->device_type == DEVICE_INTEL_ICH4 && !spdif_aclink) {
		/* enable separate SDINs for ICH4 */
		iputbyte(chip, ICHREG(SDM), chip->sdm_saved);
		/* use slot 10/11 for SPDIF */
		iputdword(chip, ICHREG(GLOB_CNT),
			  (igetdword(chip, ICHREG(GLOB_CNT)) & ~ICH_PCM_SPDIF_MASK) |
			  ICH_PCM_SPDIF_1011);
	}

	/* refill nocache */
	if (chip->fix_nocache)
		fill_nocache(chip->bdbars.area, chip->bdbars.bytes, 1);

	for (i = 0; i < chip->ncodecs; i++)
		snd_ac97_resume(chip->ac97[i]);

	/* refill nocache */
	if (chip->fix_nocache) {
		for (i = 0; i < chip->bdbars_count; i++) {
			struct ichdev *ichdev = &chip->ichd[i];
			if (ichdev->substream && ichdev->page_attr_changed) {
				struct snd_pcm_runtime *runtime = ichdev->substream->runtime;
				if (runtime->dma_area)
					fill_nocache(runtime->dma_area, runtime->dma_bytes, 1);
			}
		}
	}

	/* resume status */
	for (i = 0; i < chip->bdbars_count; i++) {
		struct ichdev *ichdev = &chip->ichd[i];
		unsigned long port = ichdev->reg_offset;
		if (! ichdev->substream || ! ichdev->suspended)
			continue;
		if (ichdev->ichd == ICHD_PCMOUT)
			snd_intel8x0_setup_pcm_out(chip, ichdev->substream->runtime);
		iputdword(chip, port + ICH_REG_OFF_BDBAR, ichdev->bdbar_addr);
		iputbyte(chip, port + ICH_REG_OFF_LVI, ichdev->lvi);
		iputbyte(chip, port + ICH_REG_OFF_CIV, ichdev->civ);
		iputbyte(chip, port + ichdev->roff_sr, ICH_FIFOE | ICH_BCIS | ICH_LVBCI);
	}

	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(intel8x0_pm, intel8x0_suspend, intel8x0_resume);
#define INTEL8X0_PM_OPS	&intel8x0_pm
#else
#define INTEL8X0_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

#define INTEL8X0_TESTBUF_SIZE	32768	/* enough large for one shot */

static void intel8x0_measure_ac97_clock(struct intel8x0 *chip)
{
	struct snd_pcm_substream *subs;
	struct ichdev *ichdev;
	unsigned long port;
	unsigned long pos, pos1, t;
	int civ, timeout = 1000, attempt = 1;
	ktime_t start_time, stop_time;

	if (chip->ac97_bus->clock != 48000)
		return; /* specified in module option */

      __again:
	subs = chip->pcm[0]->streams[0].substream;
	if (! subs || subs->dma_buffer.bytes < INTEL8X0_TESTBUF_SIZE) {
		dev_warn(chip->card->dev,
			 "no playback buffer allocated - aborting measure ac97 clock\n");
		return;
	}
	ichdev = &chip->ichd[ICHD_PCMOUT];
	ichdev->physbuf = subs->dma_buffer.addr;
	ichdev->size = ichdev->fragsize = INTEL8X0_TESTBUF_SIZE;
	ichdev->substream = NULL; /* don't process interrupts */

	/* set rate */
	if (snd_ac97_set_rate(chip->ac97[0], AC97_PCM_FRONT_DAC_RATE, 48000) < 0) {
		dev_err(chip->card->dev, "cannot set ac97 rate: clock = %d\n",
			chip->ac97_bus->clock);
		return;
	}
	snd_intel8x0_setup_periods(chip, ichdev);
	port = ichdev->reg_offset;
	spin_lock_irq(&chip->reg_lock);
	chip->in_measurement = 1;
	/* trigger */
	if (chip->device_type != DEVICE_ALI)
		iputbyte(chip, port + ICH_REG_OFF_CR, ICH_IOCE | ICH_STARTBM);
	else {
		iputbyte(chip, port + ICH_REG_OFF_CR, ICH_IOCE);
		iputdword(chip, ICHREG(ALI_DMACR), 1 << ichdev->ali_slot);
	}
	start_time = ktime_get();
	spin_unlock_irq(&chip->reg_lock);
	msleep(50);
	spin_lock_irq(&chip->reg_lock);
	/* check the position */
	do {
		civ = igetbyte(chip, ichdev->reg_offset + ICH_REG_OFF_CIV);
		pos1 = igetword(chip, ichdev->reg_offset + ichdev->roff_picb);
		if (pos1 == 0) {
			udelay(10);
			continue;
		}
		if (civ == igetbyte(chip, ichdev->reg_offset + ICH_REG_OFF_CIV) &&
		    pos1 == igetword(chip, ichdev->reg_offset + ichdev->roff_picb))
			break;
	} while (timeout--);
	if (pos1 == 0) {	/* oops, this value is not reliable */
		pos = 0;
	} else {
		pos = ichdev->fragsize1;
		pos -= pos1 << ichdev->pos_shift;
		pos += ichdev->position;
	}
	chip->in_measurement = 0;
	stop_time = ktime_get();
	/* stop */
	if (chip->device_type == DEVICE_ALI) {
		iputdword(chip, ICHREG(ALI_DMACR), 1 << (ichdev->ali_slot + 16));
		iputbyte(chip, port + ICH_REG_OFF_CR, 0);
		while (igetbyte(chip, port + ICH_REG_OFF_CR))
			;
	} else {
		iputbyte(chip, port + ICH_REG_OFF_CR, 0);
		while (!(igetbyte(chip, port + ichdev->roff_sr) & ICH_DCH))
			;
	}
	iputbyte(chip, port + ICH_REG_OFF_CR, ICH_RESETREGS);
	spin_unlock_irq(&chip->reg_lock);

	if (pos == 0) {
		dev_err(chip->card->dev,
			"measure - unreliable DMA position..\n");
	      __retry:
		if (attempt < 3) {
			msleep(300);
			attempt++;
			goto __again;
		}
		goto __end;
	}

	pos /= 4;
	t = ktime_us_delta(stop_time, start_time);
	dev_info(chip->card->dev,
		 "%s: measured %lu usecs (%lu samples)\n", __func__, t, pos);
	if (t == 0) {
		dev_err(chip->card->dev, "?? calculation error..\n");
		goto __retry;
	}
	pos *= 1000;
	pos = (pos / t) * 1000 + ((pos % t) * 1000) / t;
	if (pos < 40000 || pos >= 60000) {
		/* abnormal value. hw problem? */
		dev_info(chip->card->dev, "measured clock %ld rejected\n", pos);
		goto __retry;
	} else if (pos > 40500 && pos < 41500)
		/* first exception - 41000Hz reference clock */
		chip->ac97_bus->clock = 41000;
	else if (pos > 43600 && pos < 44600)
		/* second exception - 44100HZ reference clock */
		chip->ac97_bus->clock = 44100;
	else if (pos < 47500 || pos > 48500)
		/* not 48000Hz, tuning the clock.. */
		chip->ac97_bus->clock = (chip->ac97_bus->clock * 48000) / pos;
      __end:
	dev_info(chip->card->dev, "clocking to %d\n", chip->ac97_bus->clock);
	snd_ac97_update_power(chip->ac97[0], AC97_PCM_FRONT_DAC_RATE, 0);
}

static struct snd_pci_quirk intel8x0_clock_list[] = {
	SND_PCI_QUIRK(0x0e11, 0x008a, "AD1885", 41000),
	SND_PCI_QUIRK(0x1028, 0x00be, "AD1885", 44100),
	SND_PCI_QUIRK(0x1028, 0x0177, "AD1980", 48000),
	SND_PCI_QUIRK(0x1028, 0x01ad, "AD1981B", 48000),
	SND_PCI_QUIRK(0x1043, 0x80f3, "AD1985", 48000),
	{ }	/* terminator */
};

static int intel8x0_in_clock_list(struct intel8x0 *chip)
{
	struct pci_dev *pci = chip->pci;
	const struct snd_pci_quirk *wl;

	wl = snd_pci_quirk_lookup(pci, intel8x0_clock_list);
	if (!wl)
		return 0;
	dev_info(chip->card->dev, "white list rate for %04x:%04x is %i\n",
	       pci->subsystem_vendor, pci->subsystem_device, wl->value);
	chip->ac97_bus->clock = wl->value;
	return 1;
}

static void snd_intel8x0_proc_read(struct snd_info_entry * entry,
				   struct snd_info_buffer *buffer)
{
	struct intel8x0 *chip = entry->private_data;
	unsigned int tmp;

	snd_iprintf(buffer, "Intel8x0\n\n");
	if (chip->device_type == DEVICE_ALI)
		return;
	tmp = igetdword(chip, ICHREG(GLOB_STA));
	snd_iprintf(buffer, "Global control        : 0x%08x\n", igetdword(chip, ICHREG(GLOB_CNT)));
	snd_iprintf(buffer, "Global status         : 0x%08x\n", tmp);
	if (chip->device_type == DEVICE_INTEL_ICH4)
		snd_iprintf(buffer, "SDM                   : 0x%08x\n", igetdword(chip, ICHREG(SDM)));
	snd_iprintf(buffer, "AC'97 codecs ready    :");
	if (tmp & chip->codec_isr_bits) {
		int i;
		static const char *codecs[3] = {
			"primary", "secondary", "tertiary"
		};
		for (i = 0; i < chip->max_codecs; i++)
			if (tmp & chip->codec_bit[i])
				snd_iprintf(buffer, " %s", codecs[i]);
	} else
		snd_iprintf(buffer, " none");
	snd_iprintf(buffer, "\n");
	if (chip->device_type == DEVICE_INTEL_ICH4 ||
	    chip->device_type == DEVICE_SIS)
		snd_iprintf(buffer, "AC'97 codecs SDIN     : %i %i %i\n",
			chip->ac97_sdin[0],
			chip->ac97_sdin[1],
			chip->ac97_sdin[2]);
}

static void snd_intel8x0_proc_init(struct intel8x0 *chip)
{
	struct snd_info_entry *entry;

	if (! snd_card_proc_new(chip->card, "intel8x0", &entry))
		snd_info_set_text_ops(entry, chip, snd_intel8x0_proc_read);
}

static int snd_intel8x0_dev_free(struct snd_device *device)
{
	struct intel8x0 *chip = device->device_data;
	return snd_intel8x0_free(chip);
}

struct ich_reg_info {
	unsigned int int_sta_mask;
	unsigned int offset;
};

static unsigned int ich_codec_bits[3] = {
	ICH_PCR, ICH_SCR, ICH_TCR
};
static unsigned int sis_codec_bits[3] = {
	ICH_PCR, ICH_SCR, ICH_SIS_TCR
};

static int snd_intel8x0_inside_vm(struct pci_dev *pci)
{
	int result  = inside_vm;
	char *msg   = NULL;

	/* check module parameter first (override detection) */
	if (result >= 0) {
		msg = result ? "enable (forced) VM" : "disable (forced) VM";
		goto fini;
	}

	/* detect KVM and Parallels virtual environments */
	result = kvm_para_available();
#ifdef X86_FEATURE_HYPERVISOR
	result = result || boot_cpu_has(X86_FEATURE_HYPERVISOR);
#endif
	if (!result)
		goto fini;

	/* check for known (emulated) devices */
	if (pci->subsystem_vendor == 0x1af4 &&
	    pci->subsystem_device == 0x1100) {
		/* KVM emulated sound, PCI SSID: 1af4:1100 */
		msg = "enable KVM";
	} else if (pci->subsystem_vendor == 0x1ab8) {
		/* Parallels VM emulated sound, PCI SSID: 1ab8:xxxx */
		msg = "enable Parallels VM";
	} else {
		msg = "disable (unknown or VT-d) VM";
		result = 0;
	}

fini:
	if (msg != NULL)
		dev_info(&pci->dev, "%s optimization\n", msg);

	return result;
}

static int snd_intel8x0_create(struct snd_card *card,
			       struct pci_dev *pci,
			       unsigned long device_type,
			       struct intel8x0 **r_intel8x0)
{
	struct intel8x0 *chip;
	int err;
	unsigned int i;
	unsigned int int_sta_masks;
	struct ichdev *ichdev;
	static struct snd_device_ops ops = {
		.dev_free =	snd_intel8x0_dev_free,
	};

	static unsigned int bdbars[] = {
		3, /* DEVICE_INTEL */
		6, /* DEVICE_INTEL_ICH4 */
		3, /* DEVICE_SIS */
		6, /* DEVICE_ALI */
		4, /* DEVICE_NFORCE */
	};
	static struct ich_reg_info intel_regs[6] = {
		{ ICH_PIINT, 0 },
		{ ICH_POINT, 0x10 },
		{ ICH_MCINT, 0x20 },
		{ ICH_M2INT, 0x40 },
		{ ICH_P2INT, 0x50 },
		{ ICH_SPINT, 0x60 },
	};
	static struct ich_reg_info nforce_regs[4] = {
		{ ICH_PIINT, 0 },
		{ ICH_POINT, 0x10 },
		{ ICH_MCINT, 0x20 },
		{ ICH_NVSPINT, 0x70 },
	};
	static struct ich_reg_info ali_regs[6] = {
		{ ALI_INT_PCMIN, 0x40 },
		{ ALI_INT_PCMOUT, 0x50 },
		{ ALI_INT_MICIN, 0x60 },
		{ ALI_INT_CODECSPDIFOUT, 0x70 },
		{ ALI_INT_SPDIFIN, 0xa0 },
		{ ALI_INT_SPDIFOUT, 0xb0 },
	};
	struct ich_reg_info *tbl;

	*r_intel8x0 = NULL;

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		pci_disable_device(pci);
		return -ENOMEM;
	}
	spin_lock_init(&chip->reg_lock);
	chip->device_type = device_type;
	chip->card = card;
	chip->pci = pci;
	chip->irq = -1;

	/* module parameters */
	chip->buggy_irq = buggy_irq;
	chip->buggy_semaphore = buggy_semaphore;
	if (xbox)
		chip->xbox = 1;

	chip->inside_vm = snd_intel8x0_inside_vm(pci);

	if (pci->vendor == PCI_VENDOR_ID_INTEL &&
	    pci->device == PCI_DEVICE_ID_INTEL_440MX)
		chip->fix_nocache = 1; /* enable workaround */

	if ((err = pci_request_regions(pci, card->shortname)) < 0) {
		kfree(chip);
		pci_disable_device(pci);
		return err;
	}

	if (device_type == DEVICE_ALI) {
		/* ALI5455 has no ac97 region */
		chip->bmaddr = pci_iomap(pci, 0, 0);
		goto port_inited;
	}

	if (pci_resource_flags(pci, 2) & IORESOURCE_MEM) /* ICH4 and Nforce */
		chip->addr = pci_iomap(pci, 2, 0);
	else
		chip->addr = pci_iomap(pci, 0, 0);
	if (!chip->addr) {
		dev_err(card->dev, "AC'97 space ioremap problem\n");
		snd_intel8x0_free(chip);
		return -EIO;
	}
	if (pci_resource_flags(pci, 3) & IORESOURCE_MEM) /* ICH4 */
		chip->bmaddr = pci_iomap(pci, 3, 0);
	else
		chip->bmaddr = pci_iomap(pci, 1, 0);

 port_inited:
	if (!chip->bmaddr) {
		dev_err(card->dev, "Controller space ioremap problem\n");
		snd_intel8x0_free(chip);
		return -EIO;
	}
	chip->bdbars_count = bdbars[device_type];

	/* initialize offsets */
	switch (device_type) {
	case DEVICE_NFORCE:
		tbl = nforce_regs;
		break;
	case DEVICE_ALI:
		tbl = ali_regs;
		break;
	default:
		tbl = intel_regs;
		break;
	}
	for (i = 0; i < chip->bdbars_count; i++) {
		ichdev = &chip->ichd[i];
		ichdev->ichd = i;
		ichdev->reg_offset = tbl[i].offset;
		ichdev->int_sta_mask = tbl[i].int_sta_mask;
		if (device_type == DEVICE_SIS) {
			/* SiS 7012 swaps the registers */
			ichdev->roff_sr = ICH_REG_OFF_PICB;
			ichdev->roff_picb = ICH_REG_OFF_SR;
		} else {
			ichdev->roff_sr = ICH_REG_OFF_SR;
			ichdev->roff_picb = ICH_REG_OFF_PICB;
		}
		if (device_type == DEVICE_ALI)
			ichdev->ali_slot = (ichdev->reg_offset - 0x40) / 0x10;
		/* SIS7012 handles the pcm data in bytes, others are in samples */
		ichdev->pos_shift = (device_type == DEVICE_SIS) ? 0 : 1;
	}

	/* allocate buffer descriptor lists */
	/* the start of each lists must be aligned to 8 bytes */
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(pci),
				chip->bdbars_count * sizeof(u32) * ICH_MAX_FRAGS * 2,
				&chip->bdbars) < 0) {
		snd_intel8x0_free(chip);
		dev_err(card->dev, "cannot allocate buffer descriptors\n");
		return -ENOMEM;
	}
	/* tables must be aligned to 8 bytes here, but the kernel pages
	   are much bigger, so we don't care (on i386) */
	/* workaround for 440MX */
	if (chip->fix_nocache)
		fill_nocache(chip->bdbars.area, chip->bdbars.bytes, 1);
	int_sta_masks = 0;
	for (i = 0; i < chip->bdbars_count; i++) {
		ichdev = &chip->ichd[i];
		ichdev->bdbar = ((u32 *)chip->bdbars.area) +
			(i * ICH_MAX_FRAGS * 2);
		ichdev->bdbar_addr = chip->bdbars.addr +
			(i * sizeof(u32) * ICH_MAX_FRAGS * 2);
		int_sta_masks |= ichdev->int_sta_mask;
	}
	chip->int_sta_reg = device_type == DEVICE_ALI ?
		ICH_REG_ALI_INTERRUPTSR : ICH_REG_GLOB_STA;
	chip->int_sta_mask = int_sta_masks;

	pci_set_master(pci);

	switch(chip->device_type) {
	case DEVICE_INTEL_ICH4:
		/* ICH4 can have three codecs */
		chip->max_codecs = 3;
		chip->codec_bit = ich_codec_bits;
		chip->codec_ready_bits = ICH_PRI | ICH_SRI | ICH_TRI;
		break;
	case DEVICE_SIS:
		/* recent SIS7012 can have three codecs */
		chip->max_codecs = 3;
		chip->codec_bit = sis_codec_bits;
		chip->codec_ready_bits = ICH_PRI | ICH_SRI | ICH_SIS_TRI;
		break;
	default:
		/* others up to two codecs */
		chip->max_codecs = 2;
		chip->codec_bit = ich_codec_bits;
		chip->codec_ready_bits = ICH_PRI | ICH_SRI;
		break;
	}
	for (i = 0; i < chip->max_codecs; i++)
		chip->codec_isr_bits |= chip->codec_bit[i];

	if ((err = snd_intel8x0_chip_init(chip, 1)) < 0) {
		snd_intel8x0_free(chip);
		return err;
	}

	/* request irq after initializaing int_sta_mask, etc */
	if (request_irq(pci->irq, snd_intel8x0_interrupt,
			IRQF_SHARED, KBUILD_MODNAME, chip)) {
		dev_err(card->dev, "unable to grab IRQ %d\n", pci->irq);
		snd_intel8x0_free(chip);
		return -EBUSY;
	}
	chip->irq = pci->irq;

	if ((err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops)) < 0) {
		snd_intel8x0_free(chip);
		return err;
	}

	*r_intel8x0 = chip;
	return 0;
}

static struct shortname_table {
	unsigned int id;
	const char *s;
} shortnames[] = {
	{ PCI_DEVICE_ID_INTEL_82801AA_5, "Intel 82801AA-ICH" },
	{ PCI_DEVICE_ID_INTEL_82801AB_5, "Intel 82901AB-ICH0" },
	{ PCI_DEVICE_ID_INTEL_82801BA_4, "Intel 82801BA-ICH2" },
	{ PCI_DEVICE_ID_INTEL_440MX, "Intel 440MX" },
	{ PCI_DEVICE_ID_INTEL_82801CA_5, "Intel 82801CA-ICH3" },
	{ PCI_DEVICE_ID_INTEL_82801DB_5, "Intel 82801DB-ICH4" },
	{ PCI_DEVICE_ID_INTEL_82801EB_5, "Intel ICH5" },
	{ PCI_DEVICE_ID_INTEL_ESB_5, "Intel 6300ESB" },
	{ PCI_DEVICE_ID_INTEL_ICH6_18, "Intel ICH6" },
	{ PCI_DEVICE_ID_INTEL_ICH7_20, "Intel ICH7" },
	{ PCI_DEVICE_ID_INTEL_ESB2_14, "Intel ESB2" },
	{ PCI_DEVICE_ID_SI_7012, "SiS SI7012" },
	{ PCI_DEVICE_ID_NVIDIA_MCP1_AUDIO, "NVidia nForce" },
	{ PCI_DEVICE_ID_NVIDIA_MCP2_AUDIO, "NVidia nForce2" },
	{ PCI_DEVICE_ID_NVIDIA_MCP3_AUDIO, "NVidia nForce3" },
	{ PCI_DEVICE_ID_NVIDIA_CK8S_AUDIO, "NVidia CK8S" },
	{ PCI_DEVICE_ID_NVIDIA_CK804_AUDIO, "NVidia CK804" },
	{ PCI_DEVICE_ID_NVIDIA_CK8_AUDIO, "NVidia CK8" },
	{ 0x003a, "NVidia MCP04" },
	{ 0x746d, "AMD AMD8111" },
	{ 0x7445, "AMD AMD768" },
	{ 0x5455, "ALi M5455" },
	{ 0, NULL },
};

static struct snd_pci_quirk spdif_aclink_defaults[] = {
	SND_PCI_QUIRK(0x147b, 0x1c1a, "ASUS KN8", 1),
	{ } /* end */
};

/* look up white/black list for SPDIF over ac-link */
static int check_default_spdif_aclink(struct pci_dev *pci)
{
	const struct snd_pci_quirk *w;

	w = snd_pci_quirk_lookup(pci, spdif_aclink_defaults);
	if (w) {
		if (w->value)
			dev_dbg(&pci->dev,
				"Using SPDIF over AC-Link for %s\n",
				    snd_pci_quirk_name(w));
		else
			dev_dbg(&pci->dev,
				"Using integrated SPDIF DMA for %s\n",
				    snd_pci_quirk_name(w));
		return w->value;
	}
	return 0;
}

static int snd_intel8x0_probe(struct pci_dev *pci,
			      const struct pci_device_id *pci_id)
{
	struct snd_card *card;
	struct intel8x0 *chip;
	int err;
	struct shortname_table *name;

	err = snd_card_new(&pci->dev, index, id, THIS_MODULE, 0, &card);
	if (err < 0)
		return err;

	if (spdif_aclink < 0)
		spdif_aclink = check_default_spdif_aclink(pci);

	strcpy(card->driver, "ICH");
	if (!spdif_aclink) {
		switch (pci_id->driver_data) {
		case DEVICE_NFORCE:
			strcpy(card->driver, "NFORCE");
			break;
		case DEVICE_INTEL_ICH4:
			strcpy(card->driver, "ICH4");
		}
	}

	strcpy(card->shortname, "Intel ICH");
	for (name = shortnames; name->id; name++) {
		if (pci->device == name->id) {
			strcpy(card->shortname, name->s);
			break;
		}
	}

	if (buggy_irq < 0) {
		/* some Nforce[2] and ICH boards have problems with IRQ handling.
		 * Needs to return IRQ_HANDLED for unknown irqs.
		 */
		if (pci_id->driver_data == DEVICE_NFORCE)
			buggy_irq = 1;
		else
			buggy_irq = 0;
	}

	if ((err = snd_intel8x0_create(card, pci, pci_id->driver_data,
				       &chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	card->private_data = chip;

	if ((err = snd_intel8x0_mixer(chip, ac97_clock, ac97_quirk)) < 0) {
		snd_card_free(card);
		return err;
	}
	if ((err = snd_intel8x0_pcm(chip)) < 0) {
		snd_card_free(card);
		return err;
	}
	
	snd_intel8x0_proc_init(chip);

	snprintf(card->longname, sizeof(card->longname),
		 "%s with %s at irq %i", card->shortname,
		 snd_ac97_get_short_name(chip->ac97[0]), chip->irq);

	if (ac97_clock == 0 || ac97_clock == 1) {
		if (ac97_clock == 0) {
			if (intel8x0_in_clock_list(chip) == 0)
				intel8x0_measure_ac97_clock(chip);
		} else {
			intel8x0_measure_ac97_clock(chip);
		}
	}

	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	return 0;
}

static void snd_intel8x0_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
}

static struct pci_driver intel8x0_driver = {
	.name = KBUILD_MODNAME,
	.id_table = snd_intel8x0_ids,
	.probe = snd_intel8x0_probe,
	.remove = snd_intel8x0_remove,
	.driver = {
		.pm = INTEL8X0_PM_OPS,
	},
};

module_pci_driver(intel8x0_driver);
