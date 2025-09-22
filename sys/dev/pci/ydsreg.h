/*	$OpenBSD: ydsreg.h,v 1.7 2010/09/17 07:55:52 jakemsr Exp $	*/
/*	$NetBSD$	*/

/*
 * Copyright (c) 2000, 2001 Kazuki Sakamoto and Minoura Makoto.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * YMF724/740/744/754 registers
 */

#ifndef _DEV_PCI_YDSREG_H_
#define	_DEV_PCI_YDSREG_H_

/*
 * PCI Config Registers
 */
#define	YDS_PCI_MBA		0x10
#define	YDS_PCI_LEGACY		0x40
# define YDS_PCI_LEGACY_SBEN	0x0001
# define YDS_PCI_LEGACY_FMEN	0x0002
# define YDS_PCI_LEGACY_JPEN	0x0004
# define YDS_PCI_LEGACY_MEN	0x0008
# define YDS_PCI_LEGACY_MIEN	0x0010
# define YDS_PCI_LEGACY_IO	0x0020
# define YDS_PCI_LEGACY_SDMA0	0x0000
# define YDS_PCI_LEGACY_SDMA1	0x0040
# define YDS_PCI_LEGACY_SDMA3	0x00c0
# define YDS_PCI_LEGACY_SBIRQ5	0x0000
# define YDS_PCI_LEGACY_SBIRQ7	0x0100
# define YDS_PCI_LEGACY_SBIRQ9	0x0200
# define YDS_PCI_LEGACY_SBIRQ10	0x0300
# define YDS_PCI_LEGACY_SBIRQ11	0x0400
# define YDS_PCI_LEGACY_MPUIRQ5	0x0000
# define YDS_PCI_LEGACY_MPUIRQ7	0x0800
# define YDS_PCI_LEGACY_MPUIRQ9	0x1000
# define YDS_PCI_LEGACY_MPUIRQ10 0x1800
# define YDS_PCI_LEGACY_MPUIRQ11 0x2000
# define YDS_PCI_LEGACY_SIEN	0x4000
# define YDS_PCI_LEGACY_LAD	0x8000

# define YDS_PCI_EX_LEGACY_FMIO_388	(0x0000 << 16)
# define YDS_PCI_EX_LEGACY_FMIO_398	(0x0001 << 16)
# define YDS_PCI_EX_LEGACY_FMIO_3A0	(0x0002 << 16)
# define YDS_PCI_EX_LEGACY_FMIO_3A8	(0x0003 << 16)
# define YDS_PCI_EX_LEGACY_SBIO_220	(0x0000 << 16)
# define YDS_PCI_EX_LEGACY_SBIO_240	(0x0004 << 16)
# define YDS_PCI_EX_LEGACY_SBIO_260	(0x0008 << 16)
# define YDS_PCI_EX_LEGACY_SBIO_280	(0x000c << 16)
# define YDS_PCI_EX_LEGACY_MPUIO_330	(0x0000 << 16)
# define YDS_PCI_EX_LEGACY_MPUIO_300	(0x0010 << 16)
# define YDS_PCI_EX_LEGACY_MPUIO_332	(0x0020 << 16)
# define YDS_PCI_EX_LEGACY_MPUIO_334	(0x0030 << 16)
# define YDS_PCI_EX_LEGACY_JSIO_201	(0x0000 << 16)
# define YDS_PCI_EX_LEGACY_JSIO_202	(0x0040 << 16)
# define YDS_PCI_EX_LEGACY_JSIO_204	(0x0080 << 16)
# define YDS_PCI_EX_LEGACY_JSIO_205	(0x00c0 << 16)
# define YDS_PCI_EX_LEGACY_MAIM		(0x0100 << 16)
# define YDS_PCI_EX_LEGACY_SMOD_PCI	(0x0000 << 16)
# define YDS_PCI_EX_LEGACY_SMOD_DISABLE	(0x0800 << 16)
# define YDS_PCI_EX_LEGACY_SMOD_DDMA	(0x1000 << 16)
# define YDS_PCI_EX_LEGACY_SBVER_3	(0x0000 << 16)
# define YDS_PCI_EX_LEGACY_SBVER_2	(0x2000 << 16)
# define YDS_PCI_EX_LEGACY_SBVER_1	(0x4000 << 16)
# define YDS_PCI_EX_LEGACY_IMOD		(0x8000 << 16)

