/*
 * HD audio interface patch for Creative CA0132 chip
 *
 * Copyright (c) 2011, Creative Technology Ltd.
 *
 * Based on patch_ca0110.c
 * Copyright (c) 2008 Takashi Iwai <tiwai@suse.de>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <sound/core.h>
#include <sound/hda_codec.h>
#include "hda_local.h"
#include "hda_auto_parser.h"
#include "hda_jack.h"

#include "ca0132_regs.h"

/* Enable this to see controls for tuning purpose. */
/*#define ENABLE_TUNING_CONTROLS*/

#ifdef ENABLE_TUNING_CONTROLS
#include <sound/tlv.h>
#endif

#define FLOAT_ZERO	0x00000000
#define FLOAT_ONE	0x3f800000
#define FLOAT_TWO	0x40000000
#define FLOAT_THREE     0x40400000
#define FLOAT_EIGHT     0x41000000
#define FLOAT_MINUS_5	0xc0a00000

#define UNSOL_TAG_DSP	0x16

#define DSP_DMA_WRITE_BUFLEN_INIT (1UL<<18)
#define DSP_DMA_WRITE_BUFLEN_OVLY (1UL<<15)

#define DMA_TRANSFER_FRAME_SIZE_NWORDS		8
#define DMA_TRANSFER_MAX_FRAME_SIZE_NWORDS	32
#define DMA_OVERLAY_FRAME_SIZE_NWORDS		2

#define MASTERCONTROL				0x80
#define MASTERCONTROL_ALLOC_DMA_CHAN		10
#define MASTERCONTROL_QUERY_SPEAKER_EQ_ADDRESS	60

#define WIDGET_CHIP_CTRL      0x15
#define WIDGET_DSP_CTRL       0x16

#define MEM_CONNID_MICIN1     3
#define MEM_CONNID_MICIN2     5
#define MEM_CONNID_MICOUT1    12
#define MEM_CONNID_MICOUT2    14
#define MEM_CONNID_WUH        10
#define MEM_CONNID_DSP        16
#define MEM_CONNID_DMIC       100

#define SCP_SET    0
#define SCP_GET    1

#define EFX_FILE   "ctefx.bin"
#define DESKTOP_EFX_FILE   "ctefx-desktop.bin"
#define R3DI_EFX_FILE  "ctefx-r3di.bin"

#ifdef CONFIG_SND_HDA_CODEC_CA0132_DSP
MODULE_FIRMWARE(EFX_FILE);
MODULE_FIRMWARE(DESKTOP_EFX_FILE);
MODULE_FIRMWARE(R3DI_EFX_FILE);
#endif

static const char *const dirstr[2] = { "Playback", "Capture" };

#define NUM_OF_OUTPUTS 3
enum {
	SPEAKER_OUT,
	HEADPHONE_OUT,
	SURROUND_OUT
};

enum {
	DIGITAL_MIC,
	LINE_MIC_IN
};

/* Strings for Input Source Enum Control */
static const char *const in_src_str[3] = {"Rear Mic", "Line", "Front Mic" };
#define IN_SRC_NUM_OF_INPUTS 3
enum {
	REAR_MIC,
	REAR_LINE_IN,
	FRONT_MIC,
};

enum {
#define VNODE_START_NID    0x80
	VNID_SPK = VNODE_START_NID,			/* Speaker vnid */
	VNID_MIC,
	VNID_HP_SEL,
	VNID_AMIC1_SEL,
	VNID_HP_ASEL,
	VNID_AMIC1_ASEL,
	VNODE_END_NID,
#define VNODES_COUNT  (VNODE_END_NID - VNODE_START_NID)

#define EFFECT_START_NID    0x90
#define OUT_EFFECT_START_NID    EFFECT_START_NID
	SURROUND = OUT_EFFECT_START_NID,
	CRYSTALIZER,
	DIALOG_PLUS,
	SMART_VOLUME,
	X_BASS,
	EQUALIZER,
	OUT_EFFECT_END_NID,
#define OUT_EFFECTS_COUNT  (OUT_EFFECT_END_NID - OUT_EFFECT_START_NID)

#define IN_EFFECT_START_NID  OUT_EFFECT_END_NID
	ECHO_CANCELLATION = IN_EFFECT_START_NID,
	VOICE_FOCUS,
	MIC_SVM,
	NOISE_REDUCTION,
	IN_EFFECT_END_NID,
#define IN_EFFECTS_COUNT  (IN_EFFECT_END_NID - IN_EFFECT_START_NID)

	VOICEFX = IN_EFFECT_END_NID,
	PLAY_ENHANCEMENT,
	CRYSTAL_VOICE,
	EFFECT_END_NID,
	OUTPUT_SOURCE_ENUM,
	INPUT_SOURCE_ENUM,
	XBASS_XOVER,
	EQ_PRESET_ENUM,
	SMART_VOLUME_ENUM,
	MIC_BOOST_ENUM,
	AE5_HEADPHONE_GAIN_ENUM,
	AE5_SOUND_FILTER_ENUM,
	ZXR_HEADPHONE_GAIN
#define EFFECTS_COUNT  (EFFECT_END_NID - EFFECT_START_NID)
};

/* Effects values size*/
#define EFFECT_VALS_MAX_COUNT 12

/*
 * Default values for the effect slider controls, they are in order of their
 * effect NID's. Surround, Crystalizer, Dialog Plus, Smart Volume, and then
 * X-bass.
 */
static const unsigned int effect_slider_defaults[] = {67, 65, 50, 74, 50};
/* Amount of effect level sliders for ca0132_alt controls. */
#define EFFECT_LEVEL_SLIDERS 5

/* Latency introduced by DSP blocks in milliseconds. */
#define DSP_CAPTURE_INIT_LATENCY        0
#define DSP_CRYSTAL_VOICE_LATENCY       124
#define DSP_PLAYBACK_INIT_LATENCY       13
#define DSP_PLAY_ENHANCEMENT_LATENCY    30
#define DSP_SPEAKER_OUT_LATENCY         7

struct ct_effect {
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	hda_nid_t nid;
	int mid; /*effect module ID*/
	int reqs[EFFECT_VALS_MAX_COUNT]; /*effect module request*/
	int direct; /* 0:output; 1:input*/
	int params; /* number of default non-on/off params */
	/*effect default values, 1st is on/off. */
	unsigned int def_vals[EFFECT_VALS_MAX_COUNT];
};

#define EFX_DIR_OUT 0
#define EFX_DIR_IN  1

static const struct ct_effect ca0132_effects[EFFECTS_COUNT] = {
	{ .name = "Surround",
	  .nid = SURROUND,
	  .mid = 0x96,
	  .reqs = {0, 1},
	  .direct = EFX_DIR_OUT,
	  .params = 1,
	  .def_vals = {0x3F800000, 0x3F2B851F}
	},
	{ .name = "Crystalizer",
	  .nid = CRYSTALIZER,
	  .mid = 0x96,
	  .reqs = {7, 8},
	  .direct = EFX_DIR_OUT,
	  .params = 1,
	  .def_vals = {0x3F800000, 0x3F266666}
	},
	{ .name = "Dialog Plus",
	  .nid = DIALOG_PLUS,
	  .mid = 0x96,
	  .reqs = {2, 3},
	  .direct = EFX_DIR_OUT,
	  .params = 1,
	  .def_vals = {0x00000000, 0x3F000000}
	},
	{ .name = "Smart Volume",
	  .nid = SMART_VOLUME,
	  .mid = 0x96,
	  .reqs = {4, 5, 6},
	  .direct = EFX_DIR_OUT,
	  .params = 2,
	  .def_vals = {0x3F800000, 0x3F3D70A4, 0x00000000}
	},
	{ .name = "X-Bass",
	  .nid = X_BASS,
	  .mid = 0x96,
	  .reqs = {24, 23, 25},
	  .direct = EFX_DIR_OUT,
	  .params = 2,
	  .def_vals = {0x3F800000, 0x42A00000, 0x3F000000}
	},
	{ .name = "Equalizer",
	  .nid = EQUALIZER,
	  .mid = 0x96,
	  .reqs = {9, 10, 11, 12, 13, 14,
			15, 16, 17, 18, 19, 20},
	  .direct = EFX_DIR_OUT,
	  .params = 11,
	  .def_vals = {0x00000000, 0x00000000, 0x00000000, 0x00000000,
		       0x00000000, 0x00000000, 0x00000000, 0x00000000,
		       0x00000000, 0x00000000, 0x00000000, 0x00000000}
	},
	{ .name = "Echo Cancellation",
	  .nid = ECHO_CANCELLATION,
	  .mid = 0x95,
	  .reqs = {0, 1, 2, 3},
	  .direct = EFX_DIR_IN,
	  .params = 3,
	  .def_vals = {0x00000000, 0x3F3A9692, 0x00000000, 0x00000000}
	},
	{ .name = "Voice Focus",
	  .nid = VOICE_FOCUS,
	  .mid = 0x95,
	  .reqs = {6, 7, 8, 9},
	  .direct = EFX_DIR_IN,
	  .params = 3,
	  .def_vals = {0x3F800000, 0x3D7DF3B6, 0x41F00000, 0x41F00000}
	},
	{ .name = "Mic SVM",
	  .nid = MIC_SVM,
	  .mid = 0x95,
	  .reqs = {44, 45},
	  .direct = EFX_DIR_IN,
	  .params = 1,
	  .def_vals = {0x00000000, 0x3F3D70A4}
	},
	{ .name = "Noise Reduction",
	  .nid = NOISE_REDUCTION,
	  .mid = 0x95,
	  .reqs = {4, 5},
	  .direct = EFX_DIR_IN,
	  .params = 1,
	  .def_vals = {0x3F800000, 0x3F000000}
	},
	{ .name = "VoiceFX",
	  .nid = VOICEFX,
	  .mid = 0x95,
	  .reqs = {10, 11, 12, 13, 14, 15, 16, 17, 18},
	  .direct = EFX_DIR_IN,
	  .params = 8,
	  .def_vals = {0x00000000, 0x43C80000, 0x44AF0000, 0x44FA0000,
		       0x3F800000, 0x3F800000, 0x3F800000, 0x00000000,
		       0x00000000}
	}
};

/* Tuning controls */
#ifdef ENABLE_TUNING_CONTROLS

enum {
#define TUNING_CTL_START_NID  0xC0
	WEDGE_ANGLE = TUNING_CTL_START_NID,
	SVM_LEVEL,
	EQUALIZER_BAND_0,
	EQUALIZER_BAND_1,
	EQUALIZER_BAND_2,
	EQUALIZER_BAND_3,
	EQUALIZER_BAND_4,
	EQUALIZER_BAND_5,
	EQUALIZER_BAND_6,
	EQUALIZER_BAND_7,
	EQUALIZER_BAND_8,
	EQUALIZER_BAND_9,
	TUNING_CTL_END_NID
#define TUNING_CTLS_COUNT  (TUNING_CTL_END_NID - TUNING_CTL_START_NID)
};

struct ct_tuning_ctl {
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	hda_nid_t parent_nid;
	hda_nid_t nid;
	int mid; /*effect module ID*/
	int req; /*effect module request*/
	int direct; /* 0:output; 1:input*/
	unsigned int def_val;/*effect default values*/
};

static const struct ct_tuning_ctl ca0132_tuning_ctls[] = {
	{ .name = "Wedge Angle",
	  .parent_nid = VOICE_FOCUS,
	  .nid = WEDGE_ANGLE,
	  .mid = 0x95,
	  .req = 8,
	  .direct = EFX_DIR_IN,
	  .def_val = 0x41F00000
	},
	{ .name = "SVM Level",
	  .parent_nid = MIC_SVM,
	  .nid = SVM_LEVEL,
	  .mid = 0x95,
	  .req = 45,
	  .direct = EFX_DIR_IN,
	  .def_val = 0x3F3D70A4
	},
	{ .name = "EQ Band0",
	  .parent_nid = EQUALIZER,
	  .nid = EQUALIZER_BAND_0,
	  .mid = 0x96,
	  .req = 11,
	  .direct = EFX_DIR_OUT,
	  .def_val = 0x00000000
	},
	{ .name = "EQ Band1",
	  .parent_nid = EQUALIZER,
	  .nid = EQUALIZER_BAND_1,
	  .mid = 0x96,
	  .req = 12,
	  .direct = EFX_DIR_OUT,
	  .def_val = 0x00000000
	},
	{ .name = "EQ Band2",
	  .parent_nid = EQUALIZER,
	  .nid = EQUALIZER_BAND_2,
	  .mid = 0x96,
	  .req = 13,
	  .direct = EFX_DIR_OUT,
	  .def_val = 0x00000000
	},
	{ .name = "EQ Band3",
	  .parent_nid = EQUALIZER,
	  .nid = EQUALIZER_BAND_3,
	  .mid = 0x96,
	  .req = 14,
	  .direct = EFX_DIR_OUT,
	  .def_val = 0x00000000
	},
	{ .name = "EQ Band4",
	  .parent_nid = EQUALIZER,
	  .nid = EQUALIZER_BAND_4,
	  .mid = 0x96,
	  .req = 15,
	  .direct = EFX_DIR_OUT,
	  .def_val = 0x00000000
	},
	{ .name = "EQ Band5",
	  .parent_nid = EQUALIZER,
	  .nid = EQUALIZER_BAND_5,
	  .mid = 0x96,
	  .req = 16,
	  .direct = EFX_DIR_OUT,
	  .def_val = 0x00000000
	},
	{ .name = "EQ Band6",
	  .parent_nid = EQUALIZER,
	  .nid = EQUALIZER_BAND_6,
	  .mid = 0x96,
	  .req = 17,
	  .direct = EFX_DIR_OUT,
	  .def_val = 0x00000000
	},
	{ .name = "EQ Band7",
	  .parent_nid = EQUALIZER,
	  .nid = EQUALIZER_BAND_7,
	  .mid = 0x96,
	  .req = 18,
	  .direct = EFX_DIR_OUT,
	  .def_val = 0x00000000
	},
	{ .name = "EQ Band8",
	  .parent_nid = EQUALIZER,
	  .nid = EQUALIZER_BAND_8,
	  .mid = 0x96,
	  .req = 19,
	  .direct = EFX_DIR_OUT,
	  .def_val = 0x00000000
	},
	{ .name = "EQ Band9",
	  .parent_nid = EQUALIZER,
	  .nid = EQUALIZER_BAND_9,
	  .mid = 0x96,
	  .req = 20,
	  .direct = EFX_DIR_OUT,
	  .def_val = 0x00000000
	}
};
#endif

/* Voice FX Presets */
#define VOICEFX_MAX_PARAM_COUNT 9

struct ct_voicefx {
	char *name;
	hda_nid_t nid;
	int mid;
	int reqs[VOICEFX_MAX_PARAM_COUNT]; /*effect module request*/
};

struct ct_voicefx_preset {
	char *name; /*preset name*/
	unsigned int vals[VOICEFX_MAX_PARAM_COUNT];
};

static const struct ct_voicefx ca0132_voicefx = {
	.name = "VoiceFX Capture Switch",
	.nid = VOICEFX,
	.mid = 0x95,
	.reqs = {10, 11, 12, 13, 14, 15, 16, 17, 18}
};

static const struct ct_voicefx_preset ca0132_voicefx_presets[] = {
	{ .name = "Neutral",
	  .vals = { 0x00000000, 0x43C80000, 0x44AF0000,
		    0x44FA0000, 0x3F800000, 0x3F800000,
		    0x3F800000, 0x00000000, 0x00000000 }
	},
	{ .name = "Female2Male",
	  .vals = { 0x3F800000, 0x43C80000, 0x44AF0000,
		    0x44FA0000, 0x3F19999A, 0x3F866666,
		    0x3F800000, 0x00000000, 0x00000000 }
	},
	{ .name = "Male2Female",
	  .vals = { 0x3F800000, 0x43C80000, 0x44AF0000,
		    0x450AC000, 0x4017AE14, 0x3F6B851F,
		    0x3F800000, 0x00000000, 0x00000000 }
	},
	{ .name = "ScrappyKid",
	  .vals = { 0x3F800000, 0x43C80000, 0x44AF0000,
		    0x44FA0000, 0x40400000, 0x3F28F5C3,
		    0x3F800000, 0x00000000, 0x00000000 }
	},
	{ .name = "Elderly",
	  .vals = { 0x3F800000, 0x44324000, 0x44BB8000,
		    0x44E10000, 0x3FB33333, 0x3FB9999A,
		    0x3F800000, 0x3E3A2E43, 0x00000000 }
	},
	{ .name = "Orc",
	  .vals = { 0x3F800000, 0x43EA0000, 0x44A52000,
		    0x45098000, 0x3F266666, 0x3FC00000,
		    0x3F800000, 0x00000000, 0x00000000 }
	},
	{ .name = "Elf",
	  .vals = { 0x3F800000, 0x43C70000, 0x44AE6000,
		    0x45193000, 0x3F8E147B, 0x3F75C28F,
		    0x3F800000, 0x00000000, 0x00000000 }
	},
	{ .name = "Dwarf",
	  .vals = { 0x3F800000, 0x43930000, 0x44BEE000,
		    0x45007000, 0x3F451EB8, 0x3F7851EC,
		    0x3F800000, 0x00000000, 0x00000000 }
	},
	{ .name = "AlienBrute",
	  .vals = { 0x3F800000, 0x43BFC5AC, 0x44B28FDF,
		    0x451F6000, 0x3F266666, 0x3FA7D945,
		    0x3F800000, 0x3CF5C28F, 0x00000000 }
	},
	{ .name = "Robot",
	  .vals = { 0x3F800000, 0x43C80000, 0x44AF0000,
		    0x44FA0000, 0x3FB2718B, 0x3F800000,
		    0xBC07010E, 0x00000000, 0x00000000 }
	},
	{ .name = "Marine",
	  .vals = { 0x3F800000, 0x43C20000, 0x44906000,
		    0x44E70000, 0x3F4CCCCD, 0x3F8A3D71,
		    0x3F0A3D71, 0x00000000, 0x00000000 }
	},
	{ .name = "Emo",
	  .vals = { 0x3F800000, 0x43C80000, 0x44AF0000,
		    0x44FA0000, 0x3F800000, 0x3F800000,
		    0x3E4CCCCD, 0x00000000, 0x00000000 }
	},
	{ .name = "DeepVoice",
	  .vals = { 0x3F800000, 0x43A9C5AC, 0x44AA4FDF,
		    0x44FFC000, 0x3EDBB56F, 0x3F99C4CA,
		    0x3F800000, 0x00000000, 0x00000000 }
	},
	{ .name = "Munchkin",
	  .vals = { 0x3F800000, 0x43C80000, 0x44AF0000,
		    0x44FA0000, 0x3F800000, 0x3F1A043C,
		    0x3F800000, 0x00000000, 0x00000000 }
	}
};

/* ca0132 EQ presets, taken from Windows Sound Blaster Z Driver */

#define EQ_PRESET_MAX_PARAM_COUNT 11

struct ct_eq {
	char *name;
	hda_nid_t nid;
	int mid;
	int reqs[EQ_PRESET_MAX_PARAM_COUNT]; /*effect module request*/
};

struct ct_eq_preset {
	char *name; /*preset name*/
	unsigned int vals[EQ_PRESET_MAX_PARAM_COUNT];
};

static const struct ct_eq ca0132_alt_eq_enum = {
	.name = "FX: Equalizer Preset Switch",
	.nid = EQ_PRESET_ENUM,
	.mid = 0x96,
	.reqs = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}
};


static const struct ct_eq_preset ca0132_alt_eq_presets[] = {
	{ .name = "Flat",
	 .vals = { 0x00000000, 0x00000000, 0x00000000,
		   0x00000000, 0x00000000, 0x00000000,
		   0x00000000, 0x00000000, 0x00000000,
		   0x00000000, 0x00000000	     }
	},
	{ .name = "Acoustic",
	 .vals = { 0x00000000, 0x00000000, 0x3F8CCCCD,
		   0x40000000, 0x00000000, 0x00000000,
		   0x00000000, 0x00000000, 0x40000000,
		   0x40000000, 0x40000000	     }
	},
	{ .name = "Classical",
	 .vals = { 0x00000000, 0x00000000, 0x40C00000,
		   0x40C00000, 0x40466666, 0x00000000,
		   0x00000000, 0x00000000, 0x00000000,
		   0x40466666, 0x40466666	     }
	},
	{ .name = "Country",
	 .vals = { 0x00000000, 0xBF99999A, 0x00000000,
		   0x3FA66666, 0x3FA66666, 0x3F8CCCCD,
		   0x00000000, 0x00000000, 0x40000000,
		   0x40466666, 0x40800000	     }
	},
	{ .name = "Dance",
	 .vals = { 0x00000000, 0xBF99999A, 0x40000000,
		   0x40466666, 0x40866666, 0xBF99999A,
		   0xBF99999A, 0x00000000, 0x00000000,
		   0x40800000, 0x40800000	     }
	},
	{ .name = "Jazz",
	 .vals = { 0x00000000, 0x00000000, 0x00000000,
		   0x3F8CCCCD, 0x40800000, 0x40800000,
		   0x40800000, 0x00000000, 0x3F8CCCCD,
		   0x40466666, 0x40466666	     }
	},
	{ .name = "New Age",
	 .vals = { 0x00000000, 0x00000000, 0x40000000,
		   0x40000000, 0x00000000, 0x00000000,
		   0x00000000, 0x3F8CCCCD, 0x40000000,
		   0x40000000, 0x40000000	     }
	},
	{ .name = "Pop",
	 .vals = { 0x00000000, 0xBFCCCCCD, 0x00000000,
		   0x40000000, 0x40000000, 0x00000000,
		   0xBF99999A, 0xBF99999A, 0x00000000,
		   0x40466666, 0x40C00000	     }
	},
	{ .name = "Rock",
	 .vals = { 0x00000000, 0xBF99999A, 0xBF99999A,
		   0x3F8CCCCD, 0x40000000, 0xBF99999A,
		   0xBF99999A, 0x00000000, 0x00000000,
		   0x40800000, 0x40800000	     }
	},
	{ .name = "Vocal",
	 .vals = { 0x00000000, 0xC0000000, 0xBF99999A,
		   0xBF99999A, 0x00000000, 0x40466666,
		   0x40800000, 0x40466666, 0x00000000,
		   0x00000000, 0x3F8CCCCD	     }
	}
};

/* DSP command sequences for ca0132_alt_select_out */
#define ALT_OUT_SET_MAX_COMMANDS 9 /* Max number of commands in sequence */
struct ca0132_alt_out_set {
	char *name; /*preset name*/
	unsigned char commands;
	unsigned int mids[ALT_OUT_SET_MAX_COMMANDS];
	unsigned int reqs[ALT_OUT_SET_MAX_COMMANDS];
	unsigned int vals[ALT_OUT_SET_MAX_COMMANDS];
};

static const struct ca0132_alt_out_set alt_out_presets[] = {
	{ .name = "Line Out",
	  .commands = 7,
	  .mids = { 0x96, 0x96, 0x96, 0x8F,
		    0x96, 0x96, 0x96 },
	  .reqs = { 0x19, 0x17, 0x18, 0x01,
		    0x1F, 0x15, 0x3A },
	  .vals = { 0x3F000000, 0x42A00000, 0x00000000,
		    0x00000000, 0x00000000, 0x00000000,
		    0x00000000 }
	},
	{ .name = "Headphone",
	  .commands = 7,
	  .mids = { 0x96, 0x96, 0x96, 0x8F,
		    0x96, 0x96, 0x96 },
	  .reqs = { 0x19, 0x17, 0x18, 0x01,
		    0x1F, 0x15, 0x3A },
	  .vals = { 0x3F000000, 0x42A00000, 0x00000000,
		    0x00000000, 0x00000000, 0x00000000,
		    0x00000000 }
	},
	{ .name = "Surround",
	  .commands = 8,
	  .mids = { 0x96, 0x8F, 0x96, 0x96,
		    0x96, 0x96, 0x96, 0x96 },
	  .reqs = { 0x18, 0x01, 0x1F, 0x15,
		    0x3A, 0x1A, 0x1B, 0x1C },
	  .vals = { 0x00000000, 0x00000000, 0x00000000,
		    0x00000000, 0x00000000, 0x00000000,
		    0x00000000, 0x00000000 }
	}
};

/*
 * DSP volume setting structs. Req 1 is left volume, req 2 is right volume,
 * and I don't know what the third req is, but it's always zero. I assume it's
 * some sort of update or set command to tell the DSP there's new volume info.
 */
#define DSP_VOL_OUT 0
#define DSP_VOL_IN  1

struct ct_dsp_volume_ctl {
	hda_nid_t vnid;
	int mid; /* module ID*/
	unsigned int reqs[3]; /* scp req ID */
};

static const struct ct_dsp_volume_ctl ca0132_alt_vol_ctls[] = {
	{ .vnid = VNID_SPK,
	  .mid = 0x32,
	  .reqs = {3, 4, 2}
	},
	{ .vnid = VNID_MIC,
	  .mid = 0x37,
	  .reqs = {2, 3, 1}
	}
};

/* Values for ca0113_mmio_command_set for selecting output. */
#define AE5_CA0113_OUT_SET_COMMANDS 6
struct ae5_ca0113_output_set {
	unsigned int group[AE5_CA0113_OUT_SET_COMMANDS];
	unsigned int target[AE5_CA0113_OUT_SET_COMMANDS];
	unsigned int vals[AE5_CA0113_OUT_SET_COMMANDS];
};

static const struct ae5_ca0113_output_set ae5_ca0113_output_presets[] = {
	{ .group =  { 0x30, 0x30, 0x48, 0x48, 0x48, 0x30 },
	  .target = { 0x2e, 0x30, 0x0d, 0x17, 0x19, 0x32 },
	  .vals =   { 0x00, 0x00, 0x40, 0x00, 0x00, 0x3f }
	},
	{ .group =  { 0x30, 0x30, 0x48, 0x48, 0x48, 0x30 },
	  .target = { 0x2e, 0x30, 0x0d, 0x17, 0x19, 0x32 },
	  .vals =   { 0x3f, 0x3f, 0x00, 0x00, 0x00, 0x00 }
	},
	{ .group =  { 0x30, 0x30, 0x48, 0x48, 0x48, 0x30 },
	  .target = { 0x2e, 0x30, 0x0d, 0x17, 0x19, 0x32 },
	  .vals =   { 0x00, 0x00, 0x40, 0x00, 0x00, 0x3f }
	}
};

/* ae5 ca0113 command sequences to set headphone gain levels. */
#define AE5_HEADPHONE_GAIN_PRESET_MAX_COMMANDS 4
struct ae5_headphone_gain_set {
	char *name;
	unsigned int vals[AE5_HEADPHONE_GAIN_PRESET_MAX_COMMANDS];
};

static const struct ae5_headphone_gain_set ae5_headphone_gain_presets[] = {
	{ .name = "Low (16-31",
	  .vals = { 0xff, 0x2c, 0xf5, 0x32 }
	},
	{ .name = "Medium (32-149",
	  .vals = { 0x38, 0xa8, 0x3e, 0x4c }
	},
	{ .name = "High (150-600",
	  .vals = { 0xff, 0xff, 0xff, 0x7f }
	}
};

struct ae5_filter_set {
	char *name;
	unsigned int val;
};

static const struct ae5_filter_set ae5_filter_presets[] = {
	{ .name = "Slow Roll Off",
	  .val = 0xa0
	},
	{ .name = "Minimum Phase",
	  .val = 0xc0
	},
	{ .name = "Fast Roll Off",
	  .val = 0x80
	}
};

enum hda_cmd_vendor_io {
	/* for DspIO node */
	VENDOR_DSPIO_SCP_WRITE_DATA_LOW      = 0x000,
	VENDOR_DSPIO_SCP_WRITE_DATA_HIGH     = 0x100,

	VENDOR_DSPIO_STATUS                  = 0xF01,
	VENDOR_DSPIO_SCP_POST_READ_DATA      = 0x702,
	VENDOR_DSPIO_SCP_READ_DATA           = 0xF02,
	VENDOR_DSPIO_DSP_INIT                = 0x703,
	VENDOR_DSPIO_SCP_POST_COUNT_QUERY    = 0x704,
	VENDOR_DSPIO_SCP_READ_COUNT          = 0xF04,

	/* for ChipIO node */
	VENDOR_CHIPIO_ADDRESS_LOW            = 0x000,
	VENDOR_CHIPIO_ADDRESS_HIGH           = 0x100,
	VENDOR_CHIPIO_STREAM_FORMAT          = 0x200,
	VENDOR_CHIPIO_DATA_LOW               = 0x300,
	VENDOR_CHIPIO_DATA_HIGH              = 0x400,

	VENDOR_CHIPIO_8051_WRITE_DIRECT      = 0x500,
	VENDOR_CHIPIO_8051_READ_DIRECT       = 0xD00,

	VENDOR_CHIPIO_GET_PARAMETER          = 0xF00,
	VENDOR_CHIPIO_STATUS                 = 0xF01,
	VENDOR_CHIPIO_HIC_POST_READ          = 0x702,
	VENDOR_CHIPIO_HIC_READ_DATA          = 0xF03,

	VENDOR_CHIPIO_8051_DATA_WRITE        = 0x707,
	VENDOR_CHIPIO_8051_DATA_READ         = 0xF07,
	VENDOR_CHIPIO_8051_PMEM_READ         = 0xF08,
	VENDOR_CHIPIO_8051_IRAM_WRITE        = 0x709,
	VENDOR_CHIPIO_8051_IRAM_READ         = 0xF09,

	VENDOR_CHIPIO_CT_EXTENSIONS_ENABLE   = 0x70A,
	VENDOR_CHIPIO_CT_EXTENSIONS_GET      = 0xF0A,

	VENDOR_CHIPIO_PLL_PMU_WRITE          = 0x70C,
	VENDOR_CHIPIO_PLL_PMU_READ           = 0xF0C,
	VENDOR_CHIPIO_8051_ADDRESS_LOW       = 0x70D,
	VENDOR_CHIPIO_8051_ADDRESS_HIGH      = 0x70E,
	VENDOR_CHIPIO_FLAG_SET               = 0x70F,
	VENDOR_CHIPIO_FLAGS_GET              = 0xF0F,
	VENDOR_CHIPIO_PARAM_SET              = 0x710,
	VENDOR_CHIPIO_PARAM_GET              = 0xF10,

	VENDOR_CHIPIO_PORT_ALLOC_CONFIG_SET  = 0x711,
	VENDOR_CHIPIO_PORT_ALLOC_SET         = 0x712,
	VENDOR_CHIPIO_PORT_ALLOC_GET         = 0xF12,
	VENDOR_CHIPIO_PORT_FREE_SET          = 0x713,

	VENDOR_CHIPIO_PARAM_EX_ID_GET        = 0xF17,
	VENDOR_CHIPIO_PARAM_EX_ID_SET        = 0x717,
	VENDOR_CHIPIO_PARAM_EX_VALUE_GET     = 0xF18,
	VENDOR_CHIPIO_PARAM_EX_VALUE_SET     = 0x718,

	VENDOR_CHIPIO_DMIC_CTL_SET           = 0x788,
	VENDOR_CHIPIO_DMIC_CTL_GET           = 0xF88,
	VENDOR_CHIPIO_DMIC_PIN_SET           = 0x789,
	VENDOR_CHIPIO_DMIC_PIN_GET           = 0xF89,
	VENDOR_CHIPIO_DMIC_MCLK_SET          = 0x78A,
	VENDOR_CHIPIO_DMIC_MCLK_GET          = 0xF8A,

	VENDOR_CHIPIO_EAPD_SEL_SET           = 0x78D
};

/*
 *  Control flag IDs
 */
enum control_flag_id {
	/* Connection manager stream setup is bypassed/enabled */
	CONTROL_FLAG_C_MGR                  = 0,
	/* DSP DMA is bypassed/enabled */
	CONTROL_FLAG_DMA                    = 1,
	/* 8051 'idle' mode is disabled/enabled */
	CONTROL_FLAG_IDLE_ENABLE            = 2,
	/* Tracker for the SPDIF-in path is bypassed/enabled */
	CONTROL_FLAG_TRACKER                = 3,
	/* DigitalOut to Spdif2Out connection is disabled/enabled */
	CONTROL_FLAG_SPDIF2OUT              = 4,
	/* Digital Microphone is disabled/enabled */
	CONTROL_FLAG_DMIC                   = 5,
	/* ADC_B rate is 48 kHz/96 kHz */
	CONTROL_FLAG_ADC_B_96KHZ            = 6,
	/* ADC_C rate is 48 kHz/96 kHz */
	CONTROL_FLAG_ADC_C_96KHZ            = 7,
	/* DAC rate is 48 kHz/96 kHz (affects all DACs) */
	CONTROL_FLAG_DAC_96KHZ              = 8,
	/* DSP rate is 48 kHz/96 kHz */
	CONTROL_FLAG_DSP_96KHZ              = 9,
	/* SRC clock is 98 MHz/196 MHz (196 MHz forces rate to 96 KHz) */
	CONTROL_FLAG_SRC_CLOCK_196MHZ       = 10,
	/* SRC rate is 48 kHz/96 kHz (48 kHz disabled when clock is 196 MHz) */
	CONTROL_FLAG_SRC_RATE_96KHZ         = 11,
	/* Decode Loop (DSP->SRC->DSP) is disabled/enabled */
	CONTROL_FLAG_DECODE_LOOP            = 12,
	/* De-emphasis filter on DAC-1 disabled/enabled */
	CONTROL_FLAG_DAC1_DEEMPHASIS        = 13,
	/* De-emphasis filter on DAC-2 disabled/enabled */
	CONTROL_FLAG_DAC2_DEEMPHASIS        = 14,
	/* De-emphasis filter on DAC-3 disabled/enabled */
	CONTROL_FLAG_DAC3_DEEMPHASIS        = 15,
	/* High-pass filter on ADC_B disabled/enabled */
	CONTROL_FLAG_ADC_B_HIGH_PASS        = 16,
	/* High-pass filter on ADC_C disabled/enabled */
	CONTROL_FLAG_ADC_C_HIGH_PASS        = 17,
	/* Common mode on Port_A disabled/enabled */
	CONTROL_FLAG_PORT_A_COMMON_MODE     = 18,
	/* Common mode on Port_D disabled/enabled */
	CONTROL_FLAG_PORT_D_COMMON_MODE     = 19,
	/* Impedance for ramp generator on Port_A 16 Ohm/10K Ohm */
	CONTROL_FLAG_PORT_A_10KOHM_LOAD     = 20,
	/* Impedance for ramp generator on Port_D, 16 Ohm/10K Ohm */
	CONTROL_FLAG_PORT_D_10KOHM_LOAD     = 21,
	/* ASI rate is 48kHz/96kHz */
	CONTROL_FLAG_ASI_96KHZ              = 22,
	/* DAC power settings able to control attached ports no/yes */
	CONTROL_FLAG_DACS_CONTROL_PORTS     = 23,
	/* Clock Stop OK reporting is disabled/enabled */
	CONTROL_FLAG_CONTROL_STOP_OK_ENABLE = 24,
	/* Number of control flags */
	CONTROL_FLAGS_MAX = (CONTROL_FLAG_CONTROL_STOP_OK_ENABLE+1)
};

/*
 * Control parameter IDs
 */
enum control_param_id {
	/* 0: None, 1: Mic1In*/
	CONTROL_PARAM_VIP_SOURCE               = 1,
	/* 0: force HDA, 1: allow DSP if HDA Spdif1Out stream is idle */
	CONTROL_PARAM_SPDIF1_SOURCE            = 2,
	/* Port A output stage gain setting to use when 16 Ohm output
	 * impedance is selected*/
	CONTROL_PARAM_PORTA_160OHM_GAIN        = 8,
	/* Port D output stage gain setting to use when 16 Ohm output
	 * impedance is selected*/
	CONTROL_PARAM_PORTD_160OHM_GAIN        = 10,

	/*
	 * This control param name was found in the 8051 memory, and makes
	 * sense given the fact the AE-5 uses it and has the ASI flag set.
	 */
	CONTROL_PARAM_ASI                      = 23,

	/* Stream Control */

	/* Select stream with the given ID */
	CONTROL_PARAM_STREAM_ID                = 24,
	/* Source connection point for the selected stream */
	CONTROL_PARAM_STREAM_SOURCE_CONN_POINT = 25,
	/* Destination connection point for the selected stream */
	CONTROL_PARAM_STREAM_DEST_CONN_POINT   = 26,
	/* Number of audio channels in the selected stream */
	CONTROL_PARAM_STREAMS_CHANNELS         = 27,
	/*Enable control for the selected stream */
	CONTROL_PARAM_STREAM_CONTROL           = 28,

	/* Connection Point Control */

	/* Select connection point with the given ID */
	CONTROL_PARAM_CONN_POINT_ID            = 29,
	/* Connection point sample rate */
	CONTROL_PARAM_CONN_POINT_SAMPLE_RATE   = 30,

	/* Node Control */

	/* Select HDA node with the given ID */
	CONTROL_PARAM_NODE_ID                  = 31
};

/*
 *  Dsp Io Status codes
 */
enum hda_vendor_status_dspio {
	/* Success */
	VENDOR_STATUS_DSPIO_OK                       = 0x00,
	/* Busy, unable to accept new command, the host must retry */
	VENDOR_STATUS_DSPIO_BUSY                     = 0x01,
	/* SCP command queue is full */
	VENDOR_STATUS_DSPIO_SCP_COMMAND_QUEUE_FULL   = 0x02,
	/* SCP response queue is empty */
	VENDOR_STATUS_DSPIO_SCP_RESPONSE_QUEUE_EMPTY = 0x03
};

/*
 *  Chip Io Status codes
 */
enum hda_vendor_status_chipio {
	/* Success */
	VENDOR_STATUS_CHIPIO_OK   = 0x00,
	/* Busy, unable to accept new command, the host must retry */
	VENDOR_STATUS_CHIPIO_BUSY = 0x01
};

/*
 *  CA0132 sample rate
 */
enum ca0132_sample_rate {
	SR_6_000        = 0x00,
	SR_8_000        = 0x01,
	SR_9_600        = 0x02,
	SR_11_025       = 0x03,
	SR_16_000       = 0x04,
	SR_22_050       = 0x05,
	SR_24_000       = 0x06,
	SR_32_000       = 0x07,
	SR_44_100       = 0x08,
	SR_48_000       = 0x09,
	SR_88_200       = 0x0A,
	SR_96_000       = 0x0B,
	SR_144_000      = 0x0C,
	SR_176_400      = 0x0D,
	SR_192_000      = 0x0E,
	SR_384_000      = 0x0F,

