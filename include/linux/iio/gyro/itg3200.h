/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * itg3200.h -- support InvenSense ITG3200
 *              Digital 3-Axis Gyroscope driver
 *
 * Copyright (c) 2011 Christian Strobel <christian.strobel@iis.fraunhofer.de>
 * Copyright (c) 2011 Manuel Stahl <manuel.stahl@iis.fraunhofer.de>
 * Copyright (c) 2012 Thorsten Nowak <thorsten.nowak@iis.fraunhofer.de>
 */

#ifndef I2C_ITG3200_H_
#define I2C_ITG3200_H_

#include <linux/iio/iio.h>

/* Register with I2C address (34h) */
#define ITG3200_REG_ADDRESS		0x00

/* Sample rate divider
 * Range: 0 to 255
 * Default value: 0x00 */
#define ITG3200_REG_SAMPLE_RATE_DIV	0x15

/* Digital low pass filter settings */
#define ITG3200_REG_DLPF		0x16
/* DLPF full scale range */
#define ITG3200_DLPF_FS_SEL_2000	0x18
/* Bandwidth (Hz) and internal sample rate
 * (kHz) of DLPF */
#define ITG3200_DLPF_256_8		0x00
#define ITG3200_DLPF_188_1		0x01
#define ITG3200_DLPF_98_1		0x02
#define ITG3200_DLPF_42_1		0x03
#define ITG3200_DLPF_20_1		0x04
#define ITG3200_DLPF_10_1		0x05
#define ITG3200_DLPF_5_1		0x06

#define ITG3200_DLPF_CFG_MASK		0x07

/* Configuration for interrupt operations */
#define ITG3200_REG_IRQ_CONFIG		0x17
/* Logic level */
#define ITG3200_IRQ_ACTIVE_LOW		0x80
#define ITG3200_IRQ_ACTIVE_HIGH		0x00
/* Drive type */
#define ITG3200_IRQ_OPEN_DRAIN		0x40
#define ITG3200_IRQ_PUSH_PULL		0x00
/* Latch mode */
#define ITG3200_IRQ_LATCH_UNTIL_CLEARED	0x20
#define ITG3200_IRQ_LATCH_50US_PULSE	0x00
/* Latch clear method */
#define ITG3200_IRQ_LATCH_CLEAR_ANY	0x10
#define ITG3200_IRQ_LATCH_CLEAR_STATUS	0x00
/* Enable interrupt when device is ready */
#define ITG3200_IRQ_DEVICE_RDY_ENABLE	0x04
/* Enable interrupt when data is available */
#define ITG3200_IRQ_DATA_RDY_ENABLE	0x01

/* Determine the status of ITG-3200 interrupts */
#define ITG3200_REG_IRQ_STATUS		0x1A
/* Status of 'device is ready'-interrupt */
#define ITG3200_IRQ_DEVICE_RDY_STATUS	0x04
/* Status of 'data is available'-interrupt */
#define ITG3200_IRQ_DATA_RDY_STATUS	0x01

/* Sensor registers */
#define ITG3200_REG_TEMP_OUT_H		0x1B
#define ITG3200_REG_TEMP_OUT_L		0x1C
#define ITG3200_REG_GYRO_XOUT_H		0x1D
#define ITG3200_REG_GYRO_XOUT_L		0x1E
#define ITG3200_REG_GYRO_YOUT_H		0x1F
#define ITG3200_REG_GYRO_YOUT_L		0x20
#define ITG3200_REG_GYRO_ZOUT_H		0x21
#define ITG3200_REG_GYRO_ZOUT_L		0x22

/* Power management */
#define ITG3200_REG_POWER_MANAGEMENT	0x3E
/* Reset device and internal registers to the
 * power-up-default settings */
#define ITG3200_RESET			0x80
/* Enable low power sleep mode */
#define ITG3200_SLEEP			0x40
/* Put according gyroscope in standby mode */
#define ITG3200_STANDBY_GYRO_X		0x20
#define ITG3200_STANDBY_GYRO_Y		0x10
#define ITG3200_STANDBY_GYRO_Z		0x08
/* Determine the device clock source */
#define ITG3200_CLK_INTERNAL		0x00
#define ITG3200_CLK_GYRO_X		0x01
#define ITG3200_CLK_GYRO_Y		0x02
#define ITG3200_CLK_GYRO_Z		0x03
#define ITG3200_CLK_EXT_32K		0x04
#define ITG3200_CLK_EXT_19M		0x05


/**
 * struct itg3200 - device instance specific data
 * @i2c:    actual i2c_client
 * @trig:   data ready trigger from itg3200 pin
 **/
struct itg3200 {
	struct i2c_client	*i2c;
	struct iio_trigger	*trig;
	struct iio_mount_matrix orientation;
};

enum ITG3200_SCAN_INDEX {
	ITG3200_SCAN_TEMP,
	ITG3200_SCAN_GYRO_X,
	ITG3200_SCAN_GYRO_Y,
	ITG3200_SCAN_GYRO_Z,
	ITG3200_SCAN_ELEMENTS,
};

int itg3200_write_reg_8(struct iio_dev *indio_dev,
		u8 reg_address, u8 val);

int itg3200_read_reg_8(struct iio_dev *indio_dev,
		u8 reg_address, u8 *val);


#ifdef CONFIG_IIO_BUFFER

void itg3200_remove_trigger(struct iio_dev *indio_dev);
int itg3200_probe_trigger(struct iio_dev *indio_dev);

int itg3200_buffer_configure(struct iio_dev *indio_dev);
void itg3200_buffer_unconfigure(struct iio_dev *indio_dev);

#else /* CONFIG_IIO_BUFFER */

static inline void itg3200_remove_trigger(struct iio_dev *indio_dev)
{
}

static inline int itg3200_probe_trigger(struct iio_dev *indio_dev)
{
	return 0;
}

static inline int itg3200_buffer_configure(struct iio_dev *indio_dev)
{
	return 0;
}

static inline void itg3200_buffer_unconfigure(struct iio_dev *indio_dev)
{
}

#endif  /* CONFIG_IIO_BUFFER */

#endif /* ITG3200_H_ */