#define	YDS_PCI_DSCTRL		0x48
# define YDS_DSCTRL_CRST	0x00000001
# define YDS_DSCTRL_WRST	0x00000004

#define YDS_PCI_FM_BA		0x60
#define YDS_PCI_SB_BA		0x62
#define YDS_PCI_MPU_BA		0x64
#define YDS_PCI_JS_BA		0x66

/*
 * DS-1 PCI Audio part registers
 */
#define YDS_INTERRUPT_FLAGS	0x0004
#define YDS_INTERRUPT_FLAGS_TI	0x0001
#define YDS_ACTIVITY		0x0006
# define YDS_ACTIVITY_DOCKA	0x0010
#define	YDS_GLOBAL_CONTROL	0x0008
# define YDS_GLCTRL_HVE		0x0001
# define YDS_GLCTRL_HVIE	0x0002

#define YDS_GPIO_IIF		0x0050
# define YDS_GPIO_GIO0		0x0001
# define YDS_GPIO_GIO1		0x0002
# define YDS_GPIO_GIO2		0x0004
#define YDS_GPIO_IIE		0x0052
# define YDS_GPIO_GIE0		0x0001
# define YDS_GPIO_GIE1		0x0002
# define YDS_GPIO_GIE2		0x0004
#define YDS_GPIO_ISTAT		0x0054
# define YDS_GPIO_GPI0		0x0001
# define YDS_GPIO_GPI1		0x0002
# define YDS_GPIO_GPI2		0x0004
#define YDS_GPIO_OCTRL		0x0056
# define YDS_GPIO_GPO0		0x0001
# define YDS_GPIO_GPO1		0x0002
# define YDS_GPIO_GPO2		0x0004
#define YDS_GPIO_FUNCE		0x0058
# define YDS_GPIO_GPC0		0x0001
# define YDS_GPIO_GPC1		0x0002
# define YDS_GPIO_GPC2		0x0004
# define YDS_GPIO_GPE0		0x0010
# define YDS_GPIO_GPE1		0x0020
# define YDS_GPIO_GPE2		0x0040
#define YDS_GPIO_ITYPE		0x005a
# define YDS_GPIO_GPT0_LEVEL	0x0000
# define YDS_GPIO_GPT0_RISE	0x0001
# define YDS_GPIO_GPT0_FALL	0x0002
# define YDS_GPIO_GPT0_BOTH	0x0003
# define YDS_GPIO_GPT0_MASK	0x0003
# define YDS_GPIO_GPT1_LEVEL	0x0004
# define YDS_GPIO_GPT1_RISE	0x0005
# define YDS_GPIO_GPT1_FALL	0x0006
# define YDS_GPIO_GPT1_BOTH	0x0007
# define YDS_GPIO_GPT1_MASK	0x0007
# define YDS_GPIO_GPT2_LEVEL	0x0000
# define YDS_GPIO_GPT2_RISE	0x0010
# define YDS_GPIO_GPT2_FALL	0x0020
# define YDS_GPIO_GPT2_BOTH	0x0030
# define YDS_GPIO_GPT2_MASK	0x0030

#define	YDS_GLOBAL_CONTROL	0x0008
# define YDS_GLCTRL_HVE		0x0001
# define YDS_GLCTRL_HVIE	0x0002

#define	AC97_CMD_DATA		0x0060
#define	AC97_CMD_ADDR		0x0062
# define AC97_ID(id)		((id) << 8)
# define AC97_CMD_READ		0x8000
# define AC97_CMD_WRITE		0x0000
#define	AC97_STAT_DATA1		0x0064
#define	AC97_STAT_ADDR1		0x0066
#define	AC97_STAT_DATA2		0x0068
#define	AC97_STAT_ADDR2		0x006a
# define AC97_BUSY		0x8000

