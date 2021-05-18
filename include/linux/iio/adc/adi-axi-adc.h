/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Analog Devices Generic AXI ADC IP core driver/library
 * Link: https://wiki.analog.com/resources/fpga/docs/axi_adc_ip
 *
 * Copyright 2012-2020 Analog Devices Inc.
 */
#ifndef __ADI_AXI_ADC_H__
#define __ADI_AXI_ADC_H__

struct device;
struct iio_chan_spec;

/**
 * struct adi_axi_adc_chip_info - Chip specific information
 * @name		Chip name
 * @id			Chip ID (usually product ID)
 * @channels		Channel specifications of type @struct iio_chan_spec
 * @num_channels	Number of @channels
 * @scale_table		Supported scales by the chip; tuples of 2 ints
 * @num_scales		Number of scales in the table
 * @max_rate		Maximum sampling rate supported by the device
 */
struct adi_axi_adc_chip_info {
	const char			*name;
	unsigned int			id;

	const struct iio_chan_spec	*channels;
	unsigned int			num_channels;

	const unsigned int		(*scale_table)[2];
	int				num_scales;

	unsigned long			max_rate;
};

/**
 * struct adi_axi_adc_conv - data of the ADC attached to the AXI ADC
 * @chip_info		chip info details for the client ADC
 * @preenable_setup	op to run in the client before enabling the AXI ADC
 * @reg_access		IIO debugfs_reg_access hook for the client ADC
 * @read_raw		IIO read_raw hook for the client ADC
 * @write_raw		IIO write_raw hook for the client ADC
 */
struct adi_axi_adc_conv {
	const struct adi_axi_adc_chip_info		*chip_info;

	int (*preenable_setup)(struct adi_axi_adc_conv *conv);
	int (*reg_access)(struct adi_axi_adc_conv *conv, unsigned int reg,
			  unsigned int writeval, unsigned int *readval);
	int (*read_raw)(struct adi_axi_adc_conv *conv,
			struct iio_chan_spec const *chan,
			int *val, int *val2, long mask);
	int (*write_raw)(struct adi_axi_adc_conv *conv,
			 struct iio_chan_spec const *chan,
			 int val, int val2, long mask);
};

struct adi_axi_adc_conv *devm_adi_axi_adc_conv_register(struct device *dev,
							size_t sizeof_priv);

void *adi_axi_adc_conv_priv(struct adi_axi_adc_conv *conv);

#endif
