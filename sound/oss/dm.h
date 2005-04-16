#ifndef _DRIVERS_SOUND_DM_H
#define _DRIVERS_SOUND_DM_H

/*
 *	Definitions of the 'direct midi sound' interface used
 *	by the newer commercial OSS package. We should export
 *	this to userland somewhere in glibc later.
 */

/*
 * Data structure composing an FM "note" or sound event.
 */

struct dm_fm_voice
{
	u8 op;
	u8 voice;
	u8 am;
	u8 vibrato;
	u8 do_sustain;
	u8 kbd_scale;
	u8 harmonic;
	u8 scale_level;
	u8 volume;
	u8 attack;
	u8 decay;
	u8 sustain;
	u8 release;
	u8 feedback;
	u8 connection;
	u8 left;
	u8 right;
	u8 waveform;
};

/*
 *	This describes an FM note by its voice, octave, frequency number (10bit)
 *	and key on/off.
 */

struct dm_fm_note
{
	u8 voice;
	u8 octave;
	u32 fnum;
	u8 key_on;
};

/*
 * FM parameters that apply globally to all voices, and thus are not "notes"
 */

struct dm_fm_params
{
	u8 am_depth;
	u8 vib_depth;
	u8 kbd_split;
	u8 rhythm;

	/* This block is the percussion instrument data */
	u8 bass;
	u8 snare;
	u8 tomtom;
	u8 cymbal;
	u8 hihat;
};

/*
 *	FM mode ioctl settings
 */
 
#define FM_IOCTL_RESET        0x20
#define FM_IOCTL_PLAY_NOTE    0x21
#define FM_IOCTL_SET_VOICE    0x22
#define FM_IOCTL_SET_PARAMS   0x23
#define FM_IOCTL_SET_MODE     0x24
#define FM_IOCTL_SET_OPL      0x25

#endif
