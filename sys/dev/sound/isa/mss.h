/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Doug Rabson
 * Copyright (c) 1997 Luigi Rizzo
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * This file contains information and macro definitions for
 * AD1848-compatible devices, used in the MSS/WSS compatible boards.
 */

/*
 *

The codec part of the board is seen as a set of 4 registers mapped
at the base address for the board (default 0x534). Note that some
(early) boards implemented 4 additional registers 4 location before
(usually 0x530) to store configuration information. This is a source
of confusion in that one never knows what address to specify. The
(current) convention is to use the old address (0x530) in the kernel
configuration file and consider MSS registers start four location
ahead.

 *
 */

struct mixer_def {
    	u_int regno:7;
    	u_int polarity:1;	/* 1 means reversed */
    	u_int bitoffs:4;
    	u_int nbits:4;
};
typedef struct mixer_def mixer_ent;
typedef struct mixer_def mixer_tab[32][2];

#define MIX_ENT(name, reg_l, pol_l, pos_l, len_l, reg_r, pol_r, pos_r, len_r) \
    	{{reg_l, pol_l, pos_l, len_l}, {reg_r, pol_r, pos_r, len_r}}

#define PMIX_ENT(name, reg_l, pos_l, len_l, reg_r, pos_r, len_r) \
    	{{reg_l, 0, pos_l, len_l}, {reg_r, 0, pos_r, len_r}}

#define MIX_NONE(name) MIX_ENT(name, 0,0,0,0, 0,0,0,0)

/*
 * The four visible registers of the MSS :
 *
 */

#define MSS_INDEX        (0 + 4)
#define	MSS_IDXBUSY		0x80	/* readonly, set when busy */
#define	MSS_MCE			0x40	/* the MCE bit. */
	/*
	 * the MCE bit must be set whenever the current mode of the
	 * codec is changed; this in particular is true for the
	 * Data Format (I8, I28) and Interface Config(I9) registers.
	 * Only exception are CEN and PEN which can be changed on the fly.
	 * The DAC output is muted when MCE is set.
	 */
#define	MSS_TRD			0x20	/* Transfer request disable */
	/*
	 * When TRD is set, DMA transfers cease when the INT bit in
	 * the MSS status reg is set. Must be cleared for automode
	 * DMA, set otherwise.
	 */
#define	MSS_IDXMASK		0x1f	/* mask for indirect address */

#define MSS_IDATA  	(1 + 4)
	/*
	 * data to be transferred to the indirect register addressed
	 * by index addr. During init and sw. powerdown, cannot be
	 * written to, and is always read as 0x80 (consistent with the
	 * busy flag).
	 */

#define MSS_STATUS      (2 + 4)

#define	IS_CUL		0x80	/* capture upper/lower */
#define	IS_CLR		0x40	/* capture left/right */
#define	IS_CRDY		0x20	/* capture ready for programmed i/o */
#define	IS_SER		0x10	/* sample error (overrun/underrun) */
#define	IS_PUL		0x08	/* playback upper/lower */
#define	IS_PLR		0x04	/* playback left/right */
#define	IS_PRDY		0x02	/* playback ready for programmed i/o */
#define	IS_INT		0x01	/* int status (1 = active) */
	/*
	 * IS_INT is clreared by any write to the status register.
	 */
#if 0
#define io_Polled_IO(d)         ((d)->io_base+3+4)
	/*
	 * this register is used in case of polled i/o
	 */
#endif

/*
 * The MSS has a set of 16 (or 32 depending on the model) indirect
 * registers accessible through the data port by specifying the
 * appropriate address in the address register.
 *
 * The 16 low registers are uniformly handled in AD1848/CS4248 compatible
 * mode (often called MODE1). For the upper 16 registers there are
 * some differences among different products, mainly Crystal uses them
 * differently from OPTi.
 *
 */

/*
 * volume registers
 */

#define	I6_MUTE		0x80

/*
 * register I9 -- interface configuration.
 */

#define	I9_PEN		0x01	/* playback enable */
#define	I9_CEN		0x02	/* capture enable */

/*
 * values used in bd_flags
 */