	SR_COUNT        = 0x10,

	SR_RATE_UNKNOWN = 0x1F
};

enum dsp_download_state {
	DSP_DOWNLOAD_FAILED = -1,
	DSP_DOWNLOAD_INIT   = 0,
	DSP_DOWNLOADING     = 1,
	DSP_DOWNLOADED      = 2
};

/* retrieve parameters from hda format */
#define get_hdafmt_chs(fmt)	(fmt & 0xf)
#define get_hdafmt_bits(fmt)	((fmt >> 4) & 0x7)
#define get_hdafmt_rate(fmt)	((fmt >> 8) & 0x7f)
#define get_hdafmt_type(fmt)	((fmt >> 15) & 0x1)

/*
 * CA0132 specific
 */

struct ca0132_spec {
	const struct snd_kcontrol_new *mixers[5];
	unsigned int num_mixers;
	const struct hda_verb *base_init_verbs;
	const struct hda_verb *base_exit_verbs;
	const struct hda_verb *chip_init_verbs;
	const struct hda_verb *desktop_init_verbs;
	struct hda_verb *spec_init_verbs;
	struct auto_pin_cfg autocfg;

	/* Nodes configurations */
	struct hda_multi_out multiout;
	hda_nid_t out_pins[AUTO_CFG_MAX_OUTS];
	hda_nid_t dacs[AUTO_CFG_MAX_OUTS];
	unsigned int num_outputs;
	hda_nid_t input_pins[AUTO_PIN_LAST];
	hda_nid_t adcs[AUTO_PIN_LAST];
	hda_nid_t dig_out;
	hda_nid_t dig_in;
	unsigned int num_inputs;
	hda_nid_t shared_mic_nid;
	hda_nid_t shared_out_nid;
	hda_nid_t unsol_tag_hp;
	hda_nid_t unsol_tag_front_hp; /* for desktop ca0132 codecs */
	hda_nid_t unsol_tag_amic1;

	/* chip access */
	struct mutex chipio_mutex; /* chip access mutex */
	u32 curr_chip_addx;

	/* DSP download related */
	enum dsp_download_state dsp_state;
	unsigned int dsp_stream_id;
	unsigned int wait_scp;
	unsigned int wait_scp_header;
	unsigned int wait_num_data;
	unsigned int scp_resp_header;
	unsigned int scp_resp_data[4];
	unsigned int scp_resp_count;
	bool alt_firmware_present;
	bool startup_check_entered;
	bool dsp_reload;

	/* mixer and effects related */
	unsigned char dmic_ctl;
	int cur_out_type;
	int cur_mic_type;
	long vnode_lvol[VNODES_COUNT];
	long vnode_rvol[VNODES_COUNT];
	long vnode_lswitch[VNODES_COUNT];
	long vnode_rswitch[VNODES_COUNT];
	long effects_switch[EFFECTS_COUNT];
	long voicefx_val;
	long cur_mic_boost;
	/* ca0132_alt control related values */
	unsigned char in_enum_val;
	unsigned char out_enum_val;
	unsigned char mic_boost_enum_val;
	unsigned char smart_volume_setting;
	long fx_ctl_val[EFFECT_LEVEL_SLIDERS];
	long xbass_xover_freq;
	long eq_preset_val;
	unsigned int tlv[4];
	struct hda_vmaster_mute_hook vmaster_mute;
	/* AE-5 Control values */
	unsigned char ae5_headphone_gain_val;
	unsigned char ae5_filter_val;
	/* ZxR Control Values */
	unsigned char zxr_gain_set;

	struct hda_codec *codec;
	struct delayed_work unsol_hp_work;
	int quirk;

#ifdef ENABLE_TUNING_CONTROLS
	long cur_ctl_vals[TUNING_CTLS_COUNT];
#endif
	/*
	 * The Recon3D, Sound Blaster Z, Sound Blaster ZxR, and Sound Blaster
	 * AE-5 all use PCI region 2 to toggle GPIO and other currently unknown
	 * things.
	 */
	bool use_pci_mmio;
	void __iomem *mem_base;

	/*
	 * Whether or not to use the alt functions like alt_select_out,
	 * alt_select_in, etc. Only used on desktop codecs for now, because of
	 * surround sound support.
	 */
	bool use_alt_functions;

	/*
	 * Whether or not to use alt controls:	volume effect sliders, EQ
	 * presets, smart volume presets, and new control names with FX prefix.
	 * Renames PlayEnhancement and CrystalVoice too.
	 */
	bool use_alt_controls;
};

/*
 * CA0132 quirks table
 */
enum {
	QUIRK_NONE,
	QUIRK_ALIENWARE,
	QUIRK_ALIENWARE_M17XR4,
	QUIRK_SBZ,
	QUIRK_ZXR,
	QUIRK_ZXR_DBPRO,
	QUIRK_R3DI,
	QUIRK_R3D,
	QUIRK_AE5,
};

static const struct hda_pintbl alienware_pincfgs[] = {
	{ 0x0b, 0x90170110 }, /* Builtin Speaker */
	{ 0x0c, 0x411111f0 }, /* N/A */
	{ 0x0d, 0x411111f0 }, /* N/A */
	{ 0x0e, 0x411111f0 }, /* N/A */
	{ 0x0f, 0x0321101f }, /* HP */
	{ 0x10, 0x411111f0 }, /* Headset?  disabled for now */
	{ 0x11, 0x03a11021 }, /* Mic */
	{ 0x12, 0xd5a30140 }, /* Builtin Mic */
	{ 0x13, 0x411111f0 }, /* N/A */
	{ 0x18, 0x411111f0 }, /* N/A */
	{}
};

/* Sound Blaster Z pin configs taken from Windows Driver */
static const struct hda_pintbl sbz_pincfgs[] = {
	{ 0x0b, 0x01017010 }, /* Port G -- Lineout FRONT L/R */
	{ 0x0c, 0x014510f0 }, /* SPDIF Out 1 */
	{ 0x0d, 0x014510f0 }, /* Digital Out */
	{ 0x0e, 0x01c510f0 }, /* SPDIF In */
	{ 0x0f, 0x0221701f }, /* Port A -- BackPanel HP */
	{ 0x10, 0x01017012 }, /* Port D -- Center/LFE or FP Hp */
	{ 0x11, 0x01017014 }, /* Port B -- LineMicIn2 / Rear L/R */
	{ 0x12, 0x01a170f0 }, /* Port C -- LineIn1 */
	{ 0x13, 0x908700f0 }, /* What U Hear In*/
	{ 0x18, 0x50d000f0 }, /* N/A */
	{}
};

/* Sound Blaster ZxR pin configs taken from Windows Driver */
static const struct hda_pintbl zxr_pincfgs[] = {
	{ 0x0b, 0x01047110 }, /* Port G -- Lineout FRONT L/R */
	{ 0x0c, 0x414510f0 }, /* SPDIF Out 1 - Disabled*/
	{ 0x0d, 0x014510f0 }, /* Digital Out */
	{ 0x0e, 0x41c520f0 }, /* SPDIF In - Disabled*/
	{ 0x0f, 0x0122711f }, /* Port A -- BackPanel HP */
	{ 0x10, 0x01017111 }, /* Port D -- Center/LFE */
	{ 0x11, 0x01017114 }, /* Port B -- LineMicIn2 / Rear L/R */
	{ 0x12, 0x01a271f0 }, /* Port C -- LineIn1 */
	{ 0x13, 0x908700f0 }, /* What U Hear In*/
	{ 0x18, 0x50d000f0 }, /* N/A */
	{}
};

/* Recon3D pin configs taken from Windows Driver */
static const struct hda_pintbl r3d_pincfgs[] = {
	{ 0x0b, 0x01014110 }, /* Port G -- Lineout FRONT L/R */
	{ 0x0c, 0x014510f0 }, /* SPDIF Out 1 */
	{ 0x0d, 0x014510f0 }, /* Digital Out */
	{ 0x0e, 0x01c520f0 }, /* SPDIF In */
	{ 0x0f, 0x0221401f }, /* Port A -- BackPanel HP */
	{ 0x10, 0x01016011 }, /* Port D -- Center/LFE or FP Hp */
	{ 0x11, 0x01011014 }, /* Port B -- LineMicIn2 / Rear L/R */
	{ 0x12, 0x02a090f0 }, /* Port C -- LineIn1 */
	{ 0x13, 0x908700f0 }, /* What U Hear In*/
	{ 0x18, 0x50d000f0 }, /* N/A */
	{}
};

/* Sound Blaster AE-5 pin configs taken from Windows Driver */
static const struct hda_pintbl ae5_pincfgs[] = {
	{ 0x0b, 0x01017010 }, /* Port G -- Lineout FRONT L/R */
	{ 0x0c, 0x014510f0 }, /* SPDIF Out 1 */
	{ 0x0d, 0x014510f0 }, /* Digital Out */
	{ 0x0e, 0x01c510f0 }, /* SPDIF In */
	{ 0x0f, 0x01017114 }, /* Port A -- Rear L/R. */
	{ 0x10, 0x01017012 }, /* Port D -- Center/LFE or FP Hp */
	{ 0x11, 0x01a170ff }, /* Port B -- LineMicIn2 / Rear Headphone */
	{ 0x12, 0x01a170f0 }, /* Port C -- LineIn1 */
	{ 0x13, 0x908700f0 }, /* What U Hear In*/
	{ 0x18, 0x50d000f0 }, /* N/A */
	{}
};

/* Recon3D integrated pin configs taken from Windows Driver */
static const struct hda_pintbl r3di_pincfgs[] = {
	{ 0x0b, 0x01014110 }, /* Port G -- Lineout FRONT L/R */
	{ 0x0c, 0x014510f0 }, /* SPDIF Out 1 */
	{ 0x0d, 0x014510f0 }, /* Digital Out */
	{ 0x0e, 0x41c520f0 }, /* SPDIF In */
	{ 0x0f, 0x0221401f }, /* Port A -- BackPanel HP */
	{ 0x10, 0x01016011 }, /* Port D -- Center/LFE or FP Hp */
	{ 0x11, 0x01011014 }, /* Port B -- LineMicIn2 / Rear L/R */
	{ 0x12, 0x02a090f0 }, /* Port C -- LineIn1 */
	{ 0x13, 0x908700f0 }, /* What U Hear In*/
	{ 0x18, 0x500000f0 }, /* N/A */
	{}
};

static const struct snd_pci_quirk ca0132_quirks[] = {
	SND_PCI_QUIRK(0x1028, 0x057b, "Alienware M17x R4", QUIRK_ALIENWARE_M17XR4),
	SND_PCI_QUIRK(0x1028, 0x0685, "Alienware 15 2015", QUIRK_ALIENWARE),
	SND_PCI_QUIRK(0x1028, 0x0688, "Alienware 17 2015", QUIRK_ALIENWARE),
	SND_PCI_QUIRK(0x1028, 0x0708, "Alienware 15 R2 2016", QUIRK_ALIENWARE),
	SND_PCI_QUIRK(0x1102, 0x0010, "Sound Blaster Z", QUIRK_SBZ),
	SND_PCI_QUIRK(0x1102, 0x0023, "Sound Blaster Z", QUIRK_SBZ),
	SND_PCI_QUIRK(0x1458, 0xA016, "Recon3Di", QUIRK_R3DI),
	SND_PCI_QUIRK(0x1458, 0xA026, "Gigabyte G1.Sniper Z97", QUIRK_R3DI),
	SND_PCI_QUIRK(0x1458, 0xA036, "Gigabyte GA-Z170X-Gaming 7", QUIRK_R3DI),
	SND_PCI_QUIRK(0x1102, 0x0013, "Recon3D", QUIRK_R3D),
	SND_PCI_QUIRK(0x1102, 0x0051, "Sound Blaster AE-5", QUIRK_AE5),
	{}
};

/*
 * CA0132 codec access
 */
static unsigned int codec_send_command(struct hda_codec *codec, hda_nid_t nid,
		unsigned int verb, unsigned int parm, unsigned int *res)
{
	unsigned int response;
	response = snd_hda_codec_read(codec, nid, 0, verb, parm);
	*res = response;

	return ((response == -1) ? -1 : 0);
}

static int codec_set_converter_format(struct hda_codec *codec, hda_nid_t nid,
		unsigned short converter_format, unsigned int *res)
{
	return codec_send_command(codec, nid, VENDOR_CHIPIO_STREAM_FORMAT,
				converter_format & 0xffff, res);
}

static int codec_set_converter_stream_channel(struct hda_codec *codec,
				hda_nid_t nid, unsigned char stream,
				unsigned char channel, unsigned int *res)
{
	unsigned char converter_stream_channel = 0;

	converter_stream_channel = (stream << 4) | (channel & 0x0f);
	return codec_send_command(codec, nid, AC_VERB_SET_CHANNEL_STREAMID,
				converter_stream_channel, res);
}

/* Chip access helper function */
static int chipio_send(struct hda_codec *codec,
		       unsigned int reg,
		       unsigned int data)
{
	unsigned int res;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	/* send bits of data specified by reg */
	do {
		res = snd_hda_codec_read(codec, WIDGET_CHIP_CTRL, 0,
					 reg, data);
		if (res == VENDOR_STATUS_CHIPIO_OK)
			return 0;
		msleep(20);
	} while (time_before(jiffies, timeout));

	return -EIO;
}

/*
 * Write chip address through the vendor widget -- NOT protected by the Mutex!
 */
static int chipio_write_address(struct hda_codec *codec,
				unsigned int chip_addx)
{
	struct ca0132_spec *spec = codec->spec;
	int res;

	if (spec->curr_chip_addx == chip_addx)
			return 0;

	/* send low 16 bits of the address */
	res = chipio_send(codec, VENDOR_CHIPIO_ADDRESS_LOW,
			  chip_addx & 0xffff);

	if (res != -EIO) {
		/* send high 16 bits of the address */
		res = chipio_send(codec, VENDOR_CHIPIO_ADDRESS_HIGH,
				  chip_addx >> 16);
	}

	spec->curr_chip_addx = (res < 0) ? ~0U : chip_addx;

	return res;
}

/*
 * Write data through the vendor widget -- NOT protected by the Mutex!
 */
static int chipio_write_data(struct hda_codec *codec, unsigned int data)
{
	struct ca0132_spec *spec = codec->spec;
	int res;

	/* send low 16 bits of the data */
	res = chipio_send(codec, VENDOR_CHIPIO_DATA_LOW, data & 0xffff);

	if (res != -EIO) {
		/* send high 16 bits of the data */
		res = chipio_send(codec, VENDOR_CHIPIO_DATA_HIGH,
				  data >> 16);
	}

	/*If no error encountered, automatically increment the address
	as per chip behaviour*/
	spec->curr_chip_addx = (res != -EIO) ?
					(spec->curr_chip_addx + 4) : ~0U;
	return res;
}

/*
 * Write multiple data through the vendor widget -- NOT protected by the Mutex!
 */
static int chipio_write_data_multiple(struct hda_codec *codec,
				      const u32 *data,
				      unsigned int count)
{
	int status = 0;

	if (data == NULL) {
		codec_dbg(codec, "chipio_write_data null ptr\n");
		return -EINVAL;
	}

	while ((count-- != 0) && (status == 0))
		status = chipio_write_data(codec, *data++);

	return status;
}


/*
 * Read data through the vendor widget -- NOT protected by the Mutex!
 */
static int chipio_read_data(struct hda_codec *codec, unsigned int *data)
{
	struct ca0132_spec *spec = codec->spec;
	int res;

	/* post read */
	res = chipio_send(codec, VENDOR_CHIPIO_HIC_POST_READ, 0);

	if (res != -EIO) {
		/* read status */
		res = chipio_send(codec, VENDOR_CHIPIO_STATUS, 0);
	}

	if (res != -EIO) {
		/* read data */
		*data = snd_hda_codec_read(codec, WIDGET_CHIP_CTRL, 0,
					   VENDOR_CHIPIO_HIC_READ_DATA,
					   0);
	}

	/*If no error encountered, automatically increment the address
	as per chip behaviour*/
	spec->curr_chip_addx = (res != -EIO) ?
					(spec->curr_chip_addx + 4) : ~0U;
	return res;
}

/*
 * Write given value to the given address through the chip I/O widget.
 * protected by the Mutex
 */
static int chipio_write(struct hda_codec *codec,
		unsigned int chip_addx, const unsigned int data)
{
	struct ca0132_spec *spec = codec->spec;
	int err;

	mutex_lock(&spec->chipio_mutex);

	/* write the address, and if successful proceed to write data */
	err = chipio_write_address(codec, chip_addx);
	if (err < 0)
		goto exit;

	err = chipio_write_data(codec, data);
	if (err < 0)
		goto exit;

exit:
	mutex_unlock(&spec->chipio_mutex);
	return err;
}

/*
 * Write given value to the given address through the chip I/O widget.
 * not protected by the Mutex
 */
static int chipio_write_no_mutex(struct hda_codec *codec,
		unsigned int chip_addx, const unsigned int data)
{
	int err;


	/* write the address, and if successful proceed to write data */
	err = chipio_write_address(codec, chip_addx);
	if (err < 0)
		goto exit;

	err = chipio_write_data(codec, data);
	if (err < 0)
		goto exit;

exit:
	return err;
}

/*
 * Write multiple values to the given address through the chip I/O widget.
 * protected by the Mutex
 */
static int chipio_write_multiple(struct hda_codec *codec,
				 u32 chip_addx,
				 const u32 *data,
				 unsigned int count)
{
	struct ca0132_spec *spec = codec->spec;
	int status;

	mutex_lock(&spec->chipio_mutex);
	status = chipio_write_address(codec, chip_addx);
	if (status < 0)
		goto error;

	status = chipio_write_data_multiple(codec, data, count);
error:
	mutex_unlock(&spec->chipio_mutex);

	return status;
}

/*
 * Read the given address through the chip I/O widget
 * protected by the Mutex
 */
static int chipio_read(struct hda_codec *codec,
		unsigned int chip_addx, unsigned int *data)
{
	struct ca0132_spec *spec = codec->spec;
	int err;

	mutex_lock(&spec->chipio_mutex);

	/* write the address, and if successful proceed to write data */
	err = chipio_write_address(codec, chip_addx);
	if (err < 0)
		goto exit;

	err = chipio_read_data(codec, data);
	if (err < 0)
		goto exit;

exit:
	mutex_unlock(&spec->chipio_mutex);
	return err;
}

/*
 * Set chip control flags through the chip I/O widget.
 */
static void chipio_set_control_flag(struct hda_codec *codec,
				    enum control_flag_id flag_id,
				    bool flag_state)
{
	unsigned int val;
	unsigned int flag_bit;

	flag_bit = (flag_state ? 1 : 0);
	val = (flag_bit << 7) | (flag_id);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_FLAG_SET, val);
}

/*
 * Set chip parameters through the chip I/O widget.
 */
static void chipio_set_control_param(struct hda_codec *codec,
		enum control_param_id param_id, int param_val)
{
	struct ca0132_spec *spec = codec->spec;
	int val;

	if ((param_id < 32) && (param_val < 8)) {
		val = (param_val << 5) | (param_id);
		snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
				    VENDOR_CHIPIO_PARAM_SET, val);
	} else {
		mutex_lock(&spec->chipio_mutex);
		if (chipio_send(codec, VENDOR_CHIPIO_STATUS, 0) == 0) {
			snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
					    VENDOR_CHIPIO_PARAM_EX_ID_SET,
					    param_id);
			snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
					    VENDOR_CHIPIO_PARAM_EX_VALUE_SET,
					    param_val);
		}
		mutex_unlock(&spec->chipio_mutex);
	}
}

/*
 * Set chip parameters through the chip I/O widget. NO MUTEX.
 */
static void chipio_set_control_param_no_mutex(struct hda_codec *codec,
		enum control_param_id param_id, int param_val)
{
	int val;

	if ((param_id < 32) && (param_val < 8)) {
		val = (param_val << 5) | (param_id);
		snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
				    VENDOR_CHIPIO_PARAM_SET, val);
	} else {
		if (chipio_send(codec, VENDOR_CHIPIO_STATUS, 0) == 0) {
			snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
					    VENDOR_CHIPIO_PARAM_EX_ID_SET,
					    param_id);
			snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
					    VENDOR_CHIPIO_PARAM_EX_VALUE_SET,
					    param_val);
		}
	}
}
/*
 * Connect stream to a source point, and then connect
 * that source point to a destination point.
 */
static void chipio_set_stream_source_dest(struct hda_codec *codec,
				int streamid, int source_point, int dest_point)
{
	chipio_set_control_param_no_mutex(codec,
			CONTROL_PARAM_STREAM_ID, streamid);
	chipio_set_control_param_no_mutex(codec,
			CONTROL_PARAM_STREAM_SOURCE_CONN_POINT, source_point);
	chipio_set_control_param_no_mutex(codec,
			CONTROL_PARAM_STREAM_DEST_CONN_POINT, dest_point);
}

/*
 * Set number of channels in the selected stream.
 */
static void chipio_set_stream_channels(struct hda_codec *codec,
				int streamid, unsigned int channels)
{
	chipio_set_control_param_no_mutex(codec,
			CONTROL_PARAM_STREAM_ID, streamid);
	chipio_set_control_param_no_mutex(codec,
			CONTROL_PARAM_STREAMS_CHANNELS, channels);
}

/*
 * Enable/Disable audio stream.
 */
static void chipio_set_stream_control(struct hda_codec *codec,
				int streamid, int enable)
{
	chipio_set_control_param_no_mutex(codec,
			CONTROL_PARAM_STREAM_ID, streamid);
	chipio_set_control_param_no_mutex(codec,
			CONTROL_PARAM_STREAM_CONTROL, enable);
}


/*
 * Set sampling rate of the connection point. NO MUTEX.
 */
static void chipio_set_conn_rate_no_mutex(struct hda_codec *codec,
				int connid, enum ca0132_sample_rate rate)
{
	chipio_set_control_param_no_mutex(codec,
			CONTROL_PARAM_CONN_POINT_ID, connid);
	chipio_set_control_param_no_mutex(codec,
			CONTROL_PARAM_CONN_POINT_SAMPLE_RATE, rate);
}

/*
 * Set sampling rate of the connection point.
 */
static void chipio_set_conn_rate(struct hda_codec *codec,
				int connid, enum ca0132_sample_rate rate)
{
	chipio_set_control_param(codec, CONTROL_PARAM_CONN_POINT_ID, connid);
	chipio_set_control_param(codec, CONTROL_PARAM_CONN_POINT_SAMPLE_RATE,
				 rate);
}

/*
 * Writes to the 8051's internal address space directly instead of indirectly,
 * giving access to the special function registers located at addresses
 * 0x80-0xFF.
 */
static void chipio_8051_write_direct(struct hda_codec *codec,
		unsigned int addr, unsigned int data)
{
	unsigned int verb;

	verb = VENDOR_CHIPIO_8051_WRITE_DIRECT | data;
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0, verb, addr);
}

/*
 * Enable clocks.
 */
static void chipio_enable_clocks(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	mutex_lock(&spec->chipio_mutex);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0xff);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 5);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0x0b);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 6);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0xff);
	mutex_unlock(&spec->chipio_mutex);
}

/*
 * CA0132 DSP IO stuffs
 */
static int dspio_send(struct hda_codec *codec, unsigned int reg,
		      unsigned int data)
{
	int res;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	/* send bits of data specified by reg to dsp */
	do {
		res = snd_hda_codec_read(codec, WIDGET_DSP_CTRL, 0, reg, data);
		if ((res >= 0) && (res != VENDOR_STATUS_DSPIO_BUSY))
			return res;
		msleep(20);
	} while (time_before(jiffies, timeout));

	return -EIO;
}

/*
 * Wait for DSP to be ready for commands
 */
static void dspio_write_wait(struct hda_codec *codec)
{
	int status;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);

	do {
		status = snd_hda_codec_read(codec, WIDGET_DSP_CTRL, 0,
						VENDOR_DSPIO_STATUS, 0);
		if ((status == VENDOR_STATUS_DSPIO_OK) ||
		    (status == VENDOR_STATUS_DSPIO_SCP_RESPONSE_QUEUE_EMPTY))
			break;
		msleep(1);
	} while (time_before(jiffies, timeout));
}

/*
 * Write SCP data to DSP
 */
static int dspio_write(struct hda_codec *codec, unsigned int scp_data)
{
	struct ca0132_spec *spec = codec->spec;
	int status;

	dspio_write_wait(codec);

	mutex_lock(&spec->chipio_mutex);
	status = dspio_send(codec, VENDOR_DSPIO_SCP_WRITE_DATA_LOW,
			    scp_data & 0xffff);
	if (status < 0)
		goto error;

	status = dspio_send(codec, VENDOR_DSPIO_SCP_WRITE_DATA_HIGH,
				    scp_data >> 16);
	if (status < 0)
		goto error;

	/* OK, now check if the write itself has executed*/
	status = snd_hda_codec_read(codec, WIDGET_DSP_CTRL, 0,
				    VENDOR_DSPIO_STATUS, 0);
error:
	mutex_unlock(&spec->chipio_mutex);

	return (status == VENDOR_STATUS_DSPIO_SCP_COMMAND_QUEUE_FULL) ?
			-EIO : 0;
}

/*
 * Write multiple SCP data to DSP
 */
static int dspio_write_multiple(struct hda_codec *codec,
				unsigned int *buffer, unsigned int size)
{
	int status = 0;
	unsigned int count;

	if (buffer == NULL)
		return -EINVAL;

	count = 0;
	while (count < size) {
		status = dspio_write(codec, *buffer++);
		if (status != 0)
			break;
		count++;
	}

	return status;
}

static int dspio_read(struct hda_codec *codec, unsigned int *data)
{
	int status;

	status = dspio_send(codec, VENDOR_DSPIO_SCP_POST_READ_DATA, 0);
	if (status == -EIO)
		return status;

	status = dspio_send(codec, VENDOR_DSPIO_STATUS, 0);
	if (status == -EIO ||
	    status == VENDOR_STATUS_DSPIO_SCP_RESPONSE_QUEUE_EMPTY)
		return -EIO;

	*data = snd_hda_codec_read(codec, WIDGET_DSP_CTRL, 0,
				   VENDOR_DSPIO_SCP_READ_DATA, 0);

	return 0;
}

static int dspio_read_multiple(struct hda_codec *codec, unsigned int *buffer,
			       unsigned int *buf_size, unsigned int size_count)
{
	int status = 0;
	unsigned int size = *buf_size;
	unsigned int count;
	unsigned int skip_count;
	unsigned int dummy;

	if (buffer == NULL)
		return -1;

	count = 0;
	while (count < size && count < size_count) {
		status = dspio_read(codec, buffer++);
		if (status != 0)
			break;
		count++;
	}

	skip_count = count;
	if (status == 0) {
		while (skip_count < size) {
			status = dspio_read(codec, &dummy);
			if (status != 0)
				break;
			skip_count++;
		}
	}
	*buf_size = count;

	return status;
}

/*
 * Construct the SCP header using corresponding fields
 */
static inline unsigned int
make_scp_header(unsigned int target_id, unsigned int source_id,
		unsigned int get_flag, unsigned int req,
		unsigned int device_flag, unsigned int resp_flag,
		unsigned int error_flag, unsigned int data_size)
{
	unsigned int header = 0;

	header = (data_size & 0x1f) << 27;
	header |= (error_flag & 0x01) << 26;
	header |= (resp_flag & 0x01) << 25;
	header |= (device_flag & 0x01) << 24;
	header |= (req & 0x7f) << 17;
	header |= (get_flag & 0x01) << 16;
	header |= (source_id & 0xff) << 8;
	header |= target_id & 0xff;

	return header;
}

/*
 * Extract corresponding fields from SCP header
 */
static inline void
extract_scp_header(unsigned int header,
		   unsigned int *target_id, unsigned int *source_id,
		   unsigned int *get_flag, unsigned int *req,
		   unsigned int *device_flag, unsigned int *resp_flag,
		   unsigned int *error_flag, unsigned int *data_size)
{
	if (data_size)
		*data_size = (header >> 27) & 0x1f;
	if (error_flag)
		*error_flag = (header >> 26) & 0x01;
	if (resp_flag)
		*resp_flag = (header >> 25) & 0x01;
	if (device_flag)
		*device_flag = (header >> 24) & 0x01;
	if (req)
		*req = (header >> 17) & 0x7f;
	if (get_flag)
		*get_flag = (header >> 16) & 0x01;
	if (source_id)
		*source_id = (header >> 8) & 0xff;
	if (target_id)
		*target_id = header & 0xff;
}

#define SCP_MAX_DATA_WORDS  (16)

/* Structure to contain any SCP message */
struct scp_msg {
	unsigned int hdr;
	unsigned int data[SCP_MAX_DATA_WORDS];
};

static void dspio_clear_response_queue(struct hda_codec *codec)
{
	unsigned int dummy = 0;
	int status = -1;

	/* clear all from the response queue */
	do {
		status = dspio_read(codec, &dummy);
	} while (status == 0);
}

static int dspio_get_response_data(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int data = 0;
	unsigned int count;

	if (dspio_read(codec, &data) < 0)
		return -EIO;

	if ((data & 0x00ffffff) == spec->wait_scp_header) {
		spec->scp_resp_header = data;
		spec->scp_resp_count = data >> 27;
		count = spec->wait_num_data;
		dspio_read_multiple(codec, spec->scp_resp_data,
				    &spec->scp_resp_count, count);
		return 0;
	}

	return -EIO;
}

/*
 * Send SCP message to DSP
 */
static int dspio_send_scp_message(struct hda_codec *codec,
				  unsigned char *send_buf,
				  unsigned int send_buf_size,
				  unsigned char *return_buf,
				  unsigned int return_buf_size,
				  unsigned int *bytes_returned)
{
	struct ca0132_spec *spec = codec->spec;
	int status = -1;
	unsigned int scp_send_size = 0;
	unsigned int total_size;
	bool waiting_for_resp = false;
	unsigned int header;
	struct scp_msg *ret_msg;
	unsigned int resp_src_id, resp_target_id;
	unsigned int data_size, src_id, target_id, get_flag, device_flag;

	if (bytes_returned)
		*bytes_returned = 0;

	/* get scp header from buffer */
	header = *((unsigned int *)send_buf);
	extract_scp_header(header, &target_id, &src_id, &get_flag, NULL,
			   &device_flag, NULL, NULL, &data_size);
	scp_send_size = data_size + 1;
	total_size = (scp_send_size * 4);

	if (send_buf_size < total_size)
		return -EINVAL;

	if (get_flag || device_flag) {
		if (!return_buf || return_buf_size < 4 || !bytes_returned)
			return -EINVAL;

		spec->wait_scp_header = *((unsigned int *)send_buf);

		/* swap source id with target id */
		resp_target_id = src_id;
		resp_src_id = target_id;
		spec->wait_scp_header &= 0xffff0000;
		spec->wait_scp_header |= (resp_src_id << 8) | (resp_target_id);
		spec->wait_num_data = return_buf_size/sizeof(unsigned int) - 1;
		spec->wait_scp = 1;
		waiting_for_resp = true;
	}

	status = dspio_write_multiple(codec, (unsigned int *)send_buf,
				      scp_send_size);
	if (status < 0) {
		spec->wait_scp = 0;
		return status;
	}

	if (waiting_for_resp) {
		unsigned long timeout = jiffies + msecs_to_jiffies(1000);
		memset(return_buf, 0, return_buf_size);
		do {
			msleep(20);
		} while (spec->wait_scp && time_before(jiffies, timeout));
		waiting_for_resp = false;
		if (!spec->wait_scp) {
			ret_msg = (struct scp_msg *)return_buf;
			memcpy(&ret_msg->hdr, &spec->scp_resp_header, 4);
			memcpy(&ret_msg->data, spec->scp_resp_data,
			       spec->wait_num_data);
			*bytes_returned = (spec->scp_resp_count + 1) * 4;
			status = 0;
		} else {
			status = -EIO;
		}
		spec->wait_scp = 0;
	}

	return status;
}

/**
 * Prepare and send the SCP message to DSP
 * @codec: the HDA codec
 * @mod_id: ID of the DSP module to send the command
 * @req: ID of request to send to the DSP module
 * @dir: SET or GET
 * @data: pointer to the data to send with the request, request specific
 * @len: length of the data, in bytes
 * @reply: point to the buffer to hold data returned for a reply
 * @reply_len: length of the reply buffer returned from GET
 *
 * Returns zero or a negative error code.
 */
static int dspio_scp(struct hda_codec *codec,
		int mod_id, int src_id, int req, int dir, const void *data,
		unsigned int len, void *reply, unsigned int *reply_len)
{
	int status = 0;
	struct scp_msg scp_send, scp_reply;
	unsigned int ret_bytes, send_size, ret_size;
	unsigned int send_get_flag, reply_resp_flag, reply_error_flag;
	unsigned int reply_data_size;

	memset(&scp_send, 0, sizeof(scp_send));
	memset(&scp_reply, 0, sizeof(scp_reply));

	if ((len != 0 && data == NULL) || (len > SCP_MAX_DATA_WORDS))
		return -EINVAL;

	if (dir == SCP_GET && reply == NULL) {
		codec_dbg(codec, "dspio_scp get but has no buffer\n");
		return -EINVAL;
	}

	if (reply != NULL && (reply_len == NULL || (*reply_len == 0))) {
		codec_dbg(codec, "dspio_scp bad resp buf len parms\n");
		return -EINVAL;
	}

	scp_send.hdr = make_scp_header(mod_id, src_id, (dir == SCP_GET), req,
				       0, 0, 0, len/sizeof(unsigned int));
	if (data != NULL && len > 0) {
		len = min((unsigned int)(sizeof(scp_send.data)), len);
		memcpy(scp_send.data, data, len);
	}

	ret_bytes = 0;
	send_size = sizeof(unsigned int) + len;
	status = dspio_send_scp_message(codec, (unsigned char *)&scp_send,
					send_size, (unsigned char *)&scp_reply,
					sizeof(scp_reply), &ret_bytes);

	if (status < 0) {
		codec_dbg(codec, "dspio_scp: send scp msg failed\n");
		return status;
	}

	/* extract send and reply headers members */
	extract_scp_header(scp_send.hdr, NULL, NULL, &send_get_flag,
			   NULL, NULL, NULL, NULL, NULL);
	extract_scp_header(scp_reply.hdr, NULL, NULL, NULL, NULL, NULL,
			   &reply_resp_flag, &reply_error_flag,
			   &reply_data_size);

	if (!send_get_flag)
		return 0;

	if (reply_resp_flag && !reply_error_flag) {
		ret_size = (ret_bytes - sizeof(scp_reply.hdr))
					/ sizeof(unsigned int);

		if (*reply_len < ret_size*sizeof(unsigned int)) {
			codec_dbg(codec, "reply too long for buf\n");
			return -EINVAL;
		} else if (ret_size != reply_data_size) {
			codec_dbg(codec, "RetLen and HdrLen .NE.\n");
			return -EINVAL;
		} else if (!reply) {
			codec_dbg(codec, "NULL reply\n");
			return -EINVAL;
		} else {
			*reply_len = ret_size*sizeof(unsigned int);
			memcpy(reply, scp_reply.data, *reply_len);
		}
	} else {
		codec_dbg(codec, "reply ill-formed or errflag set\n");
		return -EIO;
	}

	return status;
}

/*
 * Set DSP parameters
 */
static int dspio_set_param(struct hda_codec *codec, int mod_id,
			int src_id, int req, const void *data, unsigned int len)
{
	return dspio_scp(codec, mod_id, src_id, req, SCP_SET, data, len, NULL,
			NULL);
}

static int dspio_set_uint_param(struct hda_codec *codec, int mod_id,
			int req, const unsigned int data)
{
	return dspio_set_param(codec, mod_id, 0x20, req, &data,
			sizeof(unsigned int));
}

static int dspio_set_uint_param_no_source(struct hda_codec *codec, int mod_id,
			int req, const unsigned int data)
{
	return dspio_set_param(codec, mod_id, 0x00, req, &data,
			sizeof(unsigned int));
}

/*
 * Allocate a DSP DMA channel via an SCP message
 */
static int dspio_alloc_dma_chan(struct hda_codec *codec, unsigned int *dma_chan)
{
	int status = 0;
	unsigned int size = sizeof(dma_chan);

	codec_dbg(codec, "     dspio_alloc_dma_chan() -- begin\n");
	status = dspio_scp(codec, MASTERCONTROL, 0x20,
			MASTERCONTROL_ALLOC_DMA_CHAN, SCP_GET, NULL, 0,
			dma_chan, &size);

	if (status < 0) {
		codec_dbg(codec, "dspio_alloc_dma_chan: SCP Failed\n");
		return status;
	}

	if ((*dma_chan + 1) == 0) {
		codec_dbg(codec, "no free dma channels to allocate\n");
		return -EBUSY;
	}

	codec_dbg(codec, "dspio_alloc_dma_chan: chan=%d\n", *dma_chan);
	codec_dbg(codec, "     dspio_alloc_dma_chan() -- complete\n");

	return status;
}

/*
 * Free a DSP DMA via an SCP message
 */
static int dspio_free_dma_chan(struct hda_codec *codec, unsigned int dma_chan)
{
	int status = 0;
	unsigned int dummy = 0;

	codec_dbg(codec, "     dspio_free_dma_chan() -- begin\n");
	codec_dbg(codec, "dspio_free_dma_chan: chan=%d\n", dma_chan);

	status = dspio_scp(codec, MASTERCONTROL, 0x20,
			MASTERCONTROL_ALLOC_DMA_CHAN, SCP_SET, &dma_chan,
			sizeof(dma_chan), NULL, &dummy);

	if (status < 0) {
		codec_dbg(codec, "dspio_free_dma_chan: SCP Failed\n");
		return status;
	}

	codec_dbg(codec, "     dspio_free_dma_chan() -- complete\n");

	return status;
}

/*
 * (Re)start the DSP
 */
static int dsp_set_run_state(struct hda_codec *codec)
{
	unsigned int dbg_ctrl_reg;
	unsigned int halt_state;
	int err;

	err = chipio_read(codec, DSP_DBGCNTL_INST_OFFSET, &dbg_ctrl_reg);
	if (err < 0)
		return err;

	halt_state = (dbg_ctrl_reg & DSP_DBGCNTL_STATE_MASK) >>
		      DSP_DBGCNTL_STATE_LOBIT;

	if (halt_state != 0) {
		dbg_ctrl_reg &= ~((halt_state << DSP_DBGCNTL_SS_LOBIT) &
				  DSP_DBGCNTL_SS_MASK);
		err = chipio_write(codec, DSP_DBGCNTL_INST_OFFSET,
				   dbg_ctrl_reg);
		if (err < 0)
			return err;

		dbg_ctrl_reg |= (halt_state << DSP_DBGCNTL_EXEC_LOBIT) &
				DSP_DBGCNTL_EXEC_MASK;
		err = chipio_write(codec, DSP_DBGCNTL_INST_OFFSET,
				   dbg_ctrl_reg);
		if (err < 0)
			return err;
	}

	return 0;
}

