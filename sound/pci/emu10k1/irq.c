// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *                   Creative Labs, Inc.
 *  Routines for IRQ control of EMU10K1 chips
 *
 *  BUGS:
 *    --
 *
 *  TODO:
 *    --
 */

#include <linux/time.h>
#include <sound/core.h>
#include <sound/emu10k1.h>

irqreturn_t snd_emu10k1_interrupt(int irq, void *dev_id)
{
	struct snd_emu10k1 *emu = dev_id;
	unsigned int status, orig_status;
	int handled = 0;
	int timeout = 0;

	while (((status = inl(emu->port + IPR)) != 0) && (timeout < 1000)) {
		timeout++;
		orig_status = status;
		handled = 1;
		if ((status & 0xffffffff) == 0xffffffff) {
			dev_info(emu->card->dev,
				 "Suspected sound card removal\n");
			break;
		}
		if (status & IPR_PCIERROR) {
			dev_err(emu->card->dev, "interrupt: PCI error\n");
			snd_emu10k1_intr_disable(emu, INTE_PCIERRORENABLE);
			status &= ~IPR_PCIERROR;
		}
		if (status & (IPR_VOLINCR|IPR_VOLDECR|IPR_MUTE)) {
			if (emu->hwvol_interrupt)
				emu->hwvol_interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_VOLINCRENABLE|INTE_VOLDECRENABLE|INTE_MUTEENABLE);
			status &= ~(IPR_VOLINCR|IPR_VOLDECR|IPR_MUTE);
		}
		if (status & IPR_CHANNELLOOP) {
			int voice;
			int voice_max = status & IPR_CHANNELNUMBERMASK;
			u32 val;
			struct snd_emu10k1_voice *pvoice = emu->voices;

			val = snd_emu10k1_ptr_read(emu, CLIPL, 0);
			for (voice = 0; voice <= voice_max; voice++) {
				if (voice == 0x20)
					val = snd_emu10k1_ptr_read(emu, CLIPH, 0);
				if (val & 1) {
					if (pvoice->use && pvoice->interrupt != NULL) {
						pvoice->interrupt(emu, pvoice);
						snd_emu10k1_voice_intr_ack(emu, voice);
					} else {
						snd_emu10k1_voice_intr_disable(emu, voice);
					}
				}
				val >>= 1;
				pvoice++;
			}
			val = snd_emu10k1_ptr_read(emu, HLIPL, 0);
			for (voice = 0; voice <= voice_max; voice++) {
				if (voice == 0x20)
					val = snd_emu10k1_ptr_read(emu, HLIPH, 0);
				if (val & 1) {
					if (pvoice->use && pvoice->interrupt != NULL) {
						pvoice->interrupt(emu, pvoice);
						snd_emu10k1_voice_half_loop_intr_ack(emu, voice);
					} else {
						snd_emu10k1_voice_half_loop_intr_disable(emu, voice);
					}
				}
				val >>= 1;
				pvoice++;
			}
			status &= ~IPR_CHANNELLOOP;
		}
		status &= ~IPR_CHANNELNUMBERMASK;
		if (status & (IPR_ADCBUFFULL|IPR_ADCBUFHALFFULL)) {
			if (emu->capture_interrupt)
				emu->capture_interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_ADCBUFENABLE);
			status &= ~(IPR_ADCBUFFULL|IPR_ADCBUFHALFFULL);
		}
		if (status & (IPR_MICBUFFULL|IPR_MICBUFHALFFULL)) {
			if (emu->capture_mic_interrupt)
				emu->capture_mic_interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_MICBUFENABLE);
			status &= ~(IPR_MICBUFFULL|IPR_MICBUFHALFFULL);
		}
		if (status & (IPR_EFXBUFFULL|IPR_EFXBUFHALFFULL)) {
			if (emu->capture_efx_interrupt)
				emu->capture_efx_interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_EFXBUFENABLE);
			status &= ~(IPR_EFXBUFFULL|IPR_EFXBUFHALFFULL);
		}
		if (status & (IPR_MIDITRANSBUFEMPTY|IPR_MIDIRECVBUFEMPTY)) {
			if (emu->midi.interrupt)
				emu->midi.interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_MIDITXENABLE|INTE_MIDIRXENABLE);
			status &= ~(IPR_MIDITRANSBUFEMPTY|IPR_MIDIRECVBUFEMPTY);
		}
		if (status & (IPR_A_MIDITRANSBUFEMPTY2|IPR_A_MIDIRECVBUFEMPTY2)) {
			if (emu->midi2.interrupt)
				emu->midi2.interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_A_MIDITXENABLE2|INTE_A_MIDIRXENABLE2);
			status &= ~(IPR_A_MIDITRANSBUFEMPTY2|IPR_A_MIDIRECVBUFEMPTY2);
		}
		if (status & IPR_INTERVALTIMER) {
			if (emu->timer)
				snd_timer_interrupt(emu->timer, emu->timer->sticks);
			else
				snd_emu10k1_intr_disable(emu, INTE_INTERVALTIMERENB);
			status &= ~IPR_INTERVALTIMER;
		}
		if (status & (IPR_GPSPDIFSTATUSCHANGE|IPR_CDROMSTATUSCHANGE)) {
			if (emu->spdif_interrupt)
				emu->spdif_interrupt(emu, status);
			else
				snd_emu10k1_intr_disable(emu, INTE_GPSPDIFENABLE|INTE_CDSPDIFENABLE);
			status &= ~(IPR_GPSPDIFSTATUSCHANGE|IPR_CDROMSTATUSCHANGE);
		}
		if (status & IPR_FXDSP) {
			if (emu->dsp_interrupt)
				emu->dsp_interrupt(emu);
			else
				snd_emu10k1_intr_disable(emu, INTE_FXDSPENABLE);
			status &= ~IPR_FXDSP;
		}
		if (status & IPR_P16V) {
			if (emu->p16v_interrupt)
				emu->p16v_interrupt(emu);
			else
				outl(0, emu->port + INTE2);
			status &= ~IPR_P16V;
		}

		if (status) {
			unsigned int bits;
			dev_err(emu->card->dev,
				"unhandled interrupt: 0x%08x\n", status);
			//make sure any interrupts we don't handle are disabled:
			bits = INTE_FXDSPENABLE |
				INTE_PCIERRORENABLE |
				INTE_VOLINCRENABLE |
				INTE_VOLDECRENABLE |
				INTE_MUTEENABLE |
				INTE_MICBUFENABLE |
				INTE_ADCBUFENABLE |
				INTE_EFXBUFENABLE |
				INTE_GPSPDIFENABLE |
				INTE_CDSPDIFENABLE |
				INTE_INTERVALTIMERENB |
				INTE_MIDITXENABLE |
				INTE_MIDIRXENABLE;
			if (emu->audigy)
				bits |= INTE_A_MIDITXENABLE2 | INTE_A_MIDIRXENABLE2;
			snd_emu10k1_intr_disable(emu, bits);
		}
		outl(orig_status, emu->port + IPR); /* ack all */
	}
	if (timeout == 1000)
		dev_info(emu->card->dev, "emu10k1 irq routine failure\n");

	return IRQ_RETVAL(handled);
}
