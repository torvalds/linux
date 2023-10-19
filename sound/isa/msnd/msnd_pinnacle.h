/* SPDX-License-Identifier: GPL-2.0-or-later */
/*********************************************************************
 *
 * msnd_pinnacle.h
 *
 * Turtle Beach MultiSound Sound Card Driver for Linux
 *
 * Some parts of this header file were derived from the Turtle Beach
 * MultiSound Driver Development Kit.
 *
 * Copyright (C) 1998 Andrew Veliath
 * Copyright (C) 1993 Turtle Beach Systems, Inc.
 *
 ********************************************************************/
#ifndef __MSND_PINNACLE_H
#define __MSND_PINNACLE_H

#define DSP_NUMIO				0x08

#define IREG_LOGDEVICE				0x07
#define IREG_ACTIVATE				0x30
#define LD_ACTIVATE				0x01
#define LD_DISACTIVATE				0x00
#define IREG_EECONTROL				0x3F
#define IREG_MEMBASEHI				0x40
#define IREG_MEMBASELO				0x41
#define IREG_MEMCONTROL				0x42
#define IREG_MEMRANGEHI				0x43
#define IREG_MEMRANGELO				0x44
#define MEMTYPE_8BIT				0x00
#define MEMTYPE_16BIT				0x02
#define MEMTYPE_RANGE				0x00
#define MEMTYPE_HIADDR				0x01
#define IREG_IO0_BASEHI				0x60
#define IREG_IO0_BASELO				0x61
#define IREG_IO1_BASEHI				0x62
#define IREG_IO1_BASELO				0x63
#define IREG_IRQ_NUMBER				0x70
#define IREG_IRQ_TYPE				0x71
#define IRQTYPE_HIGH				0x02
#define IRQTYPE_LOW				0x00
#define IRQTYPE_LEVEL				0x01
#define IRQTYPE_EDGE				0x00

#define	HP_DSPR					0x04
#define	HP_BLKS					0x04

#define HPDSPRESET_OFF				2
#define HPDSPRESET_ON				0

#define HPBLKSEL_0				2
#define HPBLKSEL_1				3

#define	HIMT_DAT_OFF				0x03

#define	HIDSP_PLAY_UNDER			0x00
#define	HIDSP_INT_PLAY_UNDER			0x01
#define	HIDSP_SSI_TX_UNDER  			0x02
#define HIDSP_RECQ_OVERFLOW			0x08
#define HIDSP_INT_RECORD_OVER			0x09
#define HIDSP_SSI_RX_OVERFLOW			0x0a

#define	HIDSP_MIDI_IN_OVER			0x10

#define	HIDSP_MIDI_FRAME_ERR			0x11
#define	HIDSP_MIDI_PARITY_ERR			0x12
#define	HIDSP_MIDI_OVERRUN_ERR			0x13

#define HIDSP_INPUT_CLIPPING			0x20
#define	HIDSP_MIX_CLIPPING			0x30
#define HIDSP_DAT_IN_OFF			0x21

#define TIME_PRO_RESET_DONE			0x028A
#define TIME_PRO_SYSEX				0x001E
#define TIME_PRO_RESET				0x0032

#define DAR_BUFF_SIZE				0x1000

#define MIDQ_BUFF_SIZE				0x800
#define DSPQ_BUFF_SIZE				0x5A0

#define DSPQ_DATA_BUFF				0x7860

#define MOP_WAVEHDR				0
#define MOP_EXTOUT				1
#define MOP_HWINIT				0xfe
#define MOP_NONE				0xff
#define MOP_MAX					1

#define MIP_EXTIN				0
#define MIP_WAVEHDR				1
#define MIP_HWINIT				0xfe
#define MIP_MAX					1