/*
 * Reset the DSP
 */
static int dsp_reset(struct hda_codec *codec)
{
	unsigned int res;
	int retry = 20;

	codec_dbg(codec, "dsp_reset\n");
	do {
		res = dspio_send(codec, VENDOR_DSPIO_DSP_INIT, 0);
		retry--;
	} while (res == -EIO && retry);

	if (!retry) {
		codec_dbg(codec, "dsp_reset timeout\n");
		return -EIO;
	}

	return 0;
}

/*
 * Convert chip address to DSP address
 */
static unsigned int dsp_chip_to_dsp_addx(unsigned int chip_addx,
					bool *code, bool *yram)
{
	*code = *yram = false;

	if (UC_RANGE(chip_addx, 1)) {
		*code = true;
		return UC_OFF(chip_addx);
	} else if (X_RANGE_ALL(chip_addx, 1)) {
		return X_OFF(chip_addx);
	} else if (Y_RANGE_ALL(chip_addx, 1)) {
		*yram = true;
		return Y_OFF(chip_addx);
	}

	return INVALID_CHIP_ADDRESS;
}

/*
 * Check if the DSP DMA is active
 */
static bool dsp_is_dma_active(struct hda_codec *codec, unsigned int dma_chan)
{
	unsigned int dma_chnlstart_reg;

	chipio_read(codec, DSPDMAC_CHNLSTART_INST_OFFSET, &dma_chnlstart_reg);

	return ((dma_chnlstart_reg & (1 <<
			(DSPDMAC_CHNLSTART_EN_LOBIT + dma_chan))) != 0);
}

static int dsp_dma_setup_common(struct hda_codec *codec,
				unsigned int chip_addx,
				unsigned int dma_chan,
				unsigned int port_map_mask,
				bool ovly)
{
	int status = 0;
	unsigned int chnl_prop;
	unsigned int dsp_addx;
	unsigned int active;
	bool code, yram;

	codec_dbg(codec, "-- dsp_dma_setup_common() -- Begin ---------\n");

	if (dma_chan >= DSPDMAC_DMA_CFG_CHANNEL_COUNT) {
		codec_dbg(codec, "dma chan num invalid\n");
		return -EINVAL;
	}

	if (dsp_is_dma_active(codec, dma_chan)) {
		codec_dbg(codec, "dma already active\n");
		return -EBUSY;
	}

	dsp_addx = dsp_chip_to_dsp_addx(chip_addx, &code, &yram);

	if (dsp_addx == INVALID_CHIP_ADDRESS) {
		codec_dbg(codec, "invalid chip addr\n");
		return -ENXIO;
	}

	chnl_prop = DSPDMAC_CHNLPROP_AC_MASK;
	active = 0;

	codec_dbg(codec, "   dsp_dma_setup_common()    start reg pgm\n");

	if (ovly) {
		status = chipio_read(codec, DSPDMAC_CHNLPROP_INST_OFFSET,
				     &chnl_prop);

		if (status < 0) {
			codec_dbg(codec, "read CHNLPROP Reg fail\n");
			return status;
		}
		codec_dbg(codec, "dsp_dma_setup_common() Read CHNLPROP\n");
	}

	if (!code)
		chnl_prop &= ~(1 << (DSPDMAC_CHNLPROP_MSPCE_LOBIT + dma_chan));
	else
		chnl_prop |=  (1 << (DSPDMAC_CHNLPROP_MSPCE_LOBIT + dma_chan));

	chnl_prop &= ~(1 << (DSPDMAC_CHNLPROP_DCON_LOBIT + dma_chan));

	status = chipio_write(codec, DSPDMAC_CHNLPROP_INST_OFFSET, chnl_prop);
	if (status < 0) {
		codec_dbg(codec, "write CHNLPROP Reg fail\n");
		return status;
	}
	codec_dbg(codec, "   dsp_dma_setup_common()    Write CHNLPROP\n");

	if (ovly) {
		status = chipio_read(codec, DSPDMAC_ACTIVE_INST_OFFSET,
				     &active);

		if (status < 0) {
			codec_dbg(codec, "read ACTIVE Reg fail\n");
			return status;
		}
		codec_dbg(codec, "dsp_dma_setup_common() Read ACTIVE\n");
	}

	active &= (~(1 << (DSPDMAC_ACTIVE_AAR_LOBIT + dma_chan))) &
		DSPDMAC_ACTIVE_AAR_MASK;

	status = chipio_write(codec, DSPDMAC_ACTIVE_INST_OFFSET, active);
	if (status < 0) {
		codec_dbg(codec, "write ACTIVE Reg fail\n");
		return status;
	}

	codec_dbg(codec, "   dsp_dma_setup_common()    Write ACTIVE\n");

	status = chipio_write(codec, DSPDMAC_AUDCHSEL_INST_OFFSET(dma_chan),
			      port_map_mask);
	if (status < 0) {
		codec_dbg(codec, "write AUDCHSEL Reg fail\n");
		return status;
	}
	codec_dbg(codec, "   dsp_dma_setup_common()    Write AUDCHSEL\n");

	status = chipio_write(codec, DSPDMAC_IRQCNT_INST_OFFSET(dma_chan),
			DSPDMAC_IRQCNT_BICNT_MASK | DSPDMAC_IRQCNT_CICNT_MASK);
	if (status < 0) {
		codec_dbg(codec, "write IRQCNT Reg fail\n");
		return status;
	}
	codec_dbg(codec, "   dsp_dma_setup_common()    Write IRQCNT\n");

	codec_dbg(codec,
		   "ChipA=0x%x,DspA=0x%x,dmaCh=%u, "
		   "CHSEL=0x%x,CHPROP=0x%x,Active=0x%x\n",
		   chip_addx, dsp_addx, dma_chan,
		   port_map_mask, chnl_prop, active);

	codec_dbg(codec, "-- dsp_dma_setup_common() -- Complete ------\n");

	return 0;
}

/*
 * Setup the DSP DMA per-transfer-specific registers
 */
static int dsp_dma_setup(struct hda_codec *codec,
			unsigned int chip_addx,
			unsigned int count,
			unsigned int dma_chan)
{
	int status = 0;
	bool code, yram;
	unsigned int dsp_addx;
	unsigned int addr_field;
	unsigned int incr_field;
	unsigned int base_cnt;
	unsigned int cur_cnt;
	unsigned int dma_cfg = 0;
	unsigned int adr_ofs = 0;
	unsigned int xfr_cnt = 0;
	const unsigned int max_dma_count = 1 << (DSPDMAC_XFRCNT_BCNT_HIBIT -
						DSPDMAC_XFRCNT_BCNT_LOBIT + 1);

	codec_dbg(codec, "-- dsp_dma_setup() -- Begin ---------\n");

	if (count > max_dma_count) {
		codec_dbg(codec, "count too big\n");
		return -EINVAL;
	}

	dsp_addx = dsp_chip_to_dsp_addx(chip_addx, &code, &yram);
	if (dsp_addx == INVALID_CHIP_ADDRESS) {
		codec_dbg(codec, "invalid chip addr\n");
		return -ENXIO;
	}

	codec_dbg(codec, "   dsp_dma_setup()    start reg pgm\n");

	addr_field = dsp_addx << DSPDMAC_DMACFG_DBADR_LOBIT;
	incr_field   = 0;

	if (!code) {
		addr_field <<= 1;
		if (yram)
			addr_field |= (1 << DSPDMAC_DMACFG_DBADR_LOBIT);

		incr_field  = (1 << DSPDMAC_DMACFG_AINCR_LOBIT);
	}

	dma_cfg = addr_field + incr_field;
	status = chipio_write(codec, DSPDMAC_DMACFG_INST_OFFSET(dma_chan),
				dma_cfg);
	if (status < 0) {
		codec_dbg(codec, "write DMACFG Reg fail\n");
		return status;
	}
	codec_dbg(codec, "   dsp_dma_setup()    Write DMACFG\n");

	adr_ofs = (count - 1) << (DSPDMAC_DSPADROFS_BOFS_LOBIT +
							(code ? 0 : 1));

	status = chipio_write(codec, DSPDMAC_DSPADROFS_INST_OFFSET(dma_chan),
				adr_ofs);
	if (status < 0) {
		codec_dbg(codec, "write DSPADROFS Reg fail\n");
		return status;
	}
	codec_dbg(codec, "   dsp_dma_setup()    Write DSPADROFS\n");

	base_cnt = (count - 1) << DSPDMAC_XFRCNT_BCNT_LOBIT;

	cur_cnt  = (count - 1) << DSPDMAC_XFRCNT_CCNT_LOBIT;

	xfr_cnt = base_cnt | cur_cnt;

	status = chipio_write(codec,
				DSPDMAC_XFRCNT_INST_OFFSET(dma_chan), xfr_cnt);
	if (status < 0) {
		codec_dbg(codec, "write XFRCNT Reg fail\n");
		return status;
	}
	codec_dbg(codec, "   dsp_dma_setup()    Write XFRCNT\n");

	codec_dbg(codec,
		   "ChipA=0x%x, cnt=0x%x, DMACFG=0x%x, "
		   "ADROFS=0x%x, XFRCNT=0x%x\n",
		   chip_addx, count, dma_cfg, adr_ofs, xfr_cnt);

	codec_dbg(codec, "-- dsp_dma_setup() -- Complete ---------\n");

	return 0;
}

/*
 * Start the DSP DMA
 */
static int dsp_dma_start(struct hda_codec *codec,
			 unsigned int dma_chan, bool ovly)
{
	unsigned int reg = 0;
	int status = 0;

	codec_dbg(codec, "-- dsp_dma_start() -- Begin ---------\n");

	if (ovly) {
		status = chipio_read(codec,
				     DSPDMAC_CHNLSTART_INST_OFFSET, &reg);

		if (status < 0) {
			codec_dbg(codec, "read CHNLSTART reg fail\n");
			return status;
		}
		codec_dbg(codec, "-- dsp_dma_start()    Read CHNLSTART\n");

		reg &= ~(DSPDMAC_CHNLSTART_EN_MASK |
				DSPDMAC_CHNLSTART_DIS_MASK);
	}

	status = chipio_write(codec, DSPDMAC_CHNLSTART_INST_OFFSET,
			reg | (1 << (dma_chan + DSPDMAC_CHNLSTART_EN_LOBIT)));
	if (status < 0) {
		codec_dbg(codec, "write CHNLSTART reg fail\n");
		return status;
	}
	codec_dbg(codec, "-- dsp_dma_start() -- Complete ---------\n");

	return status;
}

/*
 * Stop the DSP DMA
 */
static int dsp_dma_stop(struct hda_codec *codec,
			unsigned int dma_chan, bool ovly)
{
	unsigned int reg = 0;
	int status = 0;

	codec_dbg(codec, "-- dsp_dma_stop() -- Begin ---------\n");

	if (ovly) {
		status = chipio_read(codec,
				     DSPDMAC_CHNLSTART_INST_OFFSET, &reg);

		if (status < 0) {
			codec_dbg(codec, "read CHNLSTART reg fail\n");
			return status;
		}
		codec_dbg(codec, "-- dsp_dma_stop()    Read CHNLSTART\n");
		reg &= ~(DSPDMAC_CHNLSTART_EN_MASK |
				DSPDMAC_CHNLSTART_DIS_MASK);
	}

	status = chipio_write(codec, DSPDMAC_CHNLSTART_INST_OFFSET,
			reg | (1 << (dma_chan + DSPDMAC_CHNLSTART_DIS_LOBIT)));
	if (status < 0) {
		codec_dbg(codec, "write CHNLSTART reg fail\n");
		return status;
	}
	codec_dbg(codec, "-- dsp_dma_stop() -- Complete ---------\n");

	return status;
}

/**
 * Allocate router ports
 *
 * @codec: the HDA codec
 * @num_chans: number of channels in the stream
 * @ports_per_channel: number of ports per channel
 * @start_device: start device
 * @port_map: pointer to the port list to hold the allocated ports
 *
 * Returns zero or a negative error code.
 */
static int dsp_allocate_router_ports(struct hda_codec *codec,
				     unsigned int num_chans,
				     unsigned int ports_per_channel,
				     unsigned int start_device,
				     unsigned int *port_map)
{
	int status = 0;
	int res;
	u8 val;

	status = chipio_send(codec, VENDOR_CHIPIO_STATUS, 0);
	if (status < 0)
		return status;

	val = start_device << 6;
	val |= (ports_per_channel - 1) << 4;
	val |= num_chans - 1;

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PORT_ALLOC_CONFIG_SET,
			    val);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PORT_ALLOC_SET,
			    MEM_CONNID_DSP);

	status = chipio_send(codec, VENDOR_CHIPIO_STATUS, 0);
	if (status < 0)
		return status;

	res = snd_hda_codec_read(codec, WIDGET_CHIP_CTRL, 0,
				VENDOR_CHIPIO_PORT_ALLOC_GET, 0);

	*port_map = res;

	return (res < 0) ? res : 0;
}

/*
 * Free router ports
 */
static int dsp_free_router_ports(struct hda_codec *codec)
{
	int status = 0;

	status = chipio_send(codec, VENDOR_CHIPIO_STATUS, 0);
	if (status < 0)
		return status;

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PORT_FREE_SET,
			    MEM_CONNID_DSP);

	status = chipio_send(codec, VENDOR_CHIPIO_STATUS, 0);

	return status;
}

/*
 * Allocate DSP ports for the download stream
 */
static int dsp_allocate_ports(struct hda_codec *codec,
			unsigned int num_chans,
			unsigned int rate_multi, unsigned int *port_map)
{
	int status;

	codec_dbg(codec, "     dsp_allocate_ports() -- begin\n");

	if ((rate_multi != 1) && (rate_multi != 2) && (rate_multi != 4)) {
		codec_dbg(codec, "bad rate multiple\n");
		return -EINVAL;
	}

	status = dsp_allocate_router_ports(codec, num_chans,
					   rate_multi, 0, port_map);

	codec_dbg(codec, "     dsp_allocate_ports() -- complete\n");

	return status;
}

static int dsp_allocate_ports_format(struct hda_codec *codec,
			const unsigned short fmt,
			unsigned int *port_map)
{
	int status;
	unsigned int num_chans;

	unsigned int sample_rate_div = ((get_hdafmt_rate(fmt) >> 0) & 3) + 1;
	unsigned int sample_rate_mul = ((get_hdafmt_rate(fmt) >> 3) & 3) + 1;
	unsigned int rate_multi = sample_rate_mul / sample_rate_div;

	if ((rate_multi != 1) && (rate_multi != 2) && (rate_multi != 4)) {
		codec_dbg(codec, "bad rate multiple\n");
		return -EINVAL;
	}

	num_chans = get_hdafmt_chs(fmt) + 1;

	status = dsp_allocate_ports(codec, num_chans, rate_multi, port_map);

	return status;
}

/*
 * free DSP ports
 */
static int dsp_free_ports(struct hda_codec *codec)
{
	int status;

	codec_dbg(codec, "     dsp_free_ports() -- begin\n");

	status = dsp_free_router_ports(codec);
	if (status < 0) {
		codec_dbg(codec, "free router ports fail\n");
		return status;
	}
	codec_dbg(codec, "     dsp_free_ports() -- complete\n");

	return status;
}

/*
 *  HDA DMA engine stuffs for DSP code download
 */
struct dma_engine {
	struct hda_codec *codec;
	unsigned short m_converter_format;
	struct snd_dma_buffer *dmab;
	unsigned int buf_size;
};


enum dma_state {
	DMA_STATE_STOP  = 0,
	DMA_STATE_RUN   = 1
};

static int dma_convert_to_hda_format(struct hda_codec *codec,
		unsigned int sample_rate,
		unsigned short channels,
		unsigned short *hda_format)
{
	unsigned int format_val;

	format_val = snd_hdac_calc_stream_format(sample_rate,
				channels, SNDRV_PCM_FORMAT_S32_LE, 32, 0);

	if (hda_format)
		*hda_format = (unsigned short)format_val;

	return 0;
}

/*
 *  Reset DMA for DSP download
 */
static int dma_reset(struct dma_engine *dma)
{
	struct hda_codec *codec = dma->codec;
	struct ca0132_spec *spec = codec->spec;
	int status;

	if (dma->dmab->area)
		snd_hda_codec_load_dsp_cleanup(codec, dma->dmab);

	status = snd_hda_codec_load_dsp_prepare(codec,
			dma->m_converter_format,
			dma->buf_size,
			dma->dmab);
	if (status < 0)
		return status;
	spec->dsp_stream_id = status;
	return 0;
}

static int dma_set_state(struct dma_engine *dma, enum dma_state state)
{
	bool cmd;

	switch (state) {
	case DMA_STATE_STOP:
		cmd = false;
		break;
	case DMA_STATE_RUN:
		cmd = true;
		break;
	default:
		return 0;
	}

	snd_hda_codec_load_dsp_trigger(dma->codec, cmd);
	return 0;
}

static unsigned int dma_get_buffer_size(struct dma_engine *dma)
{
	return dma->dmab->bytes;
}

static unsigned char *dma_get_buffer_addr(struct dma_engine *dma)
{
	return dma->dmab->area;
}

static int dma_xfer(struct dma_engine *dma,
		const unsigned int *data,
		unsigned int count)
{
	memcpy(dma->dmab->area, data, count);
	return 0;
}

static void dma_get_converter_format(
		struct dma_engine *dma,
		unsigned short *format)
{
	if (format)
		*format = dma->m_converter_format;
}

static unsigned int dma_get_stream_id(struct dma_engine *dma)
{
	struct ca0132_spec *spec = dma->codec->spec;

	return spec->dsp_stream_id;
}

struct dsp_image_seg {
	u32 magic;
	u32 chip_addr;
	u32 count;
	u32 data[0];
};

static const u32 g_magic_value = 0x4c46584d;
static const u32 g_chip_addr_magic_value = 0xFFFFFF01;

static bool is_valid(const struct dsp_image_seg *p)
{
	return p->magic == g_magic_value;
}

static bool is_hci_prog_list_seg(const struct dsp_image_seg *p)
{
	return g_chip_addr_magic_value == p->chip_addr;
}

static bool is_last(const struct dsp_image_seg *p)
{
	return p->count == 0;
}

static size_t dsp_sizeof(const struct dsp_image_seg *p)
{
	return sizeof(*p) + p->count*sizeof(u32);
}

static const struct dsp_image_seg *get_next_seg_ptr(
				const struct dsp_image_seg *p)
{
	return (struct dsp_image_seg *)((unsigned char *)(p) + dsp_sizeof(p));
}

/*
 * CA0132 chip DSP transfer stuffs.  For DSP download.
 */
#define INVALID_DMA_CHANNEL (~0U)

/*
 * Program a list of address/data pairs via the ChipIO widget.
 * The segment data is in the format of successive pairs of words.
 * These are repeated as indicated by the segment's count field.
 */
static int dspxfr_hci_write(struct hda_codec *codec,
			const struct dsp_image_seg *fls)
{
	int status;
	const u32 *data;
	unsigned int count;

	if (fls == NULL || fls->chip_addr != g_chip_addr_magic_value) {
		codec_dbg(codec, "hci_write invalid params\n");
		return -EINVAL;
	}

	count = fls->count;
	data = (u32 *)(fls->data);
	while (count >= 2) {
		status = chipio_write(codec, data[0], data[1]);
		if (status < 0) {
			codec_dbg(codec, "hci_write chipio failed\n");
			return status;
		}
		count -= 2;
		data  += 2;
	}
	return 0;
}

/**
 * Write a block of data into DSP code or data RAM using pre-allocated
 * DMA engine.
 *
 * @codec: the HDA codec
 * @fls: pointer to a fast load image
 * @reloc: Relocation address for loading single-segment overlays, or 0 for
 *	   no relocation
 * @dma_engine: pointer to DMA engine to be used for DSP download
 * @dma_chan: The number of DMA channels used for DSP download
 * @port_map_mask: port mapping
 * @ovly: TRUE if overlay format is required
 *
 * Returns zero or a negative error code.
 */
static int dspxfr_one_seg(struct hda_codec *codec,
			const struct dsp_image_seg *fls,
			unsigned int reloc,
			struct dma_engine *dma_engine,
			unsigned int dma_chan,
			unsigned int port_map_mask,
			bool ovly)
{
	int status = 0;
	bool comm_dma_setup_done = false;
	const unsigned int *data;
	unsigned int chip_addx;
	unsigned int words_to_write;
	unsigned int buffer_size_words;
	unsigned char *buffer_addx;
	unsigned short hda_format;
	unsigned int sample_rate_div;
	unsigned int sample_rate_mul;
	unsigned int num_chans;
	unsigned int hda_frame_size_words;
	unsigned int remainder_words;
	const u32 *data_remainder;
	u32 chip_addx_remainder;
	unsigned int run_size_words;
	const struct dsp_image_seg *hci_write = NULL;
	unsigned long timeout;
	bool dma_active;

	if (fls == NULL)
		return -EINVAL;
	if (is_hci_prog_list_seg(fls)) {
		hci_write = fls;
		fls = get_next_seg_ptr(fls);
	}

	if (hci_write && (!fls || is_last(fls))) {
		codec_dbg(codec, "hci_write\n");
		return dspxfr_hci_write(codec, hci_write);
	}

	if (fls == NULL || dma_engine == NULL || port_map_mask == 0) {
		codec_dbg(codec, "Invalid Params\n");
		return -EINVAL;
	}

	data = fls->data;
	chip_addx = fls->chip_addr,
	words_to_write = fls->count;

	if (!words_to_write)
		return hci_write ? dspxfr_hci_write(codec, hci_write) : 0;
	if (reloc)
		chip_addx = (chip_addx & (0xFFFF0000 << 2)) + (reloc << 2);

	if (!UC_RANGE(chip_addx, words_to_write) &&
	    !X_RANGE_ALL(chip_addx, words_to_write) &&
	    !Y_RANGE_ALL(chip_addx, words_to_write)) {
		codec_dbg(codec, "Invalid chip_addx Params\n");
		return -EINVAL;
	}

	buffer_size_words = (unsigned int)dma_get_buffer_size(dma_engine) /
					sizeof(u32);

	buffer_addx = dma_get_buffer_addr(dma_engine);

	if (buffer_addx == NULL) {
		codec_dbg(codec, "dma_engine buffer NULL\n");
		return -EINVAL;
	}

	dma_get_converter_format(dma_engine, &hda_format);
	sample_rate_div = ((get_hdafmt_rate(hda_format) >> 0) & 3) + 1;
	sample_rate_mul = ((get_hdafmt_rate(hda_format) >> 3) & 3) + 1;
	num_chans = get_hdafmt_chs(hda_format) + 1;

	hda_frame_size_words = ((sample_rate_div == 0) ? 0 :
			(num_chans * sample_rate_mul / sample_rate_div));

	if (hda_frame_size_words == 0) {
		codec_dbg(codec, "frmsz zero\n");
		return -EINVAL;
	}

	buffer_size_words = min(buffer_size_words,
				(unsigned int)(UC_RANGE(chip_addx, 1) ?
				65536 : 32768));
	buffer_size_words -= buffer_size_words % hda_frame_size_words;
	codec_dbg(codec,
		   "chpadr=0x%08x frmsz=%u nchan=%u "
		   "rate_mul=%u div=%u bufsz=%u\n",
		   chip_addx, hda_frame_size_words, num_chans,
		   sample_rate_mul, sample_rate_div, buffer_size_words);

	if (buffer_size_words < hda_frame_size_words) {
		codec_dbg(codec, "dspxfr_one_seg:failed\n");
		return -EINVAL;
	}

	remainder_words = words_to_write % hda_frame_size_words;
	data_remainder = data;
	chip_addx_remainder = chip_addx;

	data += remainder_words;
	chip_addx += remainder_words*sizeof(u32);
	words_to_write -= remainder_words;

	while (words_to_write != 0) {
		run_size_words = min(buffer_size_words, words_to_write);
		codec_dbg(codec, "dspxfr (seg loop)cnt=%u rs=%u remainder=%u\n",
			    words_to_write, run_size_words, remainder_words);
		dma_xfer(dma_engine, data, run_size_words*sizeof(u32));
		if (!comm_dma_setup_done) {
			status = dsp_dma_stop(codec, dma_chan, ovly);
			if (status < 0)
				return status;
			status = dsp_dma_setup_common(codec, chip_addx,
						dma_chan, port_map_mask, ovly);
			if (status < 0)
				return status;
			comm_dma_setup_done = true;
		}

		status = dsp_dma_setup(codec, chip_addx,
						run_size_words, dma_chan);
		if (status < 0)
			return status;
		status = dsp_dma_start(codec, dma_chan, ovly);
		if (status < 0)
			return status;
		if (!dsp_is_dma_active(codec, dma_chan)) {
			codec_dbg(codec, "dspxfr:DMA did not start\n");
			return -EIO;
		}
		status = dma_set_state(dma_engine, DMA_STATE_RUN);
		if (status < 0)
			return status;
		if (remainder_words != 0) {
			status = chipio_write_multiple(codec,
						chip_addx_remainder,
						data_remainder,
						remainder_words);
			if (status < 0)
				return status;
			remainder_words = 0;
		}
		if (hci_write) {
			status = dspxfr_hci_write(codec, hci_write);
			if (status < 0)
				return status;
			hci_write = NULL;
		}

		timeout = jiffies + msecs_to_jiffies(2000);
		do {
			dma_active = dsp_is_dma_active(codec, dma_chan);
			if (!dma_active)
				break;
			msleep(20);
		} while (time_before(jiffies, timeout));
		if (dma_active)
			break;

		codec_dbg(codec, "+++++ DMA complete\n");
		dma_set_state(dma_engine, DMA_STATE_STOP);
		status = dma_reset(dma_engine);

		if (status < 0)
			return status;

		data += run_size_words;
		chip_addx += run_size_words*sizeof(u32);
		words_to_write -= run_size_words;
	}

	if (remainder_words != 0) {
		status = chipio_write_multiple(codec, chip_addx_remainder,
					data_remainder, remainder_words);
	}

	return status;
}

/**
 * Write the entire DSP image of a DSP code/data overlay to DSP memories
 *
 * @codec: the HDA codec
 * @fls_data: pointer to a fast load image
 * @reloc: Relocation address for loading single-segment overlays, or 0 for
 *	   no relocation
 * @sample_rate: sampling rate of the stream used for DSP download
 * @channels: channels of the stream used for DSP download
 * @ovly: TRUE if overlay format is required
 *
 * Returns zero or a negative error code.
 */
static int dspxfr_image(struct hda_codec *codec,
			const struct dsp_image_seg *fls_data,
			unsigned int reloc,
			unsigned int sample_rate,
			unsigned short channels,
			bool ovly)
{
	struct ca0132_spec *spec = codec->spec;
	int status;
	unsigned short hda_format = 0;
	unsigned int response;
	unsigned char stream_id = 0;
	struct dma_engine *dma_engine;
	unsigned int dma_chan;
	unsigned int port_map_mask;

	if (fls_data == NULL)
		return -EINVAL;

	dma_engine = kzalloc(sizeof(*dma_engine), GFP_KERNEL);
	if (!dma_engine)
		return -ENOMEM;

	dma_engine->dmab = kzalloc(sizeof(*dma_engine->dmab), GFP_KERNEL);
	if (!dma_engine->dmab) {
		kfree(dma_engine);
		return -ENOMEM;
	}

	dma_engine->codec = codec;
	dma_convert_to_hda_format(codec, sample_rate, channels, &hda_format);
	dma_engine->m_converter_format = hda_format;
	dma_engine->buf_size = (ovly ? DSP_DMA_WRITE_BUFLEN_OVLY :
			DSP_DMA_WRITE_BUFLEN_INIT) * 2;

	dma_chan = ovly ? INVALID_DMA_CHANNEL : 0;

	status = codec_set_converter_format(codec, WIDGET_CHIP_CTRL,
					hda_format, &response);

	if (status < 0) {
		codec_dbg(codec, "set converter format fail\n");
		goto exit;
	}

	status = snd_hda_codec_load_dsp_prepare(codec,
				dma_engine->m_converter_format,
				dma_engine->buf_size,
				dma_engine->dmab);
	if (status < 0)
		goto exit;
	spec->dsp_stream_id = status;

	if (ovly) {
		status = dspio_alloc_dma_chan(codec, &dma_chan);
		if (status < 0) {
			codec_dbg(codec, "alloc dmachan fail\n");
			dma_chan = INVALID_DMA_CHANNEL;
			goto exit;
		}
	}

	port_map_mask = 0;
	status = dsp_allocate_ports_format(codec, hda_format,
					&port_map_mask);
	if (status < 0) {
		codec_dbg(codec, "alloc ports fail\n");
		goto exit;
	}

	stream_id = dma_get_stream_id(dma_engine);
	status = codec_set_converter_stream_channel(codec,
			WIDGET_CHIP_CTRL, stream_id, 0, &response);
	if (status < 0) {
		codec_dbg(codec, "set stream chan fail\n");
		goto exit;
	}

	while ((fls_data != NULL) && !is_last(fls_data)) {
		if (!is_valid(fls_data)) {
			codec_dbg(codec, "FLS check fail\n");
			status = -EINVAL;
			goto exit;
		}
		status = dspxfr_one_seg(codec, fls_data, reloc,
					dma_engine, dma_chan,
					port_map_mask, ovly);
		if (status < 0)
			break;

		if (is_hci_prog_list_seg(fls_data))
			fls_data = get_next_seg_ptr(fls_data);

		if ((fls_data != NULL) && !is_last(fls_data))
			fls_data = get_next_seg_ptr(fls_data);
	}

	if (port_map_mask != 0)
		status = dsp_free_ports(codec);

	if (status < 0)
		goto exit;

	status = codec_set_converter_stream_channel(codec,
				WIDGET_CHIP_CTRL, 0, 0, &response);

exit:
	if (ovly && (dma_chan != INVALID_DMA_CHANNEL))
		dspio_free_dma_chan(codec, dma_chan);

	if (dma_engine->dmab->area)
		snd_hda_codec_load_dsp_cleanup(codec, dma_engine->dmab);
	kfree(dma_engine->dmab);
	kfree(dma_engine);

	return status;
}

/*
 * CA0132 DSP download stuffs.
 */
static void dspload_post_setup(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	codec_dbg(codec, "---- dspload_post_setup ------\n");
	if (!spec->use_alt_functions) {
		/*set DSP speaker to 2.0 configuration*/
		chipio_write(codec, XRAM_XRAM_INST_OFFSET(0x18), 0x08080080);
		chipio_write(codec, XRAM_XRAM_INST_OFFSET(0x19), 0x3f800000);

		/*update write pointer*/
		chipio_write(codec, XRAM_XRAM_INST_OFFSET(0x29), 0x00000002);
	}
}

/**
 * dspload_image - Download DSP from a DSP Image Fast Load structure.
 *
 * @codec: the HDA codec
 * @fls: pointer to a fast load image
 * @ovly: TRUE if overlay format is required
 * @reloc: Relocation address for loading single-segment overlays, or 0 for
 *	   no relocation
 * @autostart: TRUE if DSP starts after loading; ignored if ovly is TRUE
 * @router_chans: number of audio router channels to be allocated (0 means use
 *		  internal defaults; max is 32)
 *
 * Download DSP from a DSP Image Fast Load structure. This structure is a
 * linear, non-constant sized element array of structures, each of which
 * contain the count of the data to be loaded, the data itself, and the
 * corresponding starting chip address of the starting data location.
 * Returns zero or a negative error code.
 */
static int dspload_image(struct hda_codec *codec,
			const struct dsp_image_seg *fls,
			bool ovly,
			unsigned int reloc,
			bool autostart,
			int router_chans)
{
	int status = 0;
	unsigned int sample_rate;
	unsigned short channels;

	codec_dbg(codec, "---- dspload_image begin ------\n");
	if (router_chans == 0) {
		if (!ovly)
			router_chans = DMA_TRANSFER_FRAME_SIZE_NWORDS;
		else
			router_chans = DMA_OVERLAY_FRAME_SIZE_NWORDS;
	}

	sample_rate = 48000;
	channels = (unsigned short)router_chans;

	while (channels > 16) {
		sample_rate *= 2;
		channels /= 2;
	}

	do {
		codec_dbg(codec, "Ready to program DMA\n");
		if (!ovly)
			status = dsp_reset(codec);

		if (status < 0)
			break;

		codec_dbg(codec, "dsp_reset() complete\n");
		status = dspxfr_image(codec, fls, reloc, sample_rate, channels,
				      ovly);

		if (status < 0)
			break;

		codec_dbg(codec, "dspxfr_image() complete\n");
		if (autostart && !ovly) {
			dspload_post_setup(codec);
			status = dsp_set_run_state(codec);
		}

		codec_dbg(codec, "LOAD FINISHED\n");
	} while (0);

	return status;
}

#ifdef CONFIG_SND_HDA_CODEC_CA0132_DSP
static bool dspload_is_loaded(struct hda_codec *codec)
{
	unsigned int data = 0;
	int status = 0;

	status = chipio_read(codec, 0x40004, &data);
	if ((status < 0) || (data != 1))
		return false;

	return true;
}
#else
#define dspload_is_loaded(codec)	false
#endif

static bool dspload_wait_loaded(struct hda_codec *codec)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(2000);

	do {
		if (dspload_is_loaded(codec)) {
			codec_info(codec, "ca0132 DSP downloaded and running\n");
			return true;
		}
		msleep(20);
	} while (time_before(jiffies, timeout));

	codec_err(codec, "ca0132 failed to download DSP\n");
	return false;
}

/*
 * ca0113 related functions. The ca0113 acts as the HDA bus for the pci-e
 * based cards, and has a second mmio region, region2, that's used for special
 * commands.
 */

/*
 * For cards with PCI-E region2 (Sound Blaster Z/ZxR, Recon3D, and AE-5)
 * the mmio address 0x320 is used to set GPIO pins. The format for the data
 * The first eight bits are just the number of the pin. So far, I've only seen
 * this number go to 7.
 * AE-5 note: The AE-5 seems to use pins 2 and 3 to somehow set the color value
 * of the on-card LED. It seems to use pin 2 for data, then toggles 3 to on and
 * then off to send that bit.
 */
static void ca0113_mmio_gpio_set(struct hda_codec *codec, unsigned int gpio_pin,
		bool enable)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned short gpio_data;

	gpio_data = gpio_pin & 0xF;
	gpio_data |= ((enable << 8) & 0x100);

	writew(gpio_data, spec->mem_base + 0x320);
}

/*
 * Special pci region2 commands that are only used by the AE-5. They follow
 * a set format, and require reads at certain points to seemingly 'clear'
 * the response data. My first tests didn't do these reads, and would cause
 * the card to get locked up until the memory was read. These commands
 * seem to work with three distinct values that I've taken to calling group,
 * target-id, and value.
 */
static void ca0113_mmio_command_set(struct hda_codec *codec, unsigned int group,
		unsigned int target, unsigned int value)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int write_val;

	writel(0x0000007e, spec->mem_base + 0x210);
	readl(spec->mem_base + 0x210);
	writel(0x0000005a, spec->mem_base + 0x210);
	readl(spec->mem_base + 0x210);
	readl(spec->mem_base + 0x210);

	writel(0x00800005, spec->mem_base + 0x20c);
	writel(group, spec->mem_base + 0x804);

	writel(0x00800005, spec->mem_base + 0x20c);
	write_val = (target & 0xff);
	write_val |= (value << 8);


	writel(write_val, spec->mem_base + 0x204);
	/*
	 * Need delay here or else it goes too fast and works inconsistently.
	 */
	msleep(20);

	readl(spec->mem_base + 0x860);
	readl(spec->mem_base + 0x854);
	readl(spec->mem_base + 0x840);

	writel(0x00800004, spec->mem_base + 0x20c);
	writel(0x00000000, spec->mem_base + 0x210);
	readl(spec->mem_base + 0x210);
	readl(spec->mem_base + 0x210);
}

/*
 * This second type of command is used for setting the sound filter type.
 */
static void ca0113_mmio_command_set_type2(struct hda_codec *codec,
		unsigned int group, unsigned int target, unsigned int value)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int write_val;

	writel(0x0000007e, spec->mem_base + 0x210);
	readl(spec->mem_base + 0x210);
	writel(0x0000005a, spec->mem_base + 0x210);
	readl(spec->mem_base + 0x210);
	readl(spec->mem_base + 0x210);

	writel(0x00800003, spec->mem_base + 0x20c);
	writel(group, spec->mem_base + 0x804);

	writel(0x00800005, spec->mem_base + 0x20c);
	write_val = (target & 0xff);
	write_val |= (value << 8);


	writel(write_val, spec->mem_base + 0x204);
	msleep(20);
	readl(spec->mem_base + 0x860);
	readl(spec->mem_base + 0x854);
	readl(spec->mem_base + 0x840);

	writel(0x00800004, spec->mem_base + 0x20c);
	writel(0x00000000, spec->mem_base + 0x210);
	readl(spec->mem_base + 0x210);
	readl(spec->mem_base + 0x210);
}

/*
 * Setup GPIO for the other variants of Core3D.
 */

/*
 * Sets up the GPIO pins so that they are discoverable. If this isn't done,
 * the card shows as having no GPIO pins.
 */
static void ca0132_gpio_init(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	switch (spec->quirk) {
	case QUIRK_SBZ:
	case QUIRK_AE5:
		snd_hda_codec_write(codec, 0x01, 0, 0x793, 0x00);
		snd_hda_codec_write(codec, 0x01, 0, 0x794, 0x53);
		snd_hda_codec_write(codec, 0x01, 0, 0x790, 0x23);
		break;
	case QUIRK_R3DI:
		snd_hda_codec_write(codec, 0x01, 0, 0x793, 0x00);
		snd_hda_codec_write(codec, 0x01, 0, 0x794, 0x5B);
		break;
	}

}

