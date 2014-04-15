/*
 * Copyright (C) 2011 Free Electrons
 *
 * Licensed under the GPLv2 or later.
 */

#ifndef _AT91_ADC_H_
#define _AT91_ADC_H_

/**
 * struct at91_adc_trigger - description of triggers
 * @name:		name of the trigger advertised to the user
 * @value:		value to set in the ADC's trigger setup register
			to enable the trigger
 * @is_external:	Does the trigger rely on an external pin?
 */
struct at91_adc_trigger {
	const char	*name;
	u8		value;
	bool		is_external;
};

/**
 * struct at91_adc_data - platform data for ADC driver
 * @channels_used:		channels in use on the board as a bitmask
 * @startup_time:		startup time of the ADC in microseconds
 * @trigger_list:		Triggers available in the ADC
 * @trigger_number:		Number of triggers available in the ADC
 * @use_external_triggers:	does the board has external triggers availables
 * @vref:			Reference voltage for the ADC in millivolts
 */
struct at91_adc_data {
	unsigned long			channels_used;
	u8				startup_time;
	struct at91_adc_trigger		*trigger_list;
	u8				trigger_number;
	bool				use_external_triggers;
	u16				vref;
};

extern void __init at91_add_device_adc(struct at91_adc_data *data);
#endif
