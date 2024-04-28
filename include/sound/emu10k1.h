/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>,
 *		     Creative Labs, Inc.
 *  Definitions for EMU10K1 (SB Live!) chips
 */
#ifndef __SOUND_EMU10K1_H
#define __SOUND_EMU10K1_H


#include <sound/pcm.h>
#include <sound/rawmidi.h>
#include <sound/hwdep.h>
#include <sound/ac97_codec.h>
#include <sound/util_mem.h>
#include <sound/pcm-indirect.h>
#include <sound/timer.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/firmware.h>
#include <linux/io.h>

#include <uapi/sound/emu10k1.h>

/* ------------------- DEFINES -------------------- */

#define EMUPAGESIZE     4096
#define MAXPAGES0       4096	/* 32 bit mode */
#define MAXPAGES1       8192	/* 31 bit mode */
#define NUM_G           64              /* use all channels */
#define NUM_EFX_PLAYBACK    16

/* FIXME? - according to the OSS driver the EMU10K1 needs a 29 bit DMA mask */
#define EMU10K1_DMA_MASK	0x7fffffffUL	/* 31bit */
#define AUDIGY_DMA_MASK		0xffffffffUL	/* 32bit mode */

#define TMEMSIZE        256*1024

#define IP_TO_CP(ip) ((ip == 0) ? 0 : (((0x00001000uL | (ip & 0x00000FFFL)) << (((ip >> 12) & 0x000FL) + 4)) & 0xFFFF0000uL))

// This is used to define hardware bit-fields (sub-registers) by combining
// the bit shift and count with the actual register address. The passed
// mask must represent a single run of adjacent bits.
// The non-concatenating (_NC) variant should be used directly only for
// sub-registers that do not follow the <register>_<field> naming pattern.
#define SUB_REG_NC(reg, field, mask) \
	enum { \
		field ## _MASK = mask, \
		field = reg | \
			(__builtin_ctz(mask) << 16) | \
			(__builtin_popcount(mask) << 24), \
	};
#define SUB_REG(reg, field, mask) SUB_REG_NC(reg, reg ## _ ## field, mask)

// Macros for manipulating values of bit-fields declared using the above macros.
// Best used with constant register addresses, as otherwise quite some code is
// generated. The actual register read/write functions handle combined addresses
// automatically, so use of these macros conveys no advantage when accessing a
// single sub-register at a time.
#define REG_SHIFT(r) (((r) >> 16) & 0x1f)
#define REG_SIZE(r) (((r) >> 24) & 0x1f)
#define REG_MASK0(r) ((1U << REG_SIZE(r)) - 1U)
#define REG_MASK(r) (REG_MASK0(r) << REG_SHIFT(r))
#define REG_VAL_GET(r, v) ((v & REG_MASK(r)) >> REG_SHIFT(r))
#define REG_VAL_PUT(r, v) ((v) << REG_SHIFT(r))

// List terminator for snd_emu10k1_ptr_write_multiple()
#define REGLIST_END ~0

// Audigy specify registers are prefixed with 'A_'

/************************************************************************************************/
/* PCI function 0 registers, address = <val> + PCIBASE0						*/
/************************************************************************************************/

#define PTR			0x00		/* Indexed register set pointer register	*/
						/* NOTE: The CHANNELNUM and ADDRESS words can	*/
						/* be modified independently of each other.	*/
#define PTR_CHANNELNUM_MASK	0x0000003f	/* For each per-channel register, indicates the	*/
						/* channel number of the register to be		*/
						/* accessed.  For non per-channel registers the	*/
						/* value should be set to zero.			*/
#define PTR_ADDRESS_MASK	0x07ff0000	/* Register index				*/
#define A_PTR_ADDRESS_MASK	0x0fff0000

#define DATA			0x04		/* Indexed register set data register		*/

#define IPR			0x08		/* Global interrupt pending register		*/
						/* Clear pending interrupts by writing a 1 to	*/
						/* the relevant bits and zero to the other bits	*/
#define IPR_P16V		0x80000000	/* Bit set when the CA0151 P16V chip wishes
						   to interrupt */
#define IPR_WATERMARK_REACHED	0x40000000
#define IPR_A_GPIO		0x20000000	/* GPIO input pin change			*/

/* The next two interrupts are for the midi port on the Audigy Drive (A_MPU1)			*/
#define IPR_A_MIDITRANSBUFEMPTY2 0x10000000	/* MIDI UART transmit buffer empty		*/
#define IPR_A_MIDIRECVBUFEMPTY2	0x08000000	/* MIDI UART receive buffer empty		*/

#define IPR_SPDIFBUFFULL	0x04000000	/* SPDIF capture related, 10k2 only? (RE)	*/
#define IPR_SPDIFBUFHALFFULL	0x02000000	/* SPDIF capture related? (RE)			*/

#define IPR_SAMPLERATETRACKER	0x01000000	/* Sample rate tracker lock status change	*/
#define IPR_FXDSP		0x00800000	/* Enable FX DSP interrupts			*/
#define IPR_FORCEINT		0x00400000	/* Force Sound Blaster interrupt		*/
#define IPR_PCIERROR		0x00200000	/* PCI bus error				*/
#define IPR_VOLINCR		0x00100000	/* Volume increment button pressed		*/
#define IPR_VOLDECR		0x00080000	/* Volume decrement button pressed		*/
#define IPR_MUTE		0x00040000	/* Mute button pressed				*/
#define IPR_MICBUFFULL		0x00020000	/* Microphone buffer full			*/
#define IPR_MICBUFHALFFULL	0x00010000	/* Microphone buffer half full			*/
#define IPR_ADCBUFFULL		0x00008000	/* ADC buffer full				*/
#define IPR_ADCBUFHALFFULL	0x00004000	/* ADC buffer half full				*/
#define IPR_EFXBUFFULL		0x00002000	/* Effects buffer full				*/
#define IPR_EFXBUFHALFFULL	0x00001000	/* Effects buffer half full			*/
#define IPR_GPSPDIFSTATUSCHANGE	0x00000800	/* GPSPDIF channel status change		*/
#define IPR_CDROMSTATUSCHANGE	0x00000400	/* CD-ROM channel status change			*/
#define IPR_INTERVALTIMER	0x00000200	/* Interval timer terminal count		*/
#define IPR_MIDITRANSBUFEMPTY	0x00000100	/* MIDI UART transmit buffer empty		*/
#define IPR_MIDIRECVBUFEMPTY	0x00000080	/* MIDI UART receive buffer empty		*/
#define IPR_CHANNELLOOP		0x00000040	/* Channel (half) loop interrupt(s) pending	*/
						/* The interrupt is triggered shortly after	*/
						/* CCR_READADDRESS has crossed the boundary;	*/
						/* due to the cache, this runs ahead of the	*/
						/* actual playback position.			*/
#define IPR_CHANNELNUMBERMASK	0x0000003f	/* When IPR_CHANNELLOOP is set, indicates the	*/
						/* highest set channel in CLIPL, CLIPH, HLIPL,  */
						/* or HLIPH.  When IPR is written with CL set,	*/
						/* the bit in H/CLIPL or H/CLIPH corresponding	*/
						/* to the CN value written will be cleared.	*/

#define INTE			0x0c		/* Interrupt enable register			*/
#define INTE_VIRTUALSB_MASK	0xc0000000	/* Virtual Soundblaster I/O port capture	*/
#define INTE_VIRTUALSB_220	0x00000000	/* Capture at I/O base address 0x220-0x22f	*/
#define INTE_VIRTUALSB_240	0x40000000	/* Capture at I/O base address 0x240		*/
#define INTE_VIRTUALSB_260	0x80000000	/* Capture at I/O base address 0x260		*/
#define INTE_VIRTUALSB_280	0xc0000000	/* Capture at I/O base address 0x280		*/
#define INTE_VIRTUALMPU_MASK	0x30000000	/* Virtual MPU I/O port capture			*/
#define INTE_VIRTUALMPU_300	0x00000000	/* Capture at I/O base address 0x300-0x301	*/
#define INTE_VIRTUALMPU_310	0x10000000	/* Capture at I/O base address 0x310		*/
#define INTE_VIRTUALMPU_320	0x20000000	/* Capture at I/O base address 0x320		*/
#define INTE_VIRTUALMPU_330	0x30000000	/* Capture at I/O base address 0x330		*/
#define INTE_MASTERDMAENABLE	0x08000000	/* Master DMA emulation at 0x000-0x00f		*/
#define INTE_SLAVEDMAENABLE	0x04000000	/* Slave DMA emulation at 0x0c0-0x0df		*/
#define INTE_MASTERPICENABLE	0x02000000	/* Master PIC emulation at 0x020-0x021		*/
#define INTE_SLAVEPICENABLE	0x01000000	/* Slave PIC emulation at 0x0a0-0x0a1		*/
#define INTE_VSBENABLE		0x00800000	/* Enable virtual Soundblaster			*/
#define INTE_ADLIBENABLE	0x00400000	/* Enable AdLib emulation at 0x388-0x38b	*/
#define INTE_MPUENABLE		0x00200000	/* Enable virtual MPU				*/
#define INTE_FORCEINT		0x00100000	/* Continuously assert INTAN			*/

#define INTE_MRHANDENABLE	0x00080000	/* Enable the "Mr. Hand" logic			*/
						/* NOTE: There is no reason to use this under	*/
						/* Linux, and it will cause odd hardware 	*/
						/* behavior and possibly random segfaults and	*/
						/* lockups if enabled.				*/

#define INTE_A_GPIOENABLE 	0x00040000	/* Enable GPIO input change interrupts		*/

/* The next two interrupts are for the midi port on the Audigy Drive (A_MPU1)			*/
#define INTE_A_MIDITXENABLE2	0x00020000	/* Enable MIDI transmit-buffer-empty interrupts	*/
#define INTE_A_MIDIRXENABLE2	0x00010000	/* Enable MIDI receive-buffer-empty interrupts	*/

#define INTE_A_SPDIF_BUFFULL_ENABLE 	0x00008000
#define INTE_A_SPDIF_HALFBUFFULL_ENABLE	0x00004000

#define INTE_SAMPLERATETRACKER	0x00002000	/* Enable sample rate tracker interrupts	*/
						/* NOTE: This bit must always be enabled       	*/
#define INTE_FXDSPENABLE	0x00001000	/* Enable FX DSP interrupts			*/
#define INTE_PCIERRORENABLE	0x00000800	/* Enable PCI bus error interrupts		*/
#define INTE_VOLINCRENABLE	0x00000400	/* Enable volume increment button interrupts	*/
#define INTE_VOLDECRENABLE	0x00000200	/* Enable volume decrement button interrupts	*/
#define INTE_MUTEENABLE		0x00000100	/* Enable mute button interrupts		*/
#define INTE_MICBUFENABLE	0x00000080	/* Enable microphone buffer interrupts		*/
#define INTE_ADCBUFENABLE	0x00000040	/* Enable ADC buffer interrupts			*/
#define INTE_EFXBUFENABLE	0x00000020	/* Enable Effects buffer interrupts		*/
#define INTE_GPSPDIFENABLE	0x00000010	/* Enable GPSPDIF status interrupts		*/
#define INTE_CDSPDIFENABLE	0x00000008	/* Enable CDSPDIF status interrupts		*/
#define INTE_INTERVALTIMERENB	0x00000004	/* Enable interval timer interrupts		*/
#define INTE_MIDITXENABLE	0x00000002	/* Enable MIDI transmit-buffer-empty interrupts	*/
#define INTE_MIDIRXENABLE	0x00000001	/* Enable MIDI receive-buffer-empty interrupts	*/

#define WC			0x10		/* Wall Clock register				*/
SUB_REG(WC, SAMPLECOUNTER,	0x03FFFFC0)	/* Sample periods elapsed since reset		*/
SUB_REG(WC, CURRENTCHANNEL,	0x0000003F)	/* Channel [0..63] currently being serviced	*/
						/* NOTE: Each channel takes 1/64th of a sample	*/
						/* period to be serviced.			*/

#define HCFG			0x14		/* Hardware config register			*/
						/* NOTE: There is no reason to use the legacy	*/
						/* SoundBlaster emulation stuff described below	*/
						/* under Linux, and all kinds of weird hardware	*/
						/* behavior can result if you try.  Don't.	*/
#define HCFG_LEGACYFUNC_MASK	0xe0000000	/* Legacy function number 			*/
#define HCFG_LEGACYFUNC_MPU	0x00000000	/* Legacy MPU	 				*/
#define HCFG_LEGACYFUNC_SB	0x40000000	/* Legacy SB					*/
#define HCFG_LEGACYFUNC_AD	0x60000000	/* Legacy AD					*/
#define HCFG_LEGACYFUNC_MPIC	0x80000000	/* Legacy MPIC					*/
#define HCFG_LEGACYFUNC_MDMA	0xa0000000	/* Legacy MDMA					*/
#define HCFG_LEGACYFUNC_SPCI	0xc0000000	/* Legacy SPCI					*/
#define HCFG_LEGACYFUNC_SDMA	0xe0000000	/* Legacy SDMA					*/
#define HCFG_IOCAPTUREADDR	0x1f000000	/* The 4 LSBs of the captured I/O address.	*/
#define HCFG_LEGACYWRITE	0x00800000	/* 1 = write, 0 = read 				*/
#define HCFG_LEGACYWORD		0x00400000	/* 1 = word, 0 = byte 				*/
#define HCFG_LEGACYINT		0x00200000	/* 1 = legacy event captured. Write 1 to clear.	*/
						/* NOTE: The rest of the bits in this register	*/
						/* _are_ relevant under Linux.			*/
#define HCFG_PUSH_BUTTON_ENABLE 0x00100000	/* Enables Volume Inc/Dec and Mute functions    */
#define HCFG_BAUD_RATE		0x00080000	/* 0 = 48kHz, 1 = 44.1kHz			*/
#define HCFG_EXPANDED_MEM	0x00040000	/* 1 = any 16M of 4G addr, 0 = 32M of 2G addr	*/
#define HCFG_CODECFORMAT_MASK	0x00030000	/* CODEC format					*/

/* Specific to Alice2, CA0102 */

#define HCFG_CODECFORMAT_AC97_1	0x00000000	/* AC97 CODEC format -- Ver 1.03		*/
#define HCFG_CODECFORMAT_AC97_2	0x00010000	/* AC97 CODEC format -- Ver 2.1			*/
#define HCFG_AUTOMUTE_ASYNC	0x00008000	/* When set, the async sample rate convertors	*/
						/* will automatically mute their output when	*/
						/* they are not rate-locked to the external	*/
						/* async audio source  				*/
#define HCFG_AUTOMUTE_SPDIF	0x00004000	/* When set, the async sample rate convertors	*/
						/* will automatically mute their output when	*/
						/* the SPDIF V-bit indicates invalid audio	*/
#define HCFG_EMU32_SLAVE	0x00002000	/* 0 = Master, 1 = Slave. Slave for EMU1010	*/
#define HCFG_SLOW_RAMP		0x00001000	/* Increases Send Smoothing time constant	*/
/* 0x00000800 not used on Alice2 */
#define HCFG_PHASE_TRACK_MASK	0x00000700	/* When set, forces corresponding input to	*/
						/* phase track the previous input.		*/
						/* I2S0 can phase track the last S/PDIF input	*/
#define HCFG_I2S_ASRC_ENABLE	0x00000070	/* When set, enables asynchronous sample rate   */
						/* conversion for the corresponding		*/
 						/* I2S format input				*/
/* Rest of HCFG 0x0000000f same as below. LOCKSOUNDCACHE etc.  */

/* Older chips */