/* Sets the GPIO for audio output. */
static void ca0132_gpio_setup(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	switch (spec->quirk) {
	case QUIRK_SBZ:
		snd_hda_codec_write(codec, 0x01, 0,
				AC_VERB_SET_GPIO_DIRECTION, 0x07);
		snd_hda_codec_write(codec, 0x01, 0,
				AC_VERB_SET_GPIO_MASK, 0x07);
		snd_hda_codec_write(codec, 0x01, 0,
				AC_VERB_SET_GPIO_DATA, 0x04);
		snd_hda_codec_write(codec, 0x01, 0,
				AC_VERB_SET_GPIO_DATA, 0x06);
		break;
	case QUIRK_R3DI:
		snd_hda_codec_write(codec, 0x01, 0,
				AC_VERB_SET_GPIO_DIRECTION, 0x1E);
		snd_hda_codec_write(codec, 0x01, 0,
				AC_VERB_SET_GPIO_MASK, 0x1F);
		snd_hda_codec_write(codec, 0x01, 0,
				AC_VERB_SET_GPIO_DATA, 0x0C);
		break;
	}
}

/*
 * GPIO control functions for the Recon3D integrated.
 */

enum r3di_gpio_bit {
	/* Bit 1 - Switch between front/rear mic. 0 = rear, 1 = front */
	R3DI_MIC_SELECT_BIT = 1,
	/* Bit 2 - Switch between headphone/line out. 0 = Headphone, 1 = Line */
	R3DI_OUT_SELECT_BIT = 2,
	/*
	 * I dunno what this actually does, but it stays on until the dsp
	 * is downloaded.
	 */
	R3DI_GPIO_DSP_DOWNLOADING = 3,
	/*
	 * Same as above, no clue what it does, but it comes on after the dsp
	 * is downloaded.
	 */
	R3DI_GPIO_DSP_DOWNLOADED = 4
};

enum r3di_mic_select {
	/* Set GPIO bit 1 to 0 for rear mic */
	R3DI_REAR_MIC = 0,
	/* Set GPIO bit 1 to 1 for front microphone*/
	R3DI_FRONT_MIC = 1
};

enum r3di_out_select {
	/* Set GPIO bit 2 to 0 for headphone */
	R3DI_HEADPHONE_OUT = 0,
	/* Set GPIO bit 2 to 1 for speaker */
	R3DI_LINE_OUT = 1
};
enum r3di_dsp_status {
	/* Set GPIO bit 3 to 1 until DSP is downloaded */
	R3DI_DSP_DOWNLOADING = 0,
	/* Set GPIO bit 4 to 1 once DSP is downloaded */
	R3DI_DSP_DOWNLOADED = 1
};


static void r3di_gpio_mic_set(struct hda_codec *codec,
		enum r3di_mic_select cur_mic)
{
	unsigned int cur_gpio;

	/* Get the current GPIO Data setup */
	cur_gpio = snd_hda_codec_read(codec, 0x01, 0, AC_VERB_GET_GPIO_DATA, 0);

	switch (cur_mic) {
	case R3DI_REAR_MIC:
		cur_gpio &= ~(1 << R3DI_MIC_SELECT_BIT);
		break;
	case R3DI_FRONT_MIC:
		cur_gpio |= (1 << R3DI_MIC_SELECT_BIT);
		break;
	}
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DATA, cur_gpio);
}

static void r3di_gpio_out_set(struct hda_codec *codec,
		enum r3di_out_select cur_out)
{
	unsigned int cur_gpio;

	/* Get the current GPIO Data setup */
	cur_gpio = snd_hda_codec_read(codec, 0x01, 0, AC_VERB_GET_GPIO_DATA, 0);

	switch (cur_out) {
	case R3DI_HEADPHONE_OUT:
		cur_gpio &= ~(1 << R3DI_OUT_SELECT_BIT);
		break;
	case R3DI_LINE_OUT:
		cur_gpio |= (1 << R3DI_OUT_SELECT_BIT);
		break;
	}
	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DATA, cur_gpio);
}

static void r3di_gpio_dsp_status_set(struct hda_codec *codec,
		enum r3di_dsp_status dsp_status)
{
	unsigned int cur_gpio;

	/* Get the current GPIO Data setup */
	cur_gpio = snd_hda_codec_read(codec, 0x01, 0, AC_VERB_GET_GPIO_DATA, 0);

	switch (dsp_status) {
	case R3DI_DSP_DOWNLOADING:
		cur_gpio |= (1 << R3DI_GPIO_DSP_DOWNLOADING);
		snd_hda_codec_write(codec, codec->core.afg, 0,
				AC_VERB_SET_GPIO_DATA, cur_gpio);
		break;
	case R3DI_DSP_DOWNLOADED:
		/* Set DOWNLOADING bit to 0. */
		cur_gpio &= ~(1 << R3DI_GPIO_DSP_DOWNLOADING);

		snd_hda_codec_write(codec, codec->core.afg, 0,
				AC_VERB_SET_GPIO_DATA, cur_gpio);

		cur_gpio |= (1 << R3DI_GPIO_DSP_DOWNLOADED);
		break;
	}

	snd_hda_codec_write(codec, codec->core.afg, 0,
			    AC_VERB_SET_GPIO_DATA, cur_gpio);
}

/*
 * PCM callbacks
 */
static int ca0132_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			unsigned int stream_tag,
			unsigned int format,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;

	snd_hda_codec_setup_stream(codec, spec->dacs[0], stream_tag, 0, format);

	return 0;
}

static int ca0132_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;

	if (spec->dsp_state == DSP_DOWNLOADING)
		return 0;

	/*If Playback effects are on, allow stream some time to flush
	 *effects tail*/
	if (spec->effects_switch[PLAY_ENHANCEMENT - EFFECT_START_NID])
		msleep(50);

	snd_hda_codec_cleanup_stream(codec, spec->dacs[0]);

	return 0;
}

static unsigned int ca0132_playback_pcm_delay(struct hda_pcm_stream *info,
			struct hda_codec *codec,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int latency = DSP_PLAYBACK_INIT_LATENCY;
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (spec->dsp_state != DSP_DOWNLOADED)
		return 0;

	/* Add latency if playback enhancement and either effect is enabled. */
	if (spec->effects_switch[PLAY_ENHANCEMENT - EFFECT_START_NID]) {
		if ((spec->effects_switch[SURROUND - EFFECT_START_NID]) ||
		    (spec->effects_switch[DIALOG_PLUS - EFFECT_START_NID]))
			latency += DSP_PLAY_ENHANCEMENT_LATENCY;
	}

	/* Applying Speaker EQ adds latency as well. */
	if (spec->cur_out_type == SPEAKER_OUT)
		latency += DSP_SPEAKER_OUT_LATENCY;

	return (latency * runtime->rate) / 1000;
}

/*
 * Digital out
 */
static int ca0132_dig_playback_pcm_open(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_open(codec, &spec->multiout);
}

static int ca0132_dig_playback_pcm_prepare(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			unsigned int stream_tag,
			unsigned int format,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_prepare(codec, &spec->multiout,
					     stream_tag, format, substream);
}

static int ca0132_dig_playback_pcm_cleanup(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_cleanup(codec, &spec->multiout);
}

static int ca0132_dig_playback_pcm_close(struct hda_pcm_stream *hinfo,
					 struct hda_codec *codec,
					 struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	return snd_hda_multi_out_dig_close(codec, &spec->multiout);
}

/*
 * Analog capture
 */
static int ca0132_capture_pcm_prepare(struct hda_pcm_stream *hinfo,
					struct hda_codec *codec,
					unsigned int stream_tag,
					unsigned int format,
					struct snd_pcm_substream *substream)
{
	snd_hda_codec_setup_stream(codec, hinfo->nid,
				   stream_tag, 0, format);

	return 0;
}

static int ca0132_capture_pcm_cleanup(struct hda_pcm_stream *hinfo,
			struct hda_codec *codec,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;

	if (spec->dsp_state == DSP_DOWNLOADING)
		return 0;

	snd_hda_codec_cleanup_stream(codec, hinfo->nid);
	return 0;
}

static unsigned int ca0132_capture_pcm_delay(struct hda_pcm_stream *info,
			struct hda_codec *codec,
			struct snd_pcm_substream *substream)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int latency = DSP_CAPTURE_INIT_LATENCY;
	struct snd_pcm_runtime *runtime = substream->runtime;

	if (spec->dsp_state != DSP_DOWNLOADED)
		return 0;

	if (spec->effects_switch[CRYSTAL_VOICE - EFFECT_START_NID])
		latency += DSP_CRYSTAL_VOICE_LATENCY;

	return (latency * runtime->rate) / 1000;
}

/*
 * Controls stuffs.
 */

/*
 * Mixer controls helpers.
 */
#define CA0132_CODEC_VOL_MONO(xname, nid, channel, dir) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	  .name = xname, \
	  .subdevice = HDA_SUBDEV_AMP_FLAG, \
	  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE | \
			SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
			SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK, \
	  .info = ca0132_volume_info, \
	  .get = ca0132_volume_get, \
	  .put = ca0132_volume_put, \
	  .tlv = { .c = ca0132_volume_tlv }, \
	  .private_value = HDA_COMPOSE_AMP_VAL(nid, channel, 0, dir) }

/*
 * Creates a mixer control that uses defaults of HDA_CODEC_VOL except for the
 * volume put, which is used for setting the DSP volume. This was done because
 * the ca0132 functions were taking too much time and causing lag.
 */
#define CA0132_ALT_CODEC_VOL_MONO(xname, nid, channel, dir) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	  .name = xname, \
	  .subdevice = HDA_SUBDEV_AMP_FLAG, \
	  .access = SNDRV_CTL_ELEM_ACCESS_READWRITE | \
			SNDRV_CTL_ELEM_ACCESS_TLV_READ | \
			SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK, \
	  .info = snd_hda_mixer_amp_volume_info, \
	  .get = snd_hda_mixer_amp_volume_get, \
	  .put = ca0132_alt_volume_put, \
	  .tlv = { .c = snd_hda_mixer_amp_tlv }, \
	  .private_value = HDA_COMPOSE_AMP_VAL(nid, channel, 0, dir) }

#define CA0132_CODEC_MUTE_MONO(xname, nid, channel, dir) \
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, \
	  .name = xname, \
	  .subdevice = HDA_SUBDEV_AMP_FLAG, \
	  .info = snd_hda_mixer_amp_switch_info, \
	  .get = ca0132_switch_get, \
	  .put = ca0132_switch_put, \
	  .private_value = HDA_COMPOSE_AMP_VAL(nid, channel, 0, dir) }

/* stereo */
#define CA0132_CODEC_VOL(xname, nid, dir) \
	CA0132_CODEC_VOL_MONO(xname, nid, 3, dir)
#define CA0132_ALT_CODEC_VOL(xname, nid, dir) \
	CA0132_ALT_CODEC_VOL_MONO(xname, nid, 3, dir)
#define CA0132_CODEC_MUTE(xname, nid, dir) \
	CA0132_CODEC_MUTE_MONO(xname, nid, 3, dir)

/* lookup tables */
/*
 * Lookup table with decibel values for the DSP. When volume is changed in
 * Windows, the DSP is also sent the dB value in floating point. In Windows,
 * these values have decimal points, probably because the Windows driver
 * actually uses floating point. We can't here, so I made a lookup table of
 * values -90 to 9. -90 is the lowest decibel value for both the ADC's and the
 * DAC's, and 9 is the maximum.
 */
static const unsigned int float_vol_db_lookup[] = {
0xC2B40000, 0xC2B20000, 0xC2B00000, 0xC2AE0000, 0xC2AC0000, 0xC2AA0000,
0xC2A80000, 0xC2A60000, 0xC2A40000, 0xC2A20000, 0xC2A00000, 0xC29E0000,
0xC29C0000, 0xC29A0000, 0xC2980000, 0xC2960000, 0xC2940000, 0xC2920000,
0xC2900000, 0xC28E0000, 0xC28C0000, 0xC28A0000, 0xC2880000, 0xC2860000,
0xC2840000, 0xC2820000, 0xC2800000, 0xC27C0000, 0xC2780000, 0xC2740000,
0xC2700000, 0xC26C0000, 0xC2680000, 0xC2640000, 0xC2600000, 0xC25C0000,
0xC2580000, 0xC2540000, 0xC2500000, 0xC24C0000, 0xC2480000, 0xC2440000,
0xC2400000, 0xC23C0000, 0xC2380000, 0xC2340000, 0xC2300000, 0xC22C0000,
0xC2280000, 0xC2240000, 0xC2200000, 0xC21C0000, 0xC2180000, 0xC2140000,
0xC2100000, 0xC20C0000, 0xC2080000, 0xC2040000, 0xC2000000, 0xC1F80000,
0xC1F00000, 0xC1E80000, 0xC1E00000, 0xC1D80000, 0xC1D00000, 0xC1C80000,
0xC1C00000, 0xC1B80000, 0xC1B00000, 0xC1A80000, 0xC1A00000, 0xC1980000,
0xC1900000, 0xC1880000, 0xC1800000, 0xC1700000, 0xC1600000, 0xC1500000,
0xC1400000, 0xC1300000, 0xC1200000, 0xC1100000, 0xC1000000, 0xC0E00000,
0xC0C00000, 0xC0A00000, 0xC0800000, 0xC0400000, 0xC0000000, 0xBF800000,
0x00000000, 0x3F800000, 0x40000000, 0x40400000, 0x40800000, 0x40A00000,
0x40C00000, 0x40E00000, 0x41000000, 0x41100000
};

/*
 * This table counts from float 0 to 1 in increments of .01, which is
 * useful for a few different sliders.
 */
static const unsigned int float_zero_to_one_lookup[] = {
0x00000000, 0x3C23D70A, 0x3CA3D70A, 0x3CF5C28F, 0x3D23D70A, 0x3D4CCCCD,
0x3D75C28F, 0x3D8F5C29, 0x3DA3D70A, 0x3DB851EC, 0x3DCCCCCD, 0x3DE147AE,
0x3DF5C28F, 0x3E051EB8, 0x3E0F5C29, 0x3E19999A, 0x3E23D70A, 0x3E2E147B,
0x3E3851EC, 0x3E428F5C, 0x3E4CCCCD, 0x3E570A3D, 0x3E6147AE, 0x3E6B851F,
0x3E75C28F, 0x3E800000, 0x3E851EB8, 0x3E8A3D71, 0x3E8F5C29, 0x3E947AE1,
0x3E99999A, 0x3E9EB852, 0x3EA3D70A, 0x3EA8F5C3, 0x3EAE147B, 0x3EB33333,
0x3EB851EC, 0x3EBD70A4, 0x3EC28F5C, 0x3EC7AE14, 0x3ECCCCCD, 0x3ED1EB85,
0x3ED70A3D, 0x3EDC28F6, 0x3EE147AE, 0x3EE66666, 0x3EEB851F, 0x3EF0A3D7,
0x3EF5C28F, 0x3EFAE148, 0x3F000000, 0x3F028F5C, 0x3F051EB8, 0x3F07AE14,
0x3F0A3D71, 0x3F0CCCCD, 0x3F0F5C29, 0x3F11EB85, 0x3F147AE1, 0x3F170A3D,
0x3F19999A, 0x3F1C28F6, 0x3F1EB852, 0x3F2147AE, 0x3F23D70A, 0x3F266666,
0x3F28F5C3, 0x3F2B851F, 0x3F2E147B, 0x3F30A3D7, 0x3F333333, 0x3F35C28F,
0x3F3851EC, 0x3F3AE148, 0x3F3D70A4, 0x3F400000, 0x3F428F5C, 0x3F451EB8,
0x3F47AE14, 0x3F4A3D71, 0x3F4CCCCD, 0x3F4F5C29, 0x3F51EB85, 0x3F547AE1,
0x3F570A3D, 0x3F59999A, 0x3F5C28F6, 0x3F5EB852, 0x3F6147AE, 0x3F63D70A,
0x3F666666, 0x3F68F5C3, 0x3F6B851F, 0x3F6E147B, 0x3F70A3D7, 0x3F733333,
0x3F75C28F, 0x3F7851EC, 0x3F7AE148, 0x3F7D70A4, 0x3F800000
};

/*
 * This table counts from float 10 to 1000, which is the range of the x-bass
 * crossover slider in Windows.
 */
static const unsigned int float_xbass_xover_lookup[] = {
0x41200000, 0x41A00000, 0x41F00000, 0x42200000, 0x42480000, 0x42700000,
0x428C0000, 0x42A00000, 0x42B40000, 0x42C80000, 0x42DC0000, 0x42F00000,
0x43020000, 0x430C0000, 0x43160000, 0x43200000, 0x432A0000, 0x43340000,
0x433E0000, 0x43480000, 0x43520000, 0x435C0000, 0x43660000, 0x43700000,
0x437A0000, 0x43820000, 0x43870000, 0x438C0000, 0x43910000, 0x43960000,
0x439B0000, 0x43A00000, 0x43A50000, 0x43AA0000, 0x43AF0000, 0x43B40000,
0x43B90000, 0x43BE0000, 0x43C30000, 0x43C80000, 0x43CD0000, 0x43D20000,
0x43D70000, 0x43DC0000, 0x43E10000, 0x43E60000, 0x43EB0000, 0x43F00000,
0x43F50000, 0x43FA0000, 0x43FF0000, 0x44020000, 0x44048000, 0x44070000,
0x44098000, 0x440C0000, 0x440E8000, 0x44110000, 0x44138000, 0x44160000,
0x44188000, 0x441B0000, 0x441D8000, 0x44200000, 0x44228000, 0x44250000,
0x44278000, 0x442A0000, 0x442C8000, 0x442F0000, 0x44318000, 0x44340000,
0x44368000, 0x44390000, 0x443B8000, 0x443E0000, 0x44408000, 0x44430000,
0x44458000, 0x44480000, 0x444A8000, 0x444D0000, 0x444F8000, 0x44520000,
0x44548000, 0x44570000, 0x44598000, 0x445C0000, 0x445E8000, 0x44610000,
0x44638000, 0x44660000, 0x44688000, 0x446B0000, 0x446D8000, 0x44700000,
0x44728000, 0x44750000, 0x44778000, 0x447A0000
};

/* The following are for tuning of products */
#ifdef ENABLE_TUNING_CONTROLS

static unsigned int voice_focus_vals_lookup[] = {
0x41A00000, 0x41A80000, 0x41B00000, 0x41B80000, 0x41C00000, 0x41C80000,
0x41D00000, 0x41D80000, 0x41E00000, 0x41E80000, 0x41F00000, 0x41F80000,
0x42000000, 0x42040000, 0x42080000, 0x420C0000, 0x42100000, 0x42140000,
0x42180000, 0x421C0000, 0x42200000, 0x42240000, 0x42280000, 0x422C0000,
0x42300000, 0x42340000, 0x42380000, 0x423C0000, 0x42400000, 0x42440000,
0x42480000, 0x424C0000, 0x42500000, 0x42540000, 0x42580000, 0x425C0000,
0x42600000, 0x42640000, 0x42680000, 0x426C0000, 0x42700000, 0x42740000,
0x42780000, 0x427C0000, 0x42800000, 0x42820000, 0x42840000, 0x42860000,
0x42880000, 0x428A0000, 0x428C0000, 0x428E0000, 0x42900000, 0x42920000,
0x42940000, 0x42960000, 0x42980000, 0x429A0000, 0x429C0000, 0x429E0000,
0x42A00000, 0x42A20000, 0x42A40000, 0x42A60000, 0x42A80000, 0x42AA0000,
0x42AC0000, 0x42AE0000, 0x42B00000, 0x42B20000, 0x42B40000, 0x42B60000,
0x42B80000, 0x42BA0000, 0x42BC0000, 0x42BE0000, 0x42C00000, 0x42C20000,
0x42C40000, 0x42C60000, 0x42C80000, 0x42CA0000, 0x42CC0000, 0x42CE0000,
0x42D00000, 0x42D20000, 0x42D40000, 0x42D60000, 0x42D80000, 0x42DA0000,
0x42DC0000, 0x42DE0000, 0x42E00000, 0x42E20000, 0x42E40000, 0x42E60000,
0x42E80000, 0x42EA0000, 0x42EC0000, 0x42EE0000, 0x42F00000, 0x42F20000,
0x42F40000, 0x42F60000, 0x42F80000, 0x42FA0000, 0x42FC0000, 0x42FE0000,
0x43000000, 0x43010000, 0x43020000, 0x43030000, 0x43040000, 0x43050000,
0x43060000, 0x43070000, 0x43080000, 0x43090000, 0x430A0000, 0x430B0000,
0x430C0000, 0x430D0000, 0x430E0000, 0x430F0000, 0x43100000, 0x43110000,
0x43120000, 0x43130000, 0x43140000, 0x43150000, 0x43160000, 0x43170000,
0x43180000, 0x43190000, 0x431A0000, 0x431B0000, 0x431C0000, 0x431D0000,
0x431E0000, 0x431F0000, 0x43200000, 0x43210000, 0x43220000, 0x43230000,
0x43240000, 0x43250000, 0x43260000, 0x43270000, 0x43280000, 0x43290000,
0x432A0000, 0x432B0000, 0x432C0000, 0x432D0000, 0x432E0000, 0x432F0000,
0x43300000, 0x43310000, 0x43320000, 0x43330000, 0x43340000
};

static unsigned int mic_svm_vals_lookup[] = {
0x00000000, 0x3C23D70A, 0x3CA3D70A, 0x3CF5C28F, 0x3D23D70A, 0x3D4CCCCD,
0x3D75C28F, 0x3D8F5C29, 0x3DA3D70A, 0x3DB851EC, 0x3DCCCCCD, 0x3DE147AE,
0x3DF5C28F, 0x3E051EB8, 0x3E0F5C29, 0x3E19999A, 0x3E23D70A, 0x3E2E147B,
0x3E3851EC, 0x3E428F5C, 0x3E4CCCCD, 0x3E570A3D, 0x3E6147AE, 0x3E6B851F,
0x3E75C28F, 0x3E800000, 0x3E851EB8, 0x3E8A3D71, 0x3E8F5C29, 0x3E947AE1,
0x3E99999A, 0x3E9EB852, 0x3EA3D70A, 0x3EA8F5C3, 0x3EAE147B, 0x3EB33333,
0x3EB851EC, 0x3EBD70A4, 0x3EC28F5C, 0x3EC7AE14, 0x3ECCCCCD, 0x3ED1EB85,
0x3ED70A3D, 0x3EDC28F6, 0x3EE147AE, 0x3EE66666, 0x3EEB851F, 0x3EF0A3D7,
0x3EF5C28F, 0x3EFAE148, 0x3F000000, 0x3F028F5C, 0x3F051EB8, 0x3F07AE14,
0x3F0A3D71, 0x3F0CCCCD, 0x3F0F5C29, 0x3F11EB85, 0x3F147AE1, 0x3F170A3D,
0x3F19999A, 0x3F1C28F6, 0x3F1EB852, 0x3F2147AE, 0x3F23D70A, 0x3F266666,
0x3F28F5C3, 0x3F2B851F, 0x3F2E147B, 0x3F30A3D7, 0x3F333333, 0x3F35C28F,
0x3F3851EC, 0x3F3AE148, 0x3F3D70A4, 0x3F400000, 0x3F428F5C, 0x3F451EB8,
0x3F47AE14, 0x3F4A3D71, 0x3F4CCCCD, 0x3F4F5C29, 0x3F51EB85, 0x3F547AE1,
0x3F570A3D, 0x3F59999A, 0x3F5C28F6, 0x3F5EB852, 0x3F6147AE, 0x3F63D70A,
0x3F666666, 0x3F68F5C3, 0x3F6B851F, 0x3F6E147B, 0x3F70A3D7, 0x3F733333,
0x3F75C28F, 0x3F7851EC, 0x3F7AE148, 0x3F7D70A4, 0x3F800000
};

static unsigned int equalizer_vals_lookup[] = {
0xC1C00000, 0xC1B80000, 0xC1B00000, 0xC1A80000, 0xC1A00000, 0xC1980000,
0xC1900000, 0xC1880000, 0xC1800000, 0xC1700000, 0xC1600000, 0xC1500000,
0xC1400000, 0xC1300000, 0xC1200000, 0xC1100000, 0xC1000000, 0xC0E00000,
0xC0C00000, 0xC0A00000, 0xC0800000, 0xC0400000, 0xC0000000, 0xBF800000,
0x00000000, 0x3F800000, 0x40000000, 0x40400000, 0x40800000, 0x40A00000,
0x40C00000, 0x40E00000, 0x41000000, 0x41100000, 0x41200000, 0x41300000,
0x41400000, 0x41500000, 0x41600000, 0x41700000, 0x41800000, 0x41880000,
0x41900000, 0x41980000, 0x41A00000, 0x41A80000, 0x41B00000, 0x41B80000,
0x41C00000
};

static int tuning_ctl_set(struct hda_codec *codec, hda_nid_t nid,
			  unsigned int *lookup, int idx)
{
	int i = 0;

	for (i = 0; i < TUNING_CTLS_COUNT; i++)
		if (nid == ca0132_tuning_ctls[i].nid)
			break;

	snd_hda_power_up(codec);
	dspio_set_param(codec, ca0132_tuning_ctls[i].mid, 0x20,
			ca0132_tuning_ctls[i].req,
			&(lookup[idx]), sizeof(unsigned int));
	snd_hda_power_down(codec);

	return 1;
}

static int tuning_ctl_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int idx = nid - TUNING_CTL_START_NID;

	*valp = spec->cur_ctl_vals[idx];
	return 0;
}

static int voice_focus_ctl_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	int chs = get_amp_channels(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = chs == 3 ? 2 : 1;
	uinfo->value.integer.min = 20;
	uinfo->value.integer.max = 180;
	uinfo->value.integer.step = 1;

	return 0;
}

static int voice_focus_ctl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int idx;

	idx = nid - TUNING_CTL_START_NID;
	/* any change? */
	if (spec->cur_ctl_vals[idx] == *valp)
		return 0;

	spec->cur_ctl_vals[idx] = *valp;

	idx = *valp - 20;
	tuning_ctl_set(codec, nid, voice_focus_vals_lookup, idx);

	return 1;
}

static int mic_svm_ctl_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	int chs = get_amp_channels(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = chs == 3 ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 100;
	uinfo->value.integer.step = 1;

	return 0;
}

static int mic_svm_ctl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int idx;

	idx = nid - TUNING_CTL_START_NID;
	/* any change? */
	if (spec->cur_ctl_vals[idx] == *valp)
		return 0;

	spec->cur_ctl_vals[idx] = *valp;

	idx = *valp;
	tuning_ctl_set(codec, nid, mic_svm_vals_lookup, idx);

	return 0;
}

static int equalizer_ctl_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	int chs = get_amp_channels(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = chs == 3 ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 48;
	uinfo->value.integer.step = 1;

	return 0;
}

static int equalizer_ctl_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int idx;

	idx = nid - TUNING_CTL_START_NID;
	/* any change? */
	if (spec->cur_ctl_vals[idx] == *valp)
		return 0;

	spec->cur_ctl_vals[idx] = *valp;

	idx = *valp;
	tuning_ctl_set(codec, nid, equalizer_vals_lookup, idx);

	return 1;
}

static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(voice_focus_db_scale, 2000, 100, 0);
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(eq_db_scale, -2400, 100, 0);

static int add_tuning_control(struct hda_codec *codec,
				hda_nid_t pnid, hda_nid_t nid,
				const char *name, int dir)
{
	char namestr[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	int type = dir ? HDA_INPUT : HDA_OUTPUT;
	struct snd_kcontrol_new knew =
		HDA_CODEC_VOLUME_MONO(namestr, nid, 1, 0, type);

	knew.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ;
	knew.tlv.c = 0;
	knew.tlv.p = 0;
	switch (pnid) {
	case VOICE_FOCUS:
		knew.info = voice_focus_ctl_info;
		knew.get = tuning_ctl_get;
		knew.put = voice_focus_ctl_put;
		knew.tlv.p = voice_focus_db_scale;
		break;
	case MIC_SVM:
		knew.info = mic_svm_ctl_info;
		knew.get = tuning_ctl_get;
		knew.put = mic_svm_ctl_put;
		break;
	case EQUALIZER:
		knew.info = equalizer_ctl_info;
		knew.get = tuning_ctl_get;
		knew.put = equalizer_ctl_put;
		knew.tlv.p = eq_db_scale;
		break;
	default:
		return 0;
	}
	knew.private_value =
		HDA_COMPOSE_AMP_VAL(nid, 1, 0, type);
	sprintf(namestr, "%s %s Volume", name, dirstr[dir]);
	return snd_hda_ctl_add(codec, nid, snd_ctl_new1(&knew, codec));
}

static int add_tuning_ctls(struct hda_codec *codec)
{
	int i;
	int err;

	for (i = 0; i < TUNING_CTLS_COUNT; i++) {
		err = add_tuning_control(codec,
					ca0132_tuning_ctls[i].parent_nid,
					ca0132_tuning_ctls[i].nid,
					ca0132_tuning_ctls[i].name,
					ca0132_tuning_ctls[i].direct);
		if (err < 0)
			return err;
	}

	return 0;
}

static void ca0132_init_tuning_defaults(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	int i;

	/* Wedge Angle defaults to 30.  10 below is 30 - 20.  20 is min. */
	spec->cur_ctl_vals[WEDGE_ANGLE - TUNING_CTL_START_NID] = 10;
	/* SVM level defaults to 0.74. */
	spec->cur_ctl_vals[SVM_LEVEL - TUNING_CTL_START_NID] = 74;

	/* EQ defaults to 0dB. */
	for (i = 2; i < TUNING_CTLS_COUNT; i++)
		spec->cur_ctl_vals[i] = 24;
}
#endif /*ENABLE_TUNING_CONTROLS*/

/*
 * Select the active output.
 * If autodetect is enabled, output will be selected based on jack detection.
 * If jack inserted, headphone will be selected, else built-in speakers
 * If autodetect is disabled, output will be selected based on selection.
 */
static int ca0132_select_out(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int pin_ctl;
	int jack_present;
	int auto_jack;
	unsigned int tmp;
	int err;

	codec_dbg(codec, "ca0132_select_out\n");

	snd_hda_power_up_pm(codec);

	auto_jack = spec->vnode_lswitch[VNID_HP_ASEL - VNODE_START_NID];

	if (auto_jack)
		jack_present = snd_hda_jack_detect(codec, spec->unsol_tag_hp);
	else
		jack_present =
			spec->vnode_lswitch[VNID_HP_SEL - VNODE_START_NID];

	if (jack_present)
		spec->cur_out_type = HEADPHONE_OUT;
	else
		spec->cur_out_type = SPEAKER_OUT;

	if (spec->cur_out_type == SPEAKER_OUT) {
		codec_dbg(codec, "ca0132_select_out speaker\n");
		/*speaker out config*/
		tmp = FLOAT_ONE;
		err = dspio_set_uint_param(codec, 0x80, 0x04, tmp);
		if (err < 0)
			goto exit;
		/*enable speaker EQ*/
		tmp = FLOAT_ONE;
		err = dspio_set_uint_param(codec, 0x8f, 0x00, tmp);
		if (err < 0)
			goto exit;

		/* Setup EAPD */
		snd_hda_codec_write(codec, spec->out_pins[1], 0,
				    VENDOR_CHIPIO_EAPD_SEL_SET, 0x02);
		snd_hda_codec_write(codec, spec->out_pins[0], 0,
				    AC_VERB_SET_EAPD_BTLENABLE, 0x00);
		snd_hda_codec_write(codec, spec->out_pins[0], 0,
				    VENDOR_CHIPIO_EAPD_SEL_SET, 0x00);
		snd_hda_codec_write(codec, spec->out_pins[0], 0,
				    AC_VERB_SET_EAPD_BTLENABLE, 0x02);

		/* disable headphone node */
		pin_ctl = snd_hda_codec_read(codec, spec->out_pins[1], 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, spec->out_pins[1],
				    pin_ctl & ~PIN_HP);
		/* enable speaker node */
		pin_ctl = snd_hda_codec_read(codec, spec->out_pins[0], 0,
				AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, spec->out_pins[0],
				    pin_ctl | PIN_OUT);
	} else {
		codec_dbg(codec, "ca0132_select_out hp\n");
		/*headphone out config*/
		tmp = FLOAT_ZERO;
		err = dspio_set_uint_param(codec, 0x80, 0x04, tmp);
		if (err < 0)
			goto exit;
		/*disable speaker EQ*/
		tmp = FLOAT_ZERO;
		err = dspio_set_uint_param(codec, 0x8f, 0x00, tmp);
		if (err < 0)
			goto exit;

		/* Setup EAPD */
		snd_hda_codec_write(codec, spec->out_pins[0], 0,
				    VENDOR_CHIPIO_EAPD_SEL_SET, 0x00);
		snd_hda_codec_write(codec, spec->out_pins[0], 0,
				    AC_VERB_SET_EAPD_BTLENABLE, 0x00);
		snd_hda_codec_write(codec, spec->out_pins[1], 0,
				    VENDOR_CHIPIO_EAPD_SEL_SET, 0x02);
		snd_hda_codec_write(codec, spec->out_pins[0], 0,
				    AC_VERB_SET_EAPD_BTLENABLE, 0x02);

		/* disable speaker*/
		pin_ctl = snd_hda_codec_read(codec, spec->out_pins[0], 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, spec->out_pins[0],
				    pin_ctl & ~PIN_HP);
		/* enable headphone*/
		pin_ctl = snd_hda_codec_read(codec, spec->out_pins[1], 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, spec->out_pins[1],
				    pin_ctl | PIN_HP);
	}

exit:
	snd_hda_power_down_pm(codec);

	return err < 0 ? err : 0;
}

static int ae5_headphone_gain_set(struct hda_codec *codec, long val);
static int zxr_headphone_gain_set(struct hda_codec *codec, long val);
static int ca0132_effects_set(struct hda_codec *codec, hda_nid_t nid, long val);

static void ae5_mmio_select_out(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int i;

	for (i = 0; i < AE5_CA0113_OUT_SET_COMMANDS; i++)
		ca0113_mmio_command_set(codec,
			ae5_ca0113_output_presets[spec->cur_out_type].group[i],
			ae5_ca0113_output_presets[spec->cur_out_type].target[i],
			ae5_ca0113_output_presets[spec->cur_out_type].vals[i]);
}

/*
 * These are the commands needed to setup output on each of the different card
 * types.
 */
static void ca0132_alt_select_out_quirk_handler(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;

	switch (spec->cur_out_type) {
	case SPEAKER_OUT:
		switch (spec->quirk) {
		case QUIRK_SBZ:
			ca0113_mmio_gpio_set(codec, 7, false);
			ca0113_mmio_gpio_set(codec, 4, true);
			ca0113_mmio_gpio_set(codec, 1, true);
			chipio_set_control_param(codec, 0x0d, 0x18);
			break;
		case QUIRK_ZXR:
			ca0113_mmio_gpio_set(codec, 2, true);
			ca0113_mmio_gpio_set(codec, 3, true);
			ca0113_mmio_gpio_set(codec, 5, false);
			zxr_headphone_gain_set(codec, 0);
			chipio_set_control_param(codec, 0x0d, 0x24);
			break;
		case QUIRK_R3DI:
			chipio_set_control_param(codec, 0x0d, 0x24);
			r3di_gpio_out_set(codec, R3DI_LINE_OUT);
			break;
		case QUIRK_R3D:
			chipio_set_control_param(codec, 0x0d, 0x24);
			ca0113_mmio_gpio_set(codec, 1, true);
			break;
		case QUIRK_AE5:
			ae5_mmio_select_out(codec);
			ae5_headphone_gain_set(codec, 2);
			tmp = FLOAT_ZERO;
			dspio_set_uint_param(codec, 0x96, 0x29, tmp);
			dspio_set_uint_param(codec, 0x96, 0x2a, tmp);
			chipio_set_control_param(codec, 0x0d, 0xa4);
			chipio_write(codec, 0x18b03c, 0x00000012);
			break;
		}
		break;
	case HEADPHONE_OUT:
		switch (spec->quirk) {
		case QUIRK_SBZ:
			ca0113_mmio_gpio_set(codec, 7, true);
			ca0113_mmio_gpio_set(codec, 4, true);
			ca0113_mmio_gpio_set(codec, 1, false);
			chipio_set_control_param(codec, 0x0d, 0x12);
			break;
		case QUIRK_ZXR:
			ca0113_mmio_gpio_set(codec, 2, false);
			ca0113_mmio_gpio_set(codec, 3, false);
			ca0113_mmio_gpio_set(codec, 5, true);
			zxr_headphone_gain_set(codec, spec->zxr_gain_set);
			chipio_set_control_param(codec, 0x0d, 0x21);
			break;
		case QUIRK_R3DI:
			chipio_set_control_param(codec, 0x0d, 0x21);
			r3di_gpio_out_set(codec, R3DI_HEADPHONE_OUT);
			break;
		case QUIRK_R3D:
			chipio_set_control_param(codec, 0x0d, 0x21);
			ca0113_mmio_gpio_set(codec, 0x1, false);
			break;
		case QUIRK_AE5:
			ae5_mmio_select_out(codec);
			ae5_headphone_gain_set(codec,
					spec->ae5_headphone_gain_val);
			tmp = FLOAT_ONE;
			dspio_set_uint_param(codec, 0x96, 0x29, tmp);
			dspio_set_uint_param(codec, 0x96, 0x2a, tmp);
			chipio_set_control_param(codec, 0x0d, 0xa1);
			chipio_write(codec, 0x18b03c, 0x00000012);
			break;
		}
		break;
	case SURROUND_OUT:
		switch (spec->quirk) {
		case QUIRK_SBZ:
			ca0113_mmio_gpio_set(codec, 7, false);
			ca0113_mmio_gpio_set(codec, 4, true);
			ca0113_mmio_gpio_set(codec, 1, true);
			chipio_set_control_param(codec, 0x0d, 0x18);
			break;
		case QUIRK_ZXR:
			ca0113_mmio_gpio_set(codec, 2, true);
			ca0113_mmio_gpio_set(codec, 3, true);
			ca0113_mmio_gpio_set(codec, 5, false);
			zxr_headphone_gain_set(codec, 0);
			chipio_set_control_param(codec, 0x0d, 0x24);
			break;
		case QUIRK_R3DI:
			chipio_set_control_param(codec, 0x0d, 0x24);
			r3di_gpio_out_set(codec, R3DI_LINE_OUT);
			break;
		case QUIRK_R3D:
			ca0113_mmio_gpio_set(codec, 1, true);
			chipio_set_control_param(codec, 0x0d, 0x24);
			break;
		case QUIRK_AE5:
			ae5_mmio_select_out(codec);
			ae5_headphone_gain_set(codec, 2);
			tmp = FLOAT_ZERO;
			dspio_set_uint_param(codec, 0x96, 0x29, tmp);
			dspio_set_uint_param(codec, 0x96, 0x2a, tmp);
			chipio_set_control_param(codec, 0x0d, 0xa4);
			chipio_write(codec, 0x18b03c, 0x00000012);
			break;
		}
		break;
	}
}

/*
 * This function behaves similarly to the ca0132_select_out funciton above,
 * except with a few differences. It adds the ability to select the current
 * output with an enumerated control "output source" if the auto detect
 * mute switch is set to off. If the auto detect mute switch is enabled, it
 * will detect either headphone or lineout(SPEAKER_OUT) from jack detection.
 * It also adds the ability to auto-detect the front headphone port. The only
 * way to select surround is to disable auto detect, and set Surround with the
 * enumerated control.
 */
static int ca0132_alt_select_out(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int pin_ctl;
	int jack_present;
	int auto_jack;
	unsigned int i;
	unsigned int tmp;
	int err;
	/* Default Headphone is rear headphone */
	hda_nid_t headphone_nid = spec->out_pins[1];

	codec_dbg(codec, "%s\n", __func__);

	snd_hda_power_up_pm(codec);

	auto_jack = spec->vnode_lswitch[VNID_HP_ASEL - VNODE_START_NID];

	/*
	 * If headphone rear or front is plugged in, set to headphone.
	 * If neither is plugged in, set to rear line out. Only if
	 * hp/speaker auto detect is enabled.
	 */
	if (auto_jack) {
		jack_present = snd_hda_jack_detect(codec, spec->unsol_tag_hp) ||
			   snd_hda_jack_detect(codec, spec->unsol_tag_front_hp);

		if (jack_present)
			spec->cur_out_type = HEADPHONE_OUT;
		else
			spec->cur_out_type = SPEAKER_OUT;
	} else
		spec->cur_out_type = spec->out_enum_val;

	/* Begin DSP output switch */
	tmp = FLOAT_ONE;
	err = dspio_set_uint_param(codec, 0x96, 0x3A, tmp);
	if (err < 0)
		goto exit;

	ca0132_alt_select_out_quirk_handler(codec);

	switch (spec->cur_out_type) {
	case SPEAKER_OUT:
		codec_dbg(codec, "%s speaker\n", __func__);

		/* disable headphone node */
		pin_ctl = snd_hda_codec_read(codec, spec->out_pins[1], 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, spec->out_pins[1],
				    pin_ctl & ~PIN_HP);
		/* enable line-out node */
		pin_ctl = snd_hda_codec_read(codec, spec->out_pins[0], 0,
				AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, spec->out_pins[0],
				    pin_ctl | PIN_OUT);
		/* Enable EAPD */
		snd_hda_codec_write(codec, spec->out_pins[0], 0,
			AC_VERB_SET_EAPD_BTLENABLE, 0x01);

		/* If PlayEnhancement is enabled, set different source */
		if (spec->effects_switch[PLAY_ENHANCEMENT - EFFECT_START_NID])
			dspio_set_uint_param(codec, 0x80, 0x04, FLOAT_ONE);
		else
			dspio_set_uint_param(codec, 0x80, 0x04, FLOAT_EIGHT);
		break;
	case HEADPHONE_OUT:
		codec_dbg(codec, "%s hp\n", __func__);

		snd_hda_codec_write(codec, spec->out_pins[0], 0,
			AC_VERB_SET_EAPD_BTLENABLE, 0x00);

		/* disable speaker*/
		pin_ctl = snd_hda_codec_read(codec, spec->out_pins[0], 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, spec->out_pins[0],
				pin_ctl & ~PIN_HP);

		/* enable headphone, either front or rear */

		if (snd_hda_jack_detect(codec, spec->unsol_tag_front_hp))
			headphone_nid = spec->out_pins[2];
		else if (snd_hda_jack_detect(codec, spec->unsol_tag_hp))
			headphone_nid = spec->out_pins[1];

		pin_ctl = snd_hda_codec_read(codec, headphone_nid, 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, headphone_nid,
				    pin_ctl | PIN_HP);

		if (spec->effects_switch[PLAY_ENHANCEMENT - EFFECT_START_NID])
			dspio_set_uint_param(codec, 0x80, 0x04, FLOAT_ONE);
		else
			dspio_set_uint_param(codec, 0x80, 0x04, FLOAT_ZERO);
		break;
	case SURROUND_OUT:
		codec_dbg(codec, "%s surround\n", __func__);

		/* enable line out node */
		pin_ctl = snd_hda_codec_read(codec, spec->out_pins[0], 0,
				AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, spec->out_pins[0],
						pin_ctl | PIN_OUT);
		/* Disable headphone out */
		pin_ctl = snd_hda_codec_read(codec, spec->out_pins[1], 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, spec->out_pins[1],
				    pin_ctl & ~PIN_HP);
		/* Enable EAPD on line out */
		snd_hda_codec_write(codec, spec->out_pins[0], 0,
			AC_VERB_SET_EAPD_BTLENABLE, 0x01);
		/* enable center/lfe out node */
		pin_ctl = snd_hda_codec_read(codec, spec->out_pins[2], 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, spec->out_pins[2],
				    pin_ctl | PIN_OUT);
		/* Now set rear surround node as out. */
		pin_ctl = snd_hda_codec_read(codec, spec->out_pins[3], 0,
					AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		snd_hda_set_pin_ctl(codec, spec->out_pins[3],
				    pin_ctl | PIN_OUT);

		dspio_set_uint_param(codec, 0x80, 0x04, FLOAT_EIGHT);
		break;
	}
	/*
	 * Surround always sets it's scp command to req 0x04 to FLOAT_EIGHT.
	 * With this set though, X_BASS cannot be enabled. So, if we have OutFX
	 * enabled, we need to make sure X_BASS is off, otherwise everything
	 * sounds all muffled. Running ca0132_effects_set with X_BASS as the
	 * effect should sort this out.
	 */
	if (spec->effects_switch[PLAY_ENHANCEMENT - EFFECT_START_NID])
		ca0132_effects_set(codec, X_BASS,
			spec->effects_switch[X_BASS - EFFECT_START_NID]);

	/* run through the output dsp commands for the selected output. */
	for (i = 0; i < alt_out_presets[spec->cur_out_type].commands; i++) {
		err = dspio_set_uint_param(codec,
		alt_out_presets[spec->cur_out_type].mids[i],
		alt_out_presets[spec->cur_out_type].reqs[i],
		alt_out_presets[spec->cur_out_type].vals[i]);

		if (err < 0)
			goto exit;
	}

exit:
	snd_hda_power_down_pm(codec);

	return err < 0 ? err : 0;
}

static void ca0132_unsol_hp_delayed(struct work_struct *work)
{
	struct ca0132_spec *spec = container_of(
		to_delayed_work(work), struct ca0132_spec, unsol_hp_work);
	struct hda_jack_tbl *jack;

	if (spec->use_alt_functions)
		ca0132_alt_select_out(spec->codec);
	else
		ca0132_select_out(spec->codec);

	jack = snd_hda_jack_tbl_get(spec->codec, spec->unsol_tag_hp);
	if (jack) {
		jack->block_report = 0;
		snd_hda_jack_report_sync(spec->codec);
	}
}

static void ca0132_set_dmic(struct hda_codec *codec, int enable);
static int ca0132_mic_boost_set(struct hda_codec *codec, long val);
static void resume_mic1(struct hda_codec *codec, unsigned int oldval);
static int stop_mic1(struct hda_codec *codec);
static int ca0132_cvoice_switch_set(struct hda_codec *codec);
static int ca0132_alt_mic_boost_set(struct hda_codec *codec, long val);

/*
 * Select the active VIP source
 */
static int ca0132_set_vipsource(struct hda_codec *codec, int val)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;

	if (spec->dsp_state != DSP_DOWNLOADED)
		return 0;

	/* if CrystalVoice if off, vipsource should be 0 */
	if (!spec->effects_switch[CRYSTAL_VOICE - EFFECT_START_NID] ||
	    (val == 0)) {
		chipio_set_control_param(codec, CONTROL_PARAM_VIP_SOURCE, 0);
		chipio_set_conn_rate(codec, MEM_CONNID_MICIN1, SR_96_000);
		chipio_set_conn_rate(codec, MEM_CONNID_MICOUT1, SR_96_000);
		if (spec->cur_mic_type == DIGITAL_MIC)
			tmp = FLOAT_TWO;
		else
			tmp = FLOAT_ONE;
		dspio_set_uint_param(codec, 0x80, 0x00, tmp);
		tmp = FLOAT_ZERO;
		dspio_set_uint_param(codec, 0x80, 0x05, tmp);
	} else {
		chipio_set_conn_rate(codec, MEM_CONNID_MICIN1, SR_16_000);
		chipio_set_conn_rate(codec, MEM_CONNID_MICOUT1, SR_16_000);
		if (spec->cur_mic_type == DIGITAL_MIC)
			tmp = FLOAT_TWO;
		else
			tmp = FLOAT_ONE;
		dspio_set_uint_param(codec, 0x80, 0x00, tmp);
		tmp = FLOAT_ONE;
		dspio_set_uint_param(codec, 0x80, 0x05, tmp);
		msleep(20);
		chipio_set_control_param(codec, CONTROL_PARAM_VIP_SOURCE, val);
	}

	return 1;
}

static int ca0132_alt_set_vipsource(struct hda_codec *codec, int val)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;

	if (spec->dsp_state != DSP_DOWNLOADED)
		return 0;

	codec_dbg(codec, "%s\n", __func__);

	chipio_set_stream_control(codec, 0x03, 0);
	chipio_set_stream_control(codec, 0x04, 0);

	/* if CrystalVoice is off, vipsource should be 0 */
	if (!spec->effects_switch[CRYSTAL_VOICE - EFFECT_START_NID] ||
	    (val == 0) || spec->in_enum_val == REAR_LINE_IN) {
		codec_dbg(codec, "%s: off.", __func__);
		chipio_set_control_param(codec, CONTROL_PARAM_VIP_SOURCE, 0);

		tmp = FLOAT_ZERO;
		dspio_set_uint_param(codec, 0x80, 0x05, tmp);

		chipio_set_conn_rate(codec, MEM_CONNID_MICIN1, SR_96_000);
		chipio_set_conn_rate(codec, MEM_CONNID_MICOUT1, SR_96_000);
		if (spec->quirk == QUIRK_R3DI)
			chipio_set_conn_rate(codec, 0x0F, SR_96_000);


		if (spec->in_enum_val == REAR_LINE_IN)
			tmp = FLOAT_ZERO;
		else {
			if (spec->quirk == QUIRK_SBZ)
				tmp = FLOAT_THREE;
			else
				tmp = FLOAT_ONE;
		}

		dspio_set_uint_param(codec, 0x80, 0x00, tmp);

	} else {
		codec_dbg(codec, "%s: on.", __func__);
		chipio_set_conn_rate(codec, MEM_CONNID_MICIN1, SR_16_000);
		chipio_set_conn_rate(codec, MEM_CONNID_MICOUT1, SR_16_000);
		if (spec->quirk == QUIRK_R3DI)
			chipio_set_conn_rate(codec, 0x0F, SR_16_000);

		if (spec->effects_switch[VOICE_FOCUS - EFFECT_START_NID])
			tmp = FLOAT_TWO;
		else
			tmp = FLOAT_ONE;
		dspio_set_uint_param(codec, 0x80, 0x00, tmp);

		tmp = FLOAT_ONE;
		dspio_set_uint_param(codec, 0x80, 0x05, tmp);

		msleep(20);
		chipio_set_control_param(codec, CONTROL_PARAM_VIP_SOURCE, val);
	}

	chipio_set_stream_control(codec, 0x03, 1);
	chipio_set_stream_control(codec, 0x04, 1);

	return 1;
}

