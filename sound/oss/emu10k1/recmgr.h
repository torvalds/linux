/*     
 **********************************************************************
 *     recmgr.h
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

#ifndef _RECORDMGR_H
#define _RECORDMGR_H

#include "hwaccess.h"
#include "cardwi.h"

/* Recording resources */
#define WAVERECORD_AC97		0x01
#define WAVERECORD_MIC		0x02
#define WAVERECORD_FX		0x03

void emu10k1_reset_record(struct emu10k1_card *card, struct wavein_buffer *buffer);
void emu10k1_start_record(struct emu10k1_card *, struct wavein_buffer *);
void emu10k1_stop_record(struct emu10k1_card *, struct wavein_buffer *);
void emu10k1_set_record_src(struct emu10k1_card *, struct wiinst *wiinst);

#endif /* _RECORDMGR_H */
