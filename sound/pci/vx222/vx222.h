/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Driver for Digigram VX222 PCI soundcards
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 */

#ifndef __VX222_H
#define __VX222_H

#include <sound/vx_core.h>

struct snd_vx222 {

	struct vx_core core;

	/* h/w config; for PLX and for DSP */
	struct pci_dev *pci;
	unsigned long port[2];

	unsigned int regCDSP;	/* current CDSP register */
	unsigned int regCFG;	/* current CFG register */
	unsigned int regSELMIC;	/* current SELMIC reg. (for VX222 Mic) */

	int input_level[2];	/* input level for vx222 mic */
	int mic_level;		/* mic level for vx222 mic */
};

#define to_vx222(x)	container_of(x, struct snd_vx222, core)

/* we use a lookup table with 148 values, see vx_mixer.c */
#define VX2_AKM_LEVEL_MAX	0x93

extern const struct snd_vx_ops vx222_ops;
extern const struct snd_vx_ops vx222_old_ops;

/* Offset of registers with base equal to portDSP. */
#define VX_RESET_DMA_REGISTER_OFFSET    0x00000008

/* Constants used to access the INTCSR register. */
#define VX_INTCSR_VALUE                 0x00000001
#define VX_PCI_INTERRUPT_MASK           0x00000040

/* Constants used to access the CDSP register (0x20). */
#define VX_CDSP_TEST1_MASK              0x00000080
#define VX_CDSP_TOR1_MASK               0x00000040
#define VX_CDSP_TOR2_MASK               0x00000020
#define VX_CDSP_RESERVED0_0_MASK        0x00000010
#define VX_CDSP_CODEC_RESET_MASK        0x00000008
#define VX_CDSP_VALID_IRQ_MASK          0x00000004
#define VX_CDSP_TEST0_MASK              0x00000002
#define VX_CDSP_DSP_RESET_MASK          0x00000001

#define VX_CDSP_GPIO_OUT_MASK           0x00000060
#define VX_GPIO_OUT_BIT_OFFSET          5               // transform output to bit 0 and 1

/* Constants used to access the CFG register (0x24). */
#define VX_CFG_SYNCDSP_MASK             0x00000080
#define VX_CFG_RESERVED0_0_MASK         0x00000040
#define VX_CFG_RESERVED1_0_MASK         0x00000020
#define VX_CFG_RESERVED2_0_MASK         0x00000010
#define VX_CFG_DATAIN_SEL_MASK          0x00000008     // 0 (ana), 1 (UER)
#define VX_CFG_RESERVED3_0_MASK         0x00000004
#define VX_CFG_RESERVED4_0_MASK         0x00000002
#define VX_CFG_CLOCKIN_SEL_MASK         0x00000001     // 0 (internal), 1 (AES/EBU)

/* Constants used to access the STATUS register (0x30). */
#define VX_STATUS_DATA_XICOR_MASK       0x00000080
#define VX_STATUS_VAL_TEST1_MASK        0x00000040
#define VX_STATUS_VAL_TEST0_MASK        0x00000020
#define VX_STATUS_RESERVED0_MASK        0x00000010
#define VX_STATUS_VAL_TOR1_MASK         0x00000008
#define VX_STATUS_VAL_TOR0_MASK         0x00000004
#define VX_STATUS_LEVEL_IN_MASK         0x00000002    // 6 dBu (0), 22 dBu (1)
#define VX_STATUS_MEMIRQ_MASK           0x00000001

#define VX_STATUS_GPIO_IN_MASK          0x0000000C
#define VX_GPIO_IN_BIT_OFFSET           0             // leave input as bit 2 and 3

/* Constants used to access the MICRO INPUT SELECT register (0x40). */
#define MICRO_SELECT_INPUT_NORM        0x00
#define MICRO_SELECT_INPUT_MUTE        0x01
#define MICRO_SELECT_INPUT_LIMIT       0x02
#define MICRO_SELECT_INPUT_MASK        0x03

#define MICRO_SELECT_PREAMPLI_G_0      0x00
#define MICRO_SELECT_PREAMPLI_G_1      0x04
#define MICRO_SELECT_PREAMPLI_G_2      0x08
#define MICRO_SELECT_PREAMPLI_G_3      0x0C
#define MICRO_SELECT_PREAMPLI_MASK     0x0C
#define MICRO_SELECT_PREAMPLI_OFFSET   2

#define MICRO_SELECT_RAISE_COMPR       0x10

#define MICRO_SELECT_NOISE_T_52DB      0x00
#define MICRO_SELECT_NOISE_T_42DB      0x20
#define MICRO_SELECT_NOISE_T_32DB      0x40
#define MICRO_SELECT_NOISE_T_MASK      0x60

#define MICRO_SELECT_PHANTOM_ALIM      0x80


#endif /* __VX222_H */