#define HCFG_CODECFORMAT_AC97	0x00000000	/* AC97 CODEC format -- Primary Output		*/
#define HCFG_CODECFORMAT_I2S	0x00010000	/* I2S CODEC format -- Secondary (Rear) Output	*/
#define HCFG_GPINPUT0		0x00004000	/* External pin112				*/
#define HCFG_GPINPUT1		0x00002000	/* External pin110				*/
#define HCFG_GPOUTPUT_MASK	0x00001c00	/* External pins which may be controlled	*/
#define HCFG_GPOUT0		0x00001000	/* External pin? (spdif enable on 5.1)		*/
#define HCFG_GPOUT1		0x00000800	/* External pin? (IR)				*/
#define HCFG_GPOUT2		0x00000400	/* External pin? (IR)				*/
#define HCFG_JOYENABLE      	0x00000200	/* Internal joystick enable    			*/
#define HCFG_PHASETRACKENABLE	0x00000100	/* Phase tracking enable			*/
						/* 1 = Force all 3 async digital inputs to use	*/
						/* the same async sample rate tracker (ZVIDEO)	*/
#define HCFG_AC3ENABLE_MASK	0x000000e0	/* AC3 async input control - Not implemented	*/
#define HCFG_AC3ENABLE_ZVIDEO	0x00000080	/* Channels 0 and 1 replace ZVIDEO		*/
#define HCFG_AC3ENABLE_CDSPDIF	0x00000040	/* Channels 0 and 1 replace CDSPDIF		*/
#define HCFG_AC3ENABLE_GPSPDIF  0x00000020      /* Channels 0 and 1 replace GPSPDIF             */
#define HCFG_AUTOMUTE		0x00000010	/* When set, the async sample rate convertors	*/
						/* will automatically mute their output when	*/
						/* they are not rate-locked to the external	*/
						/* async audio source  				*/
#define HCFG_LOCKSOUNDCACHE	0x00000008	/* 1 = Cancel bustmaster accesses to soundcache */
						/* NOTE: This should generally never be used.  	*/
SUB_REG(HCFG, LOCKTANKCACHE,	0x00000004)	/* 1 = Cancel bustmaster accesses to tankcache	*/
						/* NOTE: This should generally never be used.  	*/
#define HCFG_MUTEBUTTONENABLE	0x00000002	/* 1 = Master mute button sets AUDIOENABLE = 0.	*/
						/* NOTE: This is a 'cheap' way to implement a	*/
						/* master mute function on the mute button, and	*/
						/* in general should not be used unless a more	*/
						/* sophisticated master mute function has not	*/
						/* been written.       				*/
#define HCFG_AUDIOENABLE	0x00000001	/* 0 = CODECs transmit zero-valued samples	*/
						/* Should be set to 1 when the EMU10K1 is	*/
						/* completely initialized.			*/

// On Audigy, the MPU port moved to the 0x70-0x74 ptr registers

#define MUDATA			0x18		/* MPU401 data register (8 bits)       		*/

#define MUCMD			0x19		/* MPU401 command register (8 bits)    		*/
#define MUCMD_RESET		0xff		/* RESET command				*/
#define MUCMD_ENTERUARTMODE	0x3f		/* Enter_UART_mode command			*/
						/* NOTE: All other commands are ignored		*/

#define MUSTAT			MUCMD		/* MPU401 status register (8 bits)     		*/
#define MUSTAT_IRDYN		0x80		/* 0 = MIDI data or command ACK			*/
#define MUSTAT_ORDYN		0x40		/* 0 = MUDATA can accept a command or data	*/

#define A_GPIO			0x18		/* GPIO on Audigy card (16bits)			*/
#define A_GPINPUT_MASK		0xff00		/* Alice/2 has 8 input pins			*/
#define A3_GPINPUT_MASK		0x3f00		/* ... while Tina/2 has only 6			*/
#define A_GPOUTPUT_MASK		0x00ff

// The GPIO port is used for I/O config on Sound Blasters;
// card-specific info can be found in the emu_chip_details table.
// On E-MU cards the port is used as the interface to the FPGA.

// Audigy output/GPIO stuff taken from the kX drivers
#define A_IOCFG			A_GPIO
#define A_IOCFG_GPOUT0		0x0044		/* analog/digital				*/
#define A_IOCFG_DISABLE_ANALOG	0x0040		/* = 'enable' for Audigy2 (chiprev=4)		*/
#define A_IOCFG_ENABLE_DIGITAL	0x0004
#define A_IOCFG_ENABLE_DIGITAL_AUDIGY4	0x0080
#define A_IOCFG_UNKNOWN_20      0x0020
#define A_IOCFG_DISABLE_AC97_FRONT      0x0080  /* turn off ac97 front -> front (10k2.1)	*/
#define A_IOCFG_GPOUT1		0x0002		/* IR? drive's internal bypass (?)		*/
#define A_IOCFG_GPOUT2		0x0001		/* IR */
#define A_IOCFG_MULTIPURPOSE_JACK	0x2000  /* center+lfe+rear_center (a2/a2ex)		*/
                                                /* + digital for generic 10k2			*/
#define A_IOCFG_DIGITAL_JACK    0x1000          /* digital for a2 platinum			*/
#define A_IOCFG_FRONT_JACK      0x4000
#define A_IOCFG_REAR_JACK       0x8000
#define A_IOCFG_PHONES_JACK     0x0100          /* LiveDrive					*/

#define TIMER			0x1a		/* Timer terminal count register		*/
						/* NOTE: After the rate is changed, a maximum	*/
						/* of 1024 sample periods should be allowed	*/
						/* before the new rate is guaranteed accurate.	*/
#define TIMER_RATE_MASK		0x03ff		/* Timer interrupt rate in sample periods	*/
						/* 0 == 1024 periods, [1..4] are not useful	*/

#define AC97DATA		0x1c		/* AC97 register set data register (16 bit)	*/

#define AC97ADDRESS		0x1e		/* AC97 register set address register (8 bit)	*/
#define AC97ADDRESS_READY	0x80		/* Read-only bit, reflects CODEC READY signal	*/
#define AC97ADDRESS_ADDRESS	0x7f		/* Address of indexed AC97 register		*/

/* Available on the Audigy 2 and Audigy 4 only. This is the P16V chip. */
#define PTR2			0x20		/* Indexed register set pointer register	*/
#define DATA2			0x24		/* Indexed register set data register		*/
#define IPR2			0x28		/* P16V interrupt pending register		*/
#define IPR2_PLAYBACK_CH_0_LOOP      0x00001000 /* Playback Channel 0 loop                               */
#define IPR2_PLAYBACK_CH_0_HALF_LOOP 0x00000100 /* Playback Channel 0 half loop                          */
#define IPR2_CAPTURE_CH_0_LOOP       0x00100000 /* Capture Channel 0 loop                               */
#define IPR2_CAPTURE_CH_0_HALF_LOOP  0x00010000 /* Capture Channel 0 half loop                          */
						/* 0x00000100 Playback. Only in once per period.
						 * 0x00110000 Capture. Int on half buffer.
						 */
#define INTE2			0x2c		/* P16V Interrupt enable register. 	*/
#define INTE2_PLAYBACK_CH_0_LOOP      0x00001000 /* Playback Channel 0 loop                               */
#define INTE2_PLAYBACK_CH_0_HALF_LOOP 0x00000100 /* Playback Channel 0 half loop                          */
#define INTE2_PLAYBACK_CH_1_LOOP      0x00002000 /* Playback Channel 1 loop                               */
#define INTE2_PLAYBACK_CH_1_HALF_LOOP 0x00000200 /* Playback Channel 1 half loop                          */
#define INTE2_PLAYBACK_CH_2_LOOP      0x00004000 /* Playback Channel 2 loop                               */
#define INTE2_PLAYBACK_CH_2_HALF_LOOP 0x00000400 /* Playback Channel 2 half loop                          */
#define INTE2_PLAYBACK_CH_3_LOOP      0x00008000 /* Playback Channel 3 loop                               */
#define INTE2_PLAYBACK_CH_3_HALF_LOOP 0x00000800 /* Playback Channel 3 half loop                          */
#define INTE2_CAPTURE_CH_0_LOOP       0x00100000 /* Capture Channel 0 loop                               */
#define INTE2_CAPTURE_CH_0_HALF_LOOP  0x00010000 /* Caputre Channel 0 half loop                          */
#define HCFG2			0x34		/* Defaults: 0, win2000 sets it to 00004201 */
						/* 0x00000000 2-channel output. */
						/* 0x00000200 8-channel output. */
						/* 0x00000004 pauses stream/irq fail. */
						/* Rest of bits do nothing to sound output */
						/* bit 0: Enable P16V audio.
						 * bit 1: Lock P16V record memory cache.
						 * bit 2: Lock P16V playback memory cache.
						 * bit 3: Dummy record insert zero samples.
						 * bit 8: Record 8-channel in phase.
						 * bit 9: Playback 8-channel in phase.
						 * bit 11-12: Playback mixer attenuation: 0=0dB, 1=-6dB, 2=-12dB, 3=Mute.
						 * bit 13: Playback mixer enable.
						 * bit 14: Route SRC48 mixer output to fx engine.
						 * bit 15: Enable IEEE 1394 chip.
						 */
#define IPR3			0x38		/* Cdif interrupt pending register		*/
#define INTE3			0x3c		/* Cdif interrupt enable register. 	*/

/************************************************************************************************/
/* PCI function 1 registers, address = <val> + PCIBASE1						*/
/************************************************************************************************/

#define JOYSTICK1		0x00		/* Analog joystick port register		*/
#define JOYSTICK2		0x01		/* Analog joystick port register		*/
#define JOYSTICK3		0x02		/* Analog joystick port register		*/
#define JOYSTICK4		0x03		/* Analog joystick port register		*/
#define JOYSTICK5		0x04		/* Analog joystick port register		*/
#define JOYSTICK6		0x05		/* Analog joystick port register		*/
#define JOYSTICK7		0x06		/* Analog joystick port register		*/
#define JOYSTICK8		0x07		/* Analog joystick port register		*/

/* When writing, any write causes JOYSTICK_COMPARATOR output enable to be pulsed on write.	*/
/* When reading, use these bitfields: */
#define JOYSTICK_BUTTONS	0x0f		/* Joystick button data				*/
#define JOYSTICK_COMPARATOR	0xf0		/* Joystick comparator data			*/

/********************************************************************************************************/
/* Emu10k1 pointer-offset register set, accessed through the PTR and DATA registers			*/
/********************************************************************************************************/

// No official documentation was released for EMU10K1, but some info
// about playback can be extrapolated from the EMU8K documents:
// "AWE32/EMU8000 Programmer’s Guide" (emu8kpgm.pdf) - registers
// "AWE32 Developer's Information Pack" (adip301.pdf) - high-level view

// The short version:
// - The engine has 64 playback channels, also called voices. The channels
//   operate independently, except when paired for stereo (see below).
// - PCM samples are fetched into the cache; see description of CD0 below.
// - Samples are consumed at the rate CPF_CURRENTPITCH.
// - 8-bit samples are transformed upon use: cooked = (raw ^ 0x80) << 8
// - 8 samples are read at CCR_READADDRESS:CPF_FRACADDRESS and interpolated
//   according to CCCA_INTERPROM_*. With CCCA_INTERPROM_0 selected and a zero
//   CPF_FRACADDRESS, this results in CCR_READADDRESS[3] being used verbatim.
// - The value is multiplied by CVCF_CURRENTVOL.
// - The value goes through a filter with cutoff CVCF_CURRENTFILTER;
//   delay stages Z1 and Z2.
// - The value is added by so-called `sends` to 4 (EMU10K1) / 8 (EMU10K2)
//   of the 16 (EMU10K1) / 64 (EMU10K2) FX bus accumulators via FXRT*,
//   multiplied by a per-send amount (*_FXSENDAMOUNT_*).
//   The scaling of the send amounts is exponential-ish.
// - The DSP has a go at FXBUS* and outputs the values to EXTOUT* or EMU32OUT*.
// - The pitch, volume, and filter cutoff can be modulated by two envelope
//   engines and two low frequency oscillators.
// - To avoid abrupt changes to the parameters (which may cause audible
//   distortion), the modulation engine sets the target registers, towards
//   which the current registers "swerve" gradually.

// For the odd channel in a stereo pair, these registers are meaningless:
//   CPF_STEREO, CPF_CURRENTPITCH, PTRX_PITCHTARGET, CCR_CACHEINVALIDSIZE,
//   PSST_LOOPSTARTADDR, DSL_LOOPENDADDR, CCCA_CURRADDR
// The somewhat non-obviously still meaningful ones are:
//   CPF_STOP, CPF_FRACADDRESS, CCR_READADDRESS (!),
//   CCCA_INTERPROM, CCCA_8BITSELECT (!)
// (The envelope engine is ignored here, as stereo matters only for verbatim playback.)

#define CPF			0x00		/* Current pitch and fraction register			*/
SUB_REG(CPF, CURRENTPITCH,	0xffff0000)	/* Current pitch (linear, 0x4000 == unity pitch shift) 	*/
#define CPF_STEREO_MASK		0x00008000	/* 1 = Even channel interleave, odd channel locked	*/
SUB_REG(CPF, STOP,		0x00004000)	/* 1 = Current pitch forced to 0			*/
						/* Can be set only while matching bit in SOLEx is 1	*/
#define CPF_FRACADDRESS_MASK	0x00003fff	/* Linear fractional address of the current channel	*/

#define PTRX			0x01		/* Pitch target and send A/B amounts register		*/
SUB_REG(PTRX, PITCHTARGET,	0xffff0000)	/* Pitch target of specified channel			*/
SUB_REG(PTRX, FXSENDAMOUNT_A,	0x0000ff00)	/* Linear level of channel output sent to FX send bus A	*/
SUB_REG(PTRX, FXSENDAMOUNT_B,	0x000000ff)	/* Linear level of channel output sent to FX send bus B	*/

// Note: the volumes are raw multpliers, so real 100% is impossible.
#define CVCF			0x02		/* Current volume and filter cutoff register		*/
SUB_REG(CVCF, CURRENTVOL,	0xffff0000)	/* Current linear volume of specified channel		*/
SUB_REG(CVCF, CURRENTFILTER,	0x0000ffff)	/* Current filter cutoff frequency of specified channel	*/

#define VTFT			0x03		/* Volume target and filter cutoff target register	*/
SUB_REG(VTFT, VOLUMETARGET,	0xffff0000)	/* Volume target of specified channel			*/
SUB_REG(VTFT, FILTERTARGET,	0x0000ffff)	/* Filter cutoff target of specified channel		*/

#define Z1			0x05		/* Filter delay memory 1 register			*/

#define Z2			0x04		/* Filter delay memory 2 register			*/

#define PSST			0x06		/* Send C amount and loop start address register	*/
SUB_REG(PSST, FXSENDAMOUNT_C,	0xff000000)	/* Linear level of channel output sent to FX send bus C	*/
SUB_REG(PSST, LOOPSTARTADDR,	0x00ffffff)	/* Loop start address of the specified channel		*/

#define DSL			0x07		/* Send D amount and loop end address register	*/
SUB_REG(DSL, FXSENDAMOUNT_D,	0xff000000)	/* Linear level of channel output sent to FX send bus D	*/
SUB_REG(DSL, LOOPENDADDR,	0x00ffffff)	/* Loop end address of the specified channel		*/

#define CCCA			0x08		/* Filter Q, interp. ROM, byte size, cur. addr register */
SUB_REG(CCCA, RESONANCE,	0xf0000000)	/* Lowpass filter resonance (Q) height			*/
#define CCCA_INTERPROM_MASK	0x0e000000	/* Selects passband of interpolation ROM		*/
						/* 1 == full band, 7 == lowpass				*/
						/* ROM 0 is used when pitch shifting downward or less	*/
						/* then 3 semitones upward.  Increasingly higher ROM	*/
						/* numbers are used, typically in steps of 3 semitones,	*/
						/* as upward pitch shifting is performed.		*/
