/*
 *   Additional mixer mapping
 *
 *   Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

struct usbmix_dB_map {
	u32 min;
	u32 max;
};

struct usbmix_name_map {
	int id;
	const char *name;
	int control;
	struct usbmix_dB_map *dB;
};

struct usbmix_selector_map {
	int id;
	int count;
	const char **names;
};

struct usbmix_ctl_map {
	u32 id;
	const struct usbmix_name_map *map;
	const struct usbmix_selector_map *selector_map;
	int ignore_ctl_error;
};

/*
 * USB control mappers for SB Exitigy
 */

/*
 * Topology of SB Extigy (see on the wide screen :)

USB_IN[1] --->FU[2]------------------------------+->MU[16]-->PU[17]-+->FU[18]--+->EU[27]--+->EU[21]-->FU[22]--+->FU[23] > Dig_OUT[24]
                                                 ^                  |          |          |                   |
USB_IN[3] -+->SU[5]-->FU[6]--+->MU[14] ->PU[15]->+                  |          |          |                   +->FU[25] > Dig_OUT[26]
           ^                 ^                   |                  |          |          |
Dig_IN[4] -+                 |                   |                  |          |          +->FU[28]---------------------> Spk_OUT[19]
                             |                   |                  |          |
Lin-IN[7] -+-->FU[8]---------+                   |                  |          +----------------------------------------> Hph_OUT[20]
           |                                     |                  |
Mic-IN[9] --+->FU[10]----------------------------+                  |
           ||                                                       |
           ||  +----------------------------------------------------+
           VV  V
           ++--+->SU[11]-->FU[12] --------------------------------------------------------------------------------------> USB_OUT[13]
*/

static struct usbmix_name_map extigy_map[] = {
	/* 1: IT pcm */
	{ 2, "PCM Playback" }, /* FU */
	/* 3: IT pcm */
	/* 4: IT digital in */
	{ 5, NULL }, /* DISABLED: this seems to be bogus on some firmware */
	{ 6, "Digital In" }, /* FU */
	/* 7: IT line */
	{ 8, "Line Playback" }, /* FU */
	/* 9: IT mic */
	{ 10, "Mic Playback" }, /* FU */
	{ 11, "Capture Source" }, /* SU */
	{ 12, "Capture" }, /* FU */
	/* 13: OT pcm capture */
	/* 14: MU (w/o controls) */
	/* 15: PU (3D enh) */
	/* 16: MU (w/o controls) */
	{ 17, NULL, 1 }, /* DISABLED: PU-switch (any effect?) */
	{ 17, "Channel Routing", 2 },	/* PU: mode select */
	{ 18, "Tone Control - Bass", UAC_FU_BASS }, /* FU */
	{ 18, "Tone Control - Treble", UAC_FU_TREBLE }, /* FU */
	{ 18, "Master Playback" }, /* FU; others */
	/* 19: OT speaker */
	/* 20: OT headphone */
	{ 21, NULL }, /* DISABLED: EU (for what?) */
	{ 22, "Digital Out Playback" }, /* FU */
	{ 23, "Digital Out1 Playback" }, /* FU */  /* FIXME: corresponds to 24 */
	/* 24: OT digital out */
	{ 25, "IEC958 Optical Playback" }, /* FU */
	{ 26, "IEC958 Optical Playback" }, /* OT */
	{ 27, NULL }, /* DISABLED: EU (for what?) */
	/* 28: FU speaker (mute) */
	{ 29, NULL }, /* Digital Input Playback Source? */
	{ 0 } /* terminator */
};

/* Sound Blaster MP3+ controls mapping
 * The default mixer channels have totally misleading names,
 * e.g. no Master and fake PCM volume
 *			Pavel Mihaylov <bin@bash.info>
 */
static struct usbmix_dB_map mp3plus_dB_1 = {-4781, 0};	/* just guess */
static struct usbmix_dB_map mp3plus_dB_2 = {-1781, 618}; /* just guess */

