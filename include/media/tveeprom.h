struct tveeprom {
	u32 has_radio;

	u32 tuner_type;
	u32 tuner_formats;

	u32 digitizer;
	u32 digitizer_formats;

	u32 audio_processor;
	/* a_p_fmts? */

	u32 model;
	u32 revision;
	u32 serial_number;
	char rev_str[5];
};

void tveeprom_hauppauge_analog(struct tveeprom *tvee,
			       unsigned char *eeprom_data);

int tveeprom_read(struct i2c_client *c, unsigned char *eedata, int len);
int tveeprom_dump(unsigned char *eedata, int len);
