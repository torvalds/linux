/*     
 **********************************************************************
 *     sblive_fx.h
 *     Copyright 1999, 2000 Creative Labs, Inc. 
 * 
 ********************************************************************** 
 * 
 *     Date                 Author          Summary of changes 
 *     ----                 ------          ------------------ 
 *     October 20, 1999     Bertrand Lee    base code release 
 * 
 ********************************************************************** 
 * 
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version. 
 * 
 *     This program is distributed in the hope that it will be useful, 
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 *     GNU General Public License for more details. 
 * 
 *     You should have received a copy of the GNU General Public 
 *     License along with this program; if not, write to the Free 
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, 
 *     USA. 
 * 
 ********************************************************************** 
 */

#ifndef _EFXMGR_H
#define _EFXMGR_H

struct emu_efx_info_t{
	int opcode_shift;
	int high_operand_shift;
	int instruction_start;
	int gpr_base;
	int output_base;
};


#define WRITE_EFX(a, b, c) sblive_writeptr((a), emu_efx_info[card->is_audigy].instruction_start + (b), 0, (c))

#define OP(op, z, w, x, y) \
	do { WRITE_EFX(card, (pc) * 2, ((x) << emu_efx_info[card->is_audigy].high_operand_shift) | (y)); \
	WRITE_EFX(card, (pc) * 2 + 1, ((op) << emu_efx_info[card->is_audigy].opcode_shift ) | ((z) << emu_efx_info[card->is_audigy].high_operand_shift) | (w)); \
	++pc; } while (0)

#define NUM_INPUTS 0x20
#define NUM_OUTPUTS 0x20
#define NUM_GPRS 0x100

#define A_NUM_INPUTS 0x60
#define A_NUM_OUTPUTS 0x60  //fixme: this may or may not be true
#define A_NUM_GPRS 0x200

#define GPR_NAME_SIZE   32
#define PATCH_NAME_SIZE 32

struct dsp_rpatch {
	char name[PATCH_NAME_SIZE];
	u16 code_start;
	u16 code_size;

	unsigned long gpr_used[NUM_GPRS / (sizeof(unsigned long) * 8) + 1];
	unsigned long gpr_input[NUM_GPRS / (sizeof(unsigned long) * 8) + 1];
	unsigned long route[NUM_OUTPUTS];
	unsigned long route_v[NUM_OUTPUTS];
};

struct dsp_patch {
	char name[PATCH_NAME_SIZE];
	u8 id;
	unsigned long input;	/* bitmap of the lines used as inputs */
	unsigned long output;	/* bitmap of the lines used as outputs */
	u16 code_start;
	u16 code_size;

	unsigned long gpr_used[NUM_GPRS / (sizeof(unsigned long) * 8) + 1];	/* bitmap of used gprs */
	unsigned long gpr_input[NUM_GPRS / (sizeof(unsigned long) * 8) + 1];
	u8 traml_istart;	/* starting address of the internal tram lines used */
	u8 traml_isize;		/* number of internal tram lines used */

	u8 traml_estart;
	u8 traml_esize;

	u16 tramb_istart;        /* starting address of the internal tram memory used */
	u16 tramb_isize;         /* amount of internal memory used */
	u32 tramb_estart;
	u32 tramb_esize;
};

struct dsp_gpr {
	u8 type;			/* gpr type, STATIC, DYNAMIC, INPUT, OUTPUT, CONTROL */
	char name[GPR_NAME_SIZE];	/* gpr value, only valid for control gprs */
	s32 min, max;			/* value range for this gpr, only valid for control gprs */
	u8 line;			/* which input/output line is the gpr attached, only valid for input/output gprs */
	u8 usage;
};

enum {
	GPR_TYPE_NULL = 0,
	GPR_TYPE_IO,
	GPR_TYPE_STATIC,
	GPR_TYPE_DYNAMIC,
	GPR_TYPE_CONTROL,
	GPR_TYPE_CONSTANT
};

#define GPR_BASE 0x100
#define OUTPUT_BASE 0x20

#define A_GPR_BASE 0x400
#define A_OUTPUT_BASE 0x60

#define MAX_PATCHES_PAGES 32

struct patch_manager {
	void *patch[MAX_PATCHES_PAGES];
	int current_pages;
	struct dsp_rpatch rpatch;
	struct dsp_gpr gpr[NUM_GPRS];   /* gpr usage table */
	spinlock_t lock;
	s16 ctrl_gpr[SOUND_MIXER_NRDEVICES][2];
};

#define PATCHES_PER_PAGE (PAGE_SIZE / sizeof(struct dsp_patch))

#define PATCH(mgr, i) ((struct dsp_patch *) (mgr)->patch[(i) / PATCHES_PER_PAGE] + (i) % PATCHES_PER_PAGE)

/* PCM volume control */
#define TMP_PCM_L     0x100 //temp PCM L (after the vol control)       
#define TMP_PCM_R     0x101
#define VOL_PCM_L     0x102 //vol PCM
#define VOL_PCM_R     0x103