#define	YDS_LEGACY_OUT_VOLUME	0x0080
#define	YDS_DAC_OUT_VOLUME	0x0084
#define	YDS_DAC_OUT_VOL_L	0x0084
#define	YDS_DAC_OUT_VOL_R	0x0086
#define	YDS_ZV_OUT_VOLUME	0x0088
#define	YDS_2ND_OUT_VOLUME	0x008C
#define	YDS_ADC_OUT_VOLUME	0x0090
#define	YDS_LEGACY_REC_VOLUME	0x0094
#define	YDS_DAC_REC_VOLUME	0x0098
#define	YDS_ZV_REC_VOLUME	0x009C
#define	YDS_2ND_REC_VOLUME	0x00A0
#define	YDS_ADC_REC_VOLUME	0x00A4
#define	YDS_ADC_IN_VOLUME	0x00A8
#define	YDS_REC_IN_VOLUME	0x00AC
#define	YDS_P44_OUT_VOLUME	0x00B0
#define	YDS_P44_REC_VOLUME	0x00B4
#define	YDS_SPDIFIN_OUT_VOLUME	0x00B8
#define	YDS_SPDIFIN_REC_VOLUME	0x00BC

#define	YDS_ADC_SAMPLE_RATE	0x00c0
#define	YDS_REC_SAMPLE_RATE	0x00c4
#define	YDS_ADC_FORMAT		0x00c8
#define	YDS_REC_FORMAT		0x00cc
# define YDS_FORMAT_8BIT	0x01
# define YDS_FORMAT_STEREO	0x02

#define	YDS_STATUS		0x0100
# define YDS_STAT_ACT		0x00000001
# define YDS_STAT_WORK		0x00000002
# define YDS_STAT_TINT		0x00008000
# define YDS_STAT_INT		0x80000000
#define	YDS_CONTROL_SELECT	0x0104
# define YDS_CSEL		0x00000001
#define	YDS_MODE		0x0108
# define YDS_MODE_ACTV		0x00000001
# define YDS_MODE_ACTV2		0x00000002
# define YDS_MODE_TOUT		0x00008000
# define YDS_MODE_RESET		0x00010000
# define YDS_MODE_AC3		0x40000000
# define YDS_MODE_MUTE		0x80000000

#define	YDS_CONFIG		0x0114
# define YDS_DSP_DISABLE	0
# define YDS_DSP_SETUP		0x00000001

#define	YDS_PLAY_CTRLSIZE	0x0140
#define	YDS_REC_CTRLSIZE	0x0144
#define	YDS_EFFECT_CTRLSIZE	0x0148
#define	YDS_WORK_SIZE		0x014c
#define	YDS_MAPOF_REC		0x0150
# define YDS_RECSLOT_VALID	0x00000001
# define YDS_ADCSLOT_VALID	0x00000002
#define	YDS_MAPOF_EFFECT	0x0154
# define YDS_DL_VALID		0x00000001
# define YDS_DR_VALID		0x00000002
# define YDS_EFFECT1_VALID	0x00000004
# define YDS_EFFECT2_VALID	0x00000008
# define YDS_EFFECT3_VALID	0x00000010

#define	YDS_PLAY_CTRLBASE	0x0158
#define	YDS_REC_CTRLBASE	0x015c
#define	YDS_EFFECT_CTRLBASE	0x0160
#define	YDS_WORK_BASE		0x0164

#define	YDS_DSP_INSTRAM		0x1000
#define	YDS_CTRL_INSTRAM	0x4000

typedef enum {
	YDS_DS_1,
	YDS_DS_1E
} yds_dstype_t;

#define	AC97_TIMEOUT		1000
#define	YDS_WORK_TIMEOUT	250000

/* slot control data structures */
#define	MAX_PLAY_SLOT_CTRL	64
#define	N_PLAY_SLOT_CTRL_BANK	2
#define	N_REC_SLOT_CTRL		2
#define	N_REC_SLOT_CTRL_BANK	2

/*
 * play slot
 */
union play_slot_table {
	u_int32_t numofplay;
	u_int32_t slotbase;
};