#define CCCA_INTERPROM_0	0x00000000	/* Select interpolation ROM 0				*/
#define CCCA_INTERPROM_1	0x02000000	/* Select interpolation ROM 1				*/
#define CCCA_INTERPROM_2	0x04000000	/* Select interpolation ROM 2				*/
#define CCCA_INTERPROM_3	0x06000000	/* Select interpolation ROM 3				*/
#define CCCA_INTERPROM_4	0x08000000	/* Select interpolation ROM 4				*/
#define CCCA_INTERPROM_5	0x0a000000	/* Select interpolation ROM 5				*/
#define CCCA_INTERPROM_6	0x0c000000	/* Select interpolation ROM 6				*/
#define CCCA_INTERPROM_7	0x0e000000	/* Select interpolation ROM 7				*/
#define CCCA_8BITSELECT		0x01000000	/* 1 = Sound memory for this channel uses 8-bit samples	*/
						/* 8-bit samples are unsigned, 16-bit ones signed	*/
SUB_REG(CCCA, CURRADDR,		0x00ffffff)	/* Current address of the selected channel		*/

#define CCR			0x09		/* Cache control register				*/
SUB_REG(CCR, CACHEINVALIDSIZE,	0xfe000000)	/* Number of invalid samples before the read address	*/
#define CCR_CACHELOOPFLAG	0x01000000	/* 1 = Cache has a loop service pending			*/
#define CCR_INTERLEAVEDSAMPLES	0x00800000	/* 1 = A cache service will fetch interleaved samples	*/
						/* Auto-set from CPF_STEREO_MASK			*/
#define CCR_WORDSIZEDSAMPLES	0x00400000	/* 1 = A cache service will fetch word sized samples	*/
						/* Auto-set from CCCA_8BITSELECT			*/
SUB_REG(CCR, READADDRESS,	0x003f0000)	/* Next cached sample to play				*/
SUB_REG(CCR, LOOPINVALSIZE,	0x0000fe00)	/* Number of invalid samples in cache prior to loop	*/
						/* NOTE: This is valid only if CACHELOOPFLAG is set	*/
#define CCR_LOOPFLAG		0x00000100	/* Set for a single sample period when a loop occurs	*/
SUB_REG(CCR, CACHELOOPADDRHI,	0x000000ff)	/* CLP_LOOPSTARTADDR's hi byte if CACHELOOPFLAG is set	*/

#define CLP			0x0a		/* Cache loop register (valid if CCR_CACHELOOPFLAG = 1) */
						/* NOTE: This register is normally not used		*/
SUB_REG(CLP, CACHELOOPADDR,	0x0000ffff)	/* Cache loop address low word				*/

#define FXRT			0x0b		/* Effects send routing register			*/
						/* NOTE: It is illegal to assign the same routing to	*/
						/* two effects sends.					*/
#define FXRT_CHANNELA		0x000f0000	/* Effects send bus number for channel's effects send A	*/
#define FXRT_CHANNELB		0x00f00000	/* Effects send bus number for channel's effects send B	*/
#define FXRT_CHANNELC		0x0f000000	/* Effects send bus number for channel's effects send C	*/
#define FXRT_CHANNELD		0xf0000000	/* Effects send bus number for channel's effects send D	*/

#define MAPA			0x0c		/* Cache map A						*/
#define MAPB			0x0d		/* Cache map B						*/

#define MAP_PTE_MASK0		0xfffff000	/* The 20 MSBs of the PTE indexed by the PTI		*/
#define MAP_PTI_MASK0		0x00000fff	/* The 12 bit index to one of the 4096 PTE dwords      	*/

#define MAP_PTE_MASK1		0xffffe000	/* The 19 MSBs of the PTE indexed by the PTI		*/
#define MAP_PTI_MASK1		0x00001fff	/* The 13 bit index to one of the 8192 PTE dwords      	*/

/* 0x0e, 0x0f: Internal state, at least on Audigy */

#define ENVVOL			0x10		/* Volume envelope register				*/
#define ENVVOL_MASK		0x0000ffff	/* Current value of volume envelope state variable	*/  
						/* 0x8000-n == 666*n usec delay	       			*/

#define ATKHLDV 		0x11		/* Volume envelope hold and attack register		*/
#define ATKHLDV_PHASE0_MASK	0x00008000	/* 0 = Begin attack phase				*/
#define ATKHLDV_HOLDTIME_MASK	0x00007f00	/* Envelope hold time (127-n == n*88.2msec)		*/
#define ATKHLDV_ATTACKTIME_MASK	0x0000007f	/* Envelope attack time, log encoded			*/
						/* 0 = infinite, 1 = 10.9msec, ... 0x7f = 5.5msec	*/

#define DCYSUSV 		0x12		/* Volume envelope sustain and decay register		*/
#define DCYSUSV_PHASE1_MASK	0x00008000	/* 0 = Begin decay phase, 1 = begin release phase	*/
#define DCYSUSV_SUSTAINLEVEL_MASK 0x00007f00	/* 127 = full, 0 = off, 0.75dB increments		*/
#define DCYSUSV_CHANNELENABLE_MASK 0x00000080	/* 0 = Inhibit envelope engine from writing values in	*/
						/* this channel and from writing to pitch, filter and	*/
						/* volume targets.					*/
#define DCYSUSV_DECAYTIME_MASK	0x0000007f	/* Volume envelope decay time, log encoded     		*/
						/* 0 = 43.7msec, 1 = 21.8msec, 0x7f = 22msec		*/

#define LFOVAL1 		0x13		/* Modulation LFO value					*/
#define LFOVAL_MASK		0x0000ffff	/* Current value of modulation LFO state variable	*/
						/* 0x8000-n == 666*n usec delay				*/

#define ENVVAL			0x14		/* Modulation envelope register				*/
#define ENVVAL_MASK		0x0000ffff	/* Current value of modulation envelope state variable 	*/
						/* 0x8000-n == 666*n usec delay				*/

#define ATKHLDM			0x15		/* Modulation envelope hold and attack register		*/
#define ATKHLDM_PHASE0_MASK	0x00008000	/* 0 = Begin attack phase				*/
#define ATKHLDM_HOLDTIME	0x00007f00	/* Envelope hold time (127-n == n*42msec)		*/
#define ATKHLDM_ATTACKTIME	0x0000007f	/* Envelope attack time, log encoded			*/
						/* 0 = infinite, 1 = 11msec, ... 0x7f = 5.5msec		*/

#define DCYSUSM			0x16		/* Modulation envelope decay and sustain register	*/
#define DCYSUSM_PHASE1_MASK	0x00008000	/* 0 = Begin decay phase, 1 = begin release phase	*/
#define DCYSUSM_SUSTAINLEVEL_MASK 0x00007f00	/* 127 = full, 0 = off, 0.75dB increments		*/
#define DCYSUSM_DECAYTIME_MASK	0x0000007f	/* Envelope decay time, log encoded			*/
						/* 0 = 43.7msec, 1 = 21.8msec, 0x7f = 22msec		*/

#define LFOVAL2 		0x17		/* Vibrato LFO register					*/
#define LFOVAL2_MASK		0x0000ffff	/* Current value of vibrato LFO state variable 		*/
						/* 0x8000-n == 666*n usec delay				*/

#define IP			0x18		/* Initial pitch register				*/
#define IP_MASK			0x0000ffff	/* Exponential initial pitch shift			*/
						/* 4 bits of octave, 12 bits of fractional octave	*/
#define IP_UNITY		0x0000e000	/* Unity pitch shift					*/

#define IFATN			0x19		/* Initial filter cutoff and attenuation register	*/
SUB_REG(IFATN, FILTERCUTOFF,	0x0000ff00)	/* Initial filter cutoff frequency in exponential units	*/
						/* 6 most significant bits are semitones		*/
						/* 2 least significant bits are fractions		*/
SUB_REG(IFATN, ATTENUATION,	0x000000ff)	/* Initial attenuation in 0.375dB steps			*/

#define PEFE			0x1a		/* Pitch envelope and filter envelope amount register	*/
SUB_REG(PEFE, PITCHAMOUNT,	0x0000ff00)	/* Pitch envlope amount					*/
						/* Signed 2's complement, +/- one octave peak extremes	*/
SUB_REG(PEFE, FILTERAMOUNT,	0x000000ff)	/* Filter envlope amount				*/
						/* Signed 2's complement, +/- six octaves peak extremes */


#define FMMOD			0x1b		/* Vibrato/filter modulation from LFO register		*/
#define FMMOD_MODVIBRATO	0x0000ff00	/* Vibrato LFO modulation depth				*/
						/* Signed 2's complement, +/- one octave extremes	*/
#define FMMOD_MOFILTER		0x000000ff	/* Filter LFO modulation depth				*/
						/* Signed 2's complement, +/- three octave extremes	*/

#define TREMFRQ 		0x1c		/* Tremolo amount and modulation LFO frequency register	*/
#define TREMFRQ_DEPTH		0x0000ff00	/* Tremolo depth					*/
						/* Signed 2's complement, with +/- 12dB extremes	*/
#define TREMFRQ_FREQUENCY	0x000000ff	/* Tremolo LFO frequency				*/
						/* ??Hz steps, maximum of ?? Hz.			*/

#define FM2FRQ2 		0x1d		/* Vibrato amount and vibrato LFO frequency register	*/
#define FM2FRQ2_DEPTH		0x0000ff00	/* Vibrato LFO vibrato depth				*/
						/* Signed 2's complement, +/- one octave extremes	*/
#define FM2FRQ2_FREQUENCY	0x000000ff	/* Vibrato LFO frequency				*/
						/* 0.039Hz steps, maximum of 9.85 Hz.			*/

#define TEMPENV 		0x1e		/* Tempory envelope register				*/
#define TEMPENV_MASK		0x0000ffff	/* 16-bit value						*/
						/* NOTE: All channels contain internal variables; do	*/
						/* not write to these locations.			*/

/* 0x1f: not used */

// 32 cache registers (== 128 bytes) per channel follow.
// In stereo mode, the two channels' caches are concatenated into one,
// and hold the interleaved frames.
// The cache holds 64 frames, so the upper half is not used in 8-bit mode.
// All registers mentioned below count in frames.
// The cache is a ring buffer; CCR_READADDRESS operates modulo 64.
// The cache is filled from (CCCA_CURRADDR - CCR_CACHEINVALIDSIZE)
// into (CCR_READADDRESS - CCR_CACHEINVALIDSIZE).
// The engine has a fetch threshold of 32 bytes, so it tries to keep
// CCR_CACHEINVALIDSIZE below 8 (16-bit stereo), 16 (16-bit mono,
// 8-bit stereo), or 32 (8-bit mono). The actual transfers are pretty
// unpredictable, especially if several voices are running.
// Frames are consumed at CCR_READADDRESS, which is incremented afterwards,
// along with CCCA_CURRADDR and CCR_CACHEINVALIDSIZE. This implies that the
// actual playback position always lags CCCA_CURRADDR by exactly 64 frames.
#define CD0			0x20		/* Cache data registers 0 .. 0x1f			*/

#define PTB			0x40		/* Page table base register				*/
#define PTB_MASK		0xfffff000	/* Physical address of the page table in host memory	*/

#define TCB			0x41		/* Tank cache base register    				*/
#define TCB_MASK		0xfffff000	/* Physical address of the bottom of host based TRAM	*/

#define ADCCR			0x42		/* ADC sample rate/stereo control register		*/
#define ADCCR_RCHANENABLE	0x00000010	/* Enables right channel for writing to the host       	*/
#define ADCCR_LCHANENABLE	0x00000008	/* Enables left channel for writing to the host		*/
						/* NOTE: To guarantee phase coherency, both channels	*/
						/* must be disabled prior to enabling both channels.	*/
#define A_ADCCR_RCHANENABLE	0x00000020
#define A_ADCCR_LCHANENABLE	0x00000010

#define A_ADCCR_SAMPLERATE_MASK 0x0000000F      /* Audigy sample rate convertor output rate		*/
#define ADCCR_SAMPLERATE_MASK	0x00000007	/* Sample rate convertor output rate			*/
#define ADCCR_SAMPLERATE_48	0x00000000	/* 48kHz sample rate					*/
#define ADCCR_SAMPLERATE_44	0x00000001	/* 44.1kHz sample rate					*/
#define ADCCR_SAMPLERATE_32	0x00000002	/* 32kHz sample rate					*/
#define ADCCR_SAMPLERATE_24	0x00000003	/* 24kHz sample rate					*/
#define ADCCR_SAMPLERATE_22	0x00000004	/* 22.05kHz sample rate					*/
#define ADCCR_SAMPLERATE_16	0x00000005	/* 16kHz sample rate					*/
#define ADCCR_SAMPLERATE_11	0x00000006	/* 11.025kHz sample rate				*/
#define ADCCR_SAMPLERATE_8	0x00000007	/* 8kHz sample rate					*/
#define A_ADCCR_SAMPLERATE_12	0x00000006	/* 12kHz sample rate					*/
#define A_ADCCR_SAMPLERATE_11	0x00000007	/* 11.025kHz sample rate				*/
#define A_ADCCR_SAMPLERATE_8	0x00000008	/* 8kHz sample rate					*/

#define FXWC			0x43		/* FX output write channels register			*/
						/* When set, each bit enables the writing of the	*/
						/* corresponding FX output channel (internal registers  */
						/* 0x20-0x3f) to host memory.  This mode of recording   */
						/* is 16bit, 48KHz only. All 32 channels can be enabled */
						/* simultaneously.					*/

#define A_TBLSZ			0x43	/* Effects Tank Internal Table Size. Only low byte or register used */

#define TCBS			0x44		/* Tank cache buffer size register			*/
#define TCBS_MASK		0x00000007	/* Tank cache buffer size field				*/
#define TCBS_BUFFSIZE_16K	0x00000000
#define TCBS_BUFFSIZE_32K	0x00000001
#define TCBS_BUFFSIZE_64K	0x00000002
#define TCBS_BUFFSIZE_128K	0x00000003
#define TCBS_BUFFSIZE_256K	0x00000004
#define TCBS_BUFFSIZE_512K	0x00000005
#define TCBS_BUFFSIZE_1024K	0x00000006
#define TCBS_BUFFSIZE_2048K	0x00000007

#define MICBA			0x45		/* AC97 microphone buffer address register		*/
#define MICBA_MASK		0xfffff000	/* 20 bit base address					*/

#define ADCBA			0x46		/* ADC buffer address register				*/
#define ADCBA_MASK		0xfffff000	/* 20 bit base address					*/

#define FXBA			0x47		/* FX Buffer Address */
#define FXBA_MASK		0xfffff000	/* 20 bit base address					*/

#define A_HWM			0x48		/* High PCI Water Mark - word access, defaults to 3f */

#define MICBS			0x49		/* Microphone buffer size register			*/

#define ADCBS			0x4a		/* ADC buffer size register				*/

#define FXBS			0x4b		/* FX buffer size register				*/

/* The following mask values define the size of the ADC, MIC and FX buffers in bytes */
#define ADCBS_BUFSIZE_NONE	0x00000000
#define ADCBS_BUFSIZE_384	0x00000001
#define ADCBS_BUFSIZE_448	0x00000002
#define ADCBS_BUFSIZE_512	0x00000003
#define ADCBS_BUFSIZE_640	0x00000004
#define ADCBS_BUFSIZE_768	0x00000005
#define ADCBS_BUFSIZE_896	0x00000006
#define ADCBS_BUFSIZE_1024	0x00000007
#define ADCBS_BUFSIZE_1280	0x00000008
#define ADCBS_BUFSIZE_1536	0x00000009
#define ADCBS_BUFSIZE_1792	0x0000000a
#define ADCBS_BUFSIZE_2048	0x0000000b
#define ADCBS_BUFSIZE_2560	0x0000000c
#define ADCBS_BUFSIZE_3072	0x0000000d
#define ADCBS_BUFSIZE_3584	0x0000000e
#define ADCBS_BUFSIZE_4096	0x0000000f
#define ADCBS_BUFSIZE_5120	0x00000010
#define ADCBS_BUFSIZE_6144	0x00000011
#define ADCBS_BUFSIZE_7168	0x00000012
#define ADCBS_BUFSIZE_8192	0x00000013
#define ADCBS_BUFSIZE_10240	0x00000014
#define ADCBS_BUFSIZE_12288	0x00000015
#define ADCBS_BUFSIZE_14366	0x00000016
#define ADCBS_BUFSIZE_16384	0x00000017
#define ADCBS_BUFSIZE_20480	0x00000018
#define ADCBS_BUFSIZE_24576	0x00000019
#define ADCBS_BUFSIZE_28672	0x0000001a
#define ADCBS_BUFSIZE_32768	0x0000001b
#define ADCBS_BUFSIZE_40960	0x0000001c
#define ADCBS_BUFSIZE_49152	0x0000001d
#define ADCBS_BUFSIZE_57344	0x0000001e
#define ADCBS_BUFSIZE_65536	0x0000001f

