#ifndef __TRID4DWAVE_H
#define __TRID4DWAVE_H

/*
 *  audio@tridentmicro.com
 *  Fri Feb 19 15:55:28 MST 1999
 *  Definitions for Trident 4DWave DX/NX chips
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
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/* PCI vendor and device ID */ 
#ifndef PCI_VENDOR_ID_TRIDENT
#define PCI_VENDOR_ID_TRIDENT		0x1023
#endif

#ifndef PCI_VENDOR_ID_SI
#define PCI_VENDOR_ID_SI			0x1039
#endif

#ifndef PCI_VENDOR_ID_ALI
#define PCI_VENDOR_ID_ALI			0x10b9
#endif

#ifndef PCI_DEVICE_ID_TRIDENT_4DWAVE_DX
#define PCI_DEVICE_ID_TRIDENT_4DWAVE_DX	0x2000
#endif

#ifndef PCI_DEVICE_ID_TRIDENT_4DWAVE_NX
#define PCI_DEVICE_ID_TRIDENT_4DWAVE_NX	0x2001
#endif

#ifndef PCI_DEVICE_ID_SI_7018
#define PCI_DEVICE_ID_SI_7018		0x7018
#endif

#ifndef PCI_DEVICE_ID_ALI_5451
#define PCI_DEVICE_ID_ALI_5451		0x5451
#endif

#ifndef PCI_DEVICE_ID_ALI_1533
#define PCI_DEVICE_ID_ALI_1533		0x1533
#endif

#define CHANNEL_REGS	5
#define CHANNEL_START	0xe0   // The first bytes of the contiguous register space.

#define BANK_A 		0
#define BANK_B 		1
#define NR_BANKS		2

#define TRIDENT_FMT_STEREO     0x01
#define TRIDENT_FMT_16BIT      0x02
#define TRIDENT_FMT_MASK       0x03

#define DAC_RUNNING	0x01
#define ADC_RUNNING	0x02

/* Register Addresses */

/* operational registers common to DX, NX, 7018 */
enum trident_op_registers {
	T4D_GAME_CR	= 0x30, T4D_GAME_LEG	= 0x31,
	T4D_GAME_AXD	= 0x34,
	T4D_REC_CH	= 0x70,
	T4D_START_A     = 0x80, T4D_STOP_A      = 0x84,
	T4D_DLY_A       = 0x88, T4D_SIGN_CSO_A  = 0x8c,
	T4D_CSPF_A      = 0x90, T4D_CEBC_A      = 0x94,
	T4D_AINT_A      = 0x98, T4D_EINT_A	= 0x9c,
	T4D_LFO_GC_CIR	= 0xa0, T4D_AINTEN_A    = 0xa4,
	T4D_MUSICVOL_WAVEVOL = 0xa8, T4D_SBDELTA_DELTA_R = 0xac,
	T4D_MISCINT	= 0xb0, T4D_START_B     = 0xb4,
	T4D_STOP_B      = 0xb8, T4D_CSPF_B	= 0xbc,
	T4D_SBBL_SBCL	= 0xc0, T4D_SBCTRL_SBE2R_SBDD    = 0xc4,
	T4D_STIMER	= 0xc8, T4D_LFO_B_I2S_DELTA      = 0xcc,
	T4D_AINT_B	= 0xd8, T4D_AINTEN_B	= 0xdc,
	ALI_MPUR2	= 0x22,	ALI_GPIO	= 0x7c,
	ALI_EBUF1 = 0xf4,
	ALI_EBUF2 = 0xf8
};

enum ali_op_registers {
	ALI_SCTRL		= 0x48,
	ALI_GLOBAL_CONTROL	= 0xd4,
	ALI_STIMER		= 0xc8,
	ALI_SPDIF_CS		= 0x70,
	ALI_SPDIF_CTRL		= 0x74
};

enum ali_registers_number {
	ALI_GLOBAL_REGS		= 56,
	ALI_CHANNEL_REGS	= 8,
	ALI_MIXER_REGS		= 20
};