#define	BD_F_MCE_BIT	0x0001
#define	BD_F_IRQ_OK	0x0002
#define	BD_F_TMR_RUN	0x0004
#define BD_F_MSS_OFFSET 0x0008	/* offset mss writes by -4 */
#define BD_F_DUPLEX	0x0010
#define BD_F_924PNP	0x0020	/* OPTi924 is in PNP mode */

/*
 * sound/ad1848_mixer.h
 *
 * Definitions for the mixer of AD1848 and compatible codecs.
 *
 * Copyright by Hannu Savolainen 1994
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * The AD1848 codec has generic input lines called Line, Aux1 and Aux2.
 * Soundcard manufacturers have connected actual inputs (CD, synth, line,
 * etc) to these inputs in different order. Therefore it's difficult
 * to assign mixer channels to these inputs correctly. The following
 * contains two alternative mappings. The first one is for GUS MAX and
 * the second is just a generic one (line1, line2 and line3).
 * (Actually this is not a mapping but rather some kind of interleaving
 * solution).
 */

#define MSS_REC_DEVICES	\
    (SOUND_MASK_LINE | SOUND_MASK_MIC | SOUND_MASK_CD|SOUND_MASK_IMIX)


/*
 * Table of mixer registers. There is a default table for the
 * AD1848/CS423x clones, one for the OPTI931 and one for the
 * OPTi930. As more MSS clones come out, there ought to be
 * more tables.
 *
 * Fields in the table are : polarity, register, offset, bits
 *
 * The channel numbering used by individual soundcards is not fixed.
 * Some cards have assigned different meanings for the AUX1, AUX2
 * and LINE inputs. Some have different features...
 *
 * Following there is a macro ...MIXER_DEVICES which is a bitmap
 * of all non-zero fields in the table.
 * MODE1_MIXER_DEVICES is the basic mixer of the 1848 in mode 1
 * registers I0..I15)
 *
 */

mixer_ent mix_devices[32][2] = {
MIX_NONE(SOUND_MIXER_VOLUME),
MIX_NONE(SOUND_MIXER_BASS),
MIX_NONE(SOUND_MIXER_TREBLE),
MIX_ENT(SOUND_MIXER_SYNTH,	 2, 1, 0, 5,	 3, 1, 0, 5),
MIX_ENT(SOUND_MIXER_PCM,	 6, 1, 0, 6,	 7, 1, 0, 6),
MIX_ENT(SOUND_MIXER_SPEAKER,	26, 1, 0, 4,	 0, 0, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,	18, 1, 0, 5,	19, 1, 0, 5),
MIX_ENT(SOUND_MIXER_MIC,	 0, 0, 5, 1,	 1, 0, 5, 1),
MIX_ENT(SOUND_MIXER_CD,	 	 4, 1, 0, 5,	 5, 1, 0, 5),
MIX_ENT(SOUND_MIXER_IMIX,	13, 1, 2, 6,	 0, 0, 0, 0),
MIX_NONE(SOUND_MIXER_ALTPCM),
MIX_NONE(SOUND_MIXER_RECLEV),
MIX_ENT(SOUND_MIXER_IGAIN,	 0, 0, 0, 4,	 1, 0, 0, 4),
MIX_NONE(SOUND_MIXER_OGAIN),
MIX_NONE(SOUND_MIXER_LINE1),
MIX_NONE(SOUND_MIXER_LINE2),
MIX_NONE(SOUND_MIXER_LINE3),
};

#define MODE2_MIXER_DEVICES	\
    (SOUND_MASK_SYNTH | SOUND_MASK_PCM    | SOUND_MASK_SPEAKER | \
     SOUND_MASK_LINE  | SOUND_MASK_MIC    | SOUND_MASK_CD      | \
     SOUND_MASK_IMIX  | SOUND_MASK_IGAIN                         )

#define MODE1_MIXER_DEVICES	\
    (SOUND_MASK_SYNTH | SOUND_MASK_PCM    | SOUND_MASK_MIC     | \
     SOUND_MASK_CD    | SOUND_MASK_IMIX   | SOUND_MASK_IGAIN     )


