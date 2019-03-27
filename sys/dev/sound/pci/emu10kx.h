/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Cameron Grant <gandalf@vilnya.demon.co.uk>
 * Copyright (c) 2003-2006 Yuriy Tsibizov <yuriy.tsibizov@gfk.ru>
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
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHERIN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef EMU10KX_H
#define	EMU10KX_H

#define	SND_EMU10KX_MINVER	1
#define	SND_EMU10KX_PREFVER	1
#define	SND_EMU10KX_MAXVER	1

#ifdef _KERNEL

#define	EMUPAGESIZE	4096
#define	NUM_G		64
/* XXX some (empty) samples are played when play buffer is > EMUPAGESIZE */
#define	EMU_PLAY_BUFSZ	EMUPAGESIZE
/* Recording is limited by EMUPAGESIZE*16=64K buffer */
#define	EMU_REC_BUFSZ	EMUPAGESIZE*16
#define	EMU_MAX_BUFSZ	EMUPAGESIZE*16
#define	EMU_MAXPAGES	8192


#define	EMU_VAR_FUNC		0
#define	EMU_VAR_ROUTE		1
#define	EMU_VAR_ISEMU10K1 	2
#define	EMU_VAR_MCH_DISABLED 	3
#define	EMU_VAR_MCH_REC 	4

#define EMU_A_IOCFG_DISABLE_ANALOG	0x0040	/* = 'enable' for Audigy2 */
#define EMU_A_IOCFG_GPOUT2	0x0001
#define EMU_AC97SLOT_REAR_RIGHT	0x01
#define EMU_AC97SLOT_REAR_LEFT	0x02
#define EMU_HCFG_GPOUT0		0x00001000
#define EMU_HCFG_GPOUT1		0x00000800
#define EMU_HCFG_GPOUT2		0x00000400

#define	RT_FRONT		0
#define	RT_REAR			1
#define	RT_CENTER		2
#define	RT_SUB			3
#define	RT_SIDE			4
#define	RT_MCHRECORD		5
#define	RT_COUNT		6

/* mixer controls */
/* fx play */
#define	M_FX0_FRONT_L		0
#define	M_FX1_FRONT_R		1
#define	M_FX2_REAR_L		2
#define	M_FX3_REAR_R		3
#define	M_FX4_CENTER		4
#define	M_FX5_SUBWOOFER		5
#define	M_FX6_SIDE_L		6
#define	M_FX7_SIDE_R		7
/* fx rec */
#define	M_FX0_REC_L		8
#define	M_FX1_REC_R		9
/* inputs play */
#define	M_IN0_FRONT_L		10
#define	M_IN0_FRONT_R		11
#define	M_IN1_FRONT_L		12
#define	M_IN1_FRONT_R		13
#define	M_IN2_FRONT_L		14
#define	M_IN2_FRONT_R		15
#define	M_IN3_FRONT_L		16
#define	M_IN3_FRONT_R		17
#define	M_IN4_FRONT_L		18
#define	M_IN4_FRONT_R		19
#define	M_IN5_FRONT_L		20
#define	M_IN5_FRONT_R		21
#define	M_IN6_FRONT_L		22
#define	M_IN6_FRONT_R		23
#define	M_IN7_FRONT_L		24
#define	M_IN7_FRONT_R		25
/* inputs rec */
#define	M_IN0_REC_L		26
#define	M_IN0_REC_R		27
#define	M_IN1_REC_L		28
#define	M_IN1_REC_R		29
#define	M_IN2_REC_L		30
#define	M_IN2_REC_R		31
#define	M_IN3_REC_L		32
#define	M_IN3_REC_R		33
#define	M_IN4_REC_L		34
#define	M_IN4_REC_R		35
#define	M_IN5_REC_L		36
#define	M_IN5_REC_R		37
#define	M_IN6_REC_L		38
#define	M_IN6_REC_R		39
#define	M_IN7_REC_L		40
#define	M_IN7_REC_R		41
/* master volume */
#define	M_MASTER_FRONT_L	42
#define	M_MASTER_FRONT_R	43
#define	M_MASTER_REAR_L		44
#define	M_MASTER_REAR_R		45
#define	M_MASTER_CENTER		46
#define	M_MASTER_SUBWOOFER	47
#define	M_MASTER_SIDE_L		48
#define	M_MASTER_SIDE_R		49
/* master rec volume */
#define	M_MASTER_REC_L		50
#define	M_MASTER_REC_R		51

#define	NUM_MIXERS		52

struct emu_sc_info;

/* MIDI device parameters */
struct emu_midiinfo {
	struct emu_sc_info *card;
	int		port;
	int		portnr;
};

/* PCM device parameters */
struct emu_pcminfo {
	struct emu_sc_info *card;
	int		route;
};

int	emu_intr_register(struct emu_sc_info *sc, uint32_t inte_mask, uint32_t intr_mask, uint32_t(*func) (void *softc, uint32_t irq), void *isc);
int	emu_intr_unregister(struct emu_sc_info *sc, int ihandle);

uint32_t emu_rd(struct emu_sc_info *sc, unsigned int regno, unsigned int size);
void	emu_wr(struct emu_sc_info *sc, unsigned int regno, uint32_t data, unsigned int size);

uint32_t emu_rdptr(struct emu_sc_info *sc, unsigned int chn, unsigned int reg);
void	emu_wrptr(struct emu_sc_info *sc, unsigned int chn, unsigned int reg, uint32_t data);

uint32_t emu_rd_p16vptr(struct emu_sc_info *sc, uint16_t chn, uint16_t reg);
void	emu_wr_p16vptr(struct emu_sc_info *sc, uint16_t chn, uint16_t reg, uint32_t data);

int	emu_timer_create(struct emu_sc_info *sc);
int	emu_timer_set(struct emu_sc_info *sc, int timer, int delay);
int	emu_timer_enable(struct emu_sc_info *sc, int timer, int go);
int	emu_timer_clear(struct emu_sc_info *sc, int timer);

struct emu_voice;

struct emu_route {
	int	routing_left[8];
	int	amounts_left[8];
	int	routing_right[8];
	int	amounts_right[8];
};

struct emu_voice* emu_valloc(struct emu_sc_info *sc);
void 	emu_vfree(struct emu_sc_info *sc, struct emu_voice *v);
int	emu_vinit(struct emu_sc_info *sc, struct emu_voice *m, struct emu_voice *s,
    uint32_t sz, struct snd_dbuf *b);
void	emu_vroute(struct emu_sc_info *sc, struct emu_route *rt,  struct emu_voice *v);
void	emu_vsetup(struct emu_voice *v, int fmt, int spd);
void	emu_vwrite(struct emu_sc_info *sc, struct emu_voice *v);
void	emu_vtrigger(struct emu_sc_info *sc, struct emu_voice *v, int go);
int	emu_vpos(struct emu_sc_info *sc, struct emu_voice *v);

bus_dma_tag_t emu_gettag(struct emu_sc_info *sc);

void	emumix_set_volume(struct emu_sc_info *sc, int mixer_idx, int volume);
int	emumix_get_volume(struct emu_sc_info *sc, int mixer_idx);

void	emu_enable_ir(struct emu_sc_info *sc);
#endif				/* _KERNEL */
#endif				/* EMU10K1_H */