struct play_slot_ctrl_bank {
	u_int32_t format;
#define	PSLT_FORMAT_STEREO	0x00010000
#define	PSLT_FORMAT_8BIT	0x80000000
#define	PSLT_FORMAT_SRC441	0x10000000
#define PSLT_FORMAT_RCH		0x00000001
	u_int32_t loopdefault;
	u_int32_t pgbase;
	u_int32_t pgloop;
	u_int32_t pgloopend;
	u_int32_t pgloopfrac;
	u_int32_t pgdeltaend;
	u_int32_t lpfkend;
	u_int32_t eggainend;
	u_int32_t lchgainend;
	u_int32_t rchgainend;
	u_int32_t effect1gainend;
	u_int32_t effect2gainend;
	u_int32_t effect3gainend;
	u_int32_t lpfq;
	u_int32_t status;
#define	PSLT_STATUS_DEND	0x00000001
	u_int32_t numofframes;
	u_int32_t loopcount;
	u_int32_t pgstart;
	u_int32_t pgstartfrac;
	u_int32_t pgdelta;
	u_int32_t lpfk;
	u_int32_t eggain;
	u_int32_t lchgain;
	u_int32_t rchgain;
	u_int32_t effect1gain;
	u_int32_t effect2gain;
	u_int32_t effect3gain;
	u_int32_t lpfd1;
	u_int32_t lpfd2;
};

/*
 * rec slot
 */
struct rec_slot_ctrl_bank {
	u_int32_t pgbase;
	u_int32_t pgloopendadr;
	u_int32_t pgstartadr;
	u_int32_t numofloops;
};

struct rec_slot {
	struct rec_slot_ctrl {
		struct rec_slot_ctrl_bank bank[N_REC_SLOT_CTRL_BANK];
	} ctrl[N_REC_SLOT_CTRL];
};

/*
 * effect slot
 */
struct effect_slot_ctrl_bank {
	u_int32_t pgbase;
	u_int32_t pgloopend;
	u_int32_t pgstart;
	u_int32_t temp;
};

#define N_PLAY_SLOTS		2		/* We use only 2 (R and L) */
#define	N_PLAY_SLOT_CTRL	2
#define WORK_SIZE		0x0400

/*
 * softc
 */
struct yds_dma {
	bus_dmamap_t map;
	caddr_t addr;			/* VA */
	bus_dma_segment_t segs[1];
	int nsegs;
	size_t size;
	struct yds_dma *next;
};

struct yds_codec_softc {
	struct device sc_dev;		/* base device */
	struct yds_softc *sc;
	int id;
	int status_data;
	int status_addr;
	struct ac97_host_if host_if;
	struct ac97_codec_if *codec_if;
};

struct yds_softc {
	struct device		sc_dev;		/* base device */
	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_pcitag;
	pcireg_t		sc_id;
	int			sc_revision;
	void			*sc_ih;		/* interrupt vectoring */
	bus_space_tag_t		memt;
	bus_space_handle_t	memh;
	bus_dma_tag_t		sc_dmatag;	/* DMA tag */
	u_int			sc_flags;

	struct yds_codec_softc	sc_codec[2];	/* Primary/Secondary AC97 */

	struct yds_dma		*sc_dmas;	/* List of DMA handles */

	/*
	 * Play/record status
	 */
	struct {
		void		(*intr)(void *); /* rint/pint */
		void		*intr_arg;	/* arg for intr */
		u_int	 	offset;		/* filled up to here */
		u_int	 	blksize;
		u_int	 	factor;		/* byte per sample */
		u_int		length;		/* ring buffer length */
		struct yds_dma	*dma;		/* DMA handle for ring buf */
	} sc_play, sc_rec;

	/*
	 * DSP control data
	 *
	 * Work space, play control data table, play slot control data,
	 * rec slot control data and effect slot control data are
	 * stored in a single memory segment in this order.
	 */
	struct yds_dma			sc_ctrldata;
	/* KVA and offset in buffer of play ctrl data tbl */
	u_int32_t			*ptbl;
	off_t				ptbloff;
	/* KVA and offset in buffer of rec slot ctrl data */
	struct rec_slot_ctrl_bank	*rbank;
	off_t				rbankoff;
	/* Array of KVA pointers and offset of play slot control data */
	struct play_slot_ctrl_bank	*pbankp[N_PLAY_SLOT_CTRL_BANK
					       *N_PLAY_SLOTS];
	off_t				pbankoff;

	/*
	 * Legacy support
	 */
	bus_space_tag_t		sc_legacy_iot;
	bus_space_handle_t	sc_opl_ioh;
	struct device		*sc_mpu;
	bus_space_handle_t	sc_mpu_ioh;

	/*
	 * Suspend/resume support
	 */
	int			suspend;
	int			sc_resume_active;
};
#define sc_opl_iot	sc_legacy_iot
#define sc_mpu_iot	sc_legacy_iot

#endif /* _DEV_PCI_YDSREG_H_ */
