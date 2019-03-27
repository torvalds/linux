/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Orion Hodson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/pcm/ac97.h>
#include <dev/sound/pcm/ac97_patch.h>

SND_DECLARE_FILE("$FreeBSD$");

void ad1886_patch(struct ac97_info* codec)
{
#define AC97_AD_JACK_SPDIF 0x72
	/*
	 *    Presario700 workaround
	 *     for Jack Sense/SPDIF Register misetting causing
	 *    no audible output
	 *    by Santiago Nullo 04/05/2002
	 */
	ac97_wrcd(codec, AC97_AD_JACK_SPDIF, 0x0010);
}

void ad198x_patch(struct ac97_info* codec)
{
	switch (ac97_getsubvendor(codec)) {
	case 0x11931043:	/* Not for ASUS A9T (probably else too). */
		break;
	default:
		ac97_wrcd(codec, 0x76, ac97_rdcd(codec, 0x76) | 0x0420);
		break;
	}
}

void ad1981b_patch(struct ac97_info* codec)
{
	/*
	 * Enable headphone jack sensing.
	 */
	switch (ac97_getsubvendor(codec)) {
	case 0x02d91014:	/* IBM Thinkcentre */
	case 0x099c103c:	/* HP nx6110 */
		ac97_wrcd(codec, AC97_AD_JACK_SPDIF,
		    ac97_rdcd(codec, AC97_AD_JACK_SPDIF) | 0x0800);
		break;
	default:
		break;
	}
}

void cmi9739_patch(struct ac97_info* codec)
{
	/*
	 * Few laptops need extra register initialization
	 * to power up the internal speakers.
	 */
	switch (ac97_getsubvendor(codec)) {
	case 0x18431043:	/* ASUS W1000N */
		ac97_wrcd(codec, AC97_REG_POWER, 0x000f);
		ac97_wrcd(codec, AC97_MIXEXT_CLFE, 0x0000);
		ac97_wrcd(codec, 0x64, 0x7110);
		break;
	default:
		break;
	}
}

void alc655_patch(struct ac97_info* codec)
{
	/*
	 * MSI (Micro-Star International) specific EAPD quirk.
	 */
	switch (ac97_getsubvendor(codec)) {
	case 0x00611462:	/* MSI S250 */
	case 0x01311462:	/* MSI S270 */
	case 0x01611462:	/* LG K1 Express */
	case 0x03511462:	/* MSI L725 */
		ac97_wrcd(codec, 0x7a, ac97_rdcd(codec, 0x7a) & 0xfffd);
		break;
	case 0x10ca1734:
		/*
		 * Amilo Pro V2055 with ALC655 has phone out by default
		 * disabled (surround on), leaving us only with internal
		 * speakers. This should really go to mixer. We write the
		 * Data Flow Control reg.
		 */
		ac97_wrcd(codec, 0x6a, ac97_rdcd(codec, 0x6a) | 0x0001);
		break;
	default:
		break;
	}
}
