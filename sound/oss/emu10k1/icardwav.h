/*     
 **********************************************************************
 *     icardwav.h
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

#ifndef _ICARDWAV_H
#define _ICARDWAV_H

struct wave_format 
{
	int id;
	int samplingrate;
	u8 bitsperchannel;
	u8 channels;		/* 1 = Mono, 2 = Stereo, 3, ... = Multichannel */
	u8 bytesperchannel;
	u8 bytespervoicesample;
	u8 bytespersample;
	int bytespersec;
	u8 passthrough;
};

/* emu10k1_wave states */
#define WAVE_STATE_OPEN		0x01	
#define WAVE_STATE_STARTED	0x02
#define WAVE_STATE_CLOSED	0x04

#endif /* _ICARDWAV_H */
