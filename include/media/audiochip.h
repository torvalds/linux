/*
 */

#ifndef AUDIOCHIP_H
#define AUDIOCHIP_H

enum audiochip {
	AUDIO_CHIP_NONE,
	AUDIO_CHIP_UNKNOWN,
	/* Provided by video chip */
	AUDIO_CHIP_INTERNAL,
	/* Provided by tvaudio.c */
	AUDIO_CHIP_TDA8425,
	AUDIO_CHIP_TEA6300,
	AUDIO_CHIP_TEA6420,
	AUDIO_CHIP_TDA9840,
	AUDIO_CHIP_TDA985X,
	AUDIO_CHIP_TDA9874,
	AUDIO_CHIP_PIC16C54,
	/* Provided by msp3400.c */
	AUDIO_CHIP_MSP34XX
};

/* ---------------------------------------------------------------------- */

/* v4l device was opened in Radio mode */
#define AUDC_SET_RADIO        _IO('m',2)
/* select from TV,radio,extern,MUTE */
#define AUDC_SET_INPUT        _IOW('m',17,int)

/* audio inputs */
#define AUDIO_TUNER        0x00
#define AUDIO_RADIO        0x01
#define AUDIO_EXTERN       0x02
#define AUDIO_INTERN       0x03
#define AUDIO_OFF          0x04
#define AUDIO_ON           0x05
#define AUDIO_EXTERN_1     AUDIO_EXTERN
#define AUDIO_EXTERN_2     0x06
#define AUDIO_MUTE         0x80
#define AUDIO_UNMUTE       0x81

/* all the stuff below is obsolete and just here for reference.  I'll
 * remove it once the driver is tested and works fine.
 *
 * Instead creating alot of tiny API's for all kinds of different
 * chips, we'll just pass throuth the v4l ioctl structs (v4l2 not
 * yet...).  It is a bit less flexible, but most/all used i2c chips
 * make sense in v4l context only.  So I think that's acceptable...
 */

/* misc stuff to pass around config info to i2c chips */
#define AUDC_CONFIG_PINNACLE  _IOW('m',32,int)
#endif /* AUDIOCHIP_H */
