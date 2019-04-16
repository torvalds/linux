/*
 * Support code for Analog Devices Sigma-Delta ADCs
 *
 * Copyright 2012 Analog Devices Inc.
 *  Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2.
 */
#ifndef __AD_SIGMA_DELTA_H__
#define __AD_SIGMA_DELTA_H__

enum ad_sigma_delta_mode {
	AD_SD_MODE_CONTINUOUS = 0,
	AD_SD_MODE_SINGLE = 1,
	AD_SD_MODE_IDLE = 2,
	AD_SD_MODE_POWERDOWN = 3,
};

/**
 * struct ad_sigma_delta_calib_data - Calibration data for Sigma Delta devices
 * @mode: Calibration mode.
 * @channel: Calibration channel.
 */
struct ad_sd_calib_data {
	unsigned int mode;
	unsigned int channel;
};

struct ad_sigma_delta;
struct iio_dev;

/**
 * struct ad_sigma_delta_info - Sigma Delta driver specific callbacks and options
 * @set_channel: Will be called to select the current channel, may be NULL.
 * @set_mode: Will be called to select the current mode, may be NULL.
 * @postprocess_sample: Is called for each sampled data word, can be used to
 *		modify or drop the sample data, it, may be NULL.
 * @has_registers: true if the device has writable and readable registers, false
 *		if there is just one read-only sample data shift register.
 * @addr_shift: Shift of the register address in the communications register.
 * @read_mask: Mask for the communications register having the read bit set.
 * @data_reg: Address of the data register, if 0 the default address of 0x3 will
 *   be used.
 */
struct ad_sigma_delta_info {
	int (*set_channel)(struct ad_sigma_delta *, unsigned int channel);
	int (*set_mode)(struct ad_sigma_delta *, enum ad_sigma_delta_mode mode);
	int (*postprocess_sample)(struct ad_sigma_delta *, unsigned int raw_sample);
	bool has_registers;
	unsigned int addr_shift;
	unsigned int read_mask;
	unsigned int data_reg;
};

/**
 * struct ad_sigma_delta - Sigma Delta device struct
 * @spi: The spi device associated with the Sigma Delta device.
 * @trig: The IIO trigger associated with the Sigma Delta device.
 *
 * Most of the fields are private to the sigma delta library code and should not
 * be accessed by individual drivers.
 */
struct ad_sigma_delta {
	struct spi_device	*spi;
	struct iio_trigger	*trig;

/* private: */
	struct completion	completion;
	bool			irq_dis;

	bool			bus_locked;

	uint8_t			comm;

	const struct ad_sigma_delta_info *info;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	uint8_t				data[4] ____cacheline_aligned;
};

static inline int ad_sigma_delta_set_channel(struct ad_sigma_delta *sd,
	unsigned int channel)
{
	if (sd->info->set_channel)
		return sd->info->set_channel(sd, channel);

	return 0;
}

static inline int ad_sigma_delta_set_mode(struct ad_sigma_delta *sd,
	unsigned int mode)
{
	if (sd->info->set_mode)
		return sd->info->set_mode(sd, mode);

	return 0;
}

static inline int ad_sigma_delta_postprocess_sample(struct ad_sigma_delta *sd,
	unsigned int raw_sample)
{
	if (sd->info->postprocess_sample)
		return sd->info->postprocess_sample(sd, raw_sample);

	return 0;
}

void ad_sd_set_comm(struct ad_sigma_delta *sigma_delta, uint8_t comm);
int ad_sd_write_reg(struct ad_sigma_delta *sigma_delta, unsigned int reg,
	unsigned int size, unsigned int val);
int ad_sd_read_reg(struct ad_sigma_delta *sigma_delta, unsigned int reg,
	unsigned int size, unsigned int *val);

int ad_sd_reset(struct ad_sigma_delta *sigma_delta,
	unsigned int reset_length);

int ad_sigma_delta_single_conversion(struct iio_dev *indio_dev,
	const struct iio_chan_spec *chan, int *val);
int ad_sd_calibrate_all(struct ad_sigma_delta *sigma_delta,
	const struct ad_sd_calib_data *cd, unsigned int n);
int ad_sd_init(struct ad_sigma_delta *sigma_delta, struct iio_dev *indio_dev,
	struct spi_device *spi, const struct ad_sigma_delta_info *info);

int ad_sd_setup_buffer_and_trigger(struct iio_dev *indio_dev);
void ad_sd_cleanup_buffer_and_trigger(struct iio_dev *indio_dev);

int ad_sd_validate_trigger(struct iio_dev *indio_dev, struct iio_trigger *trig);

#define __AD_SD_CHANNEL(_si, _channel1, _channel2, _address, _bits, \
	_storagebits, _shift, _extend_name, _type, _mask_all) \
	{ \
		.type = (_type), \
		.differential = (_channel2 == -1 ? 0 : 1), \
		.indexed = 1, \
		.channel = (_channel1), \
		.channel2 = (_channel2), \
		.address = (_address), \
		.extend_name = (_extend_name), \
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) | \
			BIT(IIO_CHAN_INFO_OFFSET), \
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
		.info_mask_shared_by_all = _mask_all, \
		.scan_index = (_si), \
		.scan_type = { \
			.sign = 'u', \
			.realbits = (_bits), \
			.storagebits = (_storagebits), \
			.shift = (_shift), \
			.endianness = IIO_BE, \
		}, \
	}

#define AD_SD_DIFF_CHANNEL(_si, _channel1, _channel2, _address, _bits, \
	_storagebits, _shift) \
	__AD_SD_CHANNEL(_si, _channel1, _channel2, _address, _bits, \
		_storagebits, _shift, NULL, IIO_VOLTAGE, \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD_SD_SHORTED_CHANNEL(_si, _channel, _address, _bits, \
	_storagebits, _shift) \
	__AD_SD_CHANNEL(_si, _channel, _channel, _address, _bits, \
		_storagebits, _shift, "shorted", IIO_VOLTAGE, \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD_SD_CHANNEL(_si, _channel, _address, _bits, \
	_storagebits, _shift) \
	__AD_SD_CHANNEL(_si, _channel, -1, _address, _bits, \
		_storagebits, _shift, NULL, IIO_VOLTAGE, \
		 BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD_SD_CHANNEL_NO_SAMP_FREQ(_si, _channel, _address, _bits, \
	_storagebits, _shift) \
	__AD_SD_CHANNEL(_si, _channel, -1, _address, _bits, \
		_storagebits, _shift, NULL, IIO_VOLTAGE, 0)

#define AD_SD_TEMP_CHANNEL(_si, _address, _bits, _storagebits, _shift) \
	__AD_SD_CHANNEL(_si, 0, -1, _address, _bits, \
		_storagebits, _shift, NULL, IIO_TEMP, \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#define AD_SD_SUPPLY_CHANNEL(_si, _channel, _address, _bits, _storagebits, \
	_shift) \
	__AD_SD_CHANNEL(_si, _channel, -1, _address, _bits, \
		_storagebits, _shift, "supply", IIO_VOLTAGE, \
		BIT(IIO_CHAN_INFO_SAMP_FREQ))

#endif