static struct usbmix_name_map mp3plus_map[] = {
	/* 1: IT pcm */
	/* 2: IT mic */
	/* 3: IT line */
	/* 4: IT digital in */
	/* 5: OT digital out */
	/* 6: OT speaker */
	/* 7: OT pcm capture */
	{ 8, "Capture Source" }, /* FU, default PCM Capture Source */
		/* (Mic, Input 1 = Line input, Input 2 = Optical input) */
	{ 9, "Master Playback" }, /* FU, default Speaker 1 */
	/* { 10, "Mic Capture", 1 }, */ /* FU, Mic Capture */
	{ 10, /* "Mic Capture", */ NULL, 2, .dB = &mp3plus_dB_2 },
		/* FU, Mic Capture */
	{ 10, "Mic Boost", 7 }, /* FU, default Auto Gain Input */
	{ 11, "Line Capture", .dB = &mp3plus_dB_2 },
		/* FU, default PCM Capture */
	{ 12, "Digital In Playback" }, /* FU, default PCM 1 */
	{ 13, /* "Mic Playback", */ .dB = &mp3plus_dB_1 },
		/* FU, default Mic Playback */
	{ 14, "Line Playback", .dB = &mp3plus_dB_1 }, /* FU, default Speaker */
	/* 15: MU */
	{ 0 } /* terminator */
};

/* Topology of SB Audigy 2 NX

          +----------------------------->EU[27]--+
          |                                      v
          | +----------------------------------->SU[29]---->FU[22]-->Dig_OUT[24]
          | |                                    ^
USB_IN[1]-+------------+              +->EU[17]->+->FU[11]-+
            |          v              |          v         |
Dig_IN[4]---+->FU[6]-->MU[16]->FU[18]-+->EU[21]->SU[31]----->FU[30]->Hph_OUT[20]
            |          ^              |                    |
Lin_IN[7]-+--->FU[8]---+              +->EU[23]->FU[28]------------->Spk_OUT[19]
          | |                                              v
          +--->FU[12]------------------------------------->SU[14]--->USB_OUT[15]
            |                                              ^
            +->FU[13]--------------------------------------+
*/
static struct usbmix_name_map audigy2nx_map[] = {
	/* 1: IT pcm playback */
	/* 4: IT digital in */
	{ 6, "Digital In Playback" }, /* FU */
	/* 7: IT line in */
	{ 8, "Line Playback" }, /* FU */
	{ 11, "What-U-Hear Capture" }, /* FU */
	{ 12, "Line Capture" }, /* FU */
	{ 13, "Digital In Capture" }, /* FU */
	{ 14, "Capture Source" }, /* SU */
	/* 15: OT pcm capture */
	/* 16: MU w/o controls */
	{ 17, NULL }, /* DISABLED: EU (for what?) */
	{ 18, "Master Playback" }, /* FU */
	/* 19: OT speaker */
	/* 20: OT headphone */
	{ 21, NULL }, /* DISABLED: EU (for what?) */
	{ 22, "Digital Out Playback" }, /* FU */
	{ 23, NULL }, /* DISABLED: EU (for what?) */
	/* 24: OT digital out */
	{ 27, NULL }, /* DISABLED: EU (for what?) */
	{ 28, "Speaker Playback" }, /* FU */
	{ 29, "Digital Out Source" }, /* SU */
	{ 30, "Headphone Playback" }, /* FU */
	{ 31, "Headphone Source" }, /* SU */
	{ 0 } /* terminator */
};

static struct usbmix_selector_map c400_selectors[] = {
	{
		.id = 0x80,
		.count = 2,
		.names = (const char*[]) {"Internal", "SPDIF"}
	},
	{ 0 } /* terminator */
};

static struct usbmix_selector_map audigy2nx_selectors[] = {
	{
		.id = 14, /* Capture Source */
		.count = 3,
		.names = (const char*[]) {"Line", "Digital In", "What-U-Hear"}
	},
	{
		.id = 29, /* Digital Out Source */
		.count = 3,
		.names = (const char*[]) {"Front", "PCM", "Digital In"}
	},
	{
		.id = 31, /* Headphone Source */
		.count = 2,
		.names = (const char*[]) {"Front", "Side"}
	},
	{ 0 } /* terminator */
};

