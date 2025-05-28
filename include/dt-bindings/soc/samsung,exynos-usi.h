/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) 2021 Linaro Ltd.
 * Author: Sam Protsenko <semen.protsenko@linaro.org>
 *
 * Device Tree bindings for Samsung Exynos USI (Universal Serial Interface).
 */

#ifndef __DT_BINDINGS_SAMSUNG_EXYNOS_USI_H
#define __DT_BINDINGS_SAMSUNG_EXYNOS_USI_H

#define USI_MODE_NONE		0
#define USI_MODE_UART		1
#define USI_MODE_SPI		2
#define USI_MODE_I2C		3
#define USI_MODE_I2C1		4
#define USI_MODE_I2C0_1		5
#define USI_MODE_UART_I2C1	6

/* Deprecated */
#define USI_V2_NONE		USI_MODE_NONE
#define USI_V2_UART		USI_MODE_UART
#define USI_V2_SPI		USI_MODE_SPI
#define USI_V2_I2C		USI_MODE_I2C

#endif /* __DT_BINDINGS_SAMSUNG_EXYNOS_USI_H */
