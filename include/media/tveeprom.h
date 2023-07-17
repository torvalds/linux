/* SPDX-License-Identifier: GPL-2.0 */

/*
 * tveeprom - Contains structures and functions to work with Hauppauge
 *	      eeproms.
 */

#include <uapi/linux/if_ether.h>

/**
 * enum tveeprom_audio_processor - Specifies the type of audio processor
 *				   used on a Hauppauge device.
 *
 * @TVEEPROM_AUDPROC_NONE:	No audio processor present
 * @TVEEPROM_AUDPROC_INTERNAL:	The audio processor is internal to the
 *				video processor
 * @TVEEPROM_AUDPROC_MSP:	The audio processor is a MSPXXXX device
 * @TVEEPROM_AUDPROC_OTHER:	The audio processor is another device
 */
enum tveeprom_audio_processor {
	TVEEPROM_AUDPROC_NONE,
	TVEEPROM_AUDPROC_INTERNAL,
	TVEEPROM_AUDPROC_MSP,
	TVEEPROM_AUDPROC_OTHER,
};

/**
 * struct tveeprom - Contains the fields parsed from Hauppauge eeproms
 *
 * @has_radio:			1 if the device has radio; 0 otherwise.
 *
 * @has_ir:			If has_ir == 0, then it is unknown what the IR
 *				capabilities are. Otherwise:
 *				bit 0) 1 (= IR capabilities are known);
 *				bit 1) IR receiver present;
 *				bit 2) IR transmitter (blaster) present.
 *
 * @has_MAC_address:		0: no MAC, 1: MAC present, 2: unknown.
 * @tuner_type:			type of the tuner (TUNER_*, as defined at
 *				include/media/tuner.h).
 *
 * @tuner_formats:		Supported analog TV standards (V4L2_STD_*).
 * @tuner_hauppauge_model:	Hauppauge's code for the device model number.
 * @tuner2_type:		type of the second tuner (TUNER_*, as defined
 *				at include/media/tuner.h).
 *
 * @tuner2_formats:		Tuner 2 supported analog TV standards
 *				(V4L2_STD_*).
 *
 * @tuner2_hauppauge_model:	tuner 2 Hauppauge's code for the device model
 *				number.
 *
 * @audio_processor:		analog audio decoder, as defined by enum
 *				tveeprom_audio_processor.
 *
 * @decoder_processor:		Hauppauge's code for the decoder chipset.
 *				Unused by the drivers, as they probe the
 *				decoder based on the PCI or USB ID.
 *
 * @model:			Hauppauge's model number
 *
 * @revision:			Card revision number
 *
 * @serial_number:		Card's serial number
 *
 * @rev_str:			Card revision converted to number
 *
 * @MAC_address:		MAC address for the network interface
 */
struct tveeprom {
	u32 has_radio;
	u32 has_ir;
	u32 has_MAC_address;

	u32 tuner_type;
	u32 tuner_formats;
	u32 tuner_hauppauge_model;

	u32 tuner2_type;
	u32 tuner2_formats;
	u32 tuner2_hauppauge_model;

	u32 audio_processor;
	u32 decoder_processor;

	u32 model;
	u32 revision;
	u32 serial_number;
	char rev_str[5];
	u8 MAC_address[ETH_ALEN];
};

/**
 * tveeprom_hauppauge_analog - Fill struct tveeprom using the contents
 *			       of the eeprom previously filled at
 *			       @eeprom_data field.
 *
 * @tvee:		Struct to where the eeprom parsed data will be filled;
 * @eeprom_data:	Array with the contents of the eeprom_data. It should
 *			contain 256 bytes filled with the contents of the
 *			eeprom read from the Hauppauge device.
 */
void tveeprom_hauppauge_analog(struct tveeprom *tvee,
			       unsigned char *eeprom_data);

/**
 * tveeprom_read - Reads the contents of the eeprom found at the Hauppauge
 *		   devices.
 *
 * @c:		I2C client struct
 * @eedata:	Array where the eeprom content will be stored.
 * @len:	Size of @eedata array. If the eeprom content will be latter
 *		be parsed by tveeprom_hauppauge_analog(), len should be, at
 *		least, 256.
 */
int tveeprom_read(struct i2c_client *c, unsigned char *eedata, int len);
