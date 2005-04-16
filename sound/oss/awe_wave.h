/*
 * sound/awe_config.h
 *
 * Configuration of AWE32/SB32/AWE64 wave table synth driver.
 *   version 0.4.4; Jan. 4, 2000
 *
 * Copyright (C) 1996-1998 Takashi Iwai
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * chorus & reverb effects send for FM chip: from 0 to 0xff
 * larger numbers often cause weird sounds.
 */

#define DEF_FM_CHORUS_DEPTH	0x10
#define DEF_FM_REVERB_DEPTH	0x10


/*
 * other compile conditions
 */

/* initialize FM passthrough even without extended RAM */
#undef AWE_ALWAYS_INIT_FM

/* debug on */
#define AWE_DEBUG_ON

/* GUS compatible mode */
#define AWE_HAS_GUS_COMPATIBILITY

/* add MIDI emulation by wavetable */
#define CONFIG_AWE32_MIDIEMU

/* add mixer control of emu8000 equalizer */
#undef CONFIG_AWE32_MIXER

/* use new volume calculation method as default */
#define AWE_USE_NEW_VOLUME_CALC

/* check current volume target for searching empty voices */
#define AWE_CHECK_VTARGET

/* allow sample sharing */
#define AWE_ALLOW_SAMPLE_SHARING

/*
 * AWE32 card configuration:
 * uncomment the following lines *ONLY* when auto detection doesn't
 * work properly on your machine.
 */

/*#define AWE_DEFAULT_BASE_ADDR	0x620*/	/* base port address */
/*#define AWE_DEFAULT_MEM_SIZE	512*/	/* kbytes */

/*
 * AWE driver version number
 */
#define AWE_MAJOR_VERSION	0
#define AWE_MINOR_VERSION	4
#define AWE_TINY_VERSION	4
#define AWE_VERSION_NUMBER	((AWE_MAJOR_VERSION<<16)|(AWE_MINOR_VERSION<<8)|AWE_TINY_VERSION)
#define AWEDRV_VERSION		"0.4.4"
