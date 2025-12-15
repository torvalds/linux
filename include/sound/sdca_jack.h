/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The MIPI SDCA specification is available for public downloads at
 * https://www.mipi.org/mipi-sdca-v1-0-download
 *
 * Copyright (C) 2025 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#ifndef __SDCA_JACK_H__
#define __SDCA_JACK_H__

struct sdca_interrupt;
struct snd_kcontrol;

/**
 * struct jack_state - Jack state structure to keep data between interrupts
 * @kctl: Pointer to the ALSA control attached to this jack
 */
struct jack_state {
	struct snd_kcontrol *kctl;
};

int sdca_jack_alloc_state(struct sdca_interrupt *interrupt);
int sdca_jack_process(struct sdca_interrupt *interrupt);

#endif // __SDCA_JACK_H__