mixer_ent opti930_devices[32][2] = {
MIX_ENT(SOUND_MIXER_VOLUME,	22, 1, 0, 4,	23, 1, 0, 4),
MIX_NONE(SOUND_MIXER_BASS),
MIX_NONE(SOUND_MIXER_TREBLE),
MIX_ENT(SOUND_MIXER_SYNTH,	4,  1, 0, 4,    5,  1, 0, 4),
MIX_ENT(SOUND_MIXER_PCM,	6,  1, 1, 5,	7,  1, 1, 5),
MIX_ENT(SOUND_MIXER_LINE,	18, 1, 1, 4,	19, 1, 1, 4),
MIX_NONE(SOUND_MIXER_SPEAKER),
MIX_ENT(SOUND_MIXER_MIC,	21, 1, 0, 4,	22, 1, 0, 4),
MIX_ENT(SOUND_MIXER_CD,		2,  1, 1, 4,	3,  1, 1, 4),
MIX_NONE(SOUND_MIXER_IMIX),
MIX_NONE(SOUND_MIXER_ALTPCM),
MIX_NONE(SOUND_MIXER_RECLEV),
MIX_NONE(SOUND_MIXER_IGAIN),
MIX_NONE(SOUND_MIXER_OGAIN),
MIX_NONE(SOUND_MIXER_LINE1),
MIX_NONE(SOUND_MIXER_LINE2),
MIX_NONE(SOUND_MIXER_LINE3),
};

#define OPTI930_MIXER_DEVICES	\
    (SOUND_MASK_VOLUME | SOUND_MASK_SYNTH | SOUND_MASK_PCM | \
     SOUND_MASK_LINE   | SOUND_MASK_MIC   | SOUND_MASK_CD )

/*
 * entries for the opti931...
 */

mixer_ent opti931_devices[32][2] = {	/* for the opti931 */
MIX_ENT(SOUND_MIXER_VOLUME,	22, 1, 1, 5,	23, 1, 1, 5),
MIX_NONE(SOUND_MIXER_BASS),
MIX_NONE(SOUND_MIXER_TREBLE),
MIX_ENT(SOUND_MIXER_SYNTH,	 4, 1, 1, 4,	 5, 1, 1, 4),
MIX_ENT(SOUND_MIXER_PCM,	 6, 1, 0, 5,	 7, 1, 0, 5),
MIX_NONE(SOUND_MIXER_SPEAKER),
MIX_ENT(SOUND_MIXER_LINE,	18, 1, 1, 4,	19, 1, 1, 4),
MIX_ENT(SOUND_MIXER_MIC,	 0, 0, 5, 1,	 1, 0, 5, 1),
MIX_ENT(SOUND_MIXER_CD,	 	 2, 1, 1, 4,	 3, 1, 1, 4),
MIX_NONE(SOUND_MIXER_IMIX),
MIX_NONE(SOUND_MIXER_ALTPCM),
MIX_NONE(SOUND_MIXER_RECLEV),
MIX_ENT(SOUND_MIXER_IGAIN,	 0, 0, 0, 4,	 1, 0, 0, 4),
MIX_NONE(SOUND_MIXER_OGAIN),
MIX_ENT(SOUND_MIXER_LINE1, 	16, 1, 1, 4,	17, 1, 1, 4),
MIX_NONE(SOUND_MIXER_LINE2),
MIX_NONE(SOUND_MIXER_LINE3),
};

#define OPTI931_MIXER_DEVICES	\
    (SOUND_MASK_VOLUME | SOUND_MASK_SYNTH | SOUND_MASK_PCM | \
     SOUND_MASK_LINE   | SOUND_MASK_MIC   | SOUND_MASK_CD  | \
     SOUND_MASK_IGAIN  | SOUND_MASK_LINE1                    )

/*
 * Register definitions for the Yamaha OPL3-SA[23x].
 */
#define OPL3SAx_POWER	0x01		/* Power Management (R/W) */
#define OPL3SAx_POWER_PDX	0x01	/* Set to 1 to halt oscillator */
#define OPL3SAx_POWER_PDN	0x02	/* Set to 1 to power down */
#define OPL3SAx_POWER_PSV	0x04	/* Set to 1 to power save */
#define OPL3SAx_POWER_ADOWN	0x20	/* Analog power (?) */

