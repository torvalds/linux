/*
 *  linux/include/linux/l3/uda1341.h
 *
 * Philips UDA1341 mixer device driver for ALSA
 *
 * Copyright (c) 2002 Tomas Kasparek <tomas.kasparek@seznam.cz>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 *
 * History:
 *
 * 2002-03-13 Tomas Kasparek Initial release - based on uda1341.h from OSS
 * 2002-03-30 Tomas Kasparek Proc filesystem support, complete mixer and DSP
 *                           features support
 */

/* $Id: uda1341.h,v 1.6 2004/05/03 17:36:50 tiwai Exp $ */

#define UDA1341_ALSA_NAME "snd-uda1341"

/*
 * Default rate set after inicialization
 */
#define AUDIO_RATE_DEFAULT	44100

/*
 * UDA1341 L3 address and command types
 */
#define UDA1341_L3ADDR		5
#define UDA1341_DATA0		(UDA1341_L3ADDR << 2 | 0)
#define UDA1341_DATA1		(UDA1341_L3ADDR << 2 | 1)
#define UDA1341_STATUS		(UDA1341_L3ADDR << 2 | 2)

enum uda1341_onoff {
	OFF=0,
	ON,
};

const char *onoff_names[] = {
	"Off",
	"On",
};

enum uda1341_format {
	I2S=0,
	LSB16,
	LSB18,
	LSB20,
	MSB,
	LSB16MSB,
	LSB18MSB,
	LSB20MSB,        
};

const char *format_names[] = {
	"I2S-bus",
	"LSB 16bits",
	"LSB 18bits",
	"LSB 20bits",
	"MSB",
	"in LSB 16bits/out MSB",
	"in LSB 18bits/out MSB",
	"in LSB 20bits/out MSB",        
};

enum uda1341_fs {
	F512=0,
	F384,
	F256,
	Funused,
};

const char *fs_names[] = {
	"512*fs",
	"384*fs",
	"256*fs",
	"Unused - bad value!",
};

enum uda1341_peak {
	BEFORE=0,
	AFTER,
};

const char *peak_names[] = {
	"before",
	"after",
};

enum uda1341_filter {
	FLAT=0,
	MIN,
	MIN2,
	MAX,
};

const char *filter_names[] = {
	"flat",
	"min",
	"min",
	"max",
};

const char*bass_values[][16] = {
	{"0 dB", "0 dB", "0 dB", "0 dB", "0 dB", "0 dB", "0 dB", "0 dB", "0 dB", "0 dB", "0 dB",
	 "0 dB", "0 dB", "0 dB", "0 dB", "undefined", }, //flat
	{"0 dB", "2 dB", "4 dB", "6 dB", "8 dB", "10 dB", "12 dB", "14 dB", "16 dB", "18 dB", "18 dB",
	 "18 dB", "18 dB", "18 dB", "18 dB", "undefined",}, // min
	{"0 dB", "2 dB", "4 dB", "6 dB", "8 dB", "10 dB", "12 dB", "14 dB", "16 dB", "18 dB", "18 dB",
	 "18 dB", "18 dB", "18 dB", "18 dB", "undefined",}, // min
	{"0 dB", "2 dB", "4 dB", "6 dB", "8 dB", "10 dB", "12 dB", "14 dB", "16 dB", "18 dB", "20 dB",
	 "22 dB", "24 dB", "24 dB", "24 dB", "undefined",}, // max
};

enum uda1341_mixer {
	DOUBLE,
	LINE,
	MIC,
	MIXER,
};

const char *mixer_names[] = {
	"double differential",
	"input channel 1 (line in)",
	"input channel 2 (microphone)",
	"digital mixer",
};

enum uda1341_deemp {
	NONE,
	D32,
	D44,
	D48,
};

const char *deemp_names[] = {
	"none",
	"32 kHz",
	"44.1 kHz",
	"48 kHz",        
};

const char *mic_sens_value[] = {
	"-3 dB", "0 dB", "3 dB", "9 dB", "15 dB", "21 dB", "27 dB", "not used",
};