enum ali_sctrl_control_bit {
	ALI_SPDIF_OUT_ENABLE	= 0x20
};

enum ali_global_control_bit {
	ALI_SPDIF_OUT_SEL_PCM	= 0x00000400,
	ALI_SPDIF_IN_SUPPORT	= 0x00000800,
	ALI_SPDIF_OUT_CH_ENABLE	= 0x00008000,
	ALI_SPDIF_IN_CH_ENABLE	= 0x00080000,
	ALI_PCM_IN_DISABLE	= 0x7fffffff,
	ALI_PCM_IN_ENABLE	= 0x80000000,
	ALI_SPDIF_IN_CH_DISABLE	= 0xfff7ffff,
	ALI_SPDIF_OUT_CH_DISABLE = 0xffff7fff,
	ALI_SPDIF_OUT_SEL_SPDIF	= 0xfffffbff
	
};

enum ali_spdif_control_bit {
	ALI_SPDIF_IN_FUNC_ENABLE	= 0x02,
	ALI_SPDIF_IN_CH_STATUS		= 0x40,
	ALI_SPDIF_OUT_CH_STATUS		= 0xbf
	
};

enum ali_control_all {
	ALI_DISABLE_ALL_IRQ	= 0,
	ALI_CHANNELS		= 32,
	ALI_STOP_ALL_CHANNELS	= 0xffffffff,
	ALI_MULTI_CHANNELS_START_STOP	= 0x07800000
};

enum ali_EMOD_control_bit {
	ALI_EMOD_DEC	= 0x00000000,
	ALI_EMOD_INC	= 0x10000000,
	ALI_EMOD_Delay	= 0x20000000,
	ALI_EMOD_Still	= 0x30000000
};

enum ali_pcm_in_channel_num {
	ALI_NORMAL_CHANNEL	= 0,
	ALI_SPDIF_OUT_CHANNEL	= 15,
	ALI_SPDIF_IN_CHANNEL    = 19,
	ALI_LEF_CHANNEL		= 23,
	ALI_CENTER_CHANNEL	= 24,
	ALI_SURR_RIGHT_CHANNEL	= 25,
	ALI_SURR_LEFT_CHANNEL	= 26,
	ALI_PCM_IN_CHANNEL	= 31
};

enum ali_pcm_out_channel_num {
	ALI_PCM_OUT_CHANNEL_FIRST = 0,
	ALI_PCM_OUT_CHANNEL_LAST = 31
};

enum ali_ac97_power_control_bit {
	ALI_EAPD_POWER_DOWN	= 0x8000
};

enum ali_update_ptr_flags {
	ALI_ADDRESS_INT_UPDATE	= 0x01
};

enum ali_revision {
	ALI_5451_V02	= 0x02
};

enum ali_spdif_out_control {
	ALI_PCM_TO_SPDIF_OUT		= 0,
	ALI_SPDIF_OUT_TO_SPDIF_OUT	= 1,
	ALI_SPDIF_OUT_PCM		= 0,
	ALI_SPDIF_OUT_NON_PCM		= 2
};

/* S/PDIF Operational Registers for 4D-NX */
enum nx_spdif_registers {
	NX_SPCTRL_SPCSO	= 0x24, NX_SPLBA = 0x28,
	NX_SPESO	= 0x2c, NX_SPCSTATUS = 0x64
};

/* OP registers to access each hardware channel */
enum channel_registers {
	CH_DX_CSO_ALPHA_FMS = 0xe0, CH_DX_ESO_DELTA = 0xe8,
	CH_DX_FMC_RVOL_CVOL = 0xec,
	CH_NX_DELTA_CSO     = 0xe0, CH_NX_DELTA_ESO = 0xe8,
	CH_NX_ALPHA_FMS_FMC_RVOL_CVOL = 0xec,
	CH_LBA              = 0xe4,
	CH_GVSEL_PAN_VOL_CTRL_EC      = 0xf0
};

/* registers to read/write/control AC97 codec */
enum dx_ac97_registers {
	DX_ACR0_AC97_W        = 0x40, DX_ACR1_AC97_R = 0x44,
	DX_ACR2_AC97_COM_STAT = 0x48
};

