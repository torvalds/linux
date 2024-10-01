/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ACI_H_
#define _ACI_H_

#define ACI_REG_COMMAND		0	/* write register offset */
#define ACI_REG_STATUS		1	/* read register offset */
#define ACI_REG_BUSY		2	/* busy register offset */
#define ACI_REG_RDS		2	/* PCM20: RDS register offset */
#define ACI_MINTIME		500	/* ACI time out limit */

#define ACI_SET_MUTE		0x0d
#define ACI_SET_POWERAMP	0x0f
#define ACI_SET_TUNERMUTE	0xa3
#define ACI_SET_TUNERMONO	0xa4
#define ACI_SET_IDE		0xd0
#define ACI_SET_WSS		0xd1
#define ACI_SET_SOLOMODE	0xd2
#define ACI_SET_PREAMP		0x03
#define ACI_GET_PREAMP		0x21
#define ACI_WRITE_TUNE		0xa7
#define ACI_READ_TUNERSTEREO	0xa8
#define ACI_READ_TUNERSTATION	0xa9
#define ACI_READ_VERSION	0xf1
#define ACI_READ_IDCODE		0xf2
#define ACI_INIT		0xff
#define ACI_STATUS		0xf0
#define ACI_S_GENERAL		0x00
#define ACI_ERROR_OP		0xdf

/* ACI Mixer */

/* These are the values for the right channel GET registers.
   Add an offset of 0x01 for the left channel register.
   (left=right+0x01) */

#define ACI_GET_MASTER		0x03
#define ACI_GET_MIC		0x05
#define ACI_GET_LINE		0x07
#define ACI_GET_CD		0x09
#define ACI_GET_SYNTH		0x0b
#define ACI_GET_PCM		0x0d
#define ACI_GET_LINE1		0x10	/* Radio on PCM20 */
#define ACI_GET_LINE2		0x12

#define ACI_GET_EQ1		0x22	/* from Bass ... */
#define ACI_GET_EQ2		0x24
#define ACI_GET_EQ3		0x26
#define ACI_GET_EQ4		0x28
#define ACI_GET_EQ5		0x2a
#define ACI_GET_EQ6		0x2c
#define ACI_GET_EQ7		0x2e	/* ... to Treble */

/* And these are the values for the right channel SET registers.
   For left channel access you have to add an offset of 0x08.
   MASTER is an exception, which needs an offset of 0x01 */

#define ACI_SET_MASTER		0x00
#define ACI_SET_MIC		0x30
#define ACI_SET_LINE		0x31
#define ACI_SET_CD		0x34
#define ACI_SET_SYNTH		0x33
#define ACI_SET_PCM		0x32
#define ACI_SET_LINE1		0x35	/* Radio on PCM20 */
#define ACI_SET_LINE2		0x36

#define ACI_SET_EQ1		0x40	/* from Bass ... */
#define ACI_SET_EQ2		0x41
#define ACI_SET_EQ3		0x42
#define ACI_SET_EQ4		0x43
#define ACI_SET_EQ5		0x44
#define ACI_SET_EQ6		0x45
#define ACI_SET_EQ7		0x46	/* ... to Treble */

struct snd_miro_aci {
	struct snd_card *card;
	unsigned long aci_port;
	int aci_vendor;
	int aci_product;
	int aci_version;
	int aci_amp;
	int aci_preamp;
	int aci_solomode;

	struct mutex aci_mutex;
};

int snd_aci_cmd(struct snd_miro_aci *aci, int write1, int write2, int write3);

struct snd_miro_aci *snd_aci_get_aci(void);

#endif  /* _ACI_H_ */