const unsigned short AGC_atime[] = {
	11, 16, 11, 16, 21, 11, 16, 21,
};

const unsigned short AGC_dtime[] = {
	100, 100, 200, 200, 200, 400, 400, 400,
};

const char *AGC_level[] = {
	"-9.0", "-11.5", "-15.0", "-17.5",
};

const char *ig_small_value[] = {
	"-3.0", "-2.5", "-2.0", "-1.5", "-1.0", "-0.5",
};

/*
 * this was computed as peak_value[i] = pow((63-i)*1.42,1.013)
 *
 * UDA1341 datasheet on page 21: Peak value (dB) = (Peak level - 63.5)*5*log2
 * There is an table with these values [level]=value: [3]=-90.31, [7]=-84.29
 * [61]=-2.78, [62] = -1.48, [63] = 0.0
 * I tried to compute it, but using but even using logarithm with base either 10 or 2
 * i was'n able to get values in the table from the formula. So I constructed another
 * formula (see above) to interpolate the values as good as possible. If there is some
 * mistake, please contact me on tomas.kasparek@seznam.cz. Thanks.
 * UDA1341TS datasheet is available at:
 *   http://www-us9.semiconductors.com/acrobat/datasheets/UDA1341TS_3.pdf 
 */
const char *peak_value[] = {
	"-INF dB", "N.A.", "N.A", "90.31 dB", "N.A.", "N.A.", "N.A.", "-84.29 dB",
	"-82.65 dB", "-81.13 dB", "-79.61 dB", "-78.09 dB", "-76.57 dB", "-75.05 dB", "-73.53 dB",
	"-72.01 dB", "-70.49 dB", "-68.97 dB", "-67.45 dB", "-65.93 dB", "-64.41 dB", "-62.90 dB",
	"-61.38 dB", "-59.86 dB", "-58.35 dB", "-56.83 dB", "-55.32 dB", "-53.80 dB", "-52.29 dB",
	"-50.78 dB", "-49.26 dB", "-47.75 dB", "-46.24 dB", "-44.73 dB", "-43.22 dB", "-41.71 dB",
	"-40.20 dB", "-38.69 dB", "-37.19 dB", "-35.68 dB", "-34.17 dB", "-32.67 dB", "-31.17 dB",
	"-29.66 dB", "-28.16 dB", "-26.66 dB", "-25.16 dB", "-23.66 dB", "-22.16 dB", "-20.67 dB",
	"-19.17 dB", "-17.68 dB", "-16.19 dB", "-14.70 dB", "-13.21 dB", "-11.72 dB", "-10.24 dB",
	"-8.76 dB", "-7.28 dB", "-5.81 dB", "-4.34 dB", "-2.88 dB", "-1.43 dB", "0.00 dB",
};

enum uda1341_config {
	CMD_READ_REG = 0,
	CMD_RESET,
	CMD_FS,
	CMD_FORMAT,
	CMD_OGAIN,
	CMD_IGAIN,
	CMD_DAC,
	CMD_ADC,
	CMD_VOLUME,
	CMD_BASS,
	CMD_TREBBLE,
	CMD_PEAK,
	CMD_DEEMP,
	CMD_MUTE,        
	CMD_FILTER,
	CMD_CH1,
	CMD_CH2,
	CMD_MIC,       
	CMD_MIXER,
	CMD_AGC,
	CMD_IG,
	CMD_AGC_TIME,
	CMD_AGC_LEVEL,
#ifdef CONFIG_PM
	CMD_SUSPEND,
	CMD_RESUME,
#endif
	CMD_LAST,
};

enum write_through {
	//used in update_bits (write_cfg) to avoid l3_write - just update local copy of regs.
	REGS_ONLY=0,
	//update local regs and write value to uda1341 - do l3_write
	FLUSH,
};

int __init snd_chip_uda1341_mixer_new(snd_card_t *card, struct l3_client **clnt);

/*
 * Local variables:
 * indent-tabs-mode: t
 * End:
 */