/*
 * Select the active microphone.
 * If autodetect is enabled, mic will be selected based on jack detection.
 * If jack inserted, ext.mic will be selected, else built-in mic
 * If autodetect is disabled, mic will be selected based on selection.
 */
static int ca0132_select_mic(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	int jack_present;
	int auto_jack;

	codec_dbg(codec, "ca0132_select_mic\n");

	snd_hda_power_up_pm(codec);

	auto_jack = spec->vnode_lswitch[VNID_AMIC1_ASEL - VNODE_START_NID];

	if (auto_jack)
		jack_present = snd_hda_jack_detect(codec, spec->unsol_tag_amic1);
	else
		jack_present =
			spec->vnode_lswitch[VNID_AMIC1_SEL - VNODE_START_NID];

	if (jack_present)
		spec->cur_mic_type = LINE_MIC_IN;
	else
		spec->cur_mic_type = DIGITAL_MIC;

	if (spec->cur_mic_type == DIGITAL_MIC) {
		/* enable digital Mic */
		chipio_set_conn_rate(codec, MEM_CONNID_DMIC, SR_32_000);
		ca0132_set_dmic(codec, 1);
		ca0132_mic_boost_set(codec, 0);
		/* set voice focus */
		ca0132_effects_set(codec, VOICE_FOCUS,
				   spec->effects_switch
				   [VOICE_FOCUS - EFFECT_START_NID]);
	} else {
		/* disable digital Mic */
		chipio_set_conn_rate(codec, MEM_CONNID_DMIC, SR_96_000);
		ca0132_set_dmic(codec, 0);
		ca0132_mic_boost_set(codec, spec->cur_mic_boost);
		/* disable voice focus */
		ca0132_effects_set(codec, VOICE_FOCUS, 0);
	}

	snd_hda_power_down_pm(codec);

	return 0;
}

/*
 * Select the active input.
 * Mic detection isn't used, because it's kind of pointless on the SBZ.
 * The front mic has no jack-detection, so the only way to switch to it
 * is to do it manually in alsamixer.
 */
static int ca0132_alt_select_in(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;

	codec_dbg(codec, "%s\n", __func__);

	snd_hda_power_up_pm(codec);

	chipio_set_stream_control(codec, 0x03, 0);
	chipio_set_stream_control(codec, 0x04, 0);

	spec->cur_mic_type = spec->in_enum_val;

	switch (spec->cur_mic_type) {
	case REAR_MIC:
		switch (spec->quirk) {
		case QUIRK_SBZ:
		case QUIRK_R3D:
			ca0113_mmio_gpio_set(codec, 0, false);
			tmp = FLOAT_THREE;
			break;
		case QUIRK_ZXR:
			tmp = FLOAT_THREE;
			break;
		case QUIRK_R3DI:
			r3di_gpio_mic_set(codec, R3DI_REAR_MIC);
			tmp = FLOAT_ONE;
			break;
		case QUIRK_AE5:
			ca0113_mmio_command_set(codec, 0x48, 0x28, 0x00);
			tmp = FLOAT_THREE;
			break;
		default:
			tmp = FLOAT_ONE;
			break;
		}

		chipio_set_conn_rate(codec, MEM_CONNID_MICIN1, SR_96_000);
		chipio_set_conn_rate(codec, MEM_CONNID_MICOUT1, SR_96_000);
		if (spec->quirk == QUIRK_R3DI)
			chipio_set_conn_rate(codec, 0x0F, SR_96_000);

		dspio_set_uint_param(codec, 0x80, 0x00, tmp);

		chipio_set_stream_control(codec, 0x03, 1);
		chipio_set_stream_control(codec, 0x04, 1);
		switch (spec->quirk) {
		case QUIRK_SBZ:
			chipio_write(codec, 0x18B098, 0x0000000C);
			chipio_write(codec, 0x18B09C, 0x0000000C);
			break;
		case QUIRK_ZXR:
			chipio_write(codec, 0x18B098, 0x0000000C);
			chipio_write(codec, 0x18B09C, 0x000000CC);
			break;
		case QUIRK_AE5:
			chipio_write(codec, 0x18B098, 0x0000000C);
			chipio_write(codec, 0x18B09C, 0x0000004C);
			break;
		}
		ca0132_alt_mic_boost_set(codec, spec->mic_boost_enum_val);
		break;
	case REAR_LINE_IN:
		ca0132_mic_boost_set(codec, 0);
		switch (spec->quirk) {
		case QUIRK_SBZ:
		case QUIRK_R3D:
			ca0113_mmio_gpio_set(codec, 0, false);
			break;
		case QUIRK_R3DI:
			r3di_gpio_mic_set(codec, R3DI_REAR_MIC);
			break;
		case QUIRK_AE5:
			ca0113_mmio_command_set(codec, 0x48, 0x28, 0x00);
			break;
		}

		chipio_set_conn_rate(codec, MEM_CONNID_MICIN1, SR_96_000);
		chipio_set_conn_rate(codec, MEM_CONNID_MICOUT1, SR_96_000);
		if (spec->quirk == QUIRK_R3DI)
			chipio_set_conn_rate(codec, 0x0F, SR_96_000);

		tmp = FLOAT_ZERO;
		dspio_set_uint_param(codec, 0x80, 0x00, tmp);

		switch (spec->quirk) {
		case QUIRK_SBZ:
		case QUIRK_AE5:
			chipio_write(codec, 0x18B098, 0x00000000);
			chipio_write(codec, 0x18B09C, 0x00000000);
			break;
		}
		chipio_set_stream_control(codec, 0x03, 1);
		chipio_set_stream_control(codec, 0x04, 1);
		break;
	case FRONT_MIC:
		switch (spec->quirk) {
		case QUIRK_SBZ:
		case QUIRK_R3D:
			ca0113_mmio_gpio_set(codec, 0, true);
			ca0113_mmio_gpio_set(codec, 5, false);
			tmp = FLOAT_THREE;
			break;
		case QUIRK_R3DI:
			r3di_gpio_mic_set(codec, R3DI_FRONT_MIC);
			tmp = FLOAT_ONE;
			break;
		case QUIRK_AE5:
			ca0113_mmio_command_set(codec, 0x48, 0x28, 0x3f);
			tmp = FLOAT_THREE;
			break;
		default:
			tmp = FLOAT_ONE;
			break;
		}

		chipio_set_conn_rate(codec, MEM_CONNID_MICIN1, SR_96_000);
		chipio_set_conn_rate(codec, MEM_CONNID_MICOUT1, SR_96_000);
		if (spec->quirk == QUIRK_R3DI)
			chipio_set_conn_rate(codec, 0x0F, SR_96_000);

		dspio_set_uint_param(codec, 0x80, 0x00, tmp);

		chipio_set_stream_control(codec, 0x03, 1);
		chipio_set_stream_control(codec, 0x04, 1);

		switch (spec->quirk) {
		case QUIRK_SBZ:
			chipio_write(codec, 0x18B098, 0x0000000C);
			chipio_write(codec, 0x18B09C, 0x000000CC);
			break;
		case QUIRK_AE5:
			chipio_write(codec, 0x18B098, 0x0000000C);
			chipio_write(codec, 0x18B09C, 0x0000004C);
			break;
		}
		ca0132_alt_mic_boost_set(codec, spec->mic_boost_enum_val);
		break;
	}
	ca0132_cvoice_switch_set(codec);

	snd_hda_power_down_pm(codec);
	return 0;
}

/*
 * Check if VNODE settings take effect immediately.
 */
static bool ca0132_is_vnode_effective(struct hda_codec *codec,
				     hda_nid_t vnid,
				     hda_nid_t *shared_nid)
{
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid;

	switch (vnid) {
	case VNID_SPK:
		nid = spec->shared_out_nid;
		break;
	case VNID_MIC:
		nid = spec->shared_mic_nid;
		break;
	default:
		return false;
	}

	if (shared_nid)
		*shared_nid = nid;

	return true;
}

/*
* The following functions are control change helpers.
* They return 0 if no changed.  Return 1 if changed.
*/
static int ca0132_voicefx_set(struct hda_codec *codec, int enable)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;

	/* based on CrystalVoice state to enable VoiceFX. */
	if (enable) {
		tmp = spec->effects_switch[CRYSTAL_VOICE - EFFECT_START_NID] ?
			FLOAT_ONE : FLOAT_ZERO;
	} else {
		tmp = FLOAT_ZERO;
	}

	dspio_set_uint_param(codec, ca0132_voicefx.mid,
			     ca0132_voicefx.reqs[0], tmp);

	return 1;
}

/*
 * Set the effects parameters
 */
static int ca0132_effects_set(struct hda_codec *codec, hda_nid_t nid, long val)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int on, tmp;
	int num_fx = OUT_EFFECTS_COUNT + IN_EFFECTS_COUNT;
	int err = 0;
	int idx = nid - EFFECT_START_NID;

	if ((idx < 0) || (idx >= num_fx))
		return 0; /* no changed */

	/* for out effect, qualify with PE */
	if ((nid >= OUT_EFFECT_START_NID) && (nid < OUT_EFFECT_END_NID)) {
		/* if PE if off, turn off out effects. */
		if (!spec->effects_switch[PLAY_ENHANCEMENT - EFFECT_START_NID])
			val = 0;
		if (spec->cur_out_type == SURROUND_OUT && nid == X_BASS)
			val = 0;
	}

	/* for in effect, qualify with CrystalVoice */
	if ((nid >= IN_EFFECT_START_NID) && (nid < IN_EFFECT_END_NID)) {
		/* if CrystalVoice if off, turn off in effects. */
		if (!spec->effects_switch[CRYSTAL_VOICE - EFFECT_START_NID])
			val = 0;

		/* Voice Focus applies to 2-ch Mic, Digital Mic */
		if ((nid == VOICE_FOCUS) && (spec->cur_mic_type != DIGITAL_MIC))
			val = 0;

		/* If Voice Focus on SBZ, set to two channel. */
		if ((nid == VOICE_FOCUS) && (spec->use_pci_mmio)
				&& (spec->cur_mic_type != REAR_LINE_IN)) {
			if (spec->effects_switch[CRYSTAL_VOICE -
						 EFFECT_START_NID]) {

				if (spec->effects_switch[VOICE_FOCUS -
							 EFFECT_START_NID]) {
					tmp = FLOAT_TWO;
					val = 1;
				} else
					tmp = FLOAT_ONE;

				dspio_set_uint_param(codec, 0x80, 0x00, tmp);
			}
		}
		/*
		 * For SBZ noise reduction, there's an extra command
		 * to module ID 0x47. No clue why.
		 */
		if ((nid == NOISE_REDUCTION) && (spec->use_pci_mmio)
				&& (spec->cur_mic_type != REAR_LINE_IN)) {
			if (spec->effects_switch[CRYSTAL_VOICE -
						 EFFECT_START_NID]) {
				if (spec->effects_switch[NOISE_REDUCTION -
							 EFFECT_START_NID])
					tmp = FLOAT_ONE;
				else
					tmp = FLOAT_ZERO;
			} else
				tmp = FLOAT_ZERO;

			dspio_set_uint_param(codec, 0x47, 0x00, tmp);
		}

		/* If rear line in disable effects. */
		if (spec->use_alt_functions &&
				spec->in_enum_val == REAR_LINE_IN)
			val = 0;
	}

	codec_dbg(codec, "ca0132_effect_set: nid=0x%x, val=%ld\n",
		    nid, val);

	on = (val == 0) ? FLOAT_ZERO : FLOAT_ONE;
	err = dspio_set_uint_param(codec, ca0132_effects[idx].mid,
				   ca0132_effects[idx].reqs[0], on);

	if (err < 0)
		return 0; /* no changed */

	return 1;
}

/*
 * Turn on/off Playback Enhancements
 */
static int ca0132_pe_switch_set(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid;
	int i, ret = 0;

	codec_dbg(codec, "ca0132_pe_switch_set: val=%ld\n",
		    spec->effects_switch[PLAY_ENHANCEMENT - EFFECT_START_NID]);

	if (spec->use_alt_functions)
		ca0132_alt_select_out(codec);

	i = OUT_EFFECT_START_NID - EFFECT_START_NID;
	nid = OUT_EFFECT_START_NID;
	/* PE affects all out effects */
	for (; nid < OUT_EFFECT_END_NID; nid++, i++)
		ret |= ca0132_effects_set(codec, nid, spec->effects_switch[i]);

	return ret;
}

/* Check if Mic1 is streaming, if so, stop streaming */
static int stop_mic1(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int oldval = snd_hda_codec_read(codec, spec->adcs[0], 0,
						 AC_VERB_GET_CONV, 0);
	if (oldval != 0)
		snd_hda_codec_write(codec, spec->adcs[0], 0,
				    AC_VERB_SET_CHANNEL_STREAMID,
				    0);
	return oldval;
}

/* Resume Mic1 streaming if it was stopped. */
static void resume_mic1(struct hda_codec *codec, unsigned int oldval)
{
	struct ca0132_spec *spec = codec->spec;
	/* Restore the previous stream and channel */
	if (oldval != 0)
		snd_hda_codec_write(codec, spec->adcs[0], 0,
				    AC_VERB_SET_CHANNEL_STREAMID,
				    oldval);
}

/*
 * Turn on/off CrystalVoice
 */
static int ca0132_cvoice_switch_set(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid;
	int i, ret = 0;
	unsigned int oldval;

	codec_dbg(codec, "ca0132_cvoice_switch_set: val=%ld\n",
		    spec->effects_switch[CRYSTAL_VOICE - EFFECT_START_NID]);

	i = IN_EFFECT_START_NID - EFFECT_START_NID;
	nid = IN_EFFECT_START_NID;
	/* CrystalVoice affects all in effects */
	for (; nid < IN_EFFECT_END_NID; nid++, i++)
		ret |= ca0132_effects_set(codec, nid, spec->effects_switch[i]);

	/* including VoiceFX */
	ret |= ca0132_voicefx_set(codec, (spec->voicefx_val ? 1 : 0));

	/* set correct vipsource */
	oldval = stop_mic1(codec);
	if (spec->use_alt_functions)
		ret |= ca0132_alt_set_vipsource(codec, 1);
	else
		ret |= ca0132_set_vipsource(codec, 1);
	resume_mic1(codec, oldval);
	return ret;
}

static int ca0132_mic_boost_set(struct hda_codec *codec, long val)
{
	struct ca0132_spec *spec = codec->spec;
	int ret = 0;

	if (val) /* on */
		ret = snd_hda_codec_amp_update(codec, spec->input_pins[0], 0,
					HDA_INPUT, 0, HDA_AMP_VOLMASK, 3);
	else /* off */
		ret = snd_hda_codec_amp_update(codec, spec->input_pins[0], 0,
					HDA_INPUT, 0, HDA_AMP_VOLMASK, 0);

	return ret;
}

static int ca0132_alt_mic_boost_set(struct hda_codec *codec, long val)
{
	struct ca0132_spec *spec = codec->spec;
	int ret = 0;

	ret = snd_hda_codec_amp_update(codec, spec->input_pins[0], 0,
				HDA_INPUT, 0, HDA_AMP_VOLMASK, val);
	return ret;
}

static int ae5_headphone_gain_set(struct hda_codec *codec, long val)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		ca0113_mmio_command_set(codec, 0x48, 0x11 + i,
				ae5_headphone_gain_presets[val].vals[i]);
	return 0;
}

/*
 * gpio pin 1 is a relay that switches on/off, apparently setting the headphone
 * amplifier to handle a 600 ohm load.
 */
static int zxr_headphone_gain_set(struct hda_codec *codec, long val)
{
	ca0113_mmio_gpio_set(codec, 1, val);

	return 0;
}

static int ca0132_vnode_switch_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	hda_nid_t nid = get_amp_nid(kcontrol);
	hda_nid_t shared_nid = 0;
	bool effective;
	int ret = 0;
	struct ca0132_spec *spec = codec->spec;
	int auto_jack;

	if (nid == VNID_HP_SEL) {
		auto_jack =
			spec->vnode_lswitch[VNID_HP_ASEL - VNODE_START_NID];
		if (!auto_jack) {
			if (spec->use_alt_functions)
				ca0132_alt_select_out(codec);
			else
				ca0132_select_out(codec);
		}
		return 1;
	}

	if (nid == VNID_AMIC1_SEL) {
		auto_jack =
			spec->vnode_lswitch[VNID_AMIC1_ASEL - VNODE_START_NID];
		if (!auto_jack)
			ca0132_select_mic(codec);
		return 1;
	}

	if (nid == VNID_HP_ASEL) {
		if (spec->use_alt_functions)
			ca0132_alt_select_out(codec);
		else
			ca0132_select_out(codec);
		return 1;
	}

	if (nid == VNID_AMIC1_ASEL) {
		ca0132_select_mic(codec);
		return 1;
	}

	/* if effective conditions, then update hw immediately. */
	effective = ca0132_is_vnode_effective(codec, nid, &shared_nid);
	if (effective) {
		int dir = get_amp_direction(kcontrol);
		int ch = get_amp_channels(kcontrol);
		unsigned long pval;

		mutex_lock(&codec->control_mutex);
		pval = kcontrol->private_value;
		kcontrol->private_value = HDA_COMPOSE_AMP_VAL(shared_nid, ch,
								0, dir);
		ret = snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
		kcontrol->private_value = pval;
		mutex_unlock(&codec->control_mutex);
	}

	return ret;
}
/* End of control change helpers. */
/*
 * Below I've added controls to mess with the effect levels, I've only enabled
 * them on the Sound Blaster Z, but they would probably also work on the
 * Chromebook. I figured they were probably tuned specifically for it, and left
 * out for a reason.
 */

/* Sets DSP effect level from the sliders above the controls */
static int ca0132_alt_slider_ctl_set(struct hda_codec *codec, hda_nid_t nid,
			  const unsigned int *lookup, int idx)
{
	int i = 0;
	unsigned int y;
	/*
	 * For X_BASS, req 2 is actually crossover freq instead of
	 * effect level
	 */
	if (nid == X_BASS)
		y = 2;
	else
		y = 1;

	snd_hda_power_up(codec);
	if (nid == XBASS_XOVER) {
		for (i = 0; i < OUT_EFFECTS_COUNT; i++)
			if (ca0132_effects[i].nid == X_BASS)
				break;

		dspio_set_param(codec, ca0132_effects[i].mid, 0x20,
				ca0132_effects[i].reqs[1],
				&(lookup[idx - 1]), sizeof(unsigned int));
	} else {
		/* Find the actual effect structure */
		for (i = 0; i < OUT_EFFECTS_COUNT; i++)
			if (nid == ca0132_effects[i].nid)
				break;

		dspio_set_param(codec, ca0132_effects[i].mid, 0x20,
				ca0132_effects[i].reqs[y],
				&(lookup[idx]), sizeof(unsigned int));
	}

	snd_hda_power_down(codec);

	return 0;
}

static int ca0132_alt_xbass_xover_slider_ctl_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	long *valp = ucontrol->value.integer.value;

	*valp = spec->xbass_xover_freq;
	return 0;
}

static int ca0132_alt_slider_ctl_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int idx = nid - OUT_EFFECT_START_NID;

	*valp = spec->fx_ctl_val[idx];
	return 0;
}

/*
 * The X-bass crossover starts at 10hz, so the min is 1. The
 * frequency is set in multiples of 10.
 */
static int ca0132_alt_xbass_xover_slider_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 1;
	uinfo->value.integer.max = 100;
	uinfo->value.integer.step = 1;

	return 0;
}

static int ca0132_alt_effect_slider_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	int chs = get_amp_channels(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = chs == 3 ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 100;
	uinfo->value.integer.step = 1;

	return 0;
}

static int ca0132_alt_xbass_xover_slider_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int idx;

	/* any change? */
	if (spec->xbass_xover_freq == *valp)
		return 0;

	spec->xbass_xover_freq = *valp;

	idx = *valp;
	ca0132_alt_slider_ctl_set(codec, nid, float_xbass_xover_lookup, idx);

	return 0;
}

static int ca0132_alt_effect_slider_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int idx;

	idx = nid - EFFECT_START_NID;
	/* any change? */
	if (spec->fx_ctl_val[idx] == *valp)
		return 0;

	spec->fx_ctl_val[idx] = *valp;

	idx = *valp;
	ca0132_alt_slider_ctl_set(codec, nid, float_zero_to_one_lookup, idx);

	return 0;
}


/*
 * Mic Boost Enum for alternative ca0132 codecs. I didn't like that the original
 * only has off or full 30 dB, and didn't like making a volume slider that has
 * traditional 0-100 in alsamixer that goes in big steps. I like enum better.
 */
#define MIC_BOOST_NUM_OF_STEPS 4
#define MIC_BOOST_ENUM_MAX_STRLEN 10

static int ca0132_alt_mic_boost_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	char *sfx = "dB";
	char namestr[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = MIC_BOOST_NUM_OF_STEPS;
	if (uinfo->value.enumerated.item >= MIC_BOOST_NUM_OF_STEPS)
		uinfo->value.enumerated.item = MIC_BOOST_NUM_OF_STEPS - 1;
	sprintf(namestr, "%d %s", (uinfo->value.enumerated.item * 10), sfx);
	strcpy(uinfo->value.enumerated.name, namestr);
	return 0;
}

static int ca0132_alt_mic_boost_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->mic_boost_enum_val;
	return 0;
}

static int ca0132_alt_mic_boost_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	int sel = ucontrol->value.enumerated.item[0];
	unsigned int items = MIC_BOOST_NUM_OF_STEPS;

	if (sel >= items)
		return 0;

	codec_dbg(codec, "ca0132_alt_mic_boost: boost=%d\n",
		    sel);

	spec->mic_boost_enum_val = sel;

	if (spec->in_enum_val != REAR_LINE_IN)
		ca0132_alt_mic_boost_set(codec, spec->mic_boost_enum_val);

	return 1;
}

/*
 * Sound BlasterX AE-5 Headphone Gain Controls.
 */
#define AE5_HEADPHONE_GAIN_MAX 3
static int ae5_headphone_gain_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	char *sfx = " Ohms)";
	char namestr[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = AE5_HEADPHONE_GAIN_MAX;
	if (uinfo->value.enumerated.item >= AE5_HEADPHONE_GAIN_MAX)
		uinfo->value.enumerated.item = AE5_HEADPHONE_GAIN_MAX - 1;
	sprintf(namestr, "%s %s",
		ae5_headphone_gain_presets[uinfo->value.enumerated.item].name,
		sfx);
	strcpy(uinfo->value.enumerated.name, namestr);
	return 0;
}

static int ae5_headphone_gain_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->ae5_headphone_gain_val;
	return 0;
}

static int ae5_headphone_gain_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	int sel = ucontrol->value.enumerated.item[0];
	unsigned int items = AE5_HEADPHONE_GAIN_MAX;

	if (sel >= items)
		return 0;

	codec_dbg(codec, "ae5_headphone_gain: boost=%d\n",
		    sel);

	spec->ae5_headphone_gain_val = sel;

	if (spec->out_enum_val == HEADPHONE_OUT)
		ae5_headphone_gain_set(codec, spec->ae5_headphone_gain_val);

	return 1;
}

/*
 * Sound BlasterX AE-5 sound filter enumerated control.
 */
#define AE5_SOUND_FILTER_MAX 3

static int ae5_sound_filter_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	char namestr[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = AE5_SOUND_FILTER_MAX;
	if (uinfo->value.enumerated.item >= AE5_SOUND_FILTER_MAX)
		uinfo->value.enumerated.item = AE5_SOUND_FILTER_MAX - 1;
	sprintf(namestr, "%s",
			ae5_filter_presets[uinfo->value.enumerated.item].name);
	strcpy(uinfo->value.enumerated.name, namestr);
	return 0;
}

static int ae5_sound_filter_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->ae5_filter_val;
	return 0;
}

static int ae5_sound_filter_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	int sel = ucontrol->value.enumerated.item[0];
	unsigned int items = AE5_SOUND_FILTER_MAX;

	if (sel >= items)
		return 0;

	codec_dbg(codec, "ae5_sound_filter: %s\n",
			ae5_filter_presets[sel].name);

	spec->ae5_filter_val = sel;

	ca0113_mmio_command_set_type2(codec, 0x48, 0x07,
			ae5_filter_presets[sel].val);

	return 1;
}

/*
 * Input Select Control for alternative ca0132 codecs. This exists because
 * front microphone has no auto-detect, and we need a way to set the rear
 * as line-in
 */
static int ca0132_alt_input_source_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = IN_SRC_NUM_OF_INPUTS;
	if (uinfo->value.enumerated.item >= IN_SRC_NUM_OF_INPUTS)
		uinfo->value.enumerated.item = IN_SRC_NUM_OF_INPUTS - 1;
	strcpy(uinfo->value.enumerated.name,
			in_src_str[uinfo->value.enumerated.item]);
	return 0;
}

static int ca0132_alt_input_source_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->in_enum_val;
	return 0;
}

static int ca0132_alt_input_source_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	int sel = ucontrol->value.enumerated.item[0];
	unsigned int items = IN_SRC_NUM_OF_INPUTS;

	if (sel >= items)
		return 0;

	codec_dbg(codec, "ca0132_alt_input_select: sel=%d, preset=%s\n",
		    sel, in_src_str[sel]);

	spec->in_enum_val = sel;

	ca0132_alt_select_in(codec);

	return 1;
}

/* Sound Blaster Z Output Select Control */
static int ca0132_alt_output_select_get_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = NUM_OF_OUTPUTS;
	if (uinfo->value.enumerated.item >= NUM_OF_OUTPUTS)
		uinfo->value.enumerated.item = NUM_OF_OUTPUTS - 1;
	strcpy(uinfo->value.enumerated.name,
			alt_out_presets[uinfo->value.enumerated.item].name);
	return 0;
}

static int ca0132_alt_output_select_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->out_enum_val;
	return 0;
}

static int ca0132_alt_output_select_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	int sel = ucontrol->value.enumerated.item[0];
	unsigned int items = NUM_OF_OUTPUTS;
	unsigned int auto_jack;

	if (sel >= items)
		return 0;

	codec_dbg(codec, "ca0132_alt_output_select: sel=%d, preset=%s\n",
		    sel, alt_out_presets[sel].name);

	spec->out_enum_val = sel;

	auto_jack = spec->vnode_lswitch[VNID_HP_ASEL - VNODE_START_NID];

	if (!auto_jack)
		ca0132_alt_select_out(codec);

	return 1;
}

/*
 * Smart Volume output setting control. Three different settings, Normal,
 * which takes the value from the smart volume slider. The two others, loud
 * and night, disregard the slider value and have uneditable values.
 */
#define NUM_OF_SVM_SETTINGS 3
static const char *const out_svm_set_enum_str[3] = {"Normal", "Loud", "Night" };

static int ca0132_alt_svm_setting_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = NUM_OF_SVM_SETTINGS;
	if (uinfo->value.enumerated.item >= NUM_OF_SVM_SETTINGS)
		uinfo->value.enumerated.item = NUM_OF_SVM_SETTINGS - 1;
	strcpy(uinfo->value.enumerated.name,
			out_svm_set_enum_str[uinfo->value.enumerated.item]);
	return 0;
}

static int ca0132_alt_svm_setting_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->smart_volume_setting;
	return 0;
}