// On Audigy, the FX send amounts are not applied instantly, but determine
// targets towards which the following registers swerve gradually.
#define A_CSBA			0x4c		/* FX send B & A current amounts			*/
#define A_CSDC			0x4d		/* FX send D & C current amounts			*/
#define A_CSFE			0x4e		/* FX send F & E current amounts			*/
#define A_CSHG			0x4f		/* FX send H & G current amounts			*/

// NOTE: 0x50,51,52: 64-bit (split over voices 0 & 1)
#define CDCS			0x50		/* CD-ROM digital channel status register		*/

#define GPSCS			0x51		/* General Purpose SPDIF channel status register	*/

// Corresponding EMU10K1_DBG_* constants are in the public header
#define DBG			0x52

#define A_SPSC			0x52		/* S/PDIF Input C Channel Status			*/

#define REG53			0x53		/* DO NOT PROGRAM THIS REGISTER!!! MAY DESTROY CHIP	*/

// Corresponding A_DBG_* constants are in the public header
#define A_DBG			0x53

// NOTE: 0x54,55,56: 64-bit (split over voices 0 & 1)
#define SPCS0			0x54		/* SPDIF output Channel Status 0 register	*/

#define SPCS1			0x55		/* SPDIF output Channel Status 1 register	*/

#define SPCS2			0x56		/* SPDIF output Channel Status 2 register	*/

#define SPCS_CLKACCYMASK	0x30000000	/* Clock accuracy				*/
#define SPCS_CLKACCY_1000PPM	0x00000000	/* 1000 parts per million			*/
#define SPCS_CLKACCY_50PPM	0x10000000	/* 50 parts per million				*/
#define SPCS_CLKACCY_VARIABLE	0x20000000	/* Variable accuracy				*/
#define SPCS_SAMPLERATEMASK	0x0f000000	/* Sample rate					*/
#define SPCS_SAMPLERATE_44	0x00000000	/* 44.1kHz sample rate				*/
#define SPCS_SAMPLERATE_48	0x02000000	/* 48kHz sample rate				*/
#define SPCS_SAMPLERATE_32	0x03000000	/* 32kHz sample rate				*/
#define SPCS_CHANNELNUMMASK	0x00f00000	/* Channel number				*/
#define SPCS_CHANNELNUM_UNSPEC	0x00000000	/* Unspecified channel number			*/
#define SPCS_CHANNELNUM_LEFT	0x00100000	/* Left channel					*/
#define SPCS_CHANNELNUM_RIGHT	0x00200000	/* Right channel				*/
#define SPCS_SOURCENUMMASK	0x000f0000	/* Source number				*/
#define SPCS_SOURCENUM_UNSPEC	0x00000000	/* Unspecified source number			*/
#define SPCS_GENERATIONSTATUS	0x00008000	/* Originality flag (see IEC-958 spec)		*/
#define SPCS_CATEGORYCODEMASK	0x00007f00	/* Category code (see IEC-958 spec)		*/
#define SPCS_MODEMASK		0x000000c0	/* Mode (see IEC-958 spec)			*/
#define SPCS_EMPHASISMASK	0x00000038	/* Emphasis					*/
#define SPCS_EMPHASIS_NONE	0x00000000	/* No emphasis					*/
#define SPCS_EMPHASIS_50_15	0x00000008	/* 50/15 usec 2 channel				*/
#define SPCS_COPYRIGHT		0x00000004	/* Copyright asserted flag -- do not modify	*/
#define SPCS_NOTAUDIODATA	0x00000002	/* 0 = Digital audio, 1 = not audio		*/
#define SPCS_PROFESSIONAL	0x00000001	/* 0 = Consumer (IEC-958), 1 = pro (AES3-1992)	*/

/* 0x57: Not used */

/* The 32-bit CLIx and SOLEx registers all have one bit per channel control/status      	*/
#define CLIEL			0x58		/* Channel loop interrupt enable low register	*/
#define CLIEH			0x59		/* Channel loop interrupt enable high register	*/

#define CLIPL			0x5a		/* Channel loop interrupt pending low register	*/
#define CLIPH			0x5b		/* Channel loop interrupt pending high register	*/

// These cause CPF_STOP_MASK to be set shortly after CCCA_CURRADDR passes DSL_LOOPENDADDR.
// Subsequent changes to the address registers don't resume; clearing the bit here or in CPF does.
// The registers are NOT synchronized; the next serviced channel picks up immediately.
#define SOLEL			0x5c		/* Stop on loop enable low register		*/
#define SOLEH			0x5d		/* Stop on loop enable high register		*/

#define SPBYPASS		0x5e		/* SPDIF BYPASS mode register			*/
#define SPBYPASS_SPDIF0_MASK	0x00000003	/* SPDIF 0 bypass mode				*/
#define SPBYPASS_SPDIF1_MASK	0x0000000c	/* SPDIF 1 bypass mode				*/
/* bypass mode: 0 - DSP; 1 - SPDIF A, 2 - SPDIF B, 3 - SPDIF C					*/
#define SPBYPASS_FORMAT		0x00000f00      /* If 1, SPDIF XX uses 24 bit, if 0 - 20 bit	*/

#define AC97SLOT		0x5f            /* additional AC97 slots enable bits		*/
#define AC97SLOT_REAR_RIGHT	0x01		/* Rear left					*/
#define AC97SLOT_REAR_LEFT	0x02		/* Rear right					*/
#define AC97SLOT_CNTR		0x10            /* Center enable				*/
#define AC97SLOT_LFE		0x20            /* LFE enable					*/

#define A_PCB			0x5f		/* PCB Revision					*/

// NOTE: 0x60,61,62: 64-bit
#define CDSRCS			0x60		/* CD-ROM Sample Rate Converter status register	*/

#define GPSRCS			0x61		/* General Purpose SPDIF sample rate cvt status */

#define ZVSRCS			0x62		/* ZVideo sample rate converter status		*/
						/* NOTE: This one has no SPDIFLOCKED field	*/
						/* Assumes sample lock				*/

/* These three bitfields apply to CDSRCS, GPSRCS, and (except as noted) ZVSRCS.			*/
#define SRCS_SPDIFVALID		0x04000000	/* SPDIF stream valid				*/
#define SRCS_SPDIFLOCKED	0x02000000	/* SPDIF stream locked				*/
#define SRCS_RATELOCKED		0x01000000	/* Sample rate locked				*/
#define SRCS_ESTSAMPLERATE	0x0007ffff	/* Do not modify this field.			*/

/* Note that these values can vary +/- by a small amount                                        */
#define SRCS_SPDIFRATE_44	0x0003acd9
#define SRCS_SPDIFRATE_48	0x00040000
#define SRCS_SPDIFRATE_96	0x00080000

#define MICIDX                  0x63            /* Microphone recording buffer index register   */
SUB_REG(MICIDX, IDX,		0x0000ffff)

#define ADCIDX			0x64		/* ADC recording buffer index register		*/
SUB_REG(ADCIDX, IDX,		0x0000ffff)

#define A_ADCIDX		0x63
SUB_REG(A_ADCIDX, IDX,		0x0000ffff)

#define A_MICIDX		0x64
SUB_REG(A_MICIDX, IDX,		0x0000ffff)

#define FXIDX			0x65		/* FX recording buffer index register		*/
SUB_REG(FXIDX, IDX,		0x0000ffff)

/* The 32-bit HLIEx and HLIPx registers all have one bit per channel control/status      		*/
#define HLIEL			0x66		/* Channel half loop interrupt enable low register	*/
#define HLIEH			0x67		/* Channel half loop interrupt enable high register	*/

#define HLIPL			0x68		/* Channel half loop interrupt pending low register	*/
#define HLIPH			0x69		/* Channel half loop interrupt pending high register	*/

#define A_SPRI			0x6a		/* S/PDIF Host Record Index (bypasses SRC)	*/
#define A_SPRA			0x6b		/* S/PDIF Host Record Address			*/
#define A_SPRC			0x6c		/* S/PDIF Host Record Control			*/

#define A_DICE			0x6d		/* Delayed Interrupt Counter & Enable		*/

#define A_TTB			0x6e		/* Tank Table Base				*/
#define A_TDOF			0x6f		/* Tank Delay Offset				*/

/* This is the MPU port on the card (via the game port)						*/
#define A_MUDATA1		0x70
#define A_MUCMD1		0x71
#define A_MUSTAT1		A_MUCMD1

/* This is the MPU port on the Audigy Drive 							*/
#define A_MUDATA2		0x72
#define A_MUCMD2		0x73
#define A_MUSTAT2		A_MUCMD2	

/* The next two are the Audigy equivalent of FXWC						*/
/* the Audigy can record any output (16bit, 48kHz, up to 64 channels simultaneously) 		*/
/* Each bit selects a channel for recording */
#define A_FXWC1			0x74            /* Selects 0x7f-0x60 for FX recording           */
#define A_FXWC2			0x75		/* Selects 0x9f-0x80 for FX recording           */

#define A_EHC			0x76		/* Extended Hardware Control */

#define A_SPDIF_SAMPLERATE	A_EHC		/* Set the sample rate of SPDIF output		*/
#define A_SPDIF_RATE_MASK	0x000000e0	/* Any other values for rates, just use 48000	*/
#define A_SPDIF_48000		0x00000000	/* kX calls this BYPASS				*/
#define A_SPDIF_192000		0x00000020
#define A_SPDIF_96000		0x00000040
#define A_SPDIF_44100		0x00000080
#define A_SPDIF_MUTED		0x000000c0

SUB_REG_NC(A_EHC, A_I2S_CAPTURE_RATE, 0x00000e00)  /* This sets the capture PCM rate, but it is  */
						   /* unclear if this sets the ADC rate as well. */
#define A_I2S_CAPTURE_48000	0x0
#define A_I2S_CAPTURE_192000	0x1
#define A_I2S_CAPTURE_96000	0x2
#define A_I2S_CAPTURE_44100	0x4

#define A_EHC_SRC48_MASK	0x0000e000	/* This sets the playback PCM rate on the P16V	*/
#define A_EHC_SRC48_BYPASS	0x00000000
#define A_EHC_SRC48_192		0x00002000
#define A_EHC_SRC48_96		0x00004000
#define A_EHC_SRC48_44		0x00008000
#define A_EHC_SRC48_MUTED	0x0000c000

#define A_EHC_P17V_TVM		0x00000001	/* Tank virtual memory mode			*/
#define A_EHC_P17V_SEL0_MASK	0x00030000	/* Aka A_EHC_P16V_PB_RATE; 00: 48, 01: 44.1, 10: 96, 11: 192 */
#define A_EHC_P17V_SEL1_MASK	0x000c0000
#define A_EHC_P17V_SEL2_MASK	0x00300000
#define A_EHC_P17V_SEL3_MASK	0x00c00000

#define A_EHC_ASYNC_BYPASS	0x80000000

#define A_SRT3			0x77		/* I2S0 Sample Rate Tracker Status		*/
#define A_SRT4			0x78		/* I2S1 Sample Rate Tracker Status		*/
#define A_SRT5			0x79		/* I2S2 Sample Rate Tracker Status		*/
/* - default to 0x01080000 on my audigy 2 ZS --rlrevell	*/

#define A_SRT_ESTSAMPLERATE	0x001fffff
#define A_SRT_RATELOCKED	0x01000000

#define A_TTDA			0x7a		/* Tank Table DMA Address			*/
#define A_TTDD			0x7b		/* Tank Table DMA Data				*/

// In A_FXRT1 & A_FXRT2, the 0x80 bit of each byte completely disables the
// filter (CVCF_CURRENTFILTER) for the corresponding channel. There is no
// effect on the volume (CVCF_CURRENTVOLUME) or the interpolator's filter
// (CCCA_INTERPROM_MASK).

#define A_FXRT2			0x7c
#define A_FXRT_CHANNELE		0x0000003f	/* Effects send bus number for channel's effects send E	*/
#define A_FXRT_CHANNELF		0x00003f00	/* Effects send bus number for channel's effects send F	*/
#define A_FXRT_CHANNELG		0x003f0000	/* Effects send bus number for channel's effects send G	*/
#define A_FXRT_CHANNELH		0x3f000000	/* Effects send bus number for channel's effects send H	*/

#define A_SENDAMOUNTS		0x7d
#define A_FXSENDAMOUNT_E_MASK	0xFF000000
#define A_FXSENDAMOUNT_F_MASK	0x00FF0000
#define A_FXSENDAMOUNT_G_MASK	0x0000FF00
#define A_FXSENDAMOUNT_H_MASK	0x000000FF

/* The send amounts for this one are the same as used with the emu10k1 */
#define A_FXRT1			0x7e
#define A_FXRT_CHANNELA		0x0000003f
#define A_FXRT_CHANNELB		0x00003f00
#define A_FXRT_CHANNELC		0x003f0000
#define A_FXRT_CHANNELD		0x3f000000

/* 0x7f: Not used */

/* The public header defines the GPR and TRAM base addresses that
 * are valid for _both_ CPU and DSP addressing. */

/* Each DSP microcode instruction is mapped into 2 doublewords 					*/
/* NOTE: When writing, always write the LO doubleword first.  Reads can be in either order.	*/
#define MICROCODEBASE		0x400		/* Microcode data base address			*/
#define A_MICROCODEBASE		0x600


/************************************************************************************************/
/* E-MU Digital Audio System overview								*/
/************************************************************************************************/

// - These cards use a regular PCI-attached Audigy chip (Alice2/Tina/Tina2);
//   the PCIe variants simply put the Audigy chip behind a PCI bridge.
// - All physical PCM I/O is routed through an additional FPGA; the regular
//   EXTIN/EXTOUT ports are unconnected.
// - The FPGA has a signal routing matrix, to connect each destination (output
//   socket or capture channel) to a source (input socket or playback channel).
// - The FPGA is controlled via Audigy's GPIO port, while sample data is
//   transmitted via proprietary EMU32 serial links. On first-generation
//   E-MU 1010 cards, Audigy's I2S inputs are also used for sample data.
// - The Audio/Micro Dock is attached to Hana via EDI, a "network" link.
// - The Audigy chip operates in slave mode; the clock is supplied by the FPGA.
//   Gen1 E-MU 1010 cards have two crystals (for 44.1 kHz and 48 kHz multiples),
//   while the later cards use a single crystal and a PLL chip.
// - The whole card is switched to 2x/4x mode to achieve 88.2/96/176.4/192 kHz
//   sample rates. Alice2/Tina keeps running at 44.1/48 kHz, but multiple channels
//   are bundled.
// - The number of available EMU32/EDI channels is hit in 2x/4x mode, so the total
//   number of usable inputs/outputs is limited, esp. with ADAT in use.
// - S/PDIF is unavailable in 4x mode (only over TOSLINK on newer 1010 cards) due
//   to being unspecified at 176.4/192 kHz. Therefore, the Dock's S/PDIF channels
//   can overlap with the Dock's ADC/DAC's high channels.
// - The code names are mentioned below and in the emu_chip_details table.

/************************************************************************************************/
/* EMU1010 FPGA registers									*/
/************************************************************************************************/

#define EMU_HANA_DESTHI		0x00	/* 0000xxx  3 bits Link Destination */
#define EMU_HANA_DESTLO		0x01	/* 00xxxxx  5 bits */

#define EMU_HANA_SRCHI		0x02	/* 0000xxx  3 bits Link Source */
#define EMU_HANA_SRCLO		0x03	/* 00xxxxx  5 bits */

