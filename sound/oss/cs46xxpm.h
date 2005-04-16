/*******************************************************************************
*
*      "cs46xxpm.h" --  Cirrus Logic-Crystal CS46XX linux audio driver.
*
*      Copyright (C) 2000,2001  Cirrus Logic Corp.  
*            -- tom woller (twoller@crystal.cirrus.com) or
*               (pcaudio@crystal.cirrus.com).
*
*      This program is free software; you can redistribute it and/or modify
*      it under the terms of the GNU General Public License as published by
*      the Free Software Foundation; either version 2 of the License, or
*      (at your option) any later version.
*
*      This program is distributed in the hope that it will be useful,
*      but WITHOUT ANY WARRANTY; without even the implied warranty of
*      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*      GNU General Public License for more details.
*
*      You should have received a copy of the GNU General Public License
*      along with this program; if not, write to the Free Software
*      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*
* 12/22/00 trw - new file. 
*
*******************************************************************************/
#ifndef __CS46XXPM_H
#define __CS46XXPM_H

#define CS46XX_AC97_HIGHESTREGTORESTORE 0x26
#define CS46XX_AC97_NUMBER_RESTORE_REGS (CS46XX_AC97_HIGHESTREGTORESTORE/2-1)

/* PM state defintions */
#define CS46XX_PM_NOT_REGISTERED	0x1000
#define CS46XX_PM_IDLE			0x0001
#define CS46XX_PM_SUSPENDING		0x0002
#define CS46XX_PM_SUSPENDED		0x0004
#define CS46XX_PM_RESUMING		0x0008
#define CS46XX_PM_RESUMED		0x0010

#define CS_POWER_DAC			0x0001
#define CS_POWER_ADC			0x0002
#define CS_POWER_MIXVON			0x0004
#define CS_POWER_MIXVOFF		0x0008
#define CS_AC97_POWER_CONTROL_ON	0xf000  /* always on bits (inverted) */
#define CS_AC97_POWER_CONTROL_ADC	0x0100
#define CS_AC97_POWER_CONTROL_DAC	0x0200
#define CS_AC97_POWER_CONTROL_MIXVON	0x0400
#define CS_AC97_POWER_CONTROL_MIXVOFF	0x0800
#define CS_AC97_POWER_CONTROL_ADC_ON	0x0001
#define CS_AC97_POWER_CONTROL_DAC_ON	0x0002
#define CS_AC97_POWER_CONTROL_MIXVON_ON	0x0004
#define CS_AC97_POWER_CONTROL_MIXVOFF_ON 0x0008

struct cs46xx_pm {
	unsigned long flags;
	u32 u32CLKCR1_SAVE,u32SSPMValue,u32PPLVCvalue,u32PPRVCvalue;
	u32 u32FMLVCvalue,u32FMRVCvalue,u32GPIORvalue,u32JSCTLvalue,u32SSCR;
	u32 u32SRCSA,u32DacASR,u32AdcASR,u32DacSR,u32AdcSR,u32MIDCR_Save;
	u32 u32SSPM_BITS;
	u32 ac97[CS46XX_AC97_NUMBER_RESTORE_REGS];
	u32 u32AC97_master_volume, u32AC97_headphone_volume, u32AC97_master_volume_mono;
	u32 u32AC97_pcm_out_volume, u32AC97_powerdown, u32AC97_general_purpose;
	u32 u32hwptr_playback,u32hwptr_capture;
	unsigned dmabuf_swptr_play;
	int dmabuf_count_play;
	unsigned dmabuf_swptr_capture;
	int dmabuf_count_capture;
};

#endif
