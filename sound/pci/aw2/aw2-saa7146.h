/* SPDX-License-Identifier: GPL-2.0-only */
/*****************************************************************************
 *
 * Copyright (C) 2008 Cedric Bregardis <cedric.bregardis@free.fr> and
 * Jean-Christian Hassler <jhassler@free.fr>
 *
 * This file is part of the Audiowerk2 ALSA driver
 *
 *****************************************************************************/

#ifndef AW2_SAA7146_H
#define AW2_SAA7146_H

#define NB_STREAM_PLAYBACK 2
#define NB_STREAM_CAPTURE 1

#define NUM_STREAM_PLAYBACK_ANA 0
#define NUM_STREAM_PLAYBACK_DIG 1

#define NUM_STREAM_CAPTURE_ANA 0

typedef void (*snd_aw2_saa7146_it_cb) (void *);

struct snd_aw2_saa7146_cb_param {
	snd_aw2_saa7146_it_cb p_it_callback;
	void *p_callback_param;
};

/* definition of the chip-specific record */

struct snd_aw2_saa7146 {
	void __iomem *base_addr;
};

extern void snd_aw2_saa7146_setup(struct snd_aw2_saa7146 *chip,
				  void __iomem *pci_base_addr);
extern int snd_aw2_saa7146_free(struct snd_aw2_saa7146 *chip);

extern void snd_aw2_saa7146_pcm_init_playback(struct snd_aw2_saa7146 *chip,
					      int stream_number,
					      unsigned long dma_addr,
					      unsigned long period_size,
					      unsigned long buffer_size);
extern void snd_aw2_saa7146_pcm_init_capture(struct snd_aw2_saa7146 *chip,
					     int stream_number,
					     unsigned long dma_addr,
					     unsigned long period_size,
					     unsigned long buffer_size);
extern void snd_aw2_saa7146_define_it_playback_callback(unsigned int
							stream_number,
							snd_aw2_saa7146_it_cb
							p_it_callback,
							void *p_callback_param);
extern void snd_aw2_saa7146_define_it_capture_callback(unsigned int
						       stream_number,
						       snd_aw2_saa7146_it_cb
						       p_it_callback,
						       void *p_callback_param);
extern void snd_aw2_saa7146_pcm_trigger_start_capture(struct snd_aw2_saa7146
						      *chip, int stream_number);
extern void snd_aw2_saa7146_pcm_trigger_stop_capture(struct snd_aw2_saa7146
						     *chip, int stream_number);

extern void snd_aw2_saa7146_pcm_trigger_start_playback(struct snd_aw2_saa7146
						       *chip,
						       int stream_number);
extern void snd_aw2_saa7146_pcm_trigger_stop_playback(struct snd_aw2_saa7146
						      *chip, int stream_number);

extern irqreturn_t snd_aw2_saa7146_interrupt(int irq, void *dev_id);
extern unsigned int snd_aw2_saa7146_get_hw_ptr_playback(struct snd_aw2_saa7146
							*chip,
							int stream_number,
							unsigned char
							*start_addr,
							unsigned int
							buffer_size);
extern unsigned int snd_aw2_saa7146_get_hw_ptr_capture(struct snd_aw2_saa7146
						       *chip,
						       int stream_number,
						       unsigned char
						       *start_addr,
						       unsigned int
						       buffer_size);

extern void snd_aw2_saa7146_use_digital_input(struct snd_aw2_saa7146 *chip,
					      int use_digital);

extern int snd_aw2_saa7146_is_using_digital_input(struct snd_aw2_saa7146
						  *chip);

#endif
