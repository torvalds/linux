/*********************************************************************
 *
 * msnd_classic.h
 *
 * Turtle Beach MultiSound Sound Card Driver for Linux
 *
 * Some parts of this header file were derived from the Turtle Beach
 * MultiSound Driver Development Kit.
 *
 * Copyright (C) 1998 Andrew Veliath
 * Copyright (C) 1993 Turtle Beach Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * 
 ********************************************************************/
#ifndef __MSND_CLASSIC_H
#define __MSND_CLASSIC_H


#define DSP_NUMIO				0x10

#define	HP_MEMM					0x08

#define	HP_BITM					0x0E
#define	HP_WAIT					0x0D
#define	HP_DSPR					0x0A
#define	HP_PROR					0x0B
#define	HP_BLKS					0x0C

#define	HPPRORESET_OFF				0
#define HPPRORESET_ON				1

#define HPDSPRESET_OFF				0
#define HPDSPRESET_ON				1

#define HPBLKSEL_0				0
#define HPBLKSEL_1				1

#define HPWAITSTATE_0				0
#define HPWAITSTATE_1				1

#define HPBITMODE_16				0
#define HPBITMODE_8				1

#define	HIDSP_INT_PLAY_UNDER			0x00
#define	HIDSP_INT_RECORD_OVER			0x01
#define	HIDSP_INPUT_CLIPPING			0x02
#define	HIDSP_MIDI_IN_OVER			0x10
#define	HIDSP_MIDI_OVERRUN_ERR  0x13

#define	HDEXAR_CLEAR_PEAKS			1
#define	HDEXAR_IN_SET_POTS			2
#define	HDEXAR_AUX_SET_POTS			3
#define	HDEXAR_CAL_A_TO_D			4
#define	HDEXAR_RD_EXT_DSP_BITS			5

#define TIME_PRO_RESET_DONE			0x028A
#define TIME_PRO_SYSEX				0x0040
#define TIME_PRO_RESET				0x0032

#define AGND					0x01
#define SIGNAL					0x02

#define EXT_DSP_BIT_DCAL			0x0001
#define EXT_DSP_BIT_MIDI_CON			0x0002

#define BUFFSIZE				0x8000
#define HOSTQ_SIZE				0x40

#define SRAM_CNTL_START				0x7F00
#define SMA_STRUCT_START			0x7F40

#define DAP_BUFF_SIZE				0x2400
#define DAR_BUFF_SIZE				0x2000

#define DAPQ_STRUCT_SIZE			0x10
#define DARQ_STRUCT_SIZE			0x10
#define DAPQ_BUFF_SIZE				(3 * 0x10)
#define DARQ_BUFF_SIZE				(3 * 0x10)
#define MODQ_BUFF_SIZE				0x400
#define MIDQ_BUFF_SIZE				0x200
#define DSPQ_BUFF_SIZE				0x40

#define DAPQ_DATA_BUFF				0x6C00
#define DARQ_DATA_BUFF				0x6C30
#define MODQ_DATA_BUFF				0x6C60
#define MIDQ_DATA_BUFF				0x7060
#define DSPQ_DATA_BUFF				0x7260

#define DAPQ_OFFSET				SRAM_CNTL_START
#define DARQ_OFFSET				(SRAM_CNTL_START + 0x08)
#define MODQ_OFFSET				(SRAM_CNTL_START + 0x10)
#define MIDQ_OFFSET				(SRAM_CNTL_START + 0x18)
#define DSPQ_OFFSET				(SRAM_CNTL_START + 0x20)

#define MOP_SYNTH				0x10
#define MOP_EXTOUT				0x32
#define MOP_EXTTHRU				0x02
#define MOP_OUTMASK				0x01

#define MIP_EXTIN				0x01
#define MIP_SYNTH				0x00
#define MIP_INMASK				0x32

/* Classic SMA Common Data */
#define SMA_wCurrPlayBytes			0x0000
#define SMA_wCurrRecordBytes			0x0002
#define SMA_wCurrPlayVolLeft			0x0004
#define SMA_wCurrPlayVolRight			0x0006
#define SMA_wCurrInVolLeft			0x0008
#define SMA_wCurrInVolRight			0x000a
#define SMA_wUser_3				0x000c
#define SMA_wUser_4				0x000e
#define SMA_dwUser_5				0x0010
#define SMA_dwUser_6				0x0014
#define SMA_wUser_7				0x0018
#define SMA_wReserved_A				0x001a
#define SMA_wReserved_B				0x001c
#define SMA_wReserved_C				0x001e
#define SMA_wReserved_D				0x0020
#define SMA_wReserved_E				0x0022
#define SMA_wReserved_F				0x0024
#define SMA_wReserved_G				0x0026
#define SMA_wReserved_H				0x0028
#define SMA_wCurrDSPStatusFlags			0x002a
#define SMA_wCurrHostStatusFlags		0x002c
#define SMA_wCurrInputTagBits			0x002e
#define SMA_wCurrLeftPeak			0x0030
#define SMA_wCurrRightPeak			0x0032
#define SMA_wExtDSPbits				0x0034
#define SMA_bExtHostbits			0x0036
#define SMA_bBoardLevel				0x0037
#define SMA_bInPotPosRight			0x0038
#define SMA_bInPotPosLeft			0x0039
#define SMA_bAuxPotPosRight			0x003a
#define SMA_bAuxPotPosLeft			0x003b
#define SMA_wCurrMastVolLeft			0x003c
#define SMA_wCurrMastVolRight			0x003e
#define SMA_bUser_12				0x0040
#define SMA_bUser_13				0x0041
#define SMA_wUser_14				0x0042
#define SMA_wUser_15				0x0044
#define SMA_wCalFreqAtoD			0x0046
#define SMA_wUser_16				0x0048
#define SMA_wUser_17				0x004a
#define SMA__size				0x004c

#ifdef HAVE_DSPCODEH
#  include "msndperm.c"
#  include "msndinit.c"
#  define PERMCODE		msndperm
#  define INITCODE		msndinit
#  define PERMCODESIZE		sizeof(msndperm)
#  define INITCODESIZE		sizeof(msndinit)
#else
#  ifndef CONFIG_MSNDCLAS_INIT_FILE
#    define CONFIG_MSNDCLAS_INIT_FILE				\
				"/etc/sound/msndinit.bin"
#  endif
#  ifndef CONFIG_MSNDCLAS_PERM_FILE
#    define CONFIG_MSNDCLAS_PERM_FILE				\
				"/etc/sound/msndperm.bin"
#  endif
#  define PERMCODEFILE		CONFIG_MSNDCLAS_PERM_FILE
#  define INITCODEFILE		CONFIG_MSNDCLAS_INIT_FILE
#  define PERMCODE		dspini
#  define INITCODE		permini
#  define PERMCODESIZE		sizeof_dspini
#  define INITCODESIZE		sizeof_permini
#endif
#define LONGNAME		"MultiSound (Classic/Monterey/Tahiti)"

#endif /* __MSND_CLASSIC_H */