/* Creative SoundBlaster Live! 24-bit External */
static struct usbmix_name_map live24ext_map[] = {
	/* 2: PCM Playback Volume */
	{ 5, "Mic Capture" }, /* FU, default PCM Capture Volume */
	{ 0 } /* terminator */
};

/* LineX FM Transmitter entry - needed to bypass controls bug */
static struct usbmix_name_map linex_map[] = {
	/* 1: IT pcm */
	/* 2: OT Speaker */ 
	{ 3, "Master" }, /* FU: master volume - left / right / mute */
	{ 0 } /* terminator */
};

static struct usbmix_name_map maya44_map[] = {
	/* 1: IT line */
	{ 2, "Line Playback" }, /* FU */
	/* 3: IT line */
	{ 4, "Line Playback" }, /* FU */
	/* 5: IT pcm playback */
	/* 6: MU */
	{ 7, "Master Playback" }, /* FU */
	/* 8: OT speaker */
	/* 9: IT line */
	{ 10, "Line Capture" }, /* FU */
	/* 11: MU */
	/* 12: OT pcm capture */
	{ }
};

/* Section "justlink_map" below added by James Courtier-Dutton <James@superbug.demon.co.uk>
 * sourced from Maplin Electronics (http://www.maplin.co.uk), part number A56AK
 * Part has 2 connectors that act as a single output. (TOSLINK Optical for digital out, and 3.5mm Jack for Analogue out.)
 * The USB Mixer publishes a Microphone and extra Volume controls for it, but none exist on the device,
 * so this map removes all unwanted sliders from alsamixer
 */

static struct usbmix_name_map justlink_map[] = {
	/* 1: IT pcm playback */
	/* 2: Not present */
	{ 3, NULL}, /* IT mic (No mic input on device) */
	/* 4: Not present */
	/* 5: OT speacker */
	/* 6: OT pcm capture */
	{ 7, "Master Playback" }, /* Mute/volume for speaker */
	{ 8, NULL }, /* Capture Switch (No capture inputs on device) */
	{ 9, NULL }, /* Capture Mute/volume (No capture inputs on device */
	/* 0xa: Not present */
	/* 0xb: MU (w/o controls) */
	{ 0xc, NULL }, /* Mic feedback Mute/volume (No capture inputs on device) */
	{ 0 } /* terminator */
};

/* TerraTec Aureon 5.1 MkII USB */
static struct usbmix_name_map aureon_51_2_map[] = {
	/* 1: IT USB */
	/* 2: IT Mic */
	/* 3: IT Line */
	/* 4: IT SPDIF */
	/* 5: OT SPDIF */
	/* 6: OT Speaker */
	/* 7: OT USB */
	{ 8, "Capture Source" }, /* SU */
	{ 9, "Master Playback" }, /* FU */
	{ 10, "Mic Capture" }, /* FU */
	{ 11, "Line Capture" }, /* FU */
	{ 12, "IEC958 In Capture" }, /* FU */
	{ 13, "Mic Playback" }, /* FU */
	{ 14, "Line Playback" }, /* FU */
	/* 15: MU */
	{} /* terminator */
};

static struct usbmix_name_map scratch_live_map[] = {
	/* 1: IT Line 1 (USB streaming) */
	/* 2: OT Line 1 (Speaker) */
	/* 3: IT Line 1 (Line connector) */
	{ 4, "Line 1 In" }, /* FU */
	/* 5: OT Line 1 (USB streaming) */
	/* 6: IT Line 2 (USB streaming) */
	/* 7: OT Line 2 (Speaker) */
	/* 8: IT Line 2 (Line connector) */
	{ 9, "Line 2 In" }, /* FU */
	/* 10: OT Line 2 (USB streaming) */
	/* 11: IT Mic (Line connector) */
	/* 12: OT Mic (USB streaming) */
	{ 0 } /* terminator */
};