enum nx_ac97_registers {
	NX_ACR0_AC97_COM_STAT  = 0x40, NX_ACR1_AC97_W           = 0x44,
	NX_ACR2_AC97_R_PRIMARY = 0x48, NX_ACR3_AC97_R_SECONDARY	= 0x4c
};

enum si_ac97_registers {
	SI_AC97_WRITE       = 0x40, SI_AC97_READ = 0x44,
	SI_SERIAL_INTF_CTRL = 0x48, SI_AC97_GPIO = 0x4c
};

enum ali_ac97_registers {
	ALI_AC97_WRITE       = 0x40, ALI_AC97_READ = 0x44
};

/* Bit mask for operational registers */
#define AC97_REG_ADDR      0x000000ff

enum ali_ac97_bits {
	ALI_AC97_BUSY_WRITE = 0x8000, ALI_AC97_BUSY_READ = 0x8000,
	ALI_AC97_WRITE_ACTION = 0x8000, ALI_AC97_READ_ACTION = 0x8000,
	ALI_AC97_AUDIO_BUSY = 0x4000, ALI_AC97_SECONDARY  = 0x0080,
	ALI_AC97_READ_MIXER_REGISTER = 0xfeff,
	ALI_AC97_WRITE_MIXER_REGISTER = 0x0100
};

enum sis7018_ac97_bits {
	SI_AC97_BUSY_WRITE = 0x8000, SI_AC97_BUSY_READ = 0x8000,
	SI_AC97_AUDIO_BUSY = 0x4000, SI_AC97_MODEM_BUSY = 0x2000,
	SI_AC97_SECONDARY  = 0x0080
};

enum trident_dx_ac97_bits {
	DX_AC97_BUSY_WRITE = 0x8000, DX_AC97_BUSY_READ = 0x8000,
	DX_AC97_READY      = 0x0010, DX_AC97_RECORD    = 0x0008,
	DX_AC97_PLAYBACK   = 0x0002
};

enum trident_nx_ac97_bits {
	/* ACR1-3 */
	NX_AC97_BUSY_WRITE = 0x0800, NX_AC97_BUSY_READ = 0x0800,
	NX_AC97_BUSY_DATA  = 0x0400, NX_AC97_WRITE_SECONDARY = 0x0100,
	/* ACR0 */
	NX_AC97_SECONDARY_READY = 0x0040, NX_AC97_SECONDARY_RECORD = 0x0020,
	NX_AC97_SURROUND_OUTPUT = 0x0010,
	NX_AC97_PRIMARY_READY   = 0x0008, NX_AC97_PRIMARY_RECORD   = 0x0004,
	NX_AC97_PCM_OUTPUT      = 0x0002,
	NX_AC97_WARM_RESET      = 0x0001
};

enum serial_intf_ctrl_bits {
	WARM_REST   = 0x00000001, COLD_RESET  = 0x00000002,
	I2S_CLOCK   = 0x00000004, PCM_SEC_AC97= 0x00000008,
	AC97_DBL_RATE = 0x00000010, SPDIF_EN  = 0x00000020,
	I2S_OUTPUT_EN = 0x00000040, I2S_INPUT_EN = 0x00000080,
	PCMIN       = 0x00000100, LINE1IN     = 0x00000200,
	MICIN       = 0x00000400, LINE2IN     = 0x00000800,
	HEAD_SET_IN = 0x00001000, GPIOIN      = 0x00002000,
	/* 7018 spec says id = 01 but the demo board routed to 10 
	   SECONDARY_ID= 0x00004000, */
	SECONDARY_ID= 0x00004000,
	PCMOUT      = 0x00010000, SURROUT     = 0x00020000,
	CENTEROUT   = 0x00040000, LFEOUT      = 0x00080000,
	LINE1OUT    = 0x00100000, LINE2OUT    = 0x00200000,
	GPIOOUT     = 0x00400000,
	SI_AC97_PRIMARY_READY   = 0x01000000,
	SI_AC97_SECONDARY_READY = 0x02000000,
};