#define OPL3SAx_SYSTEM	0x02		/* System control (R/W) */
#define OPL3SAx_SYSTEM_VZE	0x01	/* I2S audio routing */
#define OPL3SAx_SYSTEM_IDSEL	0x03	/* SB compat version select */
#define OPL3SAx_SYSTEM_SBHE	0x80	/* 0 for AT bus, 1 for XT bus */

#define OPL3SAx_IRQCONF	0x03		/* Interrupt configuration (R/W */
#define OPL3SAx_IRQCONF_WSSA	0x01	/* WSS interrupts through IRQA */
#define OPL3SAx_IRQCONF_SBA	0x02	/* WSS interrupts through IRQA */
#define OPL3SAx_IRQCONF_MPUA	0x04	/* WSS interrupts through IRQA */
#define OPL3SAx_IRQCONF_OPL3A	0x08	/* WSS interrupts through IRQA */
#define OPL3SAx_IRQCONF_WSSB	0x10	/* WSS interrupts through IRQB */
#define OPL3SAx_IRQCONF_SBB	0x20	/* WSS interrupts through IRQB */
#define OPL3SAx_IRQCONF_MPUB	0x40	/* WSS interrupts through IRQB */
#define OPL3SAx_IRQCONF_OPL3B	0x80	/* WSS interrupts through IRQB */

#define OPL3SAx_IRQSTATUSA 0x04		/* Interrupt (IRQ-A) Status (RO) */
#define OPL3SAx_IRQSTATUSB 0x05		/* Interrupt (IRQ-B) Status (RO) */
#define OPL3SAx_IRQSTATUS_PI	0x01	/* Playback Flag of CODEC */
#define OPL3SAx_IRQSTATUS_CI	0x02	/* Recording Flag of CODEC */
#define OPL3SAx_IRQSTATUS_TI	0x04	/* Timer Flag of CODEC */
#define OPL3SAx_IRQSTATUS_SB	0x08	/* SB compat Playback Interrupt Flag */
#define OPL3SAx_IRQSTATUS_MPU	0x10	/* MPU401 Interrupt Flag */
#define OPL3SAx_IRQSTATUS_OPL3	0x20	/* Internal FM Timer Flag */
#define OPL3SAx_IRQSTATUS_MV	0x40	/* HW Volume Interrupt Flag */
#define OPL3SAx_IRQSTATUS_PI	0x01	/* Playback Flag of CODEC */
#define OPL3SAx_IRQSTATUS_CI	0x02	/* Recording Flag of CODEC */
#define OPL3SAx_IRQSTATUS_TI	0x04	/* Timer Flag of CODEC */
#define OPL3SAx_IRQSTATUS_SB	0x08	/* SB compat Playback Interrupt Flag */
#define OPL3SAx_IRQSTATUS_MPU	0x10	/* MPU401 Interrupt Flag */
#define OPL3SAx_IRQSTATUS_OPL3	0x20	/* Internal FM Timer Flag */
#define OPL3SAx_IRQSTATUS_MV	0x40	/* HW Volume Interrupt Flag */

#define OPL3SAx_DMACONF	0x06		/* DMA configuration (R/W) */
#define OPL3SAx_DMACONF_WSSPA	0x01	/* WSS Playback on DMA-A */
#define OPL3SAx_DMACONF_WSSRA	0x02	/* WSS Recording on DMA-A */
#define OPL3SAx_DMACONF_SBA	0x02	/* SB Playback on DMA-A */
#define OPL3SAx_DMACONF_WSSPB	0x10	/* WSS Playback on DMA-A */
#define OPL3SAx_DMACONF_WSSRB	0x20	/* WSS Recording on DMA-A */
#define OPL3SAx_DMACONF_SBB	0x20	/* SB Playback on DMA-A */

#define OPL3SAx_VOLUMEL	0x07		/* Master Volume Left (R/W) */
#define OPL3SAx_VOLUMEL_MVL	0x0f	/* Attenuation level */
#define OPL3SAx_VOLUMEL_MVLM	0x80	/* Mute */

#define OPL3SAx_VOLUMER	0x08		/* Master Volume Right (R/W) */
#define OPL3SAx_VOLUMER_MVR	0x0f	/* Attenuation level */
#define OPL3SAx_VOLUMER_MVRM	0x80	/* Mute */

