/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file discribe the STM32 DFSDM IIO driver API for audio part
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author(s): Arnaud Pouliquen <arnaud.pouliquen@st.com>.
 */

#ifndef STM32_DFSDM_ADC_H
#define STM32_DFSDM_ADC_H

#include <linux/iio/iio.h>

int stm32_dfsdm_get_buff_cb(struct iio_dev *iio_dev,
			    int (*cb)(const void *data, size_t size,
				      void *private),
			    void *private);
int stm32_dfsdm_release_buff_cb(struct iio_dev *iio_dev);

#endif
