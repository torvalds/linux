/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Liebherr-Electronics and Drives GmbH
 */
#ifndef _MC33XS2410_H
#define _MC33XS2410_H

#include <linux/spi/spi.h>

MODULE_IMPORT_NS("PWM_MC33XS2410");

int mc33xs2410_read_reg_ctrl(struct spi_device *spi, u8 reg, u16 *val);
int mc33xs2410_read_reg_diag(struct spi_device *spi, u8 reg, u16 *val);
int mc33xs2410_modify_reg(struct spi_device *spi, u8 reg, u8 mask, u8 val);

#endif /* _MC33XS2410_H */
