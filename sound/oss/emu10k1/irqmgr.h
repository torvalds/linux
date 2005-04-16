/*
 **********************************************************************
 *     irq.h
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

#ifndef _IRQ_H
#define _IRQ_H

/* EMU Irq Types */
#define IRQTYPE_PCIBUSERROR	IPR_PCIERROR
#define IRQTYPE_MIXERBUTTON	(IPR_VOLINCR | IPR_VOLDECR | IPR_MUTE)
#define IRQTYPE_VOICE		(IPR_CHANNELLOOP | IPR_CHANNELNUMBERMASK)
#define IRQTYPE_RECORD		(IPR_ADCBUFFULL | IPR_ADCBUFHALFFULL | IPR_MICBUFFULL | IPR_MICBUFHALFFULL | IPR_EFXBUFFULL | IPR_EFXBUFHALFFULL)
#define IRQTYPE_MPUOUT		(IPR_MIDITRANSBUFEMPTY | A_IPR_MIDITRANSBUFEMPTY2) 
#define IRQTYPE_MPUIN		(IPR_MIDIRECVBUFEMPTY | A_IPR_MIDIRECVBUFEMPTY2)
#define IRQTYPE_TIMER		IPR_INTERVALTIMER
#define IRQTYPE_SPDIF		(IPR_GPSPDIFSTATUSCHANGE | IPR_CDROMSTATUSCHANGE)
#define IRQTYPE_DSP		IPR_FXDSP

void emu10k1_timer_irqhandler(struct emu10k1_card *);
void emu10k1_dsp_irqhandler(struct emu10k1_card *);
void emu10k1_mute_irqhandler(struct emu10k1_card *);
void emu10k1_volincr_irqhandler(struct emu10k1_card *);
void emu10k1_voldecr_irqhandler(struct emu10k1_card *);

#endif /* _IRQ_H */