#define OPL3SAx_MIC	0x09		/* MIC Volume (R/W) */
#define OPL3SAx_VOLUMER_MCV	0x1f	/* Attenuation level */
#define OPL3SAx_VOLUMER_MICM	0x80	/* Mute */

#define OPL3SAx_MISC	0x0a		/* Miscellaneous */
#define OPL3SAx_MISC_VER	0x07	/* Version */
#define OPL3SAx_MISC_MODE	0x08	/* SB or WSS mode */
#define OPL3SAx_MISC_MCSW	0x10	/*  */
#define OPL3SAx_MISC_VEN	0x80	/* Enable hardware volume control */

#define OPL3SAx_WSSDMA	0x0b		/* WSS DMA Counter (RW) (4 regs) */

#define OPL3SAx_WSSIRQSCAN 0x0f		/* WSS Interrupt Scan out/in (R/W) */
#define OPL3SAx_WSSIRQSCAN_SPI	0x01
#define OPL3SAx_WSSIRQSCAN_SCI	0x02
#define OPL3SAx_WSSIRQSCAN_STI	0x04

#define OPL3SAx_SBSTATE	0x10		/* SB compat Internal State (R/W) */
#define OPL3SAx_SBSTATE_SBPDR	0x01	/* SB Power Down Request */
#define OPL3SAx_SBSTATE_SE	0x02	/* Scan Enable */
#define OPL3SAx_SBSTATE_SM	0x04	/* Scan Mode */
#define OPL3SAx_SBSTATE_SS	0x08	/* Scan Select */
#define OPL3SAx_SBSTATE_SBPDA	0x80	/* SB Power Down Acknowledge */

#define OPL3SAx_SBDATA 0x11		/* SB compat State Scan Data (R/W) */

#define OPL3SAx_DIGITALPOWER 0x12	/* Digital Partial Power Down (R/W) */
#define OPL3SAx_DIGITALPOWER_PnP  0x01
#define OPL3SAx_DIGITALPOWER_SB	  0x02
#define OPL3SAx_DIGITALPOWER_WSSP 0x04
#define OPL3SAx_DIGITALPOWER_WSSR 0x08
#define OPL3SAx_DIGITALPOWER_FM	  0x10
#define OPL3SAx_DIGITALPOWER_MCLK0 0x20
#define OPL3SAx_DIGITALPOWER_MPU  0x40
#define OPL3SAx_DIGITALPOWER_JOY  0x80

#define OPL3SAx_ANALOGPOWER 0x13	/* Analog Partial Power Down (R/W) */
#define OPL3SAx_ANALOGPOWER_WIDE  0x01
#define OPL3SAx_ANALOGPOWER_SBDAC 0x02
#define OPL3SAx_ANALOGPOWER_DA    0x04
#define OPL3SAx_ANALOGPOWER_AD    0x08
#define OPL3SAx_ANALOGPOWER_FMDAC 0x10

#define OPL3SAx_WIDE	0x14		/* Enhanced control(WIDE) (R/W) */
#define OPL3SAx_WIDE_WIDEL	0x07	/* Wide level on Left Channel */
#define OPL3SAx_WIDE_WIDER	0x70	/* Wide level on Right Channel */

#define OPL3SAx_BASS	0x15		/* Enhanced control(BASS) (R/W) */
#define OPL3SAx_BASS_BASSL	0x07	/* Bass level on Left Channel */
#define OPL3SAx_BASS_BASSR	0x70	/* Bass level on Right Channel */

#define OPL3SAx_TREBLE	0x16		/* Enhanced control(TREBLE) (R/W) */
#define OPL3SAx_TREBLE_TREBLEL	0x07	/* Treble level on Left Channel */
#define OPL3SAx_TREBLE_TREBLER	0x70	/* Treble level on Right Channel */

#define OPL3SAx_HWVOL	0x17		/* HW Volume IRQ Configuration (R/W) */
#define OPL3SAx_HWVOL_IRQA	0x10	/* HW Volume IRQ on IRQ-A */
#define OPL3SAx_HWVOL_IRQB	0x20	/* HW Volume IRQ on IRQ-B */