#define EMU_HANA_DOCK_PWR	0x04	/* 000000x  1 bits Audio Dock power */
#define EMU_HANA_DOCK_PWR_ON		0x01 /* Audio Dock power on */

#define EMU_HANA_WCLOCK		0x05	/* 0000xxx  3 bits Word Clock source select  */
					/* Must be written after power on to reset DLL */
					/* One is unable to detect the Audio dock without this */
#define EMU_HANA_WCLOCK_SRC_MASK	0x07
#define EMU_HANA_WCLOCK_INT_48K		0x00
#define EMU_HANA_WCLOCK_INT_44_1K	0x01
#define EMU_HANA_WCLOCK_HANA_SPDIF_IN	0x02
#define EMU_HANA_WCLOCK_HANA_ADAT_IN	0x03
#define EMU_HANA_WCLOCK_SYNC_BNC	0x04
#define EMU_HANA_WCLOCK_2ND_HANA	0x05
#define EMU_HANA_WCLOCK_SRC_RESERVED	0x06
#define EMU_HANA_WCLOCK_OFF		0x07 /* For testing, forces fallback to DEFCLOCK */
#define EMU_HANA_WCLOCK_MULT_MASK	0x18
#define EMU_HANA_WCLOCK_1X		0x00
#define EMU_HANA_WCLOCK_2X		0x08
#define EMU_HANA_WCLOCK_4X		0x10
#define EMU_HANA_WCLOCK_MULT_RESERVED	0x18

// If the selected external clock source is/becomes invalid or incompatible
// with the clock multiplier, the clock source is reset to this value, and
// a WCLK_CHANGED interrupt is raised.
#define EMU_HANA_DEFCLOCK	0x06	/* 000000x  1 bits Default Word Clock  */
#define EMU_HANA_DEFCLOCK_48K		0x00
#define EMU_HANA_DEFCLOCK_44_1K		0x01

#define EMU_HANA_UNMUTE		0x07	/* 000000x  1 bits Mute all audio outputs  */
#define EMU_MUTE			0x00
#define EMU_UNMUTE			0x01

#define EMU_HANA_FPGA_CONFIG	0x08	/* 00000xx  2 bits Config control of FPGAs  */
#define EMU_HANA_FPGA_CONFIG_AUDIODOCK	0x01 /* Set in order to program FPGA on Audio Dock */
#define EMU_HANA_FPGA_CONFIG_HANA	0x02 /* Set in order to program FPGA on Hana */

#define EMU_HANA_IRQ_ENABLE	0x09	/* 000xxxx  4 bits IRQ Enable  */
#define EMU_HANA_IRQ_WCLK_CHANGED	0x01
#define EMU_HANA_IRQ_ADAT		0x02
#define EMU_HANA_IRQ_DOCK		0x04
#define EMU_HANA_IRQ_DOCK_LOST		0x08

#define EMU_HANA_SPDIF_MODE	0x0a	/* 00xxxxx  5 bits SPDIF MODE  */
#define EMU_HANA_SPDIF_MODE_TX_CONSUMER	0x00
#define EMU_HANA_SPDIF_MODE_TX_PRO	0x01
#define EMU_HANA_SPDIF_MODE_TX_NOCOPY	0x02
#define EMU_HANA_SPDIF_MODE_RX_CONSUMER	0x00
#define EMU_HANA_SPDIF_MODE_RX_PRO	0x04
#define EMU_HANA_SPDIF_MODE_RX_NOCOPY	0x08
#define EMU_HANA_SPDIF_MODE_RX_INVALID	0x10

#define EMU_HANA_OPTICAL_TYPE	0x0b	/* 00000xx  2 bits ADAT or SPDIF in/out  */
#define EMU_HANA_OPTICAL_IN_SPDIF	0x00
#define EMU_HANA_OPTICAL_IN_ADAT	0x01
#define EMU_HANA_OPTICAL_OUT_SPDIF	0x00
#define EMU_HANA_OPTICAL_OUT_ADAT	0x02

#define EMU_HANA_MIDI_IN		0x0c	/* 000000x  1 bit  Control MIDI  */
#define EMU_HANA_MIDI_INA_FROM_HAMOA	0x01 /* HAMOA MIDI in to Alice 2 MIDI A */
#define EMU_HANA_MIDI_INA_FROM_DOCK1	0x02 /* Audio Dock-1 MIDI in to Alice 2 MIDI A */
#define EMU_HANA_MIDI_INA_FROM_DOCK2	0x03 /* Audio Dock-2 MIDI in to Alice 2 MIDI A */
#define EMU_HANA_MIDI_INB_FROM_HAMOA	0x08 /* HAMOA MIDI in to Alice 2 MIDI B */
#define EMU_HANA_MIDI_INB_FROM_DOCK1	0x10 /* Audio Dock-1 MIDI in to Alice 2 MIDI B */
#define EMU_HANA_MIDI_INB_FROM_DOCK2	0x18 /* Audio Dock-2 MIDI in to Alice 2 MIDI B */

#define EMU_HANA_DOCK_LEDS_1	0x0d	/* 000xxxx  4 bit  Audio Dock LEDs  */
#define EMU_HANA_DOCK_LEDS_1_MIDI1	0x01	/* MIDI 1 LED on */
#define EMU_HANA_DOCK_LEDS_1_MIDI2	0x02	/* MIDI 2 LED on */
#define EMU_HANA_DOCK_LEDS_1_SMPTE_IN	0x04	/* SMPTE IN LED on */
#define EMU_HANA_DOCK_LEDS_1_SMPTE_OUT	0x08	/* SMPTE OUT LED on */

#define EMU_HANA_DOCK_LEDS_2	0x0e	/* 0xxxxxx  6 bit  Audio Dock LEDs  */
#define EMU_HANA_DOCK_LEDS_2_44K	0x01	/* 44.1 kHz LED on */
#define EMU_HANA_DOCK_LEDS_2_48K	0x02	/* 48 kHz LED on */
#define EMU_HANA_DOCK_LEDS_2_96K	0x04	/* 96 kHz LED on */
#define EMU_HANA_DOCK_LEDS_2_192K	0x08	/* 192 kHz LED on */
#define EMU_HANA_DOCK_LEDS_2_LOCK	0x10	/* LOCK LED on */
#define EMU_HANA_DOCK_LEDS_2_EXT	0x20	/* EXT LED on */

#define EMU_HANA_DOCK_LEDS_3	0x0f	/* 0xxxxxx  6 bit  Audio Dock LEDs  */
#define EMU_HANA_DOCK_LEDS_3_CLIP_A	0x01	/* Mic A Clip LED on */
#define EMU_HANA_DOCK_LEDS_3_CLIP_B	0x02	/* Mic B Clip LED on */
#define EMU_HANA_DOCK_LEDS_3_SIGNAL_A	0x04	/* Signal A Clip LED on */
#define EMU_HANA_DOCK_LEDS_3_SIGNAL_B	0x08	/* Signal B Clip LED on */
#define EMU_HANA_DOCK_LEDS_3_MANUAL_CLIP	0x10	/* Manual Clip detection */
#define EMU_HANA_DOCK_LEDS_3_MANUAL_SIGNAL	0x20	/* Manual Signal detection */

#define EMU_HANA_ADC_PADS	0x10	/* 0000xxx  3 bit  Audio Dock ADC 14dB pads */
#define EMU_HANA_DOCK_ADC_PAD1		0x01	/* 14dB Attenuation on Audio Dock ADC 1 */
#define EMU_HANA_DOCK_ADC_PAD2		0x02	/* 14dB Attenuation on Audio Dock ADC 2 */
#define EMU_HANA_DOCK_ADC_PAD3		0x04	/* 14dB Attenuation on Audio Dock ADC 3 */
#define EMU_HANA_0202_ADC_PAD1		0x08	/* 14dB Attenuation on 0202 ADC 1 */

#define EMU_HANA_DOCK_MISC	0x11	/* 0xxxxxx  6 bit  Audio Dock misc bits */
#define EMU_HANA_DOCK_DAC1_MUTE		0x01	/* DAC 1 Mute */
#define EMU_HANA_DOCK_DAC2_MUTE		0x02	/* DAC 2 Mute */
#define EMU_HANA_DOCK_DAC3_MUTE		0x04	/* DAC 3 Mute */
#define EMU_HANA_DOCK_DAC4_MUTE		0x08	/* DAC 4 Mute */
#define EMU_HANA_DOCK_PHONES_192_DAC1	0x00	/* DAC 1 Headphones source at 192kHz */
#define EMU_HANA_DOCK_PHONES_192_DAC2	0x10	/* DAC 2 Headphones source at 192kHz */
#define EMU_HANA_DOCK_PHONES_192_DAC3	0x20	/* DAC 3 Headphones source at 192kHz */
#define EMU_HANA_DOCK_PHONES_192_DAC4	0x30	/* DAC 4 Headphones source at 192kHz */

#define EMU_HANA_MIDI_OUT	0x12	/* 00xxxxx  5 bit  Source for each MIDI out port */
#define EMU_HANA_MIDI_OUT_0202		0x01 /* 0202 MIDI from Alice 2. 0 = A, 1 = B */
#define EMU_HANA_MIDI_OUT_DOCK1		0x02 /* Audio Dock MIDI1 front, from Alice 2. 0 = A, 1 = B */
#define EMU_HANA_MIDI_OUT_DOCK2		0x04 /* Audio Dock MIDI2 rear, from Alice 2. 0 = A, 1 = B */
#define EMU_HANA_MIDI_OUT_SYNC2		0x08 /* Sync card. Not the actual MIDI out jack. 0 = A, 1 = B */
#define EMU_HANA_MIDI_OUT_LOOP		0x10 /* 0 = bits (3:0) normal. 1 = MIDI loopback enabled. */

#define EMU_HANA_DAC_PADS	0x13	/* 00xxxxx  5 bit  DAC 14dB attenuation pads */
#define EMU_HANA_DOCK_DAC_PAD1		0x01	/* 14dB Attenuation on AudioDock DAC 1. Left and Right */
#define EMU_HANA_DOCK_DAC_PAD2		0x02	/* 14dB Attenuation on AudioDock DAC 2. Left and Right */
#define EMU_HANA_DOCK_DAC_PAD3		0x04	/* 14dB Attenuation on AudioDock DAC 3. Left and Right */
#define EMU_HANA_DOCK_DAC_PAD4		0x08	/* 14dB Attenuation on AudioDock DAC 4. Left and Right */
#define EMU_HANA_0202_DAC_PAD1		0x10	/* 14dB Attenuation on 0202 DAC 1. Left and Right */

/* 0x14 - 0x1f Unused R/W registers */

#define EMU_HANA_IRQ_STATUS	0x20	/* 00xxxxx  5 bits IRQ Status  */
					/* Same bits as for EMU_HANA_IRQ_ENABLE */
					/* Reading the register resets it. */

#define EMU_HANA_OPTION_CARDS	0x21	/* 000xxxx  4 bits Presence of option cards */
#define EMU_HANA_OPTION_HAMOA		0x01	/* Hamoa (analog I/O) card present */
#define EMU_HANA_OPTION_SYNC		0x02	/* Sync card present */
#define EMU_HANA_OPTION_DOCK_ONLINE	0x04	/* Audio/Micro dock present and FPGA configured */
#define EMU_HANA_OPTION_DOCK_OFFLINE	0x08	/* Audio/Micro dock present and FPGA not configured */

#define EMU_HANA_ID		0x22	/* 1010101  7 bits ID byte & 0x7f = 0x55 with Alice2 */
					/* 0010101  5 bits ID byte & 0x1f = 0x15 with Tina/2 */

#define EMU_HANA_MAJOR_REV	0x23	/* 0000xxx  3 bit  Hana FPGA Major rev */
#define EMU_HANA_MINOR_REV	0x24	/* 0000xxx  3 bit  Hana FPGA Minor rev */

#define EMU_DOCK_MAJOR_REV	0x25	/* 0000xxx  3 bit  Audio Dock FPGA Major rev */
#define EMU_DOCK_MINOR_REV	0x26	/* 0000xxx  3 bit  Audio Dock FPGA Minor rev */

#define EMU_DOCK_BOARD_ID	0x27	/* 00000xx  2 bits Audio Dock ID pins */
#define EMU_DOCK_BOARD_ID0		0x00	/* ID bit 0 */
#define EMU_DOCK_BOARD_ID1		0x03	/* ID bit 1 */

// The actual code disagrees about the bit width of the registers -
// the formula used is freq = 0x1770000 / (((X_HI << 5) | X_LO) + 1)

#define EMU_HANA_WC_SPDIF_HI	0x28	/* 0xxxxxx  6 bit  SPDIF IN Word clock, upper 6 bits */
#define EMU_HANA_WC_SPDIF_LO	0x29	/* 0xxxxxx  6 bit  SPDIF IN Word clock, lower 6 bits */

#define EMU_HANA_WC_ADAT_HI	0x2a	/* 0xxxxxx  6 bit  ADAT IN Word clock, upper 6 bits */
#define EMU_HANA_WC_ADAT_LO	0x2b	/* 0xxxxxx  6 bit  ADAT IN Word clock, lower 6 bits */

#define EMU_HANA_WC_BNC_LO	0x2c	/* 0xxxxxx  6 bit  BNC IN Word clock, lower 6 bits */
#define EMU_HANA_WC_BNC_HI	0x2d	/* 0xxxxxx  6 bit  BNC IN Word clock, upper 6 bits */

#define EMU_HANA2_WC_SPDIF_HI	0x2e	/* 0xxxxxx  6 bit  HANA2 SPDIF IN Word clock, upper 6 bits */
#define EMU_HANA2_WC_SPDIF_LO	0x2f	/* 0xxxxxx  6 bit  HANA2 SPDIF IN Word clock, lower 6 bits */

/* 0x30 - 0x3f Unused Read only registers */

// The meaning of this is not clear; kX-project just calls it "lock" in some info-only code.
#define EMU_HANA_LOCK_STS_LO	0x38	/* 0xxxxxx  lower 6 bits */
#define EMU_HANA_LOCK_STS_HI	0x39	/* 0xxxxxx  upper 6 bits */

/************************************************************************************************/
/* EMU1010 Audio Destinations									*/
/************************************************************************************************/
/* Hana, original 1010,1212m,1820[m] using Alice2
 * 0x00, 0x00-0x0f: 16 EMU32 channels to Alice2
 * 0x01, 0x00-0x1f: 32 EDI channels to Audio Dock
 *       0x00: Dock DAC 1 Left
 *       0x04: Dock DAC 1 Right
 *       0x08: Dock DAC 2 Left
 *       0x0c: Dock DAC 2 Right
 *       0x10: Dock DAC 3 Left
 *       0x12: PHONES Left (n/a in 2x/4x mode; output mirrors DAC4 Left)
 *       0x14: Dock DAC 3 Right
 *       0x16: PHONES Right (n/a in 2x/4x mode; output mirrors DAC4 Right)
 *       0x18: Dock DAC 4 Left
 *       0x1a: S/PDIF Left
 *       0x1c: Dock DAC 4 Right
 *       0x1e: S/PDIF Right
 * 0x02, 0x00: Hana S/PDIF Left
 * 0x02, 0x01: Hana S/PDIF Right
 * 0x03, 0x00: Hamoa DAC Left
 * 0x03, 0x01: Hamoa DAC Right
 * 0x04, 0x00-0x07: Hana ADAT
 * 0x05, 0x00: I2S0 Left to Alice2
 * 0x05, 0x01: I2S0 Right to Alice2
 * 0x06, 0x00: I2S0 Left to Alice2
 * 0x06, 0x01: I2S0 Right to Alice2
 * 0x07, 0x00: I2S0 Left to Alice2
 * 0x07, 0x01: I2S0 Right to Alice2
 *
 * Hana2 never released, but used Tina
 * Not needed.
 *
 * Hana3, rev2 1010,1212m,1616[m] using Tina
 * 0x00, 0x00-0x0f: 16 EMU32A channels to Tina
 * 0x01, 0x00-0x1f: 32 EDI channels to Micro Dock
 *       0x00: Dock DAC 1 Left
 *       0x04: Dock DAC 1 Right
 *       0x08: Dock DAC 2 Left
 *       0x0c: Dock DAC 2 Right
 *       0x10: Dock DAC 3 Left
 *       0x12: Dock S/PDIF Left
 *       0x14: Dock DAC 3 Right
 *       0x16: Dock S/PDIF Right
 *       0x18-0x1f: Dock ADAT 0-7
 * 0x02, 0x00: Hana3 S/PDIF Left
 * 0x02, 0x01: Hana3 S/PDIF Right
 * 0x03, 0x00: Hamoa DAC Left
 * 0x03, 0x01: Hamoa DAC Right
 * 0x04, 0x00-0x07: Hana3 ADAT 0-7
 * 0x05, 0x00-0x0f: 16 EMU32B channels to Tina
 * 0x06-0x07: Not used
 *
 * HanaLite, rev1 0404 using Alice2
 * HanaLiteLite, rev2 0404 using Tina
 * 0x00, 0x00-0x0f: 16 EMU32 channels to Alice2/Tina
 * 0x01: Not used
 * 0x02, 0x00: S/PDIF Left
 * 0x02, 0x01: S/PDIF Right
 * 0x03, 0x00: DAC Left
 * 0x03, 0x01: DAC Right
 * 0x04-0x07: Not used
 *
 * Mana, Cardbus 1616 using Tina2
 * 0x00, 0x00-0x0f: 16 EMU32A channels to Tina2
 * 0x01, 0x00-0x1f: 32 EDI channels to Micro Dock
 *       (same as rev2 1010)
 * 0x02: Not used
 * 0x03, 0x00: Mana DAC Left
 * 0x03, 0x01: Mana DAC Right
 * 0x04, 0x00-0x0f: 16 EMU32B channels to Tina2
 * 0x05-0x07: Not used
 */

