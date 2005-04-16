#ifndef NOT_CS4281_PM
/*******************************************************************************
*
*      "cs4281pm.h" --  Cirrus Logic-Crystal CS4281 linux audio driver.
*
*      Copyright (C) 2000,2001  Cirrus Logic Corp.  
*            -- tom woller (twoller@crystal.cirrus.com) or
*               (audio@crystal.cirrus.com).
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
/* general pm definitions */
#define CS4281_AC97_HIGHESTREGTORESTORE 0x26
#define CS4281_AC97_NUMBER_RESTORE_REGS (CS4281_AC97_HIGHESTREGTORESTORE/2-1)

/* pipeline definitions */
#define CS4281_NUMBER_OF_PIPELINES 	4
#define CS4281_PIPELINE_VALID 		0x0001
#define CS4281_PLAYBACK_PIPELINE_NUMBER	0x0000
#define CS4281_CAPTURE_PIPELINE_NUMBER 	0x0001

/* PM state defintions */
#define CS4281_PM_NOT_REGISTERED	0x1000
#define CS4281_PM_IDLE			0x0001
#define CS4281_PM_SUSPENDING		0x0002
#define CS4281_PM_SUSPENDED		0x0004
#define CS4281_PM_RESUMING		0x0008
#define CS4281_PM_RESUMED		0x0010

struct cs4281_pm {
	unsigned long flags;
	u32 u32CLKCR1_SAVE,u32SSPMValue,u32PPLVCvalue,u32PPRVCvalue;
	u32 u32FMLVCvalue,u32FMRVCvalue,u32GPIORvalue,u32JSCTLvalue,u32SSCR;
	u32 u32SRCSA,u32DacASR,u32AdcASR,u32DacSR,u32AdcSR,u32MIDCR_Save;
	u32 u32SSPM_BITS;
	u32 ac97[CS4281_AC97_NUMBER_RESTORE_REGS];
	u32 u32AC97_master_volume, u32AC97_headphone_volume, u32AC97_master_volume_mono;
	u32 u32AC97_pcm_out_volume, u32AC97_powerdown, u32AC97_general_purpose;
	u32 u32hwptr_playback,u32hwptr_capture;
};

struct cs4281_pipeline {
	unsigned flags;
	unsigned number;
	u32 u32DBAnValue,u32DBCnValue,u32DMRnValue,u32DCRnValue;
	u32 u32DBAnAddress,u32DCAnAddress,u32DBCnAddress,u32DCCnAddress;
	u32 u32DMRnAddress,u32DCRnAddress,u32HDSRnAddress;
	u32 u32DBAn_Save,u32DBCn_Save,u32DMRn_Save,u32DCRn_Save;
	u32 u32DCCn_Save,u32DCAn_Save;
/* 
* technically, these are fifo variables, but just map the 
* first fifo with the first pipeline and then use the fifo
* variables inside of the pipeline struct.
*/
	u32 u32FCRn_Save,u32FSICn_Save;
	u32 u32FCRnValue,u32FCRnAddress,u32FSICnValue,u32FSICnAddress;
	u32 u32FPDRnValue,u32FPDRnAddress;
};
#endif