static int ca0132_alt_svm_setting_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	int sel = ucontrol->value.enumerated.item[0];
	unsigned int items = NUM_OF_SVM_SETTINGS;
	unsigned int idx = SMART_VOLUME - EFFECT_START_NID;
	unsigned int tmp;

	if (sel >= items)
		return 0;

	codec_dbg(codec, "ca0132_alt_svm_setting: sel=%d, preset=%s\n",
		    sel, out_svm_set_enum_str[sel]);

	spec->smart_volume_setting = sel;

	switch (sel) {
	case 0:
		tmp = FLOAT_ZERO;
		break;
	case 1:
		tmp = FLOAT_ONE;
		break;
	case 2:
		tmp = FLOAT_TWO;
		break;
	default:
		tmp = FLOAT_ZERO;
		break;
	}
	/* Req 2 is the Smart Volume Setting req. */
	dspio_set_uint_param(codec, ca0132_effects[idx].mid,
			ca0132_effects[idx].reqs[2], tmp);
	return 1;
}

/* Sound Blaster Z EQ preset controls */
static int ca0132_alt_eq_preset_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	unsigned int items = ARRAY_SIZE(ca0132_alt_eq_presets);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = items;
	if (uinfo->value.enumerated.item >= items)
		uinfo->value.enumerated.item = items - 1;
	strcpy(uinfo->value.enumerated.name,
		ca0132_alt_eq_presets[uinfo->value.enumerated.item].name);
	return 0;
}

static int ca0132_alt_eq_preset_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->eq_preset_val;
	return 0;
}

static int ca0132_alt_eq_preset_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	int i, err = 0;
	int sel = ucontrol->value.enumerated.item[0];
	unsigned int items = ARRAY_SIZE(ca0132_alt_eq_presets);

	if (sel >= items)
		return 0;

	codec_dbg(codec, "%s: sel=%d, preset=%s\n", __func__, sel,
			ca0132_alt_eq_presets[sel].name);
	/*
	 * Idx 0 is default.
	 * Default needs to qualify with CrystalVoice state.
	 */
	for (i = 0; i < EQ_PRESET_MAX_PARAM_COUNT; i++) {
		err = dspio_set_uint_param(codec, ca0132_alt_eq_enum.mid,
				ca0132_alt_eq_enum.reqs[i],
				ca0132_alt_eq_presets[sel].vals[i]);
		if (err < 0)
			break;
	}

	if (err >= 0)
		spec->eq_preset_val = sel;

	return 1;
}

static int ca0132_voicefx_info(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_info *uinfo)
{
	unsigned int items = ARRAY_SIZE(ca0132_voicefx_presets);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = items;
	if (uinfo->value.enumerated.item >= items)
		uinfo->value.enumerated.item = items - 1;
	strcpy(uinfo->value.enumerated.name,
	       ca0132_voicefx_presets[uinfo->value.enumerated.item].name);
	return 0;
}

static int ca0132_voicefx_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->voicefx_val;
	return 0;
}

static int ca0132_voicefx_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	int i, err = 0;
	int sel = ucontrol->value.enumerated.item[0];

	if (sel >= ARRAY_SIZE(ca0132_voicefx_presets))
		return 0;

	codec_dbg(codec, "ca0132_voicefx_put: sel=%d, preset=%s\n",
		    sel, ca0132_voicefx_presets[sel].name);

	/*
	 * Idx 0 is default.
	 * Default needs to qualify with CrystalVoice state.
	 */
	for (i = 0; i < VOICEFX_MAX_PARAM_COUNT; i++) {
		err = dspio_set_uint_param(codec, ca0132_voicefx.mid,
				ca0132_voicefx.reqs[i],
				ca0132_voicefx_presets[sel].vals[i]);
		if (err < 0)
			break;
	}

	if (err >= 0) {
		spec->voicefx_val = sel;
		/* enable voice fx */
		ca0132_voicefx_set(codec, (sel ? 1 : 0));
	}

	return 1;
}

static int ca0132_switch_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	int ch = get_amp_channels(kcontrol);
	long *valp = ucontrol->value.integer.value;

	/* vnode */
	if ((nid >= VNODE_START_NID) && (nid < VNODE_END_NID)) {
		if (ch & 1) {
			*valp = spec->vnode_lswitch[nid - VNODE_START_NID];
			valp++;
		}
		if (ch & 2) {
			*valp = spec->vnode_rswitch[nid - VNODE_START_NID];
			valp++;
		}
		return 0;
	}

	/* effects, include PE and CrystalVoice */
	if ((nid >= EFFECT_START_NID) && (nid < EFFECT_END_NID)) {
		*valp = spec->effects_switch[nid - EFFECT_START_NID];
		return 0;
	}

	/* mic boost */
	if (nid == spec->input_pins[0]) {
		*valp = spec->cur_mic_boost;
		return 0;
	}

	return 0;
}

static int ca0132_switch_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	int ch = get_amp_channels(kcontrol);
	long *valp = ucontrol->value.integer.value;
	int changed = 1;

	codec_dbg(codec, "ca0132_switch_put: nid=0x%x, val=%ld\n",
		    nid, *valp);

	snd_hda_power_up(codec);
	/* vnode */
	if ((nid >= VNODE_START_NID) && (nid < VNODE_END_NID)) {
		if (ch & 1) {
			spec->vnode_lswitch[nid - VNODE_START_NID] = *valp;
			valp++;
		}
		if (ch & 2) {
			spec->vnode_rswitch[nid - VNODE_START_NID] = *valp;
			valp++;
		}
		changed = ca0132_vnode_switch_set(kcontrol, ucontrol);
		goto exit;
	}

	/* PE */
	if (nid == PLAY_ENHANCEMENT) {
		spec->effects_switch[nid - EFFECT_START_NID] = *valp;
		changed = ca0132_pe_switch_set(codec);
		goto exit;
	}

	/* CrystalVoice */
	if (nid == CRYSTAL_VOICE) {
		spec->effects_switch[nid - EFFECT_START_NID] = *valp;
		changed = ca0132_cvoice_switch_set(codec);
		goto exit;
	}

	/* out and in effects */
	if (((nid >= OUT_EFFECT_START_NID) && (nid < OUT_EFFECT_END_NID)) ||
	    ((nid >= IN_EFFECT_START_NID) && (nid < IN_EFFECT_END_NID))) {
		spec->effects_switch[nid - EFFECT_START_NID] = *valp;
		changed = ca0132_effects_set(codec, nid, *valp);
		goto exit;
	}

	/* mic boost */
	if (nid == spec->input_pins[0]) {
		spec->cur_mic_boost = *valp;
		if (spec->use_alt_functions) {
			if (spec->in_enum_val != REAR_LINE_IN)
				changed = ca0132_mic_boost_set(codec, *valp);
		} else {
			/* Mic boost does not apply to Digital Mic */
			if (spec->cur_mic_type != DIGITAL_MIC)
				changed = ca0132_mic_boost_set(codec, *valp);
		}

		goto exit;
	}

	if (nid == ZXR_HEADPHONE_GAIN) {
		spec->zxr_gain_set = *valp;
		if (spec->cur_out_type == HEADPHONE_OUT)
			changed = zxr_headphone_gain_set(codec, *valp);
		else
			changed = 0;

		goto exit;
	}

exit:
	snd_hda_power_down(codec);
	return changed;
}

/*
 * Volume related
 */
/*
 * Sets the internal DSP decibel level to match the DAC for output, and the
 * ADC for input. Currently only the SBZ sets dsp capture volume level, and
 * all alternative codecs set DSP playback volume.
 */
static void ca0132_alt_dsp_volume_put(struct hda_codec *codec, hda_nid_t nid)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int dsp_dir;
	unsigned int lookup_val;

	if (nid == VNID_SPK)
		dsp_dir = DSP_VOL_OUT;
	else
		dsp_dir = DSP_VOL_IN;

	lookup_val = spec->vnode_lvol[nid - VNODE_START_NID];

	dspio_set_uint_param(codec,
		ca0132_alt_vol_ctls[dsp_dir].mid,
		ca0132_alt_vol_ctls[dsp_dir].reqs[0],
		float_vol_db_lookup[lookup_val]);

	lookup_val = spec->vnode_rvol[nid - VNODE_START_NID];

	dspio_set_uint_param(codec,
		ca0132_alt_vol_ctls[dsp_dir].mid,
		ca0132_alt_vol_ctls[dsp_dir].reqs[1],
		float_vol_db_lookup[lookup_val]);

	dspio_set_uint_param(codec,
		ca0132_alt_vol_ctls[dsp_dir].mid,
		ca0132_alt_vol_ctls[dsp_dir].reqs[2], FLOAT_ZERO);
}

static int ca0132_volume_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	int ch = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	unsigned long pval;
	int err;

	switch (nid) {
	case VNID_SPK:
		/* follow shared_out info */
		nid = spec->shared_out_nid;
		mutex_lock(&codec->control_mutex);
		pval = kcontrol->private_value;
		kcontrol->private_value = HDA_COMPOSE_AMP_VAL(nid, ch, 0, dir);
		err = snd_hda_mixer_amp_volume_info(kcontrol, uinfo);
		kcontrol->private_value = pval;
		mutex_unlock(&codec->control_mutex);
		break;
	case VNID_MIC:
		/* follow shared_mic info */
		nid = spec->shared_mic_nid;
		mutex_lock(&codec->control_mutex);
		pval = kcontrol->private_value;
		kcontrol->private_value = HDA_COMPOSE_AMP_VAL(nid, ch, 0, dir);
		err = snd_hda_mixer_amp_volume_info(kcontrol, uinfo);
		kcontrol->private_value = pval;
		mutex_unlock(&codec->control_mutex);
		break;
	default:
		err = snd_hda_mixer_amp_volume_info(kcontrol, uinfo);
	}
	return err;
}

static int ca0132_volume_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	int ch = get_amp_channels(kcontrol);
	long *valp = ucontrol->value.integer.value;

	/* store the left and right volume */
	if (ch & 1) {
		*valp = spec->vnode_lvol[nid - VNODE_START_NID];
		valp++;
	}
	if (ch & 2) {
		*valp = spec->vnode_rvol[nid - VNODE_START_NID];
		valp++;
	}
	return 0;
}

static int ca0132_volume_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	int ch = get_amp_channels(kcontrol);
	long *valp = ucontrol->value.integer.value;
	hda_nid_t shared_nid = 0;
	bool effective;
	int changed = 1;

	/* store the left and right volume */
	if (ch & 1) {
		spec->vnode_lvol[nid - VNODE_START_NID] = *valp;
		valp++;
	}
	if (ch & 2) {
		spec->vnode_rvol[nid - VNODE_START_NID] = *valp;
		valp++;
	}

	/* if effective conditions, then update hw immediately. */
	effective = ca0132_is_vnode_effective(codec, nid, &shared_nid);
	if (effective) {
		int dir = get_amp_direction(kcontrol);
		unsigned long pval;

		snd_hda_power_up(codec);
		mutex_lock(&codec->control_mutex);
		pval = kcontrol->private_value;
		kcontrol->private_value = HDA_COMPOSE_AMP_VAL(shared_nid, ch,
								0, dir);
		changed = snd_hda_mixer_amp_volume_put(kcontrol, ucontrol);
		kcontrol->private_value = pval;
		mutex_unlock(&codec->control_mutex);
		snd_hda_power_down(codec);
	}

	return changed;
}

/*
 * This function is the same as the one above, because using an if statement
 * inside of the above volume control for the DSP volume would cause too much
 * lag. This is a lot more smooth.
 */
static int ca0132_alt_volume_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	int ch = get_amp_channels(kcontrol);
	long *valp = ucontrol->value.integer.value;
	hda_nid_t vnid = 0;
	int changed = 1;

	switch (nid) {
	case 0x02:
		vnid = VNID_SPK;
		break;
	case 0x07:
		vnid = VNID_MIC;
		break;
	}

	/* store the left and right volume */
	if (ch & 1) {
		spec->vnode_lvol[vnid - VNODE_START_NID] = *valp;
		valp++;
	}
	if (ch & 2) {
		spec->vnode_rvol[vnid - VNODE_START_NID] = *valp;
		valp++;
	}

	snd_hda_power_up(codec);
	ca0132_alt_dsp_volume_put(codec, vnid);
	mutex_lock(&codec->control_mutex);
	changed = snd_hda_mixer_amp_volume_put(kcontrol, ucontrol);
	mutex_unlock(&codec->control_mutex);
	snd_hda_power_down(codec);

	return changed;
}

static int ca0132_volume_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			     unsigned int size, unsigned int __user *tlv)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct ca0132_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kcontrol);
	int ch = get_amp_channels(kcontrol);
	int dir = get_amp_direction(kcontrol);
	unsigned long pval;
	int err;

	switch (nid) {
	case VNID_SPK:
		/* follow shared_out tlv */
		nid = spec->shared_out_nid;
		mutex_lock(&codec->control_mutex);
		pval = kcontrol->private_value;
		kcontrol->private_value = HDA_COMPOSE_AMP_VAL(nid, ch, 0, dir);
		err = snd_hda_mixer_amp_tlv(kcontrol, op_flag, size, tlv);
		kcontrol->private_value = pval;
		mutex_unlock(&codec->control_mutex);
		break;
	case VNID_MIC:
		/* follow shared_mic tlv */
		nid = spec->shared_mic_nid;
		mutex_lock(&codec->control_mutex);
		pval = kcontrol->private_value;
		kcontrol->private_value = HDA_COMPOSE_AMP_VAL(nid, ch, 0, dir);
		err = snd_hda_mixer_amp_tlv(kcontrol, op_flag, size, tlv);
		kcontrol->private_value = pval;
		mutex_unlock(&codec->control_mutex);
		break;
	default:
		err = snd_hda_mixer_amp_tlv(kcontrol, op_flag, size, tlv);
	}
	return err;
}

/* Add volume slider control for effect level */
static int ca0132_alt_add_effect_slider(struct hda_codec *codec, hda_nid_t nid,
					const char *pfx, int dir)
{
	char namestr[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	int type = dir ? HDA_INPUT : HDA_OUTPUT;
	struct snd_kcontrol_new knew =
		HDA_CODEC_VOLUME_MONO(namestr, nid, 1, 0, type);

	sprintf(namestr, "FX: %s %s Volume", pfx, dirstr[dir]);

	knew.tlv.c = NULL;

	switch (nid) {
	case XBASS_XOVER:
		knew.info = ca0132_alt_xbass_xover_slider_info;
		knew.get = ca0132_alt_xbass_xover_slider_ctl_get;
		knew.put = ca0132_alt_xbass_xover_slider_put;
		break;
	default:
		knew.info = ca0132_alt_effect_slider_info;
		knew.get = ca0132_alt_slider_ctl_get;
		knew.put = ca0132_alt_effect_slider_put;
		knew.private_value =
			HDA_COMPOSE_AMP_VAL(nid, 1, 0, type);
		break;
	}

	return snd_hda_ctl_add(codec, nid, snd_ctl_new1(&knew, codec));
}

/*
 * Added FX: prefix for the alternative codecs, because otherwise the surround
 * effect would conflict with the Surround sound volume control. Also seems more
 * clear as to what the switches do. Left alone for others.
 */
static int add_fx_switch(struct hda_codec *codec, hda_nid_t nid,
			 const char *pfx, int dir)
{
	struct ca0132_spec *spec = codec->spec;
	char namestr[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	int type = dir ? HDA_INPUT : HDA_OUTPUT;
	struct snd_kcontrol_new knew =
		CA0132_CODEC_MUTE_MONO(namestr, nid, 1, type);
	/* If using alt_controls, add FX: prefix. But, don't add FX:
	 * prefix to OutFX or InFX enable controls.
	 */
	if ((spec->use_alt_controls) && (nid <= IN_EFFECT_END_NID))
		sprintf(namestr, "FX: %s %s Switch", pfx, dirstr[dir]);
	else
		sprintf(namestr, "%s %s Switch", pfx, dirstr[dir]);

	return snd_hda_ctl_add(codec, nid, snd_ctl_new1(&knew, codec));
}

static int add_voicefx(struct hda_codec *codec)
{
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_MONO(ca0132_voicefx.name,
				    VOICEFX, 1, 0, HDA_INPUT);
	knew.info = ca0132_voicefx_info;
	knew.get = ca0132_voicefx_get;
	knew.put = ca0132_voicefx_put;
	return snd_hda_ctl_add(codec, VOICEFX, snd_ctl_new1(&knew, codec));
}

/* Create the EQ Preset control */
static int add_ca0132_alt_eq_presets(struct hda_codec *codec)
{
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_MONO(ca0132_alt_eq_enum.name,
				    EQ_PRESET_ENUM, 1, 0, HDA_OUTPUT);
	knew.info = ca0132_alt_eq_preset_info;
	knew.get = ca0132_alt_eq_preset_get;
	knew.put = ca0132_alt_eq_preset_put;
	return snd_hda_ctl_add(codec, EQ_PRESET_ENUM,
				snd_ctl_new1(&knew, codec));
}

/*
 * Add enumerated control for the three different settings of the smart volume
 * output effect. Normal just uses the slider value, and loud and night are
 * their own things that ignore that value.
 */
static int ca0132_alt_add_svm_enum(struct hda_codec *codec)
{
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_MONO("FX: Smart Volume Setting",
				    SMART_VOLUME_ENUM, 1, 0, HDA_OUTPUT);
	knew.info = ca0132_alt_svm_setting_info;
	knew.get = ca0132_alt_svm_setting_get;
	knew.put = ca0132_alt_svm_setting_put;
	return snd_hda_ctl_add(codec, SMART_VOLUME_ENUM,
				snd_ctl_new1(&knew, codec));

}

/*
 * Create an Output Select enumerated control for codecs with surround
 * out capabilities.
 */
static int ca0132_alt_add_output_enum(struct hda_codec *codec)
{
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_MONO("Output Select",
				    OUTPUT_SOURCE_ENUM, 1, 0, HDA_OUTPUT);
	knew.info = ca0132_alt_output_select_get_info;
	knew.get = ca0132_alt_output_select_get;
	knew.put = ca0132_alt_output_select_put;
	return snd_hda_ctl_add(codec, OUTPUT_SOURCE_ENUM,
				snd_ctl_new1(&knew, codec));
}

/*
 * Create an Input Source enumerated control for the alternate ca0132 codecs
 * because the front microphone has no auto-detect, and Line-in has to be set
 * somehow.
 */
static int ca0132_alt_add_input_enum(struct hda_codec *codec)
{
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_MONO("Input Source",
				    INPUT_SOURCE_ENUM, 1, 0, HDA_INPUT);
	knew.info = ca0132_alt_input_source_info;
	knew.get = ca0132_alt_input_source_get;
	knew.put = ca0132_alt_input_source_put;
	return snd_hda_ctl_add(codec, INPUT_SOURCE_ENUM,
				snd_ctl_new1(&knew, codec));
}

/*
 * Add mic boost enumerated control. Switches through 0dB to 30dB. This adds
 * more control than the original mic boost, which is either full 30dB or off.
 */
static int ca0132_alt_add_mic_boost_enum(struct hda_codec *codec)
{
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_MONO("Mic Boost Capture Switch",
				    MIC_BOOST_ENUM, 1, 0, HDA_INPUT);
	knew.info = ca0132_alt_mic_boost_info;
	knew.get = ca0132_alt_mic_boost_get;
	knew.put = ca0132_alt_mic_boost_put;
	return snd_hda_ctl_add(codec, MIC_BOOST_ENUM,
				snd_ctl_new1(&knew, codec));

}

/*
 * Add headphone gain enumerated control for the AE-5. This switches between
 * three modes, low, medium, and high. When non-headphone outputs are selected,
 * it is automatically set to high. This is the same behavior as Windows.
 */
static int ae5_add_headphone_gain_enum(struct hda_codec *codec)
{
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_MONO("AE-5: Headphone Gain",
				    AE5_HEADPHONE_GAIN_ENUM, 1, 0, HDA_OUTPUT);
	knew.info = ae5_headphone_gain_info;
	knew.get = ae5_headphone_gain_get;
	knew.put = ae5_headphone_gain_put;
	return snd_hda_ctl_add(codec, AE5_HEADPHONE_GAIN_ENUM,
				snd_ctl_new1(&knew, codec));
}

/*
 * Add sound filter enumerated control for the AE-5. This adds three different
 * settings: Slow Roll Off, Minimum Phase, and Fast Roll Off. From what I've
 * read into it, it changes the DAC's interpolation filter.
 */
static int ae5_add_sound_filter_enum(struct hda_codec *codec)
{
	struct snd_kcontrol_new knew =
		HDA_CODEC_MUTE_MONO("AE-5: Sound Filter",
				    AE5_SOUND_FILTER_ENUM, 1, 0, HDA_OUTPUT);
	knew.info = ae5_sound_filter_info;
	knew.get = ae5_sound_filter_get;
	knew.put = ae5_sound_filter_put;
	return snd_hda_ctl_add(codec, AE5_SOUND_FILTER_ENUM,
				snd_ctl_new1(&knew, codec));
}

static int zxr_add_headphone_gain_switch(struct hda_codec *codec)
{
	struct snd_kcontrol_new knew =
		CA0132_CODEC_MUTE_MONO("ZxR: 600 Ohm Gain",
				    ZXR_HEADPHONE_GAIN, 1, HDA_OUTPUT);

	return snd_hda_ctl_add(codec, ZXR_HEADPHONE_GAIN,
				snd_ctl_new1(&knew, codec));
}

/*
 * Need to create slave controls for the alternate codecs that have surround
 * capabilities.
 */
static const char * const ca0132_alt_slave_pfxs[] = {
	"Front", "Surround", "Center", "LFE", NULL,
};

/*
 * Also need special channel map, because the default one is incorrect.
 * I think this has to do with the pin for rear surround being 0x11,
 * and the center/lfe being 0x10. Usually the pin order is the opposite.
 */
static const struct snd_pcm_chmap_elem ca0132_alt_chmaps[] = {
	{ .channels = 2,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR } },
	{ .channels = 4,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
		   SNDRV_CHMAP_RL, SNDRV_CHMAP_RR } },
	{ .channels = 6,
	  .map = { SNDRV_CHMAP_FL, SNDRV_CHMAP_FR,
		   SNDRV_CHMAP_FC, SNDRV_CHMAP_LFE,
		   SNDRV_CHMAP_RL, SNDRV_CHMAP_RR } },
	{ }
};

/* Add the correct chmap for streams with 6 channels. */
static void ca0132_alt_add_chmap_ctls(struct hda_codec *codec)
{
	int err = 0;
	struct hda_pcm *pcm;

	list_for_each_entry(pcm, &codec->pcm_list_head, list) {
		struct hda_pcm_stream *hinfo =
			&pcm->stream[SNDRV_PCM_STREAM_PLAYBACK];
		struct snd_pcm_chmap *chmap;
		const struct snd_pcm_chmap_elem *elem;

		elem = ca0132_alt_chmaps;
		if (hinfo->channels_max == 6) {
			err = snd_pcm_add_chmap_ctls(pcm->pcm,
					SNDRV_PCM_STREAM_PLAYBACK,
					elem, hinfo->channels_max, 0, &chmap);
			if (err < 0)
				codec_dbg(codec, "snd_pcm_add_chmap_ctls failed!");
		}
	}
}

/*
 * When changing Node IDs for Mixer Controls below, make sure to update
 * Node IDs in ca0132_config() as well.
 */
static const struct snd_kcontrol_new ca0132_mixer[] = {
	CA0132_CODEC_VOL("Master Playback Volume", VNID_SPK, HDA_OUTPUT),
	CA0132_CODEC_MUTE("Master Playback Switch", VNID_SPK, HDA_OUTPUT),
	CA0132_CODEC_VOL("Capture Volume", VNID_MIC, HDA_INPUT),
	CA0132_CODEC_MUTE("Capture Switch", VNID_MIC, HDA_INPUT),
	HDA_CODEC_VOLUME("Analog-Mic2 Capture Volume", 0x08, 0, HDA_INPUT),
	HDA_CODEC_MUTE("Analog-Mic2 Capture Switch", 0x08, 0, HDA_INPUT),
	HDA_CODEC_VOLUME("What U Hear Capture Volume", 0x0a, 0, HDA_INPUT),
	HDA_CODEC_MUTE("What U Hear Capture Switch", 0x0a, 0, HDA_INPUT),
	CA0132_CODEC_MUTE_MONO("Mic1-Boost (30dB) Capture Switch",
			       0x12, 1, HDA_INPUT),
	CA0132_CODEC_MUTE_MONO("HP/Speaker Playback Switch",
			       VNID_HP_SEL, 1, HDA_OUTPUT),
	CA0132_CODEC_MUTE_MONO("AMic1/DMic Capture Switch",
			       VNID_AMIC1_SEL, 1, HDA_INPUT),
	CA0132_CODEC_MUTE_MONO("HP/Speaker Auto Detect Playback Switch",
			       VNID_HP_ASEL, 1, HDA_OUTPUT),
	CA0132_CODEC_MUTE_MONO("AMic1/DMic Auto Detect Capture Switch",
			       VNID_AMIC1_ASEL, 1, HDA_INPUT),
	{ } /* end */
};

/*
 * Desktop specific control mixer. Removes auto-detect for mic, and adds
 * surround controls. Also sets both the Front Playback and Capture Volume
 * controls to alt so they set the DSP's decibel level.
 */
static const struct snd_kcontrol_new desktop_mixer[] = {
	CA0132_ALT_CODEC_VOL("Front Playback Volume", 0x02, HDA_OUTPUT),
	CA0132_CODEC_MUTE("Front Playback Switch", VNID_SPK, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x04, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x04, 0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x03, 1, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x03, 1, 0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x03, 2, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x03, 2, 0, HDA_OUTPUT),
	CA0132_ALT_CODEC_VOL("Capture Volume", 0x07, HDA_INPUT),
	CA0132_CODEC_MUTE("Capture Switch", VNID_MIC, HDA_INPUT),
	HDA_CODEC_VOLUME("What U Hear Capture Volume", 0x0a, 0, HDA_INPUT),
	HDA_CODEC_MUTE("What U Hear Capture Switch", 0x0a, 0, HDA_INPUT),
	CA0132_CODEC_MUTE_MONO("HP/Speaker Auto Detect Playback Switch",
				VNID_HP_ASEL, 1, HDA_OUTPUT),
	{ } /* end */
};

/*
 * Same as the Sound Blaster Z, except doesn't use the alt volume for capture
 * because it doesn't set decibel levels for the DSP for capture.
 */
static const struct snd_kcontrol_new r3di_mixer[] = {
	CA0132_ALT_CODEC_VOL("Front Playback Volume", 0x02, HDA_OUTPUT),
	CA0132_CODEC_MUTE("Front Playback Switch", VNID_SPK, HDA_OUTPUT),
	HDA_CODEC_VOLUME("Surround Playback Volume", 0x04, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE("Surround Playback Switch", 0x04, 0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("Center Playback Volume", 0x03, 1, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("Center Playback Switch", 0x03, 1, 0, HDA_OUTPUT),
	HDA_CODEC_VOLUME_MONO("LFE Playback Volume", 0x03, 2, 0, HDA_OUTPUT),
	HDA_CODEC_MUTE_MONO("LFE Playback Switch", 0x03, 2, 0, HDA_OUTPUT),
	CA0132_CODEC_VOL("Capture Volume", VNID_MIC, HDA_INPUT),
	CA0132_CODEC_MUTE("Capture Switch", VNID_MIC, HDA_INPUT),
	HDA_CODEC_VOLUME("What U Hear Capture Volume", 0x0a, 0, HDA_INPUT),
	HDA_CODEC_MUTE("What U Hear Capture Switch", 0x0a, 0, HDA_INPUT),
	CA0132_CODEC_MUTE_MONO("HP/Speaker Auto Detect Playback Switch",
				VNID_HP_ASEL, 1, HDA_OUTPUT),
	{ } /* end */
};

static int ca0132_build_controls(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	int i, num_fx, num_sliders;
	int err = 0;

	/* Add Mixer controls */
	for (i = 0; i < spec->num_mixers; i++) {
		err = snd_hda_add_new_ctls(codec, spec->mixers[i]);
		if (err < 0)
			return err;
	}
	/* Setup vmaster with surround slaves for desktop ca0132 devices */
	if (spec->use_alt_functions) {
		snd_hda_set_vmaster_tlv(codec, spec->dacs[0], HDA_OUTPUT,
					spec->tlv);
		snd_hda_add_vmaster(codec, "Master Playback Volume",
					spec->tlv, ca0132_alt_slave_pfxs,
					"Playback Volume");
		err = __snd_hda_add_vmaster(codec, "Master Playback Switch",
					    NULL, ca0132_alt_slave_pfxs,
					    "Playback Switch",
					    true, &spec->vmaster_mute.sw_kctl);
		if (err < 0)
			return err;
	}

	/* Add in and out effects controls.
	 * VoiceFX, PE and CrystalVoice are added separately.
	 */
	num_fx = OUT_EFFECTS_COUNT + IN_EFFECTS_COUNT;
	for (i = 0; i < num_fx; i++) {
		/* Desktop cards break if Echo Cancellation is used. */
		if (spec->use_pci_mmio) {
			if (i == (ECHO_CANCELLATION - IN_EFFECT_START_NID +
						OUT_EFFECTS_COUNT))
				continue;
		}

		err = add_fx_switch(codec, ca0132_effects[i].nid,
				    ca0132_effects[i].name,
				    ca0132_effects[i].direct);
		if (err < 0)
			return err;
	}
	/*
	 * If codec has use_alt_controls set to true, add effect level sliders,
	 * EQ presets, and Smart Volume presets. Also, change names to add FX
	 * prefix, and change PlayEnhancement and CrystalVoice to match.
	 */
	if (spec->use_alt_controls) {
		err = ca0132_alt_add_svm_enum(codec);
		if (err < 0)
			return err;

		err = add_ca0132_alt_eq_presets(codec);
		if (err < 0)
			return err;

		err = add_fx_switch(codec, PLAY_ENHANCEMENT,
					"Enable OutFX", 0);
		if (err < 0)
			return err;

		err = add_fx_switch(codec, CRYSTAL_VOICE,
					"Enable InFX", 1);
		if (err < 0)
			return err;

		num_sliders = OUT_EFFECTS_COUNT - 1;
		for (i = 0; i < num_sliders; i++) {
			err = ca0132_alt_add_effect_slider(codec,
					    ca0132_effects[i].nid,
					    ca0132_effects[i].name,
					    ca0132_effects[i].direct);
			if (err < 0)
				return err;
		}

		err = ca0132_alt_add_effect_slider(codec, XBASS_XOVER,
					"X-Bass Crossover", EFX_DIR_OUT);

		if (err < 0)
			return err;
	} else {
		err = add_fx_switch(codec, PLAY_ENHANCEMENT,
					"PlayEnhancement", 0);
		if (err < 0)
			return err;

		err = add_fx_switch(codec, CRYSTAL_VOICE,
					"CrystalVoice", 1);
		if (err < 0)
			return err;
	}
	err = add_voicefx(codec);
	if (err < 0)
		return err;

	/*
	 * If the codec uses alt_functions, you need the enumerated controls
	 * to select the new outputs and inputs, plus add the new mic boost
	 * setting control.
	 */
	if (spec->use_alt_functions) {
		err = ca0132_alt_add_output_enum(codec);
		if (err < 0)
			return err;
		err = ca0132_alt_add_mic_boost_enum(codec);
		if (err < 0)
			return err;
		/*
		 * ZxR only has microphone input, there is no front panel
		 * header on the card, and aux-in is handled by the DBPro board.
		 */
		if (spec->quirk != QUIRK_ZXR) {
			err = ca0132_alt_add_input_enum(codec);
			if (err < 0)
				return err;
		}
	}

	if (spec->quirk == QUIRK_AE5) {
		err = ae5_add_headphone_gain_enum(codec);
		if (err < 0)
			return err;
		err = ae5_add_sound_filter_enum(codec);
		if (err < 0)
			return err;
	}

	if (spec->quirk == QUIRK_ZXR) {
		err = zxr_add_headphone_gain_switch(codec);
		if (err < 0)
			return err;
	}
#ifdef ENABLE_TUNING_CONTROLS
	add_tuning_ctls(codec);
#endif

	err = snd_hda_jack_add_kctls(codec, &spec->autocfg);
	if (err < 0)
		return err;

	if (spec->dig_out) {
		err = snd_hda_create_spdif_out_ctls(codec, spec->dig_out,
						    spec->dig_out);
		if (err < 0)
			return err;
		err = snd_hda_create_spdif_share_sw(codec, &spec->multiout);
		if (err < 0)
			return err;
		/* spec->multiout.share_spdif = 1; */
	}

	if (spec->dig_in) {
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in);
		if (err < 0)
			return err;
	}

	if (spec->use_alt_functions)
		ca0132_alt_add_chmap_ctls(codec);

	return 0;
}

static int dbpro_build_controls(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	int err = 0;

	if (spec->dig_out) {
		err = snd_hda_create_spdif_out_ctls(codec, spec->dig_out,
				spec->dig_out);
		if (err < 0)
			return err;
	}

	if (spec->dig_in) {
		err = snd_hda_create_spdif_in_ctls(codec, spec->dig_in);
		if (err < 0)
			return err;
	}

	return 0;
}

/*
 * PCM
 */
static const struct hda_pcm_stream ca0132_pcm_analog_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 6,
	.ops = {
		.prepare = ca0132_playback_pcm_prepare,
		.cleanup = ca0132_playback_pcm_cleanup,
		.get_delay = ca0132_playback_pcm_delay,
	},
};

static const struct hda_pcm_stream ca0132_pcm_analog_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.prepare = ca0132_capture_pcm_prepare,
		.cleanup = ca0132_capture_pcm_cleanup,
		.get_delay = ca0132_capture_pcm_delay,
	},
};

static const struct hda_pcm_stream ca0132_pcm_digital_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
	.ops = {
		.open = ca0132_dig_playback_pcm_open,
		.close = ca0132_dig_playback_pcm_close,
		.prepare = ca0132_dig_playback_pcm_prepare,
		.cleanup = ca0132_dig_playback_pcm_cleanup
	},
};

static const struct hda_pcm_stream ca0132_pcm_digital_capture = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};

static int ca0132_build_pcms(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	struct hda_pcm *info;

	info = snd_hda_codec_pcm_new(codec, "CA0132 Analog");
	if (!info)
		return -ENOMEM;
	if (spec->use_alt_functions) {
		info->own_chmap = true;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].chmap
			= ca0132_alt_chmaps;
	}
	info->stream[SNDRV_PCM_STREAM_PLAYBACK] = ca0132_pcm_analog_playback;
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->dacs[0];
	info->stream[SNDRV_PCM_STREAM_PLAYBACK].channels_max =
		spec->multiout.max_channels;
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = ca0132_pcm_analog_capture;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams = 1;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adcs[0];

	/* With the DSP enabled, desktops don't use this ADC. */
	if (!spec->use_alt_functions) {
		info = snd_hda_codec_pcm_new(codec, "CA0132 Analog Mic-In2");
		if (!info)
			return -ENOMEM;
		info->stream[SNDRV_PCM_STREAM_CAPTURE] =
			ca0132_pcm_analog_capture;
		info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams = 1;
		info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adcs[1];
	}

	info = snd_hda_codec_pcm_new(codec, "CA0132 What U Hear");
	if (!info)
		return -ENOMEM;
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = ca0132_pcm_analog_capture;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams = 1;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adcs[2];

	if (!spec->dig_out && !spec->dig_in)
		return 0;

	info = snd_hda_codec_pcm_new(codec, "CA0132 Digital");
	if (!info)
		return -ENOMEM;
	info->pcm_type = HDA_PCM_TYPE_SPDIF;
	if (spec->dig_out) {
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
			ca0132_pcm_digital_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->dig_out;
	}
	if (spec->dig_in) {
		info->stream[SNDRV_PCM_STREAM_CAPTURE] =
			ca0132_pcm_digital_capture;
		info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in;
	}

	return 0;
}

static int dbpro_build_pcms(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	struct hda_pcm *info;

	info = snd_hda_codec_pcm_new(codec, "CA0132 Alt Analog");
	if (!info)
		return -ENOMEM;
	info->stream[SNDRV_PCM_STREAM_CAPTURE] = ca0132_pcm_analog_capture;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].substreams = 1;
	info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->adcs[0];


	if (!spec->dig_out && !spec->dig_in)
		return 0;

	info = snd_hda_codec_pcm_new(codec, "CA0132 Digital");
	if (!info)
		return -ENOMEM;
	info->pcm_type = HDA_PCM_TYPE_SPDIF;
	if (spec->dig_out) {
		info->stream[SNDRV_PCM_STREAM_PLAYBACK] =
			ca0132_pcm_digital_playback;
		info->stream[SNDRV_PCM_STREAM_PLAYBACK].nid = spec->dig_out;
	}
	if (spec->dig_in) {
		info->stream[SNDRV_PCM_STREAM_CAPTURE] =
			ca0132_pcm_digital_capture;
		info->stream[SNDRV_PCM_STREAM_CAPTURE].nid = spec->dig_in;
	}

	return 0;
}

static void init_output(struct hda_codec *codec, hda_nid_t pin, hda_nid_t dac)
{
	if (pin) {
		snd_hda_set_pin_ctl(codec, pin, PIN_HP);
		if (get_wcaps(codec, pin) & AC_WCAP_OUT_AMP)
			snd_hda_codec_write(codec, pin, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_OUT_UNMUTE);
	}
	if (dac && (get_wcaps(codec, dac) & AC_WCAP_OUT_AMP))
		snd_hda_codec_write(codec, dac, 0,
				    AC_VERB_SET_AMP_GAIN_MUTE, AMP_OUT_ZERO);
}

static void init_input(struct hda_codec *codec, hda_nid_t pin, hda_nid_t adc)
{
	if (pin) {
		snd_hda_set_pin_ctl(codec, pin, PIN_VREF80);
		if (get_wcaps(codec, pin) & AC_WCAP_IN_AMP)
			snd_hda_codec_write(codec, pin, 0,
					    AC_VERB_SET_AMP_GAIN_MUTE,
					    AMP_IN_UNMUTE(0));
	}
	if (adc && (get_wcaps(codec, adc) & AC_WCAP_IN_AMP)) {
		snd_hda_codec_write(codec, adc, 0, AC_VERB_SET_AMP_GAIN_MUTE,
				    AMP_IN_UNMUTE(0));

		/* init to 0 dB and unmute. */
		snd_hda_codec_amp_stereo(codec, adc, HDA_INPUT, 0,
					 HDA_AMP_VOLMASK, 0x5a);
		snd_hda_codec_amp_stereo(codec, adc, HDA_INPUT, 0,
					 HDA_AMP_MUTE, 0);
	}
}