enum global_control_bits {
	CHANNLE_IDX = 0x0000003f, PB_RESET    = 0x00000100,
	PAUSE_ENG   = 0x00000200,
	OVERRUN_IE  = 0x00000400, UNDERRUN_IE = 0x00000800,
	ENDLP_IE    = 0x00001000, MIDLP_IE    = 0x00002000,
	ETOG_IE     = 0x00004000,
	EDROP_IE    = 0x00008000, BANK_B_EN   = 0x00010000
};

enum channel_control_bits {
	CHANNEL_LOOP   = 0x00001000, CHANNEL_SIGNED = 0x00002000,
	CHANNEL_STEREO = 0x00004000, CHANNEL_16BITS = 0x00008000,
};

enum channel_attribute {
	/* playback/record select */
	CHANNEL_PB     = 0x0000, CHANNEL_SPC_PB = 0x4000,
	CHANNEL_REC    = 0x8000, CHANNEL_REC_PB = 0xc000,
	/* playback destination/record source select */
	MODEM_LINE1    = 0x0000, MODEM_LINE2    = 0x0400,
	PCM_LR         = 0x0800, HSET           = 0x0c00,
	I2S_LR         = 0x1000, CENTER_LFE     = 0x1400,
	SURR_LR        = 0x1800, SPDIF_LR       = 0x1c00,
	MIC            = 0x1400,
	/* mist stuff */
	MONO_LEFT      = 0x0000, MONO_RIGHT     = 0x0100,
	MONO_MIX       = 0x0200, SRC_ENABLE     = 0x0080,
};

enum miscint_bits {
	PB_UNDERRUN_IRO = 0x00000001, REC_OVERRUN_IRQ = 0x00000002,
	SB_IRQ          = 0x00000004, MPU401_IRQ      = 0x00000008,
	OPL3_IRQ        = 0x00000010, ADDRESS_IRQ     = 0x00000020,
	ENVELOPE_IRQ    = 0x00000040, ST_IRQ          = 0x00000080,
	PB_UNDERRUN     = 0x00000100, REC_OVERRUN     = 0x00000200,
	MIXER_UNDERFLOW = 0x00000400, MIXER_OVERFLOW  = 0x00000800,
	ST_TARGET_REACHED = 0x00008000, PB_24K_MODE   = 0x00010000, 
	ST_IRQ_EN       = 0x00800000, ACGPIO_IRQ      = 0x01000000
};

#define TRID_REG( trident, x ) ( (trident) -> iobase + (x) )

#define		CYBER_PORT_AUDIO		0x3CE
#define		CYBER_IDX_AUDIO_ENABLE          0x7B
#define		CYBER_BMSK_AUDIO_INT_ENABLE	0x09
#define		CYBER_BMSK_AUENZ		0x01
#define		CYBER_BMSK_AUENZ_ENABLE		0x00
#define		CYBER_IDX_IRQ_ENABLE		0x12
      
#define VALIDATE_MAGIC(FOO,MAG)				\
({						  	\
	if (!(FOO) || (FOO)->magic != MAG) { 		\
		printk(invalid_magic,__func__);	\
		return -ENXIO;			  	\
	}					  	\
})

#define VALIDATE_STATE(a) VALIDATE_MAGIC(a,TRIDENT_STATE_MAGIC)
#define VALIDATE_CARD(a) VALIDATE_MAGIC(a,TRIDENT_CARD_MAGIC)

static inline unsigned ld2(unsigned int x)
{
	unsigned r = 0;
	
	if (x >= 0x10000) {
		x >>= 16;
		r += 16;
	}
	if (x >= 0x100) {
		x >>= 8;
		r += 8;
	}
	if (x >= 0x10) {
		x >>= 4;
		r += 4;
	}
	if (x >= 4) {
		x >>= 2;
		r += 2;
	}
	if (x >= 2)
		r++;
	return r;
}

#endif /* __TRID4DWAVE_H */
