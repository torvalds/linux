#ifndef __LINUX_TI_AM335X_ADC_H
#define __LINUX_TI_AM335X_ADC_H

/**
 * struct adc_data	ADC Input information
 * @adc_channels:	Number of analog inputs
 *			available for ADC.
 */

struct adc_data {
	unsigned int adc_channels;
};

#endif