static void refresh_amp_caps(struct hda_codec *codec, hda_nid_t nid, int dir)
{
	unsigned int caps;

	caps = snd_hda_param_read(codec, nid, dir == HDA_OUTPUT ?
				  AC_PAR_AMP_OUT_CAP : AC_PAR_AMP_IN_CAP);
	snd_hda_override_amp_caps(codec, nid, dir, caps);
}

/*
 * Switch between Digital built-in mic and analog mic.
 */
static void ca0132_set_dmic(struct hda_codec *codec, int enable)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;
	u8 val;
	unsigned int oldval;

	codec_dbg(codec, "ca0132_set_dmic: enable=%d\n", enable);

	oldval = stop_mic1(codec);
	ca0132_set_vipsource(codec, 0);
	if (enable) {
		/* set DMic input as 2-ch */
		tmp = FLOAT_TWO;
		dspio_set_uint_param(codec, 0x80, 0x00, tmp);

		val = spec->dmic_ctl;
		val |= 0x80;
		snd_hda_codec_write(codec, spec->input_pins[0], 0,
				    VENDOR_CHIPIO_DMIC_CTL_SET, val);

		if (!(spec->dmic_ctl & 0x20))
			chipio_set_control_flag(codec, CONTROL_FLAG_DMIC, 1);
	} else {
		/* set AMic input as mono */
		tmp = FLOAT_ONE;
		dspio_set_uint_param(codec, 0x80, 0x00, tmp);

		val = spec->dmic_ctl;
		/* clear bit7 and bit5 to disable dmic */
		val &= 0x5f;
		snd_hda_codec_write(codec, spec->input_pins[0], 0,
				    VENDOR_CHIPIO_DMIC_CTL_SET, val);

		if (!(spec->dmic_ctl & 0x20))
			chipio_set_control_flag(codec, CONTROL_FLAG_DMIC, 0);
	}
	ca0132_set_vipsource(codec, 1);
	resume_mic1(codec, oldval);
}

/*
 * Initialization for Digital Mic.
 */
static void ca0132_init_dmic(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	u8 val;

	/* Setup Digital Mic here, but don't enable.
	 * Enable based on jack detect.
	 */

	/* MCLK uses MPIO1, set to enable.
	 * Bit 2-0: MPIO select
	 * Bit   3: set to disable
	 * Bit 7-4: reserved
	 */
	val = 0x01;
	snd_hda_codec_write(codec, spec->input_pins[0], 0,
			    VENDOR_CHIPIO_DMIC_MCLK_SET, val);

	/* Data1 uses MPIO3. Data2 not use
	 * Bit 2-0: Data1 MPIO select
	 * Bit   3: set disable Data1
	 * Bit 6-4: Data2 MPIO select
	 * Bit   7: set disable Data2
	 */
	val = 0x83;
	snd_hda_codec_write(codec, spec->input_pins[0], 0,
			    VENDOR_CHIPIO_DMIC_PIN_SET, val);

	/* Use Ch-0 and Ch-1. Rate is 48K, mode 1. Disable DMic first.
	 * Bit 3-0: Channel mask
	 * Bit   4: set for 48KHz, clear for 32KHz
	 * Bit   5: mode
	 * Bit   6: set to select Data2, clear for Data1
	 * Bit   7: set to enable DMic, clear for AMic
	 */
	if (spec->quirk == QUIRK_ALIENWARE_M17XR4)
		val = 0x33;
	else
		val = 0x23;
	/* keep a copy of dmic ctl val for enable/disable dmic purpuse */
	spec->dmic_ctl = val;
	snd_hda_codec_write(codec, spec->input_pins[0], 0,
			    VENDOR_CHIPIO_DMIC_CTL_SET, val);
}

/*
 * Initialization for Analog Mic 2
 */
static void ca0132_init_analog_mic2(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	mutex_lock(&spec->chipio_mutex);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x20);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_HIGH, 0x19);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_DATA_WRITE, 0x00);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x2D);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_HIGH, 0x19);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_DATA_WRITE, 0x00);
	mutex_unlock(&spec->chipio_mutex);
}

static void ca0132_refresh_widget_caps(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	int i;

	codec_dbg(codec, "ca0132_refresh_widget_caps.\n");
	snd_hda_codec_update_widgets(codec);

	for (i = 0; i < spec->multiout.num_dacs; i++)
		refresh_amp_caps(codec, spec->dacs[i], HDA_OUTPUT);

	for (i = 0; i < spec->num_outputs; i++)
		refresh_amp_caps(codec, spec->out_pins[i], HDA_OUTPUT);

	for (i = 0; i < spec->num_inputs; i++) {
		refresh_amp_caps(codec, spec->adcs[i], HDA_INPUT);
		refresh_amp_caps(codec, spec->input_pins[i], HDA_INPUT);
	}
}

/*
 * Creates a dummy stream to bind the output to. This seems to have to be done
 * after changing the main outputs source and destination streams.
 */
static void ca0132_alt_create_dummy_stream(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int stream_format;

	stream_format = snd_hdac_calc_stream_format(48000, 2,
			SNDRV_PCM_FORMAT_S32_LE, 32, 0);

	snd_hda_codec_setup_stream(codec, spec->dacs[0], spec->dsp_stream_id,
					0, stream_format);

	snd_hda_codec_cleanup_stream(codec, spec->dacs[0]);
}

/*
 * Initialize mic for non-chromebook ca0132 implementations.
 */
static void ca0132_alt_init_analog_mics(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;

	/* Mic 1 Setup */
	chipio_set_conn_rate(codec, MEM_CONNID_MICIN1, SR_96_000);
	chipio_set_conn_rate(codec, MEM_CONNID_MICOUT1, SR_96_000);
	if (spec->quirk == QUIRK_R3DI) {
		chipio_set_conn_rate(codec, 0x0F, SR_96_000);
		tmp = FLOAT_ONE;
	} else
		tmp = FLOAT_THREE;
	dspio_set_uint_param(codec, 0x80, 0x00, tmp);

	/* Mic 2 setup (not present on desktop cards) */
	chipio_set_conn_rate(codec, MEM_CONNID_MICIN2, SR_96_000);
	chipio_set_conn_rate(codec, MEM_CONNID_MICOUT2, SR_96_000);
	if (spec->quirk == QUIRK_R3DI)
		chipio_set_conn_rate(codec, 0x0F, SR_96_000);
	tmp = FLOAT_ZERO;
	dspio_set_uint_param(codec, 0x80, 0x01, tmp);
}

/*
 * Sets the source of stream 0x14 to connpointID 0x48, and the destination
 * connpointID to 0x91. If this isn't done, the destination is 0x71, and
 * you get no sound. I'm guessing this has to do with the Sound Blaster Z
 * having an updated DAC, which changes the destination to that DAC.
 */
static void sbz_connect_streams(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	mutex_lock(&spec->chipio_mutex);

	codec_dbg(codec, "Connect Streams entered, mutex locked and loaded.\n");

	chipio_set_stream_channels(codec, 0x0C, 6);
	chipio_set_stream_control(codec, 0x0C, 1);

	/* This value is 0x43 for 96khz, and 0x83 for 192khz. */
	chipio_write_no_mutex(codec, 0x18a020, 0x00000043);

	/* Setup stream 0x14 with it's source and destination points */
	chipio_set_stream_source_dest(codec, 0x14, 0x48, 0x91);
	chipio_set_conn_rate_no_mutex(codec, 0x48, SR_96_000);
	chipio_set_conn_rate_no_mutex(codec, 0x91, SR_96_000);
	chipio_set_stream_channels(codec, 0x14, 2);
	chipio_set_stream_control(codec, 0x14, 1);

	codec_dbg(codec, "Connect Streams exited, mutex released.\n");

	mutex_unlock(&spec->chipio_mutex);
}

/*
 * Write data through ChipIO to setup proper stream destinations.
 * Not sure how it exactly works, but it seems to direct data
 * to different destinations. Example is f8 to c0, e0 to c0.
 * All I know is, if you don't set these, you get no sound.
 */
static void sbz_chipio_startup_data(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	mutex_lock(&spec->chipio_mutex);
	codec_dbg(codec, "Startup Data entered, mutex locked and loaded.\n");

	/* These control audio output */
	chipio_write_no_mutex(codec, 0x190060, 0x0001f8c0);
	chipio_write_no_mutex(codec, 0x190064, 0x0001f9c1);
	chipio_write_no_mutex(codec, 0x190068, 0x0001fac6);
	chipio_write_no_mutex(codec, 0x19006c, 0x0001fbc7);
	/* Signal to update I think */
	chipio_write_no_mutex(codec, 0x19042c, 0x00000001);

	chipio_set_stream_channels(codec, 0x0C, 6);
	chipio_set_stream_control(codec, 0x0C, 1);
	/* No clue what these control */
	if (spec->quirk == QUIRK_SBZ) {
		chipio_write_no_mutex(codec, 0x190030, 0x0001e0c0);
		chipio_write_no_mutex(codec, 0x190034, 0x0001e1c1);
		chipio_write_no_mutex(codec, 0x190038, 0x0001e4c2);
		chipio_write_no_mutex(codec, 0x19003c, 0x0001e5c3);
		chipio_write_no_mutex(codec, 0x190040, 0x0001e2c4);
		chipio_write_no_mutex(codec, 0x190044, 0x0001e3c5);
		chipio_write_no_mutex(codec, 0x190048, 0x0001e8c6);
		chipio_write_no_mutex(codec, 0x19004c, 0x0001e9c7);
		chipio_write_no_mutex(codec, 0x190050, 0x0001ecc8);
		chipio_write_no_mutex(codec, 0x190054, 0x0001edc9);
		chipio_write_no_mutex(codec, 0x190058, 0x0001eaca);
		chipio_write_no_mutex(codec, 0x19005c, 0x0001ebcb);
	} else if (spec->quirk == QUIRK_ZXR) {
		chipio_write_no_mutex(codec, 0x190038, 0x000140c2);
		chipio_write_no_mutex(codec, 0x19003c, 0x000141c3);
		chipio_write_no_mutex(codec, 0x190040, 0x000150c4);
		chipio_write_no_mutex(codec, 0x190044, 0x000151c5);
		chipio_write_no_mutex(codec, 0x190050, 0x000142c8);
		chipio_write_no_mutex(codec, 0x190054, 0x000143c9);
		chipio_write_no_mutex(codec, 0x190058, 0x000152ca);
		chipio_write_no_mutex(codec, 0x19005c, 0x000153cb);
	}
	chipio_write_no_mutex(codec, 0x19042c, 0x00000001);

	codec_dbg(codec, "Startup Data exited, mutex released.\n");
	mutex_unlock(&spec->chipio_mutex);
}

/*
 * Custom DSP SCP commands where the src value is 0x00 instead of 0x20. This is
 * done after the DSP is loaded.
 */
static void ca0132_alt_dsp_scp_startup(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp, i;

	/*
	 * Gotta run these twice, or else mic works inconsistently. Not clear
	 * why this is, but multiple tests have confirmed it.
	 */
	for (i = 0; i < 2; i++) {
		switch (spec->quirk) {
		case QUIRK_SBZ:
		case QUIRK_AE5:
			tmp = 0x00000003;
			dspio_set_uint_param_no_source(codec, 0x80, 0x0C, tmp);
			tmp = 0x00000000;
			dspio_set_uint_param_no_source(codec, 0x80, 0x0A, tmp);
			tmp = 0x00000001;
			dspio_set_uint_param_no_source(codec, 0x80, 0x0B, tmp);
			tmp = 0x00000004;
			dspio_set_uint_param_no_source(codec, 0x80, 0x0C, tmp);
			tmp = 0x00000005;
			dspio_set_uint_param_no_source(codec, 0x80, 0x0C, tmp);
			tmp = 0x00000000;
			dspio_set_uint_param_no_source(codec, 0x80, 0x0C, tmp);
			break;
		case QUIRK_R3D:
		case QUIRK_R3DI:
			tmp = 0x00000000;
			dspio_set_uint_param_no_source(codec, 0x80, 0x0A, tmp);
			tmp = 0x00000001;
			dspio_set_uint_param_no_source(codec, 0x80, 0x0B, tmp);
			tmp = 0x00000004;
			dspio_set_uint_param_no_source(codec, 0x80, 0x0C, tmp);
			tmp = 0x00000005;
			dspio_set_uint_param_no_source(codec, 0x80, 0x0C, tmp);
			tmp = 0x00000000;
			dspio_set_uint_param_no_source(codec, 0x80, 0x0C, tmp);
			break;
		}
		msleep(100);
	}
}

static void ca0132_alt_dsp_initial_mic_setup(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;

	chipio_set_stream_control(codec, 0x03, 0);
	chipio_set_stream_control(codec, 0x04, 0);

	chipio_set_conn_rate(codec, MEM_CONNID_MICIN1, SR_96_000);
	chipio_set_conn_rate(codec, MEM_CONNID_MICOUT1, SR_96_000);

	tmp = FLOAT_THREE;
	dspio_set_uint_param(codec, 0x80, 0x00, tmp);

	chipio_set_stream_control(codec, 0x03, 1);
	chipio_set_stream_control(codec, 0x04, 1);

	switch (spec->quirk) {
	case QUIRK_SBZ:
		chipio_write(codec, 0x18b098, 0x0000000c);
		chipio_write(codec, 0x18b09C, 0x0000000c);
		break;
	case QUIRK_AE5:
		chipio_write(codec, 0x18b098, 0x0000000c);
		chipio_write(codec, 0x18b09c, 0x0000004c);
		break;
	}
}

static void ae5_post_dsp_register_set(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	chipio_8051_write_direct(codec, 0x93, 0x10);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x44);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0xc2);

	writeb(0xff, spec->mem_base + 0x304);
	writeb(0xff, spec->mem_base + 0x304);
	writeb(0xff, spec->mem_base + 0x304);
	writeb(0xff, spec->mem_base + 0x304);
	writeb(0x00, spec->mem_base + 0x100);
	writeb(0xff, spec->mem_base + 0x304);
	writeb(0x00, spec->mem_base + 0x100);
	writeb(0xff, spec->mem_base + 0x304);
	writeb(0x00, spec->mem_base + 0x100);
	writeb(0xff, spec->mem_base + 0x304);
	writeb(0x00, spec->mem_base + 0x100);
	writeb(0xff, spec->mem_base + 0x304);

	ca0113_mmio_command_set(codec, 0x30, 0x2b, 0x3f);
	ca0113_mmio_command_set(codec, 0x30, 0x2d, 0x3f);
	ca0113_mmio_command_set(codec, 0x48, 0x07, 0x83);
}

static void ae5_post_dsp_param_setup(struct hda_codec *codec)
{
	/*
	 * Param3 in the 8051's memory is represented by the ascii string 'mch'
	 * which seems to be 'multichannel'. This is also mentioned in the
	 * AE-5's registry values in Windows.
	 */
	chipio_set_control_param(codec, 3, 0);
	/*
	 * I believe ASI is 'audio serial interface' and that it's used to
	 * change colors on the external LED strip connected to the AE-5.
	 */
	chipio_set_control_flag(codec, CONTROL_FLAG_ASI_96KHZ, 1);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0, 0x724, 0x83);
	chipio_set_control_param(codec, CONTROL_PARAM_ASI, 0);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x92);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_HIGH, 0xfa);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_DATA_WRITE, 0x22);
}

static void ae5_post_dsp_pll_setup(struct hda_codec *codec)
{
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x41);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0xc8);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x45);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0xcc);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x40);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0xcb);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x43);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0xc7);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x51);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0x8d);
}

static void ae5_post_dsp_stream_setup(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	mutex_lock(&spec->chipio_mutex);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0, 0x725, 0x81);

	chipio_set_conn_rate_no_mutex(codec, 0x70, SR_96_000);

	chipio_set_stream_channels(codec, 0x0C, 6);
	chipio_set_stream_control(codec, 0x0C, 1);

	chipio_set_stream_source_dest(codec, 0x5, 0x43, 0x0);

	chipio_set_stream_source_dest(codec, 0x18, 0x9, 0xd0);
	chipio_set_conn_rate_no_mutex(codec, 0xd0, SR_96_000);
	chipio_set_stream_channels(codec, 0x18, 6);
	chipio_set_stream_control(codec, 0x18, 1);

	chipio_set_control_param_no_mutex(codec, CONTROL_PARAM_ASI, 4);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x43);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0xc7);

	ca0113_mmio_command_set(codec, 0x48, 0x01, 0x80);

	mutex_unlock(&spec->chipio_mutex);
}

static void ae5_post_dsp_startup_data(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	mutex_lock(&spec->chipio_mutex);

	chipio_write_no_mutex(codec, 0x189000, 0x0001f101);
	chipio_write_no_mutex(codec, 0x189004, 0x0001f101);
	chipio_write_no_mutex(codec, 0x189024, 0x00014004);
	chipio_write_no_mutex(codec, 0x189028, 0x0002000f);

	ca0113_mmio_command_set(codec, 0x48, 0x0a, 0x05);
	chipio_set_control_param_no_mutex(codec, CONTROL_PARAM_ASI, 7);
	ca0113_mmio_command_set(codec, 0x48, 0x0b, 0x12);
	ca0113_mmio_command_set(codec, 0x48, 0x04, 0x00);
	ca0113_mmio_command_set(codec, 0x48, 0x06, 0x48);
	ca0113_mmio_command_set(codec, 0x48, 0x0a, 0x05);
	ca0113_mmio_command_set(codec, 0x48, 0x07, 0x83);
	ca0113_mmio_command_set(codec, 0x48, 0x0f, 0x00);
	ca0113_mmio_command_set(codec, 0x48, 0x10, 0x00);
	ca0113_mmio_gpio_set(codec, 0, true);
	ca0113_mmio_gpio_set(codec, 1, true);
	ca0113_mmio_command_set(codec, 0x48, 0x07, 0x80);

	chipio_write_no_mutex(codec, 0x18b03c, 0x00000012);

	ca0113_mmio_command_set(codec, 0x48, 0x0f, 0x00);
	ca0113_mmio_command_set(codec, 0x48, 0x10, 0x00);

	mutex_unlock(&spec->chipio_mutex);
}

/*
 * Setup default parameters for DSP
 */
static void ca0132_setup_defaults(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;
	int num_fx;
	int idx, i;

	if (spec->dsp_state != DSP_DOWNLOADED)
		return;

	/* out, in effects + voicefx */
	num_fx = OUT_EFFECTS_COUNT + IN_EFFECTS_COUNT + 1;
	for (idx = 0; idx < num_fx; idx++) {
		for (i = 0; i <= ca0132_effects[idx].params; i++) {
			dspio_set_uint_param(codec, ca0132_effects[idx].mid,
					     ca0132_effects[idx].reqs[i],
					     ca0132_effects[idx].def_vals[i]);
		}
	}

	/*remove DSP headroom*/
	tmp = FLOAT_ZERO;
	dspio_set_uint_param(codec, 0x96, 0x3C, tmp);

	/*set speaker EQ bypass attenuation*/
	dspio_set_uint_param(codec, 0x8f, 0x01, tmp);

	/* set AMic1 and AMic2 as mono mic */
	tmp = FLOAT_ONE;
	dspio_set_uint_param(codec, 0x80, 0x00, tmp);
	dspio_set_uint_param(codec, 0x80, 0x01, tmp);

	/* set AMic1 as CrystalVoice input */
	tmp = FLOAT_ONE;
	dspio_set_uint_param(codec, 0x80, 0x05, tmp);

	/* set WUH source */
	tmp = FLOAT_TWO;
	dspio_set_uint_param(codec, 0x31, 0x00, tmp);
}

/*
 * Setup default parameters for Recon3D/Recon3Di DSP.
 */

static void r3d_setup_defaults(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;
	int num_fx;
	int idx, i;

	if (spec->dsp_state != DSP_DOWNLOADED)
		return;

	ca0132_alt_dsp_scp_startup(codec);
	ca0132_alt_init_analog_mics(codec);

	/*remove DSP headroom*/
	tmp = FLOAT_ZERO;
	dspio_set_uint_param(codec, 0x96, 0x3C, tmp);

	/* set WUH source */
	tmp = FLOAT_TWO;
	dspio_set_uint_param(codec, 0x31, 0x00, tmp);
	chipio_set_conn_rate(codec, MEM_CONNID_WUH, SR_48_000);

	/* Set speaker source? */
	dspio_set_uint_param(codec, 0x32, 0x00, tmp);

	if (spec->quirk == QUIRK_R3DI)
		r3di_gpio_dsp_status_set(codec, R3DI_DSP_DOWNLOADED);

	/* Setup effect defaults */
	num_fx = OUT_EFFECTS_COUNT + IN_EFFECTS_COUNT + 1;
	for (idx = 0; idx < num_fx; idx++) {
		for (i = 0; i <= ca0132_effects[idx].params; i++) {
			dspio_set_uint_param(codec,
					ca0132_effects[idx].mid,
					ca0132_effects[idx].reqs[i],
					ca0132_effects[idx].def_vals[i]);
		}
	}
}

/*
 * Setup default parameters for the Sound Blaster Z DSP. A lot more going on
 * than the Chromebook setup.
 */
static void sbz_setup_defaults(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;
	int num_fx;
	int idx, i;

	if (spec->dsp_state != DSP_DOWNLOADED)
		return;

	ca0132_alt_dsp_scp_startup(codec);
	ca0132_alt_init_analog_mics(codec);
	sbz_connect_streams(codec);
	sbz_chipio_startup_data(codec);

	chipio_set_stream_control(codec, 0x03, 1);
	chipio_set_stream_control(codec, 0x04, 1);

	/*
	 * Sets internal input loopback to off, used to have a switch to
	 * enable input loopback, but turned out to be way too buggy.
	 */
	tmp = FLOAT_ONE;
	dspio_set_uint_param(codec, 0x37, 0x08, tmp);
	dspio_set_uint_param(codec, 0x37, 0x10, tmp);

	/*remove DSP headroom*/
	tmp = FLOAT_ZERO;
	dspio_set_uint_param(codec, 0x96, 0x3C, tmp);

	/* set WUH source */
	tmp = FLOAT_TWO;
	dspio_set_uint_param(codec, 0x31, 0x00, tmp);
	chipio_set_conn_rate(codec, MEM_CONNID_WUH, SR_48_000);

	/* Set speaker source? */
	dspio_set_uint_param(codec, 0x32, 0x00, tmp);

	ca0132_alt_dsp_initial_mic_setup(codec);

	/* out, in effects + voicefx */
	num_fx = OUT_EFFECTS_COUNT + IN_EFFECTS_COUNT + 1;
	for (idx = 0; idx < num_fx; idx++) {
		for (i = 0; i <= ca0132_effects[idx].params; i++) {
			dspio_set_uint_param(codec,
					ca0132_effects[idx].mid,
					ca0132_effects[idx].reqs[i],
					ca0132_effects[idx].def_vals[i]);
		}
	}

	ca0132_alt_create_dummy_stream(codec);
}

/*
 * Setup default parameters for the Sound BlasterX AE-5 DSP.
 */
static void ae5_setup_defaults(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int tmp;
	int num_fx;
	int idx, i;

	if (spec->dsp_state != DSP_DOWNLOADED)
		return;

	ca0132_alt_dsp_scp_startup(codec);
	ca0132_alt_init_analog_mics(codec);
	chipio_set_stream_control(codec, 0x03, 1);
	chipio_set_stream_control(codec, 0x04, 1);

	/* New, unknown SCP req's */
	tmp = FLOAT_ZERO;
	dspio_set_uint_param(codec, 0x96, 0x29, tmp);
	dspio_set_uint_param(codec, 0x96, 0x2a, tmp);
	dspio_set_uint_param(codec, 0x80, 0x0d, tmp);
	dspio_set_uint_param(codec, 0x80, 0x0e, tmp);

	ca0113_mmio_command_set(codec, 0x30, 0x2e, 0x3f);
	ca0113_mmio_gpio_set(codec, 0, false);
	ca0113_mmio_command_set(codec, 0x30, 0x28, 0x00);

	/* Internal loopback off */
	tmp = FLOAT_ONE;
	dspio_set_uint_param(codec, 0x37, 0x08, tmp);
	dspio_set_uint_param(codec, 0x37, 0x10, tmp);

	/*remove DSP headroom*/
	tmp = FLOAT_ZERO;
	dspio_set_uint_param(codec, 0x96, 0x3C, tmp);

	/* set WUH source */
	tmp = FLOAT_TWO;
	dspio_set_uint_param(codec, 0x31, 0x00, tmp);
	chipio_set_conn_rate(codec, MEM_CONNID_WUH, SR_48_000);

	/* Set speaker source? */
	dspio_set_uint_param(codec, 0x32, 0x00, tmp);

	ca0132_alt_dsp_initial_mic_setup(codec);
	ae5_post_dsp_register_set(codec);
	ae5_post_dsp_param_setup(codec);
	ae5_post_dsp_pll_setup(codec);
	ae5_post_dsp_stream_setup(codec);
	ae5_post_dsp_startup_data(codec);

	/* out, in effects + voicefx */
	num_fx = OUT_EFFECTS_COUNT + IN_EFFECTS_COUNT + 1;
	for (idx = 0; idx < num_fx; idx++) {
		for (i = 0; i <= ca0132_effects[idx].params; i++) {
			dspio_set_uint_param(codec,
					ca0132_effects[idx].mid,
					ca0132_effects[idx].reqs[i],
					ca0132_effects[idx].def_vals[i]);
		}
	}

	ca0132_alt_create_dummy_stream(codec);
}

/*
 * Initialization of flags in chip
 */
static void ca0132_init_flags(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	if (spec->use_alt_functions) {
		chipio_set_control_flag(codec, CONTROL_FLAG_DSP_96KHZ, 1);
		chipio_set_control_flag(codec, CONTROL_FLAG_DAC_96KHZ, 1);
		chipio_set_control_flag(codec, CONTROL_FLAG_ADC_B_96KHZ, 1);
		chipio_set_control_flag(codec, CONTROL_FLAG_ADC_C_96KHZ, 1);
		chipio_set_control_flag(codec, CONTROL_FLAG_SRC_RATE_96KHZ, 1);
		chipio_set_control_flag(codec, CONTROL_FLAG_IDLE_ENABLE, 0);
		chipio_set_control_flag(codec, CONTROL_FLAG_SPDIF2OUT, 0);
		chipio_set_control_flag(codec,
				CONTROL_FLAG_PORT_D_10KOHM_LOAD, 0);
		chipio_set_control_flag(codec,
				CONTROL_FLAG_PORT_A_10KOHM_LOAD, 1);
	} else {
		chipio_set_control_flag(codec, CONTROL_FLAG_IDLE_ENABLE, 0);
		chipio_set_control_flag(codec,
				CONTROL_FLAG_PORT_A_COMMON_MODE, 0);
		chipio_set_control_flag(codec,
				CONTROL_FLAG_PORT_D_COMMON_MODE, 0);
		chipio_set_control_flag(codec,
				CONTROL_FLAG_PORT_A_10KOHM_LOAD, 0);
		chipio_set_control_flag(codec,
				CONTROL_FLAG_PORT_D_10KOHM_LOAD, 0);
		chipio_set_control_flag(codec, CONTROL_FLAG_ADC_C_HIGH_PASS, 1);
	}
}

/*
 * Initialization of parameters in chip
 */
static void ca0132_init_params(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	if (spec->use_alt_functions) {
		chipio_set_conn_rate(codec, MEM_CONNID_WUH, SR_48_000);
		chipio_set_conn_rate(codec, 0x0B, SR_48_000);
		chipio_set_control_param(codec, CONTROL_PARAM_SPDIF1_SOURCE, 0);
		chipio_set_control_param(codec, 0, 0);
		chipio_set_control_param(codec, CONTROL_PARAM_VIP_SOURCE, 0);
	}

	chipio_set_control_param(codec, CONTROL_PARAM_PORTA_160OHM_GAIN, 6);
	chipio_set_control_param(codec, CONTROL_PARAM_PORTD_160OHM_GAIN, 6);
}

static void ca0132_set_dsp_msr(struct hda_codec *codec, bool is96k)
{
	chipio_set_control_flag(codec, CONTROL_FLAG_DSP_96KHZ, is96k);
	chipio_set_control_flag(codec, CONTROL_FLAG_DAC_96KHZ, is96k);
	chipio_set_control_flag(codec, CONTROL_FLAG_SRC_RATE_96KHZ, is96k);
	chipio_set_control_flag(codec, CONTROL_FLAG_SRC_CLOCK_196MHZ, is96k);
	chipio_set_control_flag(codec, CONTROL_FLAG_ADC_B_96KHZ, is96k);
	chipio_set_control_flag(codec, CONTROL_FLAG_ADC_C_96KHZ, is96k);

	chipio_set_conn_rate(codec, MEM_CONNID_MICIN1, SR_96_000);
	chipio_set_conn_rate(codec, MEM_CONNID_MICOUT1, SR_96_000);
	chipio_set_conn_rate(codec, MEM_CONNID_WUH, SR_48_000);
}

static bool ca0132_download_dsp_images(struct hda_codec *codec)
{
	bool dsp_loaded = false;
	struct ca0132_spec *spec = codec->spec;
	const struct dsp_image_seg *dsp_os_image;
	const struct firmware *fw_entry;
	/*
	 * Alternate firmwares for different variants. The Recon3Di apparently
	 * can use the default firmware, but I'll leave the option in case
	 * it needs it again.
	 */
	switch (spec->quirk) {
	case QUIRK_SBZ:
	case QUIRK_R3D:
	case QUIRK_AE5:
		if (request_firmware(&fw_entry, DESKTOP_EFX_FILE,
					codec->card->dev) != 0) {
			codec_dbg(codec, "Desktop firmware not found.");
			spec->alt_firmware_present = false;
		} else {
			codec_dbg(codec, "Desktop firmware selected.");
			spec->alt_firmware_present = true;
		}
		break;
	case QUIRK_R3DI:
		if (request_firmware(&fw_entry, R3DI_EFX_FILE,
					codec->card->dev) != 0) {
			codec_dbg(codec, "Recon3Di alt firmware not detected.");
			spec->alt_firmware_present = false;
		} else {
			codec_dbg(codec, "Recon3Di firmware selected.");
			spec->alt_firmware_present = true;
		}
		break;
	default:
		spec->alt_firmware_present = false;
		break;
	}
	/*
	 * Use default ctefx.bin if no alt firmware is detected, or if none
	 * exists for your particular codec.
	 */
	if (!spec->alt_firmware_present) {
		codec_dbg(codec, "Default firmware selected.");
		if (request_firmware(&fw_entry, EFX_FILE,
					codec->card->dev) != 0)
			return false;
	}

	dsp_os_image = (struct dsp_image_seg *)(fw_entry->data);
	if (dspload_image(codec, dsp_os_image, 0, 0, true, 0)) {
		codec_err(codec, "ca0132 DSP load image failed\n");
		goto exit_download;
	}

	dsp_loaded = dspload_wait_loaded(codec);

exit_download:
	release_firmware(fw_entry);

	return dsp_loaded;
}

static void ca0132_download_dsp(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

#ifndef CONFIG_SND_HDA_CODEC_CA0132_DSP
	return; /* NOP */
#endif

	if (spec->dsp_state == DSP_DOWNLOAD_FAILED)
		return; /* don't retry failures */

	chipio_enable_clocks(codec);
	if (spec->dsp_state != DSP_DOWNLOADED) {
		spec->dsp_state = DSP_DOWNLOADING;

		if (!ca0132_download_dsp_images(codec))
			spec->dsp_state = DSP_DOWNLOAD_FAILED;
		else
			spec->dsp_state = DSP_DOWNLOADED;
	}

	/* For codecs using alt functions, this is already done earlier */
	if (spec->dsp_state == DSP_DOWNLOADED && (!spec->use_alt_functions))
		ca0132_set_dsp_msr(codec, true);
}

static void ca0132_process_dsp_response(struct hda_codec *codec,
					struct hda_jack_callback *callback)
{
	struct ca0132_spec *spec = codec->spec;

	codec_dbg(codec, "ca0132_process_dsp_response\n");
	if (spec->wait_scp) {
		if (dspio_get_response_data(codec) >= 0)
			spec->wait_scp = 0;
	}

	dspio_clear_response_queue(codec);
}

static void hp_callback(struct hda_codec *codec, struct hda_jack_callback *cb)
{
	struct ca0132_spec *spec = codec->spec;
	struct hda_jack_tbl *tbl;

	/* Delay enabling the HP amp, to let the mic-detection
	 * state machine run.
	 */
	cancel_delayed_work_sync(&spec->unsol_hp_work);
	schedule_delayed_work(&spec->unsol_hp_work, msecs_to_jiffies(500));
	tbl = snd_hda_jack_tbl_get(codec, cb->nid);
	if (tbl)
		tbl->block_report = 1;
}

static void amic_callback(struct hda_codec *codec, struct hda_jack_callback *cb)
{
	struct ca0132_spec *spec = codec->spec;

	if (spec->use_alt_functions)
		ca0132_alt_select_in(codec);
	else
		ca0132_select_mic(codec);
}

static void ca0132_init_unsol(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	snd_hda_jack_detect_enable_callback(codec, spec->unsol_tag_hp, hp_callback);
	snd_hda_jack_detect_enable_callback(codec, spec->unsol_tag_amic1,
					    amic_callback);
	snd_hda_jack_detect_enable_callback(codec, UNSOL_TAG_DSP,
					    ca0132_process_dsp_response);
	/* Front headphone jack detection */
	if (spec->use_alt_functions)
		snd_hda_jack_detect_enable_callback(codec,
			spec->unsol_tag_front_hp, hp_callback);
}

/*
 * Verbs tables.
 */

/* Sends before DSP download. */
static struct hda_verb ca0132_base_init_verbs[] = {
	/*enable ct extension*/
	{0x15, VENDOR_CHIPIO_CT_EXTENSIONS_ENABLE, 0x1},
	{}
};

/* Send at exit. */
static struct hda_verb ca0132_base_exit_verbs[] = {
	/*set afg to D3*/
	{0x01, AC_VERB_SET_POWER_STATE, 0x03},
	/*disable ct extension*/
	{0x15, VENDOR_CHIPIO_CT_EXTENSIONS_ENABLE, 0},
	{}
};

/* Other verbs tables. Sends after DSP download. */

static struct hda_verb ca0132_init_verbs0[] = {
	/* chip init verbs */
	{0x15, 0x70D, 0xF0},
	{0x15, 0x70E, 0xFE},
	{0x15, 0x707, 0x75},
	{0x15, 0x707, 0xD3},
	{0x15, 0x707, 0x09},
	{0x15, 0x707, 0x53},
	{0x15, 0x707, 0xD4},
	{0x15, 0x707, 0xEF},
	{0x15, 0x707, 0x75},
	{0x15, 0x707, 0xD3},
	{0x15, 0x707, 0x09},
	{0x15, 0x707, 0x02},
	{0x15, 0x707, 0x37},
	{0x15, 0x707, 0x78},
	{0x15, 0x53C, 0xCE},
	{0x15, 0x575, 0xC9},
	{0x15, 0x53D, 0xCE},
	{0x15, 0x5B7, 0xC9},
	{0x15, 0x70D, 0xE8},
	{0x15, 0x70E, 0xFE},
	{0x15, 0x707, 0x02},
	{0x15, 0x707, 0x68},
	{0x15, 0x707, 0x62},
	{0x15, 0x53A, 0xCE},
	{0x15, 0x546, 0xC9},
	{0x15, 0x53B, 0xCE},
	{0x15, 0x5E8, 0xC9},
	{}
};

/* Extra init verbs for desktop cards. */
static struct hda_verb ca0132_init_verbs1[] = {
	{0x15, 0x70D, 0x20},
	{0x15, 0x70E, 0x19},
	{0x15, 0x707, 0x00},
	{0x15, 0x539, 0xCE},
	{0x15, 0x546, 0xC9},
	{0x15, 0x70D, 0xB7},
	{0x15, 0x70E, 0x09},
	{0x15, 0x707, 0x10},
	{0x15, 0x70D, 0xAF},
	{0x15, 0x70E, 0x09},
	{0x15, 0x707, 0x01},
	{0x15, 0x707, 0x05},
	{0x15, 0x70D, 0x73},
	{0x15, 0x70E, 0x09},
	{0x15, 0x707, 0x14},
	{0x15, 0x6FF, 0xC4},
	{}
};

static void ca0132_init_chip(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	int num_fx;
	int i;
	unsigned int on;

	mutex_init(&spec->chipio_mutex);

	spec->cur_out_type = SPEAKER_OUT;
	if (!spec->use_alt_functions)
		spec->cur_mic_type = DIGITAL_MIC;
	else
		spec->cur_mic_type = REAR_MIC;

	spec->cur_mic_boost = 0;

	for (i = 0; i < VNODES_COUNT; i++) {
		spec->vnode_lvol[i] = 0x5a;
		spec->vnode_rvol[i] = 0x5a;
		spec->vnode_lswitch[i] = 0;
		spec->vnode_rswitch[i] = 0;
	}

	/*
	 * Default states for effects are in ca0132_effects[].
	 */
	num_fx = OUT_EFFECTS_COUNT + IN_EFFECTS_COUNT;
	for (i = 0; i < num_fx; i++) {
		on = (unsigned int)ca0132_effects[i].reqs[0];
		spec->effects_switch[i] = on ? 1 : 0;
	}
	/*
	 * Sets defaults for the effect slider controls, only for alternative
	 * ca0132 codecs. Also sets x-bass crossover frequency to 80hz.
	 */
	if (spec->use_alt_controls) {
		spec->xbass_xover_freq = 8;
		for (i = 0; i < EFFECT_LEVEL_SLIDERS; i++)
			spec->fx_ctl_val[i] = effect_slider_defaults[i];
	}

	spec->voicefx_val = 0;
	spec->effects_switch[PLAY_ENHANCEMENT - EFFECT_START_NID] = 1;
	spec->effects_switch[CRYSTAL_VOICE - EFFECT_START_NID] = 0;

	/*
	 * The ZxR doesn't have a front panel header, and it's line-in is on
	 * the daughter board. So, there is no input enum control, and we need
	 * to make sure that spec->in_enum_val is set properly.
	 */
	if (spec->quirk == QUIRK_ZXR)
		spec->in_enum_val = REAR_MIC;

#ifdef ENABLE_TUNING_CONTROLS
	ca0132_init_tuning_defaults(codec);
#endif
}

/*
 * Recon3Di exit specific commands.
 */
/* prevents popping noise on shutdown */
static void r3di_gpio_shutdown(struct hda_codec *codec)
{
	snd_hda_codec_write(codec, 0x01, 0, AC_VERB_SET_GPIO_DATA, 0x00);
}