/* Routing patch */
#define TMP_AC_L      0x104 //tmp ac97 out
#define TMP_AC_R      0x105
#define TMP_REAR_L    0x106 //output - Temp Rear
#define TMP_REAR_R    0x107
#define TMP_DIGI_L    0x108 //output - Temp digital
#define TMP_DIGI_R    0x109
#define DSP_VOL_L     0x10a // main dsp volume
#define DSP_VOL_R     0x10b

/* hw inputs */
#define PCM_IN_L 	0x00
#define PCM_IN_R 	0x01

#define PCM1_IN_L        0x04
#define PCM1_IN_R        0x05
//mutilchannel playback stream appear here:

#define MULTI_FRONT_L	0x08
#define MULTI_FRONT_R	0x09
#define MULTI_REAR_L	0x0a
#define MULTI_REAR_R	0x0b
#define MULTI_CENTER	0x0c
#define MULTI_LFE	0x0d

#define AC97_IN_L	0x10
#define AC97_IN_R	0x11
#define SPDIF_CD_L	0x12
#define SPDIF_CD_R	0x13

/* hw outputs */
#define AC97_FRONT_L	0x20
#define AC97_FRONT_R	0x21
#define DIGITAL_OUT_L	0x22
#define DIGITAL_OUT_R	0x23
#define DIGITAL_CENTER	0x24
#define DIGITAL_LFE	0x25

#define ANALOG_REAR_L	0x28
#define ANALOG_REAR_R	0x29
#define ADC_REC_L	0x2a
#define ADC_REC_R	0x2b

#define ANALOG_CENTER	0x31
#define ANALOG_LFE	0x32


#define INPUT_PATCH_START(patch, nm, ln, i)		\
do {							\
	patch = PATCH(mgr, patch_n);			\
	strcpy(patch->name, nm);			\
	patch->code_start = pc * 2;			\
	patch->input = (1<<(0x1f&ln));			\
	patch->output= (1<<(0x1f&ln));			\
	patch->id = i;					\
} while(0)

#define INPUT_PATCH_END(patch)				\
do {							\
	patch->code_size = pc * 2 - patch->code_start;	\
	patch_n++;					\
} while(0)


#define ROUTING_PATCH_START(patch, nm)	\
do {					\
	patch = &mgr->rpatch;		\
	strcpy(patch->name, nm);	\
	patch->code_start = pc * 2;	\
} while(0)

#define ROUTING_PATCH_END(patch)			\
do {                                                    \
	patch->code_size = pc * 2 - patch->code_start;      \
} while(0)

#define CONNECT(input, output) set_bit(input, &rpatch->route[(output) - OUTPUT_BASE]);

#define CONNECT_V(input, output) set_bit(input, &rpatch->route_v[(output) - OUTPUT_BASE]);

#define OUTPUT_PATCH_START(patch, nm, ln, i)		\
do {							\
	patch = PATCH(mgr, patch_n);			\
	strcpy(patch->name, nm);			\
	patch->code_start = pc * 2;			\
	patch->input = (1<<(0x1f&ln));			\
	patch->output= (1<<(0x1f&ln));			\
	patch->id = i;					\
} while(0)

#define OUTPUT_PATCH_END(patch)				\
do {							\
	patch->code_size = pc * 2 - patch->code_start;	\
	patch_n++;					\
} while(0)

#define GET_OUTPUT_GPR(patch, g, ln)			\
do {							\
	mgr->gpr[(g) - GPR_BASE].type = GPR_TYPE_IO;	\
	mgr->gpr[(g) - GPR_BASE].usage++;		\
	mgr->gpr[(g) - GPR_BASE].line = ln;		\
	set_bit((g) - GPR_BASE, patch->gpr_used);	\
} while(0)

#define GET_INPUT_GPR(patch, g, ln)			\
do {							\
	mgr->gpr[(g) - GPR_BASE].type = GPR_TYPE_IO;	\
	mgr->gpr[(g) - GPR_BASE].usage++;		\
	mgr->gpr[(g) - GPR_BASE].line = ln;		\
	set_bit((g) - GPR_BASE, patch->gpr_used);	\
	set_bit((g) - GPR_BASE, patch->gpr_input);	\
} while(0)

#define GET_DYNAMIC_GPR(patch, g)				\
do {								\
	mgr->gpr[(g) - GPR_BASE].type = GPR_TYPE_DYNAMIC;	\
	mgr->gpr[(g) - GPR_BASE].usage++;			\
	set_bit((g) - GPR_BASE, patch->gpr_used);          	\
} while(0)

#define GET_CONTROL_GPR(patch, g, nm, a, b)			\
do {								\
	strcpy(mgr->gpr[(g) - GPR_BASE].name, nm);		\
	mgr->gpr[(g) - GPR_BASE].type = GPR_TYPE_CONTROL;	\
	mgr->gpr[(g) - GPR_BASE].usage++;			\
	mgr->gpr[(g) - GPR_BASE].min = a;			\
	mgr->gpr[(g) - GPR_BASE].max = b;			\
	sblive_writeptr(card, g, 0, b);				\
	set_bit((g) - GPR_BASE, patch->gpr_used);		\
} while(0)

#endif /* _EFXMGR_H */