static struct usbmix_name_map ebox44_map[] = {
	{ 4, NULL }, /* FU */
	{ 6, NULL }, /* MU */
	{ 7, NULL }, /* FU */
	{ 10, NULL }, /* FU */
	{ 11, NULL }, /* MU */
	{ 0 }
};

/* "Gamesurround Muse Pocket LT" looks same like "Sound Blaster MP3+"
 *  most importand difference is SU[8], it should be set to "Capture Source"
 *  to make alsamixer and PA working properly.
 *  FIXME: or mp3plus_map should use "Capture Source" too,
 *  so this maps can be merget
 */
static struct usbmix_name_map hercules_usb51_map[] = {
	{ 8, "Capture Source" },	/* SU, default "PCM Capture Source" */
	{ 9, "Master Playback" },	/* FU, default "Speaker Playback" */
	{ 10, "Mic Boost", 7 },		/* FU, default "Auto Gain Input" */
	{ 11, "Line Capture" },		/* FU, default "PCM Capture" */
	{ 13, "Mic Bypass Playback" },	/* FU, default "Mic Playback" */
	{ 14, "Line Bypass Playback" },	/* FU, default "Line Playback" */
	{ 0 }				/* terminator */
};

/*
 * Control map entries
 */

static struct usbmix_ctl_map usbmix_ctl_maps[] = {
	{
		.id = USB_ID(0x041e, 0x3000),
		.map = extigy_map,
		.ignore_ctl_error = 1,
	},
	{
		.id = USB_ID(0x041e, 0x3010),
		.map = mp3plus_map,
	},
	{
		.id = USB_ID(0x041e, 0x3020),
		.map = audigy2nx_map,
		.selector_map = audigy2nx_selectors,
	},
 	{
		.id = USB_ID(0x041e, 0x3040),
		.map = live24ext_map,
	},
	{
		.id = USB_ID(0x041e, 0x3048),
		.map = audigy2nx_map,
		.selector_map = audigy2nx_selectors,
	},
	{	/* Logitech, Inc. QuickCam Pro for Notebooks */
		.id = USB_ID(0x046d, 0x0991),
		.ignore_ctl_error = 1,
	},
	{	/* Logitech, Inc. QuickCam E 3500 */
		.id = USB_ID(0x046d, 0x09a4),
		.ignore_ctl_error = 1,
	},
	{
		/* Hercules DJ Console (Windows Edition) */
		.id = USB_ID(0x06f8, 0xb000),
		.ignore_ctl_error = 1,
	},
	{
		/* Hercules DJ Console (Macintosh Edition) */
		.id = USB_ID(0x06f8, 0xd002),
		.ignore_ctl_error = 1,
	},
	{
		/* Hercules Gamesurround Muse Pocket LT
		 * (USB 5.1 Channel Audio Adapter)
		 */
		.id = USB_ID(0x06f8, 0xc000),
		.map = hercules_usb51_map,
	},
	{
		.id = USB_ID(0x0763, 0x2030),
		.selector_map = c400_selectors,
	},
	{
		.id = USB_ID(0x0763, 0x2031),
		.selector_map = c400_selectors,
	},
	{
		.id = USB_ID(0x08bb, 0x2702),
		.map = linex_map,
		.ignore_ctl_error = 1,
	},
	{
		.id = USB_ID(0x0a92, 0x0091),
		.map = maya44_map,
	},
	{
		.id = USB_ID(0x0c45, 0x1158),
		.map = justlink_map,
	},
	{
		.id = USB_ID(0x0ccd, 0x0028),
		.map = aureon_51_2_map,
	},
	{
		.id = USB_ID(0x13e5, 0x0001),
		.map = scratch_live_map,
		.ignore_ctl_error = 1,
	},
	{
		.id = USB_ID(0x200c, 0x1018),
		.map = ebox44_map,
	},
	{ 0 } /* terminator */
};