/*
 * Sound Blaster Z exit specific commands.
 */
static void sbz_region2_exit(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int i;

	for (i = 0; i < 4; i++)
		writeb(0x0, spec->mem_base + 0x100);
	for (i = 0; i < 8; i++)
		writeb(0xb3, spec->mem_base + 0x304);

	ca0113_mmio_gpio_set(codec, 0, false);
	ca0113_mmio_gpio_set(codec, 1, false);
	ca0113_mmio_gpio_set(codec, 4, true);
	ca0113_mmio_gpio_set(codec, 5, false);
	ca0113_mmio_gpio_set(codec, 7, false);
}

static void sbz_set_pin_ctl_default(struct hda_codec *codec)
{
	hda_nid_t pins[5] = {0x0B, 0x0C, 0x0E, 0x12, 0x13};
	unsigned int i;

	snd_hda_codec_write(codec, 0x11, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL, 0x40);

	for (i = 0; i < 5; i++)
		snd_hda_codec_write(codec, pins[i], 0,
				AC_VERB_SET_PIN_WIDGET_CONTROL, 0x00);
}

static void ca0132_clear_unsolicited(struct hda_codec *codec)
{
	hda_nid_t pins[7] = {0x0B, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13};
	unsigned int i;

	for (i = 0; i < 7; i++) {
		snd_hda_codec_write(codec, pins[i], 0,
				AC_VERB_SET_UNSOLICITED_ENABLE, 0x00);
	}
}

/* On shutdown, sends commands in sets of three */
static void sbz_gpio_shutdown_commands(struct hda_codec *codec, int dir,
							int mask, int data)
{
	if (dir >= 0)
		snd_hda_codec_write(codec, 0x01, 0,
				AC_VERB_SET_GPIO_DIRECTION, dir);
	if (mask >= 0)
		snd_hda_codec_write(codec, 0x01, 0,
				AC_VERB_SET_GPIO_MASK, mask);

	if (data >= 0)
		snd_hda_codec_write(codec, 0x01, 0,
				AC_VERB_SET_GPIO_DATA, data);
}

static void zxr_dbpro_power_state_shutdown(struct hda_codec *codec)
{
	hda_nid_t pins[7] = {0x05, 0x0c, 0x09, 0x0e, 0x08, 0x11, 0x01};
	unsigned int i;

	for (i = 0; i < 7; i++)
		snd_hda_codec_write(codec, pins[i], 0,
				AC_VERB_SET_POWER_STATE, 0x03);
}

static void sbz_exit_chip(struct hda_codec *codec)
{
	chipio_set_stream_control(codec, 0x03, 0);
	chipio_set_stream_control(codec, 0x04, 0);

	/* Mess with GPIO */
	sbz_gpio_shutdown_commands(codec, 0x07, 0x07, -1);
	sbz_gpio_shutdown_commands(codec, 0x07, 0x07, 0x05);
	sbz_gpio_shutdown_commands(codec, 0x07, 0x07, 0x01);

	chipio_set_stream_control(codec, 0x14, 0);
	chipio_set_stream_control(codec, 0x0C, 0);

	chipio_set_conn_rate(codec, 0x41, SR_192_000);
	chipio_set_conn_rate(codec, 0x91, SR_192_000);

	chipio_write(codec, 0x18a020, 0x00000083);

	sbz_gpio_shutdown_commands(codec, 0x07, 0x07, 0x03);
	sbz_gpio_shutdown_commands(codec, 0x07, 0x07, 0x07);
	sbz_gpio_shutdown_commands(codec, 0x07, 0x07, 0x06);

	chipio_set_stream_control(codec, 0x0C, 0);

	chipio_set_control_param(codec, 0x0D, 0x24);

	ca0132_clear_unsolicited(codec);
	sbz_set_pin_ctl_default(codec);

	snd_hda_codec_write(codec, 0x0B, 0,
		AC_VERB_SET_EAPD_BTLENABLE, 0x00);

	sbz_region2_exit(codec);
}

static void r3d_exit_chip(struct hda_codec *codec)
{
	ca0132_clear_unsolicited(codec);
	snd_hda_codec_write(codec, 0x01, 0, 0x793, 0x00);
	snd_hda_codec_write(codec, 0x01, 0, 0x794, 0x5b);
}

static void ae5_exit_chip(struct hda_codec *codec)
{
	chipio_set_stream_control(codec, 0x03, 0);
	chipio_set_stream_control(codec, 0x04, 0);

	ca0113_mmio_command_set(codec, 0x30, 0x32, 0x3f);
	ca0113_mmio_command_set(codec, 0x48, 0x07, 0x83);
	ca0113_mmio_command_set(codec, 0x48, 0x07, 0x83);
	ca0113_mmio_command_set(codec, 0x30, 0x30, 0x00);
	ca0113_mmio_command_set(codec, 0x30, 0x2b, 0x00);
	ca0113_mmio_command_set(codec, 0x30, 0x2d, 0x00);
	ca0113_mmio_gpio_set(codec, 0, false);
	ca0113_mmio_gpio_set(codec, 1, false);

	snd_hda_codec_write(codec, 0x01, 0, 0x793, 0x00);
	snd_hda_codec_write(codec, 0x01, 0, 0x794, 0x53);

	chipio_set_control_param(codec, CONTROL_PARAM_ASI, 0);

	chipio_set_stream_control(codec, 0x18, 0);
	chipio_set_stream_control(codec, 0x0c, 0);

	snd_hda_codec_write(codec, 0x01, 0, 0x724, 0x83);
}

static void zxr_exit_chip(struct hda_codec *codec)
{
	chipio_set_stream_control(codec, 0x03, 0);
	chipio_set_stream_control(codec, 0x04, 0);
	chipio_set_stream_control(codec, 0x14, 0);
	chipio_set_stream_control(codec, 0x0C, 0);

	chipio_set_conn_rate(codec, 0x41, SR_192_000);
	chipio_set_conn_rate(codec, 0x91, SR_192_000);

	chipio_write(codec, 0x18a020, 0x00000083);

	snd_hda_codec_write(codec, 0x01, 0, 0x793, 0x00);
	snd_hda_codec_write(codec, 0x01, 0, 0x794, 0x53);

	ca0132_clear_unsolicited(codec);
	sbz_set_pin_ctl_default(codec);
	snd_hda_codec_write(codec, 0x0B, 0, AC_VERB_SET_EAPD_BTLENABLE, 0x00);

	ca0113_mmio_gpio_set(codec, 5, false);
	ca0113_mmio_gpio_set(codec, 2, false);
	ca0113_mmio_gpio_set(codec, 3, false);
	ca0113_mmio_gpio_set(codec, 0, false);
	ca0113_mmio_gpio_set(codec, 4, true);
	ca0113_mmio_gpio_set(codec, 0, true);
	ca0113_mmio_gpio_set(codec, 5, true);
	ca0113_mmio_gpio_set(codec, 2, false);
	ca0113_mmio_gpio_set(codec, 3, false);
}

static void ca0132_exit_chip(struct hda_codec *codec)
{
	/* put any chip cleanup stuffs here. */

	if (dspload_is_loaded(codec))
		dsp_reset(codec);
}

/*
 * This fixes a problem that was hard to reproduce. Very rarely, I would
 * boot up, and there would be no sound, but the DSP indicated it had loaded
 * properly. I did a few memory dumps to see if anything was different, and
 * there were a few areas of memory uninitialized with a1a2a3a4. This function
 * checks if those areas are uninitialized, and if they are, it'll attempt to
 * reload the card 3 times. Usually it fixes by the second.
 */
static void sbz_dsp_startup_check(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	unsigned int dsp_data_check[4];
	unsigned int cur_address = 0x390;
	unsigned int i;
	unsigned int failure = 0;
	unsigned int reload = 3;

	if (spec->startup_check_entered)
		return;

	spec->startup_check_entered = true;

	for (i = 0; i < 4; i++) {
		chipio_read(codec, cur_address, &dsp_data_check[i]);
		cur_address += 0x4;
	}
	for (i = 0; i < 4; i++) {
		if (dsp_data_check[i] == 0xa1a2a3a4)
			failure = 1;
	}

	codec_dbg(codec, "Startup Check: %d ", failure);
	if (failure)
		codec_info(codec, "DSP not initialized properly. Attempting to fix.");
	/*
	 * While the failure condition is true, and we haven't reached our
	 * three reload limit, continue trying to reload the driver and
	 * fix the issue.
	 */
	while (failure && (reload != 0)) {
		codec_info(codec, "Reloading... Tries left: %d", reload);
		sbz_exit_chip(codec);
		spec->dsp_state = DSP_DOWNLOAD_INIT;
		codec->patch_ops.init(codec);
		failure = 0;
		for (i = 0; i < 4; i++) {
			chipio_read(codec, cur_address, &dsp_data_check[i]);
			cur_address += 0x4;
		}
		for (i = 0; i < 4; i++) {
			if (dsp_data_check[i] == 0xa1a2a3a4)
				failure = 1;
		}
		reload--;
	}

	if (!failure && reload < 3)
		codec_info(codec, "DSP fixed.");

	if (!failure)
		return;

	codec_info(codec, "DSP failed to initialize properly. Either try a full shutdown or a suspend to clear the internal memory.");
}

/*
 * This is for the extra volume verbs 0x797 (left) and 0x798 (right). These add
 * extra precision for decibel values. If you had the dB value in floating point
 * you would take the value after the decimal point, multiply by 64, and divide
 * by 2. So for 8.59, it's (59 * 64) / 100. Useful if someone wanted to
 * implement fixed point or floating point dB volumes. For now, I'll set them
 * to 0 just incase a value has lingered from a boot into Windows.
 */
static void ca0132_alt_vol_setup(struct hda_codec *codec)
{
	snd_hda_codec_write(codec, 0x02, 0, 0x797, 0x00);
	snd_hda_codec_write(codec, 0x02, 0, 0x798, 0x00);
	snd_hda_codec_write(codec, 0x03, 0, 0x797, 0x00);
	snd_hda_codec_write(codec, 0x03, 0, 0x798, 0x00);
	snd_hda_codec_write(codec, 0x04, 0, 0x797, 0x00);
	snd_hda_codec_write(codec, 0x04, 0, 0x798, 0x00);
	snd_hda_codec_write(codec, 0x07, 0, 0x797, 0x00);
	snd_hda_codec_write(codec, 0x07, 0, 0x798, 0x00);
}

/*
 * Extra commands that don't really fit anywhere else.
 */
static void sbz_pre_dsp_setup(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	writel(0x00820680, spec->mem_base + 0x01C);
	writel(0x00820680, spec->mem_base + 0x01C);

	chipio_write(codec, 0x18b0a4, 0x000000c2);

	snd_hda_codec_write(codec, 0x11, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL, 0x44);
}

static void r3d_pre_dsp_setup(struct hda_codec *codec)
{
	chipio_write(codec, 0x18b0a4, 0x000000c2);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x1E);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_HIGH, 0x1C);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_DATA_WRITE, 0x5B);

	snd_hda_codec_write(codec, 0x11, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL, 0x44);
}

static void r3di_pre_dsp_setup(struct hda_codec *codec)
{
	chipio_write(codec, 0x18b0a4, 0x000000c2);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x1E);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_HIGH, 0x1C);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_DATA_WRITE, 0x5B);

	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x20);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_HIGH, 0x19);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_DATA_WRITE, 0x00);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_DATA_WRITE, 0x40);

	snd_hda_codec_write(codec, 0x11, 0,
			AC_VERB_SET_PIN_WIDGET_CONTROL, 0x04);
}

/*
 * These are sent before the DSP is downloaded. Not sure
 * what they do, or if they're necessary. Could possibly
 * be removed. Figure they're better to leave in.
 */
static void ca0132_mmio_init(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	if (spec->quirk == QUIRK_AE5)
		writel(0x00000001, spec->mem_base + 0x400);
	else
		writel(0x00000000, spec->mem_base + 0x400);

	if (spec->quirk == QUIRK_AE5)
		writel(0x00000001, spec->mem_base + 0x408);
	else
		writel(0x00000000, spec->mem_base + 0x408);

	if (spec->quirk == QUIRK_AE5)
		writel(0x00000001, spec->mem_base + 0x40c);
	else
		writel(0x00000000, spec->mem_base + 0x40C);

	if (spec->quirk == QUIRK_ZXR)
		writel(0x00880640, spec->mem_base + 0x01C);
	else
		writel(0x00880680, spec->mem_base + 0x01C);

	if (spec->quirk == QUIRK_AE5)
		writel(0x00000080, spec->mem_base + 0xC0C);
	else
		writel(0x00000083, spec->mem_base + 0xC0C);

	writel(0x00000030, spec->mem_base + 0xC00);
	writel(0x00000000, spec->mem_base + 0xC04);

	if (spec->quirk == QUIRK_AE5)
		writel(0x00000000, spec->mem_base + 0xC0C);
	else
		writel(0x00000003, spec->mem_base + 0xC0C);

	writel(0x00000003, spec->mem_base + 0xC0C);
	writel(0x00000003, spec->mem_base + 0xC0C);
	writel(0x00000003, spec->mem_base + 0xC0C);

	if (spec->quirk == QUIRK_AE5)
		writel(0x00000001, spec->mem_base + 0xC08);
	else
		writel(0x000000C1, spec->mem_base + 0xC08);

	writel(0x000000F1, spec->mem_base + 0xC08);
	writel(0x00000001, spec->mem_base + 0xC08);
	writel(0x000000C7, spec->mem_base + 0xC08);
	writel(0x000000C1, spec->mem_base + 0xC08);
	writel(0x00000080, spec->mem_base + 0xC04);

	if (spec->quirk == QUIRK_AE5) {
		writel(0x00000000, spec->mem_base + 0x42c);
		writel(0x00000000, spec->mem_base + 0x46c);
		writel(0x00000000, spec->mem_base + 0x4ac);
		writel(0x00000000, spec->mem_base + 0x4ec);
		writel(0x00000000, spec->mem_base + 0x43c);
		writel(0x00000000, spec->mem_base + 0x47c);
		writel(0x00000000, spec->mem_base + 0x4bc);
		writel(0x00000000, spec->mem_base + 0x4fc);
		writel(0x00000600, spec->mem_base + 0x100);
		writel(0x00000014, spec->mem_base + 0x410);
		writel(0x0000060f, spec->mem_base + 0x100);
		writel(0x0000070f, spec->mem_base + 0x100);
		writel(0x00000aff, spec->mem_base + 0x830);
		writel(0x00000000, spec->mem_base + 0x86c);
		writel(0x0000006b, spec->mem_base + 0x800);
		writel(0x00000001, spec->mem_base + 0x86c);
		writel(0x0000006b, spec->mem_base + 0x800);
		writel(0x00000057, spec->mem_base + 0x804);
		writel(0x00800000, spec->mem_base + 0x20c);
	}
}

/*
 * This function writes to some SFR's, does some region2 writes, and then
 * eventually resets the codec with the 0x7ff verb. Not quite sure why it does
 * what it does.
 */
static void ae5_register_set(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	chipio_8051_write_direct(codec, 0x93, 0x10);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x44);
	snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PLL_PMU_WRITE, 0xc2);

	writeb(0x0f, spec->mem_base + 0x304);
	writeb(0x0f, spec->mem_base + 0x304);
	writeb(0x0f, spec->mem_base + 0x304);
	writeb(0x0f, spec->mem_base + 0x304);
	writeb(0x0e, spec->mem_base + 0x100);
	writeb(0x1f, spec->mem_base + 0x304);
	writeb(0x0c, spec->mem_base + 0x100);
	writeb(0x3f, spec->mem_base + 0x304);
	writeb(0x08, spec->mem_base + 0x100);
	writeb(0x7f, spec->mem_base + 0x304);
	writeb(0x00, spec->mem_base + 0x100);
	writeb(0xff, spec->mem_base + 0x304);

	ca0113_mmio_command_set(codec, 0x30, 0x2d, 0x3f);

	chipio_8051_write_direct(codec, 0x90, 0x00);
	chipio_8051_write_direct(codec, 0x90, 0x10);

	ca0113_mmio_command_set(codec, 0x48, 0x07, 0x83);

	chipio_write(codec, 0x18b0a4, 0x000000c2);

	snd_hda_codec_write(codec, 0x01, 0, 0x7ff, 0x00);
	snd_hda_codec_write(codec, 0x01, 0, 0x7ff, 0x00);
}

/*
 * Extra init functions for alternative ca0132 codecs. Done
 * here so they don't clutter up the main ca0132_init function
 * anymore than they have to.
 */
static void ca0132_alt_init(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	ca0132_alt_vol_setup(codec);

	switch (spec->quirk) {
	case QUIRK_SBZ:
		codec_dbg(codec, "SBZ alt_init");
		ca0132_gpio_init(codec);
		sbz_pre_dsp_setup(codec);
		snd_hda_sequence_write(codec, spec->chip_init_verbs);
		snd_hda_sequence_write(codec, spec->desktop_init_verbs);
		break;
	case QUIRK_R3DI:
		codec_dbg(codec, "R3DI alt_init");
		ca0132_gpio_init(codec);
		ca0132_gpio_setup(codec);
		r3di_gpio_dsp_status_set(codec, R3DI_DSP_DOWNLOADING);
		r3di_pre_dsp_setup(codec);
		snd_hda_sequence_write(codec, spec->chip_init_verbs);
		snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0, 0x6FF, 0xC4);
		break;
	case QUIRK_R3D:
		r3d_pre_dsp_setup(codec);
		snd_hda_sequence_write(codec, spec->chip_init_verbs);
		snd_hda_sequence_write(codec, spec->desktop_init_verbs);
		break;
	case QUIRK_AE5:
		ca0132_gpio_init(codec);
		snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
				VENDOR_CHIPIO_8051_ADDRESS_LOW, 0x49);
		snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
				VENDOR_CHIPIO_PLL_PMU_WRITE, 0x88);
		chipio_write(codec, 0x18b030, 0x00000020);
		snd_hda_sequence_write(codec, spec->chip_init_verbs);
		snd_hda_sequence_write(codec, spec->desktop_init_verbs);
		ca0113_mmio_command_set(codec, 0x30, 0x32, 0x3f);
		break;
	case QUIRK_ZXR:
		snd_hda_sequence_write(codec, spec->chip_init_verbs);
		snd_hda_sequence_write(codec, spec->desktop_init_verbs);
		break;
	}
}

static int ca0132_init(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	int i;
	bool dsp_loaded;

	/*
	 * If the DSP is already downloaded, and init has been entered again,
	 * there's only two reasons for it. One, the codec has awaken from a
	 * suspended state, and in that case dspload_is_loaded will return
	 * false, and the init will be ran again. The other reason it gets
	 * re entered is on startup for some reason it triggers a suspend and
	 * resume state. In this case, it will check if the DSP is downloaded,
	 * and not run the init function again. For codecs using alt_functions,
	 * it will check if the DSP is loaded properly.
	 */
	if (spec->dsp_state == DSP_DOWNLOADED) {
		dsp_loaded = dspload_is_loaded(codec);
		if (!dsp_loaded) {
			spec->dsp_reload = true;
			spec->dsp_state = DSP_DOWNLOAD_INIT;
		} else {
			if (spec->quirk == QUIRK_SBZ)
				sbz_dsp_startup_check(codec);
			return 0;
		}
	}

	if (spec->dsp_state != DSP_DOWNLOAD_FAILED)
		spec->dsp_state = DSP_DOWNLOAD_INIT;
	spec->curr_chip_addx = INVALID_CHIP_ADDRESS;

	if (spec->use_pci_mmio)
		ca0132_mmio_init(codec);

	snd_hda_power_up_pm(codec);

	if (spec->quirk == QUIRK_AE5)
		ae5_register_set(codec);

	ca0132_init_unsol(codec);
	ca0132_init_params(codec);
	ca0132_init_flags(codec);

	snd_hda_sequence_write(codec, spec->base_init_verbs);

	if (spec->use_alt_functions)
		ca0132_alt_init(codec);

	ca0132_download_dsp(codec);

	ca0132_refresh_widget_caps(codec);

	switch (spec->quirk) {
	case QUIRK_R3DI:
	case QUIRK_R3D:
		r3d_setup_defaults(codec);
		break;
	case QUIRK_SBZ:
	case QUIRK_ZXR:
		sbz_setup_defaults(codec);
		break;
	case QUIRK_AE5:
		ae5_setup_defaults(codec);
		break;
	default:
		ca0132_setup_defaults(codec);
		ca0132_init_analog_mic2(codec);
		ca0132_init_dmic(codec);
		break;
	}

	for (i = 0; i < spec->num_outputs; i++)
		init_output(codec, spec->out_pins[i], spec->dacs[0]);

	init_output(codec, cfg->dig_out_pins[0], spec->dig_out);

	for (i = 0; i < spec->num_inputs; i++)
		init_input(codec, spec->input_pins[i], spec->adcs[i]);

	init_input(codec, cfg->dig_in_pin, spec->dig_in);

	if (!spec->use_alt_functions) {
		snd_hda_sequence_write(codec, spec->chip_init_verbs);
		snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PARAM_EX_ID_SET, 0x0D);
		snd_hda_codec_write(codec, WIDGET_CHIP_CTRL, 0,
			    VENDOR_CHIPIO_PARAM_EX_VALUE_SET, 0x20);
	}

	if (spec->quirk == QUIRK_SBZ)
		ca0132_gpio_setup(codec);

	snd_hda_sequence_write(codec, spec->spec_init_verbs);
	if (spec->use_alt_functions) {
		ca0132_alt_select_out(codec);
		ca0132_alt_select_in(codec);
	} else {
		ca0132_select_out(codec);
		ca0132_select_mic(codec);
	}

	snd_hda_jack_report_sync(codec);

	/*
	 * Re set the PlayEnhancement switch on a resume event, because the
	 * controls will not be reloaded.
	 */
	if (spec->dsp_reload) {
		spec->dsp_reload = false;
		ca0132_pe_switch_set(codec);
	}

	snd_hda_power_down_pm(codec);

	return 0;
}

static int dbpro_init(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;
	struct auto_pin_cfg *cfg = &spec->autocfg;
	unsigned int i;

	init_output(codec, cfg->dig_out_pins[0], spec->dig_out);
	init_input(codec, cfg->dig_in_pin, spec->dig_in);

	for (i = 0; i < spec->num_inputs; i++)
		init_input(codec, spec->input_pins[i], spec->adcs[i]);

	return 0;
}

static void ca0132_free(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	cancel_delayed_work_sync(&spec->unsol_hp_work);
	snd_hda_power_up(codec);
	switch (spec->quirk) {
	case QUIRK_SBZ:
		sbz_exit_chip(codec);
		break;
	case QUIRK_ZXR:
		zxr_exit_chip(codec);
		break;
	case QUIRK_R3D:
		r3d_exit_chip(codec);
		break;
	case QUIRK_AE5:
		ae5_exit_chip(codec);
		break;
	case QUIRK_R3DI:
		r3di_gpio_shutdown(codec);
		break;
	}

	snd_hda_sequence_write(codec, spec->base_exit_verbs);
	ca0132_exit_chip(codec);

	snd_hda_power_down(codec);
	if (spec->mem_base)
		iounmap(spec->mem_base);
	kfree(spec->spec_init_verbs);
	kfree(codec->spec);
}

static void dbpro_free(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	zxr_dbpro_power_state_shutdown(codec);

	kfree(spec->spec_init_verbs);
	kfree(codec->spec);
}

static void ca0132_reboot_notify(struct hda_codec *codec)
{
	codec->patch_ops.free(codec);
}

static const struct hda_codec_ops ca0132_patch_ops = {
	.build_controls = ca0132_build_controls,
	.build_pcms = ca0132_build_pcms,
	.init = ca0132_init,
	.free = ca0132_free,
	.unsol_event = snd_hda_jack_unsol_event,
	.reboot_notify = ca0132_reboot_notify,
};

static const struct hda_codec_ops dbpro_patch_ops = {
	.build_controls = dbpro_build_controls,
	.build_pcms = dbpro_build_pcms,
	.init = dbpro_init,
	.free = dbpro_free,
};

static void ca0132_config(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	spec->dacs[0] = 0x2;
	spec->dacs[1] = 0x3;
	spec->dacs[2] = 0x4;

	spec->multiout.dac_nids = spec->dacs;
	spec->multiout.num_dacs = 3;

	if (!spec->use_alt_functions)
		spec->multiout.max_channels = 2;
	else
		spec->multiout.max_channels = 6;

	switch (spec->quirk) {
	case QUIRK_ALIENWARE:
		codec_dbg(codec, "%s: QUIRK_ALIENWARE applied.\n", __func__);
		snd_hda_apply_pincfgs(codec, alienware_pincfgs);
		break;
	case QUIRK_SBZ:
		codec_dbg(codec, "%s: QUIRK_SBZ applied.\n", __func__);
		snd_hda_apply_pincfgs(codec, sbz_pincfgs);
		break;
	case QUIRK_ZXR:
		codec_dbg(codec, "%s: QUIRK_ZXR applied.\n", __func__);
		snd_hda_apply_pincfgs(codec, zxr_pincfgs);
		break;
	case QUIRK_R3D:
		codec_dbg(codec, "%s: QUIRK_R3D applied.\n", __func__);
		snd_hda_apply_pincfgs(codec, r3d_pincfgs);
		break;
	case QUIRK_R3DI:
		codec_dbg(codec, "%s: QUIRK_R3DI applied.\n", __func__);
		snd_hda_apply_pincfgs(codec, r3di_pincfgs);
		break;
	case QUIRK_AE5:
		codec_dbg(codec, "%s: QUIRK_AE5 applied.\n", __func__);
		snd_hda_apply_pincfgs(codec, r3di_pincfgs);
		break;
	}

	switch (spec->quirk) {
	case QUIRK_ALIENWARE:
		spec->num_outputs = 2;
		spec->out_pins[0] = 0x0b; /* speaker out */
		spec->out_pins[1] = 0x0f;
		spec->shared_out_nid = 0x2;
		spec->unsol_tag_hp = 0x0f;

		spec->adcs[0] = 0x7; /* digital mic / analog mic1 */
		spec->adcs[1] = 0x8; /* analog mic2 */
		spec->adcs[2] = 0xa; /* what u hear */

		spec->num_inputs = 3;
		spec->input_pins[0] = 0x12;
		spec->input_pins[1] = 0x11;
		spec->input_pins[2] = 0x13;
		spec->shared_mic_nid = 0x7;
		spec->unsol_tag_amic1 = 0x11;
		break;
	case QUIRK_SBZ:
	case QUIRK_R3D:
		spec->num_outputs = 2;
		spec->out_pins[0] = 0x0B; /* Line out */
		spec->out_pins[1] = 0x0F; /* Rear headphone out */
		spec->out_pins[2] = 0x10; /* Front Headphone / Center/LFE*/
		spec->out_pins[3] = 0x11; /* Rear surround */
		spec->shared_out_nid = 0x2;
		spec->unsol_tag_hp = spec->out_pins[1];
		spec->unsol_tag_front_hp = spec->out_pins[2];

		spec->adcs[0] = 0x7; /* Rear Mic / Line-in */
		spec->adcs[1] = 0x8; /* Front Mic, but only if no DSP */
		spec->adcs[2] = 0xa; /* what u hear */

		spec->num_inputs = 2;
		spec->input_pins[0] = 0x12; /* Rear Mic / Line-in */
		spec->input_pins[1] = 0x13; /* What U Hear */
		spec->shared_mic_nid = 0x7;
		spec->unsol_tag_amic1 = spec->input_pins[0];

		/* SPDIF I/O */
		spec->dig_out = 0x05;
		spec->multiout.dig_out_nid = spec->dig_out;
		spec->dig_in = 0x09;
		break;
	case QUIRK_ZXR:
		spec->num_outputs = 2;
		spec->out_pins[0] = 0x0B; /* Line out */
		spec->out_pins[1] = 0x0F; /* Rear headphone out */
		spec->out_pins[2] = 0x10; /* Center/LFE */
		spec->out_pins[3] = 0x11; /* Rear surround */
		spec->shared_out_nid = 0x2;
		spec->unsol_tag_hp = spec->out_pins[1];
		spec->unsol_tag_front_hp = spec->out_pins[2];

		spec->adcs[0] = 0x7; /* Rear Mic / Line-in */
		spec->adcs[1] = 0x8; /* Not connected, no front mic */
		spec->adcs[2] = 0xa; /* what u hear */

		spec->num_inputs = 2;
		spec->input_pins[0] = 0x12; /* Rear Mic / Line-in */
		spec->input_pins[1] = 0x13; /* What U Hear */
		spec->shared_mic_nid = 0x7;
		spec->unsol_tag_amic1 = spec->input_pins[0];
		break;
	case QUIRK_ZXR_DBPRO:
		spec->adcs[0] = 0x8; /* ZxR DBPro Aux In */

		spec->num_inputs = 1;
		spec->input_pins[0] = 0x11; /* RCA Line-in */

		spec->dig_out = 0x05;
		spec->multiout.dig_out_nid = spec->dig_out;

		spec->dig_in = 0x09;
		break;
	case QUIRK_AE5:
		spec->num_outputs = 2;
		spec->out_pins[0] = 0x0B; /* Line out */
		spec->out_pins[1] = 0x11; /* Rear headphone out */
		spec->out_pins[2] = 0x10; /* Front Headphone / Center/LFE*/
		spec->out_pins[3] = 0x0F; /* Rear surround */
		spec->shared_out_nid = 0x2;
		spec->unsol_tag_hp = spec->out_pins[1];
		spec->unsol_tag_front_hp = spec->out_pins[2];

		spec->adcs[0] = 0x7; /* Rear Mic / Line-in */
		spec->adcs[1] = 0x8; /* Front Mic, but only if no DSP */
		spec->adcs[2] = 0xa; /* what u hear */

		spec->num_inputs = 2;
		spec->input_pins[0] = 0x12; /* Rear Mic / Line-in */
		spec->input_pins[1] = 0x13; /* What U Hear */
		spec->shared_mic_nid = 0x7;
		spec->unsol_tag_amic1 = spec->input_pins[0];

		/* SPDIF I/O */
		spec->dig_out = 0x05;
		spec->multiout.dig_out_nid = spec->dig_out;
		break;
	case QUIRK_R3DI:
		spec->num_outputs = 2;
		spec->out_pins[0] = 0x0B; /* Line out */
		spec->out_pins[1] = 0x0F; /* Rear headphone out */
		spec->out_pins[2] = 0x10; /* Front Headphone / Center/LFE*/
		spec->out_pins[3] = 0x11; /* Rear surround */
		spec->shared_out_nid = 0x2;
		spec->unsol_tag_hp = spec->out_pins[1];
		spec->unsol_tag_front_hp = spec->out_pins[2];

		spec->adcs[0] = 0x07; /* Rear Mic / Line-in */
		spec->adcs[1] = 0x08; /* Front Mic, but only if no DSP */
		spec->adcs[2] = 0x0a; /* what u hear */

		spec->num_inputs = 2;
		spec->input_pins[0] = 0x12; /* Rear Mic / Line-in */
		spec->input_pins[1] = 0x13; /* What U Hear */
		spec->shared_mic_nid = 0x7;
		spec->unsol_tag_amic1 = spec->input_pins[0];

		/* SPDIF I/O */
		spec->dig_out = 0x05;
		spec->multiout.dig_out_nid = spec->dig_out;
		break;
	default:
		spec->num_outputs = 2;
		spec->out_pins[0] = 0x0b; /* speaker out */
		spec->out_pins[1] = 0x10; /* headphone out */
		spec->shared_out_nid = 0x2;
		spec->unsol_tag_hp = spec->out_pins[1];

		spec->adcs[0] = 0x7; /* digital mic / analog mic1 */
		spec->adcs[1] = 0x8; /* analog mic2 */
		spec->adcs[2] = 0xa; /* what u hear */

		spec->num_inputs = 3;
		spec->input_pins[0] = 0x12;
		spec->input_pins[1] = 0x11;
		spec->input_pins[2] = 0x13;
		spec->shared_mic_nid = 0x7;
		spec->unsol_tag_amic1 = spec->input_pins[0];

		/* SPDIF I/O */
		spec->dig_out = 0x05;
		spec->multiout.dig_out_nid = spec->dig_out;
		spec->dig_in = 0x09;
		break;
	}
}

static int ca0132_prepare_verbs(struct hda_codec *codec)
{
/* Verbs + terminator (an empty element) */
#define NUM_SPEC_VERBS 2
	struct ca0132_spec *spec = codec->spec;

	spec->chip_init_verbs = ca0132_init_verbs0;
	/*
	 * Since desktop cards use pci_mmio, this can be used to determine
	 * whether or not to use these verbs instead of a separate bool.
	 */
	if (spec->use_pci_mmio)
		spec->desktop_init_verbs = ca0132_init_verbs1;
	spec->spec_init_verbs = kcalloc(NUM_SPEC_VERBS,
					sizeof(struct hda_verb),
					GFP_KERNEL);
	if (!spec->spec_init_verbs)
		return -ENOMEM;

	/* config EAPD */
	spec->spec_init_verbs[0].nid = 0x0b;
	spec->spec_init_verbs[0].param = 0x78D;
	spec->spec_init_verbs[0].verb = 0x00;

	/* Previously commented configuration */
	/*
	spec->spec_init_verbs[2].nid = 0x0b;
	spec->spec_init_verbs[2].param = AC_VERB_SET_EAPD_BTLENABLE;
	spec->spec_init_verbs[2].verb = 0x02;

	spec->spec_init_verbs[3].nid = 0x10;
	spec->spec_init_verbs[3].param = 0x78D;
	spec->spec_init_verbs[3].verb = 0x02;

	spec->spec_init_verbs[4].nid = 0x10;
	spec->spec_init_verbs[4].param = AC_VERB_SET_EAPD_BTLENABLE;
	spec->spec_init_verbs[4].verb = 0x02;
	*/

	/* Terminator: spec->spec_init_verbs[NUM_SPEC_VERBS-1] */
	return 0;
}

/*
 * The Sound Blaster ZxR shares the same PCI subsystem ID as some regular
 * Sound Blaster Z cards. However, they have different HDA codec subsystem
 * ID's. So, we check for the ZxR's subsystem ID, as well as the DBPro
 * daughter boards ID.
 */
static void sbz_detect_quirk(struct hda_codec *codec)
{
	struct ca0132_spec *spec = codec->spec;

	switch (codec->core.subsystem_id) {
	case 0x11020033:
		spec->quirk = QUIRK_ZXR;
		break;
	case 0x1102003f:
		spec->quirk = QUIRK_ZXR_DBPRO;
		break;
	default:
		spec->quirk = QUIRK_SBZ;
		break;
	}
}

static int patch_ca0132(struct hda_codec *codec)
{
	struct ca0132_spec *spec;
	int err;
	const struct snd_pci_quirk *quirk;

	codec_dbg(codec, "patch_ca0132\n");

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	codec->spec = spec;
	spec->codec = codec;

	/* Detect codec quirk */
	quirk = snd_pci_quirk_lookup(codec->bus->pci, ca0132_quirks);
	if (quirk)
		spec->quirk = quirk->value;
	else
		spec->quirk = QUIRK_NONE;

	if (spec->quirk == QUIRK_SBZ)
		sbz_detect_quirk(codec);

	if (spec->quirk == QUIRK_ZXR_DBPRO)
		codec->patch_ops = dbpro_patch_ops;
	else
		codec->patch_ops = ca0132_patch_ops;

	codec->pcm_format_first = 1;
	codec->no_sticky_stream = 1;


	spec->dsp_state = DSP_DOWNLOAD_INIT;
	spec->num_mixers = 1;

	/* Set which mixers each quirk uses. */
	switch (spec->quirk) {
	case QUIRK_SBZ:
		spec->mixers[0] = desktop_mixer;
		snd_hda_codec_set_name(codec, "Sound Blaster Z");
		break;
	case QUIRK_ZXR:
		spec->mixers[0] = desktop_mixer;
		snd_hda_codec_set_name(codec, "Sound Blaster ZxR");
		break;
	case QUIRK_ZXR_DBPRO:
		break;
	case QUIRK_R3D:
		spec->mixers[0] = desktop_mixer;
		snd_hda_codec_set_name(codec, "Recon3D");
		break;
	case QUIRK_R3DI:
		spec->mixers[0] = r3di_mixer;
		snd_hda_codec_set_name(codec, "Recon3Di");
		break;
	case QUIRK_AE5:
		spec->mixers[0] = desktop_mixer;
		snd_hda_codec_set_name(codec, "Sound BlasterX AE-5");
		break;
	default:
		spec->mixers[0] = ca0132_mixer;
		break;
	}

	/* Setup whether or not to use alt functions/controls/pci_mmio */
	switch (spec->quirk) {
	case QUIRK_SBZ:
	case QUIRK_R3D:
	case QUIRK_AE5:
	case QUIRK_ZXR:
		spec->use_alt_controls = true;
		spec->use_alt_functions = true;
		spec->use_pci_mmio = true;
		break;
	case QUIRK_R3DI:
		spec->use_alt_controls = true;
		spec->use_alt_functions = true;
		spec->use_pci_mmio = false;
		break;
	default:
		spec->use_alt_controls = false;
		spec->use_alt_functions = false;
		spec->use_pci_mmio = false;
		break;
	}

	if (spec->use_pci_mmio) {
		spec->mem_base = pci_iomap(codec->bus->pci, 2, 0xC20);
		if (spec->mem_base == NULL) {
			codec_warn(codec, "pci_iomap failed! Setting quirk to QUIRK_NONE.");
			spec->quirk = QUIRK_NONE;
		}
	}

	spec->base_init_verbs = ca0132_base_init_verbs;
	spec->base_exit_verbs = ca0132_base_exit_verbs;

	INIT_DELAYED_WORK(&spec->unsol_hp_work, ca0132_unsol_hp_delayed);

	ca0132_init_chip(codec);

	ca0132_config(codec);

	err = ca0132_prepare_verbs(codec);
	if (err < 0)
		goto error;

	err = snd_hda_parse_pin_def_config(codec, &spec->autocfg, NULL);
	if (err < 0)
		goto error;

	return 0;

 error:
	ca0132_free(codec);
	return err;
}

/*
 * patch entries
 */
static struct hda_device_id snd_hda_id_ca0132[] = {
	HDA_CODEC_ENTRY(0x11020011, "CA0132", patch_ca0132),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_ca0132);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Creative Sound Core3D codec");

static struct hda_codec_driver ca0132_driver = {
	.id = snd_hda_id_ca0132,
};

module_hda_codec_driver(ca0132_driver);