/* Pinnacle/Fiji SMA Common Data */
#define SMA_wCurrPlayBytes			0x0000
#define SMA_wCurrRecordBytes			0x0002
#define SMA_wCurrPlayVolLeft			0x0004
#define SMA_wCurrPlayVolRight			0x0006
#define SMA_wCurrInVolLeft			0x0008
#define SMA_wCurrInVolRight			0x000a
#define SMA_wCurrMHdrVolLeft			0x000c
#define SMA_wCurrMHdrVolRight			0x000e
#define SMA_dwCurrPlayPitch			0x0010
#define SMA_dwCurrPlayRate			0x0014
#define SMA_wCurrMIDIIOPatch			0x0018
#define SMA_wCurrPlayFormat			0x001a
#define SMA_wCurrPlaySampleSize			0x001c
#define SMA_wCurrPlayChannels			0x001e
#define SMA_wCurrPlaySampleRate			0x0020
#define SMA_wCurrRecordFormat			0x0022
#define SMA_wCurrRecordSampleSize		0x0024
#define SMA_wCurrRecordChannels			0x0026
#define SMA_wCurrRecordSampleRate		0x0028
#define SMA_wCurrDSPStatusFlags			0x002a
#define SMA_wCurrHostStatusFlags		0x002c
#define SMA_wCurrInputTagBits			0x002e
#define SMA_wCurrLeftPeak			0x0030
#define SMA_wCurrRightPeak			0x0032
#define SMA_bMicPotPosLeft			0x0034
#define SMA_bMicPotPosRight			0x0035
#define SMA_bMicPotMaxLeft			0x0036
#define SMA_bMicPotMaxRight			0x0037
#define SMA_bInPotPosLeft			0x0038
#define SMA_bInPotPosRight			0x0039
#define SMA_bAuxPotPosLeft			0x003a
#define SMA_bAuxPotPosRight			0x003b
#define SMA_bInPotMaxLeft			0x003c
#define SMA_bInPotMaxRight			0x003d
#define SMA_bAuxPotMaxLeft			0x003e
#define SMA_bAuxPotMaxRight			0x003f
#define SMA_bInPotMaxMethod			0x0040
#define SMA_bAuxPotMaxMethod			0x0041
#define SMA_wCurrMastVolLeft			0x0042
#define SMA_wCurrMastVolRight			0x0044
#define SMA_wCalFreqAtoD			0x0046
#define SMA_wCurrAuxVolLeft			0x0048
#define SMA_wCurrAuxVolRight			0x004a
#define SMA_wCurrPlay1VolLeft			0x004c
#define SMA_wCurrPlay1VolRight			0x004e
#define SMA_wCurrPlay2VolLeft			0x0050
#define SMA_wCurrPlay2VolRight			0x0052
#define SMA_wCurrPlay3VolLeft			0x0054
#define SMA_wCurrPlay3VolRight			0x0056
#define SMA_wCurrPlay4VolLeft			0x0058
#define SMA_wCurrPlay4VolRight			0x005a
#define SMA_wCurrPlay1PeakLeft			0x005c
#define SMA_wCurrPlay1PeakRight			0x005e
#define SMA_wCurrPlay2PeakLeft			0x0060
#define SMA_wCurrPlay2PeakRight			0x0062
#define SMA_wCurrPlay3PeakLeft			0x0064
#define SMA_wCurrPlay3PeakRight			0x0066
#define SMA_wCurrPlay4PeakLeft			0x0068
#define SMA_wCurrPlay4PeakRight			0x006a
#define SMA_wCurrPlayPeakLeft			0x006c
#define SMA_wCurrPlayPeakRight			0x006e
#define SMA_wCurrDATSR				0x0070
#define SMA_wCurrDATRXCHNL			0x0072
#define SMA_wCurrDATTXCHNL			0x0074
#define SMA_wCurrDATRXRate			0x0076
#define SMA_dwDSPPlayCount			0x0078
#define SMA__size				0x007c

#define INITCODEFILE		"turtlebeach/pndspini.bin"
#define PERMCODEFILE		"turtlebeach/pndsperm.bin"
#define LONGNAME		"MultiSound (Pinnacle/Fiji)"

#endif /* __MSND_PINNACLE_H */