/* 32-bit destinations of signal in the Hana FPGA. Destinations are either
 * physical outputs of Hana, or outputs going to Alice2/Tina for capture -
 * 16 x EMU_DST_ALICE2_EMU32_X (2x on rev2 boards). Which data is fed into
 * a channel depends on the mixer control setting for each destination - see
 * the register arrays in emumixer.c.
 */
#define EMU_DST_ALICE2_EMU32_0	0x000f	/* 16 EMU32 channels to Alice2 +0 to +0xf */
					/* This channel is delayed by one sample. */
#define EMU_DST_ALICE2_EMU32_1	0x0000	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_2	0x0001	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_3	0x0002	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_4	0x0003	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_5	0x0004	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_6	0x0005	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_7	0x0006	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_8	0x0007	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_9	0x0008	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_A	0x0009	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_B	0x000a	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_C	0x000b	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_D	0x000c	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_E	0x000d	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_ALICE2_EMU32_F	0x000e	/* 16 EMU32 channels to Alice2 +0 to +0xf */
#define EMU_DST_DOCK_DAC1_LEFT1	0x0100	/* Audio Dock DAC1 Left, 1st or 48kHz only */
#define EMU_DST_DOCK_DAC1_LEFT2	0x0101	/* Audio Dock DAC1 Left, 2nd or 96kHz */
#define EMU_DST_DOCK_DAC1_LEFT3	0x0102	/* Audio Dock DAC1 Left, 3rd or 192kHz */
#define EMU_DST_DOCK_DAC1_LEFT4	0x0103	/* Audio Dock DAC1 Left, 4th or 192kHz */
#define EMU_DST_DOCK_DAC1_RIGHT1	0x0104	/* Audio Dock DAC1 Right, 1st or 48kHz only */
#define EMU_DST_DOCK_DAC1_RIGHT2	0x0105	/* Audio Dock DAC1 Right, 2nd or 96kHz */
#define EMU_DST_DOCK_DAC1_RIGHT3	0x0106	/* Audio Dock DAC1 Right, 3rd or 192kHz */
#define EMU_DST_DOCK_DAC1_RIGHT4	0x0107	/* Audio Dock DAC1 Right, 4th or 192kHz */
#define EMU_DST_DOCK_DAC2_LEFT1	0x0108	/* Audio Dock DAC2 Left, 1st or 48kHz only */
#define EMU_DST_DOCK_DAC2_LEFT2	0x0109	/* Audio Dock DAC2 Left, 2nd or 96kHz */
#define EMU_DST_DOCK_DAC2_LEFT3	0x010a	/* Audio Dock DAC2 Left, 3rd or 192kHz */
#define EMU_DST_DOCK_DAC2_LEFT4	0x010b	/* Audio Dock DAC2 Left, 4th or 192kHz */
#define EMU_DST_DOCK_DAC2_RIGHT1	0x010c	/* Audio Dock DAC2 Right, 1st or 48kHz only */
#define EMU_DST_DOCK_DAC2_RIGHT2	0x010d	/* Audio Dock DAC2 Right, 2nd or 96kHz */
#define EMU_DST_DOCK_DAC2_RIGHT3	0x010e	/* Audio Dock DAC2 Right, 3rd or 192kHz */
#define EMU_DST_DOCK_DAC2_RIGHT4	0x010f	/* Audio Dock DAC2 Right, 4th or 192kHz */
#define EMU_DST_DOCK_DAC3_LEFT1	0x0110	/* Audio Dock DAC1 Left, 1st or 48kHz only */
#define EMU_DST_DOCK_DAC3_LEFT2	0x0111	/* Audio Dock DAC1 Left, 2nd or 96kHz */
#define EMU_DST_DOCK_DAC3_LEFT3	0x0112	/* Audio Dock DAC1 Left, 3rd or 192kHz */
#define EMU_DST_DOCK_DAC3_LEFT4	0x0113	/* Audio Dock DAC1 Left, 4th or 192kHz */
#define EMU_DST_DOCK_PHONES_LEFT1	0x0112	/* Audio Dock PHONES Left, 1st or 48kHz only */
#define EMU_DST_DOCK_PHONES_LEFT2	0x0113	/* Audio Dock PHONES Left, 2nd or 96kHz */
#define EMU_DST_DOCK_DAC3_RIGHT1	0x0114	/* Audio Dock DAC1 Right, 1st or 48kHz only */
#define EMU_DST_DOCK_DAC3_RIGHT2	0x0115	/* Audio Dock DAC1 Right, 2nd or 96kHz */
#define EMU_DST_DOCK_DAC3_RIGHT3	0x0116	/* Audio Dock DAC1 Right, 3rd or 192kHz */
#define EMU_DST_DOCK_DAC3_RIGHT4	0x0117	/* Audio Dock DAC1 Right, 4th or 192kHz */
#define EMU_DST_DOCK_PHONES_RIGHT1	0x0116	/* Audio Dock PHONES Right, 1st or 48kHz only */
#define EMU_DST_DOCK_PHONES_RIGHT2	0x0117	/* Audio Dock PHONES Right, 2nd or 96kHz */
#define EMU_DST_DOCK_DAC4_LEFT1	0x0118	/* Audio Dock DAC2 Left, 1st or 48kHz only */
#define EMU_DST_DOCK_DAC4_LEFT2	0x0119	/* Audio Dock DAC2 Left, 2nd or 96kHz */
#define EMU_DST_DOCK_DAC4_LEFT3	0x011a	/* Audio Dock DAC2 Left, 3rd or 192kHz */
#define EMU_DST_DOCK_DAC4_LEFT4	0x011b	/* Audio Dock DAC2 Left, 4th or 192kHz */
#define EMU_DST_DOCK_SPDIF_LEFT1	0x011a	/* Audio Dock SPDIF Left, 1st or 48kHz only */
#define EMU_DST_DOCK_SPDIF_LEFT2	0x011b	/* Audio Dock SPDIF Left, 2nd or 96kHz */
#define EMU_DST_DOCK_DAC4_RIGHT1	0x011c	/* Audio Dock DAC2 Right, 1st or 48kHz only */
#define EMU_DST_DOCK_DAC4_RIGHT2	0x011d	/* Audio Dock DAC2 Right, 2nd or 96kHz */
#define EMU_DST_DOCK_DAC4_RIGHT3	0x011e	/* Audio Dock DAC2 Right, 3rd or 192kHz */
#define EMU_DST_DOCK_DAC4_RIGHT4	0x011f	/* Audio Dock DAC2 Right, 4th or 192kHz */
#define EMU_DST_DOCK_SPDIF_RIGHT1	0x011e	/* Audio Dock SPDIF Right, 1st or 48kHz only */
#define EMU_DST_DOCK_SPDIF_RIGHT2	0x011f	/* Audio Dock SPDIF Right, 2nd or 96kHz */
#define EMU_DST_HANA_SPDIF_LEFT1	0x0200	/* Hana SPDIF Left, 1st or 48kHz only */
#define EMU_DST_HANA_SPDIF_LEFT2	0x0202	/* Hana SPDIF Left, 2nd or 96kHz */
#define EMU_DST_HANA_SPDIF_LEFT3	0x0204	/* Hana SPDIF Left, 3rd or 192kHz */
#define EMU_DST_HANA_SPDIF_LEFT4	0x0206	/* Hana SPDIF Left, 4th or 192kHz */
#define EMU_DST_HANA_SPDIF_RIGHT1	0x0201	/* Hana SPDIF Right, 1st or 48kHz only */
#define EMU_DST_HANA_SPDIF_RIGHT2	0x0203	/* Hana SPDIF Right, 2nd or 96kHz */
#define EMU_DST_HANA_SPDIF_RIGHT3	0x0205	/* Hana SPDIF Right, 3rd or 192kHz */
#define EMU_DST_HANA_SPDIF_RIGHT4	0x0207	/* Hana SPDIF Right, 4th or 192kHz */
#define EMU_DST_HAMOA_DAC_LEFT1	0x0300	/* Hamoa DAC Left, 1st or 48kHz only */
#define EMU_DST_HAMOA_DAC_LEFT2	0x0302	/* Hamoa DAC Left, 2nd or 96kHz */
#define EMU_DST_HAMOA_DAC_LEFT3	0x0304	/* Hamoa DAC Left, 3rd or 192kHz */
#define EMU_DST_HAMOA_DAC_LEFT4	0x0306	/* Hamoa DAC Left, 4th or 192kHz */
#define EMU_DST_HAMOA_DAC_RIGHT1	0x0301	/* Hamoa DAC Right, 1st or 48kHz only */
#define EMU_DST_HAMOA_DAC_RIGHT2	0x0303	/* Hamoa DAC Right, 2nd or 96kHz */
#define EMU_DST_HAMOA_DAC_RIGHT3	0x0305	/* Hamoa DAC Right, 3rd or 192kHz */
#define EMU_DST_HAMOA_DAC_RIGHT4	0x0307	/* Hamoa DAC Right, 4th or 192kHz */
// In S/MUX mode, the samples of one channel are adjacent.
#define EMU_DST_HANA_ADAT	0x0400	/* Hana ADAT 8 channel out +0 to +7 */
#define EMU_DST_ALICE_I2S0_LEFT		0x0500	/* Alice2 I2S0 Left */
#define EMU_DST_ALICE_I2S0_RIGHT	0x0501	/* Alice2 I2S0 Right */
#define EMU_DST_ALICE_I2S1_LEFT		0x0600	/* Alice2 I2S1 Left */
#define EMU_DST_ALICE_I2S1_RIGHT	0x0601	/* Alice2 I2S1 Right */
#define EMU_DST_ALICE_I2S2_LEFT		0x0700	/* Alice2 I2S2 Left */
#define EMU_DST_ALICE_I2S2_RIGHT	0x0701	/* Alice2 I2S2 Right */

/* Additional destinations for 1616(M)/Microdock */

#define EMU_DST_MDOCK_SPDIF_LEFT1	0x0112	/* Microdock S/PDIF OUT Left, 1st or 48kHz only */
#define EMU_DST_MDOCK_SPDIF_LEFT2	0x0113	/* Microdock S/PDIF OUT Left, 2nd or 96kHz */
#define EMU_DST_MDOCK_SPDIF_RIGHT1	0x0116	/* Microdock S/PDIF OUT Right, 1st or 48kHz only */
#define EMU_DST_MDOCK_SPDIF_RIGHT2	0x0117	/* Microdock S/PDIF OUT Right, 2nd or 96kHz  */
#define EMU_DST_MDOCK_ADAT		0x0118	/* Microdock S/PDIF ADAT 8 channel out +8 to +f */

#define EMU_DST_MANA_DAC_LEFT		0x0300	/* Headphone jack on 1010 cardbus? 44.1/48kHz only? */
#define EMU_DST_MANA_DAC_RIGHT		0x0301	/* Headphone jack on 1010 cardbus? 44.1/48kHz only? */

/************************************************************************************************/
/* EMU1010 Audio Sources									*/
/************************************************************************************************/
/* Hana, original 1010,1212m,1820[m] using Alice2
 * 0x00, 0x00-0x1f: Silence
 * 0x01, 0x00-0x1f: 32 EDI channels from Audio Dock
 *       0x00: Dock Mic A
 *       0x04: Dock Mic B
 *       0x08: Dock ADC 1 Left
 *       0x0c: Dock ADC 1 Right
 *       0x10: Dock ADC 2 Left
 *       0x14: Dock ADC 2 Right
 *       0x18: Dock ADC 3 Left
 *       0x1c: Dock ADC 3 Right
 * 0x02, 0x00: Hamoa ADC Left
 * 0x02, 0x01: Hamoa ADC Right
 * 0x03, 0x00-0x0f: 16 inputs from Alice2 Emu32A output
 * 0x03, 0x10-0x1f: 16 inputs from Alice2 Emu32B output
 * 0x04, 0x00-0x07: Hana ADAT
 * 0x05, 0x00: Hana S/PDIF Left
 * 0x05, 0x01: Hana S/PDIF Right
 * 0x06-0x07: Not used
 *
 * Hana2 never released, but used Tina
 * Not needed.
 *
 * Hana3, rev2 1010,1212m,1616[m] using Tina
 * 0x00, 0x00-0x1f: Silence
 * 0x01, 0x00-0x1f: 32 EDI channels from Micro Dock
 *       0x00: Dock Mic A
 *       0x04: Dock Mic B
 *       0x08: Dock ADC 1 Left
 *       0x0c: Dock ADC 1 Right
 *       0x10: Dock ADC 2 Left
 *       0x12: Dock S/PDIF Left
 *       0x14: Dock ADC 2 Right
 *       0x16: Dock S/PDIF Right
 *       0x18-0x1f: Dock ADAT 0-7
 * 0x02, 0x00: Hamoa ADC Left
 * 0x02, 0x01: Hamoa ADC Right
 * 0x03, 0x00-0x0f: 16 inputs from Tina Emu32A output
 * 0x03, 0x10-0x1f: 16 inputs from Tina Emu32B output
 * 0x04, 0x00-0x07: Hana3 ADAT
 * 0x05, 0x00: Hana3 S/PDIF Left
 * 0x05, 0x01: Hana3 S/PDIF Right
 * 0x06-0x07: Not used
 *
 * HanaLite, rev1 0404 using Alice2
 * HanaLiteLite, rev2 0404 using Tina
 * 0x00, 0x00-0x1f: Silence
 * 0x01: Not used
 * 0x02, 0x00: ADC Left
 * 0x02, 0x01: ADC Right
 * 0x03, 0x00-0x0f: 16 inputs from Alice2/Tina Emu32A output
 * 0x03, 0x10-0x1f: 16 inputs from Alice2/Tina Emu32B output
 * 0x04: Not used
 * 0x05, 0x00: S/PDIF Left
 * 0x05, 0x01: S/PDIF Right
 * 0x06-0x07: Not used
 *
 * Mana, Cardbus 1616 using Tina2
 * 0x00, 0x00-0x1f: Silence
 * 0x01, 0x00-0x1f: 32 EDI channels from Micro Dock
 *       (same as rev2 1010)
 * 0x02: Not used
 * 0x03, 0x00-0x0f: 16 inputs from Tina2 Emu32A output
 * 0x03, 0x10-0x1f: 16 inputs from Tina2 Emu32B output
 * 0x04-0x07: Not used
 */

/* 32-bit sources of signal in the Hana FPGA. The sources are routed to
 * destinations using a mixer control for each destination - see emumixer.c.
 * Sources are either physical inputs of Hana, or inputs from Alice2/Tina -
 * 16 x EMU_SRC_ALICE_EMU32A + 16 x EMU_SRC_ALICE_EMU32B.
 */
#define EMU_SRC_SILENCE		0x0000	/* Silence */
#define EMU_SRC_DOCK_MIC_A1	0x0100	/* Audio Dock Mic A, 1st or 48kHz only */
#define EMU_SRC_DOCK_MIC_A2	0x0101	/* Audio Dock Mic A, 2nd or 96kHz */
#define EMU_SRC_DOCK_MIC_A3	0x0102	/* Audio Dock Mic A, 3rd or 192kHz */
#define EMU_SRC_DOCK_MIC_A4	0x0103	/* Audio Dock Mic A, 4th or 192kHz */
#define EMU_SRC_DOCK_MIC_B1	0x0104	/* Audio Dock Mic B, 1st or 48kHz only */
#define EMU_SRC_DOCK_MIC_B2	0x0105	/* Audio Dock Mic B, 2nd or 96kHz */
#define EMU_SRC_DOCK_MIC_B3	0x0106	/* Audio Dock Mic B, 3rd or 192kHz */
#define EMU_SRC_DOCK_MIC_B4	0x0107	/* Audio Dock Mic B, 4th or 192kHz */
#define EMU_SRC_DOCK_ADC1_LEFT1	0x0108	/* Audio Dock ADC1 Left, 1st or 48kHz only */
#define EMU_SRC_DOCK_ADC1_LEFT2	0x0109	/* Audio Dock ADC1 Left, 2nd or 96kHz */
#define EMU_SRC_DOCK_ADC1_LEFT3	0x010a	/* Audio Dock ADC1 Left, 3rd or 192kHz */
#define EMU_SRC_DOCK_ADC1_LEFT4	0x010b	/* Audio Dock ADC1 Left, 4th or 192kHz */
#define EMU_SRC_DOCK_ADC1_RIGHT1	0x010c	/* Audio Dock ADC1 Right, 1st or 48kHz only */
#define EMU_SRC_DOCK_ADC1_RIGHT2	0x010d	/* Audio Dock ADC1 Right, 2nd or 96kHz */
#define EMU_SRC_DOCK_ADC1_RIGHT3	0x010e	/* Audio Dock ADC1 Right, 3rd or 192kHz */
#define EMU_SRC_DOCK_ADC1_RIGHT4	0x010f	/* Audio Dock ADC1 Right, 4th or 192kHz */
#define EMU_SRC_DOCK_ADC2_LEFT1	0x0110	/* Audio Dock ADC2 Left, 1st or 48kHz only */
#define EMU_SRC_DOCK_ADC2_LEFT2	0x0111	/* Audio Dock ADC2 Left, 2nd or 96kHz */
#define EMU_SRC_DOCK_ADC2_LEFT3	0x0112	/* Audio Dock ADC2 Left, 3rd or 192kHz */
#define EMU_SRC_DOCK_ADC2_LEFT4	0x0113	/* Audio Dock ADC2 Left, 4th or 192kHz */
#define EMU_SRC_DOCK_ADC2_RIGHT1	0x0114	/* Audio Dock ADC2 Right, 1st or 48kHz only */
#define EMU_SRC_DOCK_ADC2_RIGHT2	0x0115	/* Audio Dock ADC2 Right, 2nd or 96kHz */
#define EMU_SRC_DOCK_ADC2_RIGHT3	0x0116	/* Audio Dock ADC2 Right, 3rd or 192kHz */
#define EMU_SRC_DOCK_ADC2_RIGHT4	0x0117	/* Audio Dock ADC2 Right, 4th or 192kHz */
#define EMU_SRC_DOCK_ADC3_LEFT1	0x0118	/* Audio Dock ADC3 Left, 1st or 48kHz only */
#define EMU_SRC_DOCK_ADC3_LEFT2	0x0119	/* Audio Dock ADC3 Left, 2nd or 96kHz */
#define EMU_SRC_DOCK_ADC3_LEFT3	0x011a	/* Audio Dock ADC3 Left, 3rd or 192kHz */
#define EMU_SRC_DOCK_ADC3_LEFT4	0x011b	/* Audio Dock ADC3 Left, 4th or 192kHz */
#define EMU_SRC_DOCK_ADC3_RIGHT1	0x011c	/* Audio Dock ADC3 Right, 1st or 48kHz only */
#define EMU_SRC_DOCK_ADC3_RIGHT2	0x011d	/* Audio Dock ADC3 Right, 2nd or 96kHz */
#define EMU_SRC_DOCK_ADC3_RIGHT3	0x011e	/* Audio Dock ADC3 Right, 3rd or 192kHz */
#define EMU_SRC_DOCK_ADC3_RIGHT4	0x011f	/* Audio Dock ADC3 Right, 4th or 192kHz */
#define EMU_SRC_HAMOA_ADC_LEFT1	0x0200	/* Hamoa ADC Left, 1st or 48kHz only */
#define EMU_SRC_HAMOA_ADC_LEFT2	0x0202	/* Hamoa ADC Left, 2nd or 96kHz */
#define EMU_SRC_HAMOA_ADC_LEFT3	0x0204	/* Hamoa ADC Left, 3rd or 192kHz */
#define EMU_SRC_HAMOA_ADC_LEFT4	0x0206	/* Hamoa ADC Left, 4th or 192kHz */
#define EMU_SRC_HAMOA_ADC_RIGHT1	0x0201	/* Hamoa ADC Right, 1st or 48kHz only */
#define EMU_SRC_HAMOA_ADC_RIGHT2	0x0203	/* Hamoa ADC Right, 2nd or 96kHz */
#define EMU_SRC_HAMOA_ADC_RIGHT3	0x0205	/* Hamoa ADC Right, 3rd or 192kHz */
#define EMU_SRC_HAMOA_ADC_RIGHT4	0x0207	/* Hamoa ADC Right, 4th or 192kHz */
#define EMU_SRC_ALICE_EMU32A		0x0300	/* Alice2 EMU32a 16 outputs. +0 to +0xf */
#define EMU_SRC_ALICE_EMU32B		0x0310	/* Alice2 EMU32b 16 outputs. +0 to +0xf */
// In S/MUX mode, the samples of one channel are adjacent.
#define EMU_SRC_HANA_ADAT	0x0400	/* Hana ADAT 8 channel in +0 to +7 */
#define EMU_SRC_HANA_SPDIF_LEFT1	0x0500	/* Hana SPDIF Left, 1st or 48kHz only */
#define EMU_SRC_HANA_SPDIF_LEFT2	0x0502	/* Hana SPDIF Left, 2nd or 96kHz */
#define EMU_SRC_HANA_SPDIF_LEFT3	0x0504	/* Hana SPDIF Left, 3rd or 192kHz */
#define EMU_SRC_HANA_SPDIF_LEFT4	0x0506	/* Hana SPDIF Left, 4th or 192kHz */
#define EMU_SRC_HANA_SPDIF_RIGHT1	0x0501	/* Hana SPDIF Right, 1st or 48kHz only */
#define EMU_SRC_HANA_SPDIF_RIGHT2	0x0503	/* Hana SPDIF Right, 2nd or 96kHz */
#define EMU_SRC_HANA_SPDIF_RIGHT3	0x0505	/* Hana SPDIF Right, 3rd or 192kHz */
#define EMU_SRC_HANA_SPDIF_RIGHT4	0x0507	/* Hana SPDIF Right, 4th or 192kHz */

/* Additional inputs for 1616(M)/Microdock */

#define EMU_SRC_MDOCK_SPDIF_LEFT1	0x0112	/* Microdock S/PDIF Left, 1st or 48kHz only */
#define EMU_SRC_MDOCK_SPDIF_LEFT2	0x0113	/* Microdock S/PDIF Left, 2nd or 96kHz */
#define EMU_SRC_MDOCK_SPDIF_RIGHT1	0x0116	/* Microdock S/PDIF Right, 1st or 48kHz only */
#define EMU_SRC_MDOCK_SPDIF_RIGHT2	0x0117	/* Microdock S/PDIF Right, 2nd or 96kHz */
#define EMU_SRC_MDOCK_ADAT		0x0118	/* Microdock ADAT 8 channel in +8 to +f */

/* 0x600 and 0x700 no used */


/* ------------------- CONSTANTS -------------------- */

extern const char * const snd_emu10k1_fxbus[32];
extern const char * const snd_emu10k1_sblive_ins[16];
extern const char * const snd_emu10k1_audigy_ins[16];
extern const char * const snd_emu10k1_sblive_outs[32];
extern const char * const snd_emu10k1_audigy_outs[32];
extern const s8 snd_emu10k1_sblive51_fxbus2_map[16];

/* ------------------- STRUCTURES -------------------- */

enum {
	EMU10K1_UNUSED,  // This must be zero
	EMU10K1_EFX,
	EMU10K1_EFX_IRQ,
	EMU10K1_PCM,
	EMU10K1_PCM_IRQ,
	EMU10K1_SYNTH,
	EMU10K1_NUM_TYPES
};

struct snd_emu10k1;

struct snd_emu10k1_voice {
	unsigned char number;
	unsigned char use;
	unsigned char dirty;
	unsigned char last;
	void (*interrupt)(struct snd_emu10k1 *emu, struct snd_emu10k1_voice *pvoice);

	struct snd_emu10k1_pcm *epcm;
};

enum {
	PLAYBACK_EMUVOICE,
	PLAYBACK_EFX,
	CAPTURE_AC97ADC,
	CAPTURE_AC97MIC,
	CAPTURE_EFX
};

struct snd_emu10k1_pcm {
	struct snd_emu10k1 *emu;
	int type;
	struct snd_pcm_substream *substream;
	struct snd_emu10k1_voice *voices[NUM_EFX_PLAYBACK];
	struct snd_emu10k1_voice *extra;
	unsigned short running;
	unsigned short first_ptr;
	snd_pcm_uframes_t resume_pos;
	struct snd_util_memblk *memblk;
	unsigned int pitch_target;
	unsigned int start_addr;
	unsigned int ccca_start_addr;
	unsigned int capture_ipr;	/* interrupt acknowledge mask */
	unsigned int capture_inte;	/* interrupt enable mask */
	unsigned int capture_ba_reg;	/* buffer address register */
	unsigned int capture_bs_reg;	/* buffer size register */
	unsigned int capture_idx_reg;	/* buffer index register */
	unsigned int capture_cr_val;	/* control value */
	unsigned int capture_cr_val2;	/* control value2 (for audigy) */
	unsigned int capture_bs_val;	/* buffer size value */
	unsigned int capture_bufsize;	/* buffer size in bytes */
};

struct snd_emu10k1_pcm_mixer {
	/* mono, left, right x 8 sends (4 on emu10k1) */
	unsigned char send_routing[3][8];
	unsigned char send_volume[3][8];
	// 0x8000 is neutral. The mixer code rescales it to 0xffff to maintain
	// backwards compatibility with user space.
	unsigned short attn[3];
	struct snd_emu10k1_pcm *epcm;
};

#define snd_emu10k1_compose_send_routing(route) \
((route[0] | (route[1] << 4) | (route[2] << 8) | (route[3] << 12)) << 16)

#define snd_emu10k1_compose_audigy_fxrt1(route) \
((unsigned int)route[0] | ((unsigned int)route[1] << 8) | ((unsigned int)route[2] << 16) | ((unsigned int)route[3] << 24) | 0x80808080)

#define snd_emu10k1_compose_audigy_fxrt2(route) \
((unsigned int)route[4] | ((unsigned int)route[5] << 8) | ((unsigned int)route[6] << 16) | ((unsigned int)route[7] << 24) | 0x80808080)

#define snd_emu10k1_compose_audigy_sendamounts(vol) \
(((unsigned int)vol[4] << 24) | ((unsigned int)vol[5] << 16) | ((unsigned int)vol[6] << 8) | (unsigned int)vol[7])

struct snd_emu10k1_memblk {
	struct snd_util_memblk mem;
	/* private part */
	int first_page, last_page, pages, mapped_page;
	unsigned int map_locked;
	struct list_head mapped_link;
	struct list_head mapped_order_link;
};

#define snd_emu10k1_memblk_offset(blk)	(((blk)->mapped_page << PAGE_SHIFT) | ((blk)->mem.offset & (PAGE_SIZE - 1)))

#define EMU10K1_MAX_TRAM_BLOCKS_PER_CODE	16

struct snd_emu10k1_fx8010_ctl {
	struct list_head list;		/* list link container */
	unsigned int vcount;
	unsigned int count;		/* count of GPR (1..16) */
	unsigned short gpr[32];		/* GPR number(s) */
	int value[32];
	int min;			/* minimum range */
	int max;			/* maximum range */
	unsigned int translation;	/* translation type (EMU10K1_GPR_TRANSLATION*) */
	struct snd_kcontrol *kcontrol;
};

typedef void (snd_fx8010_irq_handler_t)(struct snd_emu10k1 *emu, void *private_data);

struct snd_emu10k1_fx8010_irq {
	struct snd_emu10k1_fx8010_irq *next;
	snd_fx8010_irq_handler_t *handler;
	unsigned short gpr_running;
	void *private_data;
};

struct snd_emu10k1_fx8010_pcm {
	unsigned int valid: 1,
		     opened: 1,
		     active: 1;
	unsigned int channels;		/* 16-bit channels count */
	unsigned int tram_start;	/* initial ring buffer position in TRAM (in samples) */
	unsigned int buffer_size;	/* count of buffered samples */
	unsigned short gpr_size;		/* GPR containing size of ring buffer in samples (host) */
	unsigned short gpr_ptr;		/* GPR containing current pointer in the ring buffer (host = reset, FX8010) */
	unsigned short gpr_count;	/* GPR containing count of samples between two interrupts (host) */
	unsigned short gpr_tmpcount;	/* GPR containing current count of samples to interrupt (host = set, FX8010) */
	unsigned short gpr_trigger;	/* GPR containing trigger (activate) information (host) */
	unsigned short gpr_running;	/* GPR containing info if PCM is running (FX8010) */
	unsigned char etram[32];	/* external TRAM address & data */
	struct snd_pcm_indirect pcm_rec;
	unsigned int tram_pos;
	unsigned int tram_shift;
	struct snd_emu10k1_fx8010_irq irq;
};

struct snd_emu10k1_fx8010 {
	unsigned short extin_mask;	/* used external inputs (bitmask); not used for Audigy */
	unsigned short extout_mask;	/* used external outputs (bitmask); not used for Audigy */
	unsigned int itram_size;	/* internal TRAM size in samples */
	struct snd_dma_buffer etram_pages; /* external TRAM pages and size */
	unsigned int dbg;		/* FX debugger register */
	unsigned char name[128];
	int gpr_size;			/* size of allocated GPR controls */
	int gpr_count;			/* count of used kcontrols */
	struct list_head gpr_ctl;	/* GPR controls */
	struct mutex lock;
	struct snd_emu10k1_fx8010_pcm pcm[8];
	spinlock_t irq_lock;
	struct snd_emu10k1_fx8010_irq *irq_handlers;
};

struct snd_emu10k1_midi {
	struct snd_emu10k1 *emu;
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_substream *substream_input;
	struct snd_rawmidi_substream *substream_output;
	unsigned int midi_mode;
	spinlock_t input_lock;
	spinlock_t output_lock;
	spinlock_t open_lock;
	int tx_enable, rx_enable;
	int port;
	int ipr_tx, ipr_rx;
	void (*interrupt)(struct snd_emu10k1 *emu, unsigned int status);
};

enum {
	EMU_MODEL_SB,
	EMU_MODEL_EMU1010,
	EMU_MODEL_EMU1010B,
	EMU_MODEL_EMU1616,
	EMU_MODEL_EMU0404,
};

// Chip-o-logy:
// - All SB Live! cards use EMU10K1 chips
// - All SB Audigy cards use CA* chips, termed "emu10k2" by the driver
// - Original Audigy uses CA0100 "Alice"
// - Audigy 2 uses CA0102/CA10200 "Alice2"
//   - Has an interface for CA0151 (P16V) "Alice3"
// - Audigy 2 Value uses CA0108/CA10300 "Tina"
//   - Approximately a CA0102 with an on-chip CA0151 (P17V)
// - Audigy 2 ZS NB uses CA0109 "Tina2"
//   - Cardbus version of CA0108
struct snd_emu_chip_details {
	u32 vendor;
	u32 device;
	u32 subsystem;
	unsigned char revision;
	unsigned char emu_model;	/* EMU model type */
	unsigned int emu10k1_chip:1;	/* Original SB Live. Not SB Live 24bit. */
					/* Redundant with emu10k2_chip being unset. */
	unsigned int emu10k2_chip:1;	/* Audigy 1 or Audigy 2. */
	unsigned int ca0102_chip:1;	/* Audigy 1 or Audigy 2. Not SB Audigy 2 Value. */
					/* Redundant with ca0108_chip being unset. */
	unsigned int ca0108_chip:1;	/* Audigy 2 Value */
	unsigned int ca_cardbus_chip:1;	/* Audigy 2 ZS Notebook */
	unsigned int ca0151_chip:1;	/* P16V */
	unsigned int spk20:1;		/* Stereo only */
	unsigned int spk71:1;		/* Has 7.1 speakers */
	unsigned int no_adat:1;		/* Has no ADAT, only SPDIF */
	unsigned int sblive51:1;	/* SBLive! 5.1 - extout 0x11 -> center, 0x12 -> lfe */
	unsigned int spdif_bug:1;	/* Has Spdif phasing bug */
	unsigned int ac97_chip:2;	/* Has an AC97 chip: 1 = mandatory, 2 = optional */
	unsigned int ecard:1;		/* APS EEPROM */
	unsigned int spi_dac:1;		/* SPI interface for DAC; requires ca0108_chip */
	unsigned int i2c_adc:1;		/* I2C interface for ADC; requires ca0108_chip */
	unsigned int adc_1361t:1;	/* Use Philips 1361T ADC */
	unsigned int invert_shared_spdif:1;  /* analog/digital switch inverted */
	const char *driver;
	const char *name;
	const char *id;		/* for backward compatibility - can be NULL if not needed */
};

#define NUM_OUTPUT_DESTS 28
#define NUM_INPUT_DESTS 22

struct snd_emu1010 {
	unsigned char output_source[NUM_OUTPUT_DESTS];
	unsigned char input_source[NUM_INPUT_DESTS];
	unsigned int adc_pads; /* bit mask */
	unsigned int dac_pads; /* bit mask */
	unsigned int wclock;  /* Cached register value */
	unsigned int word_clock;  /* Cached effective value */
	unsigned int clock_source;
	unsigned int clock_fallback;
	unsigned int optical_in; /* 0:SPDIF, 1:ADAT */
	unsigned int optical_out; /* 0:SPDIF, 1:ADAT */
	struct work_struct work;
};

struct snd_emu10k1 {
	int irq;

	unsigned long port;			/* I/O port number */
	unsigned int tos_link: 1,		/* tos link detected */
		rear_ac97: 1,			/* rear channels are on AC'97 */
		enable_ir: 1;
	unsigned int support_tlv :1;
	/* Contains profile of card capabilities */
	const struct snd_emu_chip_details *card_capabilities;
	unsigned int audigy;			/* is Audigy? */
	unsigned int revision;			/* chip revision */
	unsigned int serial;			/* serial number */
	unsigned short model;			/* subsystem id */
	unsigned int ecard_ctrl;		/* ecard control bits */
	unsigned int address_mode;		/* address mode */
	unsigned long dma_mask;			/* PCI DMA mask */
	bool iommu_workaround;			/* IOMMU workaround needed */
	int max_cache_pages;			/* max memory size / PAGE_SIZE */
	struct snd_dma_buffer silent_page;	/* silent page */
	struct snd_dma_buffer ptb_pages;	/* page table pages */
	struct snd_dma_device p16v_dma_dev;
	struct snd_dma_buffer *p16v_buffer;

	struct snd_util_memhdr *memhdr;		/* page allocation list */

	struct list_head mapped_link_head;
	struct list_head mapped_order_link_head;
	void **page_ptr_table;
	unsigned long *page_addr_table;
	spinlock_t memblk_lock;

	unsigned int spdif_bits[3];		/* s/pdif out setup */
	unsigned int i2c_capture_source;
	u8 i2c_capture_volume[4][2];

	struct snd_emu10k1_fx8010 fx8010;		/* FX8010 info */
	int gpr_base;
	
	struct snd_ac97 *ac97;

	struct pci_dev *pci;
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm *pcm_mic;
	struct snd_pcm *pcm_efx;
	struct snd_pcm *pcm_multi;
	struct snd_pcm *pcm_p16v;

	spinlock_t synth_lock;
	void *synth;
	int (*get_synth_voice)(struct snd_emu10k1 *emu);

	spinlock_t reg_lock;  // high-level driver lock
	spinlock_t emu_lock;  // low-level i/o lock
	spinlock_t voice_lock;  // voice allocator lock
	spinlock_t spi_lock; /* serialises access to spi port */
	spinlock_t i2c_lock; /* serialises access to i2c port */

	struct snd_emu10k1_voice voices[NUM_G];
	int p16v_device_offset;
	u32 p16v_capture_source;
	u32 p16v_capture_channel;
        struct snd_emu1010 emu1010;
	struct snd_emu10k1_pcm_mixer pcm_mixer[32];
	struct snd_emu10k1_pcm_mixer efx_pcm_mixer[NUM_EFX_PLAYBACK];
	struct snd_kcontrol *ctl_send_routing;
	struct snd_kcontrol *ctl_send_volume;
	struct snd_kcontrol *ctl_attn;
	struct snd_kcontrol *ctl_efx_send_routing;
	struct snd_kcontrol *ctl_efx_send_volume;
	struct snd_kcontrol *ctl_efx_attn;
	struct snd_kcontrol *ctl_clock_source;

	void (*hwvol_interrupt)(struct snd_emu10k1 *emu, unsigned int status);
	void (*capture_interrupt)(struct snd_emu10k1 *emu, unsigned int status);
	void (*capture_mic_interrupt)(struct snd_emu10k1 *emu, unsigned int status);
	void (*capture_efx_interrupt)(struct snd_emu10k1 *emu, unsigned int status);
	void (*spdif_interrupt)(struct snd_emu10k1 *emu, unsigned int status);
	void (*dsp_interrupt)(struct snd_emu10k1 *emu);
	void (*gpio_interrupt)(struct snd_emu10k1 *emu);
	void (*p16v_interrupt)(struct snd_emu10k1 *emu);

	struct snd_pcm_substream *pcm_capture_substream;
	struct snd_pcm_substream *pcm_capture_mic_substream;
	struct snd_pcm_substream *pcm_capture_efx_substream;

	struct snd_timer *timer;

	struct snd_emu10k1_midi midi;
	struct snd_emu10k1_midi midi2; /* for audigy */

	unsigned int efx_voices_mask[2];
	unsigned int next_free_voice;

	const struct firmware *firmware;
	const struct firmware *dock_fw;

#ifdef CONFIG_PM_SLEEP
	unsigned int *saved_ptr;
	unsigned int *saved_gpr;
	unsigned int *tram_val_saved;
	unsigned int *tram_addr_saved;
	unsigned int *saved_icode;
	unsigned int *p16v_saved;
	unsigned int saved_a_iocfg, saved_hcfg;
	bool suspend;
#endif

};

int snd_emu10k1_create(struct snd_card *card,
		       struct pci_dev *pci,
		       unsigned short extin_mask,
		       unsigned short extout_mask,
		       long max_cache_bytes,
		       int enable_ir,
		       uint subsystem);

int snd_emu10k1_pcm(struct snd_emu10k1 *emu, int device);
int snd_emu10k1_pcm_mic(struct snd_emu10k1 *emu, int device);
int snd_emu10k1_pcm_efx(struct snd_emu10k1 *emu, int device);
int snd_p16v_pcm(struct snd_emu10k1 *emu, int device);
int snd_p16v_mixer(struct snd_emu10k1 * emu);
int snd_emu10k1_pcm_multi(struct snd_emu10k1 *emu, int device);
int snd_emu10k1_fx8010_pcm(struct snd_emu10k1 *emu, int device);
int snd_emu10k1_mixer(struct snd_emu10k1 * emu, int pcm_device, int multi_device);
int snd_emu10k1_timer(struct snd_emu10k1 * emu, int device);
int snd_emu10k1_fx8010_new(struct snd_emu10k1 *emu, int device);

irqreturn_t snd_emu10k1_interrupt(int irq, void *dev_id);

void snd_emu10k1_voice_init(struct snd_emu10k1 * emu, int voice);
int snd_emu10k1_init_efx(struct snd_emu10k1 *emu);
void snd_emu10k1_free_efx(struct snd_emu10k1 *emu);
int snd_emu10k1_fx8010_tram_setup(struct snd_emu10k1 *emu, u32 size);
int snd_emu10k1_done(struct snd_emu10k1 * emu);

/* I/O functions */
unsigned int snd_emu10k1_ptr_read(struct snd_emu10k1 * emu, unsigned int reg, unsigned int chn);
void snd_emu10k1_ptr_write(struct snd_emu10k1 *emu, unsigned int reg, unsigned int chn, unsigned int data);
void snd_emu10k1_ptr_write_multiple(struct snd_emu10k1 *emu, unsigned int chn, ...);
unsigned int snd_emu10k1_ptr20_read(struct snd_emu10k1 * emu, unsigned int reg, unsigned int chn);
void snd_emu10k1_ptr20_write(struct snd_emu10k1 *emu, unsigned int reg, unsigned int chn, unsigned int data);
int snd_emu10k1_spi_write(struct snd_emu10k1 * emu, unsigned int data);
int snd_emu10k1_i2c_write(struct snd_emu10k1 *emu, u32 reg, u32 value);
void snd_emu1010_fpga_write(struct snd_emu10k1 *emu, u32 reg, u32 value);
void snd_emu1010_fpga_read(struct snd_emu10k1 *emu, u32 reg, u32 *value);
void snd_emu1010_fpga_link_dst_src_write(struct snd_emu10k1 *emu, u32 dst, u32 src);
u32 snd_emu1010_fpga_link_dst_src_read(struct snd_emu10k1 *emu, u32 dst);
int snd_emu1010_get_raw_rate(struct snd_emu10k1 *emu, u8 src);
void snd_emu1010_update_clock(struct snd_emu10k1 *emu);
unsigned int snd_emu10k1_efx_read(struct snd_emu10k1 *emu, unsigned int pc);
void snd_emu10k1_intr_enable(struct snd_emu10k1 *emu, unsigned int intrenb);
void snd_emu10k1_intr_disable(struct snd_emu10k1 *emu, unsigned int intrenb);
void snd_emu10k1_voice_intr_enable(struct snd_emu10k1 *emu, unsigned int voicenum);
void snd_emu10k1_voice_intr_disable(struct snd_emu10k1 *emu, unsigned int voicenum);
void snd_emu10k1_voice_intr_ack(struct snd_emu10k1 *emu, unsigned int voicenum);
void snd_emu10k1_voice_half_loop_intr_enable(struct snd_emu10k1 *emu, unsigned int voicenum);
void snd_emu10k1_voice_half_loop_intr_disable(struct snd_emu10k1 *emu, unsigned int voicenum);
void snd_emu10k1_voice_half_loop_intr_ack(struct snd_emu10k1 *emu, unsigned int voicenum);
#if 0
void snd_emu10k1_voice_set_loop_stop(struct snd_emu10k1 *emu, unsigned int voicenum);
void snd_emu10k1_voice_clear_loop_stop(struct snd_emu10k1 *emu, unsigned int voicenum);
#endif
void snd_emu10k1_voice_set_loop_stop_multiple(struct snd_emu10k1 *emu, u64 voices);
void snd_emu10k1_voice_clear_loop_stop_multiple(struct snd_emu10k1 *emu, u64 voices);
int snd_emu10k1_voice_clear_loop_stop_multiple_atomic(struct snd_emu10k1 *emu, u64 voices);
void snd_emu10k1_wait(struct snd_emu10k1 *emu, unsigned int wait);
static inline unsigned int snd_emu10k1_wc(struct snd_emu10k1 *emu) { return (inl(emu->port + WC) >> 6) & 0xfffff; }
unsigned short snd_emu10k1_ac97_read(struct snd_ac97 *ac97, unsigned short reg);
void snd_emu10k1_ac97_write(struct snd_ac97 *ac97, unsigned short reg, unsigned short data);

#ifdef CONFIG_PM_SLEEP
void snd_emu10k1_suspend_regs(struct snd_emu10k1 *emu);
void snd_emu10k1_resume_init(struct snd_emu10k1 *emu);
void snd_emu10k1_resume_regs(struct snd_emu10k1 *emu);
int snd_emu10k1_efx_alloc_pm_buffer(struct snd_emu10k1 *emu);
void snd_emu10k1_efx_free_pm_buffer(struct snd_emu10k1 *emu);
void snd_emu10k1_efx_suspend(struct snd_emu10k1 *emu);
void snd_emu10k1_efx_resume(struct snd_emu10k1 *emu);
int snd_p16v_alloc_pm_buffer(struct snd_emu10k1 *emu);
void snd_p16v_free_pm_buffer(struct snd_emu10k1 *emu);
void snd_p16v_suspend(struct snd_emu10k1 *emu);
void snd_p16v_resume(struct snd_emu10k1 *emu);
#endif

/* memory allocation */
struct snd_util_memblk *snd_emu10k1_alloc_pages(struct snd_emu10k1 *emu, struct snd_pcm_substream *substream);
int snd_emu10k1_free_pages(struct snd_emu10k1 *emu, struct snd_util_memblk *blk);
int snd_emu10k1_alloc_pages_maybe_wider(struct snd_emu10k1 *emu, size_t size,
					struct snd_dma_buffer *dmab);
struct snd_util_memblk *snd_emu10k1_synth_alloc(struct snd_emu10k1 *emu, unsigned int size);
int snd_emu10k1_synth_free(struct snd_emu10k1 *emu, struct snd_util_memblk *blk);
int snd_emu10k1_synth_bzero(struct snd_emu10k1 *emu, struct snd_util_memblk *blk, int offset, int size);
int snd_emu10k1_synth_copy_from_user(struct snd_emu10k1 *emu, struct snd_util_memblk *blk, int offset, const char __user *data, int size);
int snd_emu10k1_memblk_map(struct snd_emu10k1 *emu, struct snd_emu10k1_memblk *blk);

/* voice allocation */
int snd_emu10k1_voice_alloc(struct snd_emu10k1 *emu, int type, int count, int channels,
			    struct snd_emu10k1_pcm *epcm, struct snd_emu10k1_voice **rvoice);
int snd_emu10k1_voice_free(struct snd_emu10k1 *emu, struct snd_emu10k1_voice *pvoice);

/* MIDI uart */
int snd_emu10k1_midi(struct snd_emu10k1 * emu);
int snd_emu10k1_audigy_midi(struct snd_emu10k1 * emu);

/* proc interface */
int snd_emu10k1_proc_init(struct snd_emu10k1 * emu);

/* fx8010 irq handler */
int snd_emu10k1_fx8010_register_irq_handler(struct snd_emu10k1 *emu,
					    snd_fx8010_irq_handler_t *handler,
					    unsigned char gpr_running,
					    void *private_data,
					    struct snd_emu10k1_fx8010_irq *irq);
int snd_emu10k1_fx8010_unregister_irq_handler(struct snd_emu10k1 *emu,
					      struct snd_emu10k1_fx8010_irq *irq);

#endif	/* __SOUND_EMU10K1_H */
