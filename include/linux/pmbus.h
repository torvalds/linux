/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Hardware monitoring driver for PMBus devices
 *
 * Copyright (c) 2010, 2011 Ericsson AB.
 */

#ifndef _PMBUS_H_
#define _PMBUS_H_

#include <linux/bits.h>

/* flags */

/*
 * PMBUS_SKIP_STATUS_CHECK
 *
 * During register detection, skip checking the status register for
 * communication or command errors.
 *
 * Some PMBus chips respond with valid data when trying to read an unsupported
 * register. For such chips, checking the status register is mandatory when
 * trying to determine if a chip register exists or not.
 * Other PMBus chips don't support the STATUS_CML register, or report
 * communication errors for no explicable reason. For such chips, checking
 * the status register must be disabled.
 */
#define PMBUS_SKIP_STATUS_CHECK	BIT(0)

/*
 * PMBUS_WRITE_PROTECTED
 * Set if the chip is write protected and write protection is not determined
 * by the standard WRITE_PROTECT command.
 */
#define PMBUS_WRITE_PROTECTED	BIT(1)

/*
 * PMBUS_NO_CAPABILITY
 *
 * Some PMBus chips don't respond with valid data when reading the CAPABILITY
 * register. For such chips, this flag should be set so that the PMBus core
 * driver doesn't use CAPABILITY to determine it's behavior.
 */
#define PMBUS_NO_CAPABILITY			BIT(2)

/*
 * PMBUS_READ_STATUS_AFTER_FAILED_CHECK
 *
 * Some PMBus chips end up in an undefined state when trying to read an
 * unsupported register. For such chips, it is necessary to reset the
 * chip pmbus controller to a known state after a failed register check.
 * This can be done by reading a known register. By setting this flag the
 * driver will try to read the STATUS register after each failed
 * register check. This read may fail, but it will put the chip in a
 * known state.
 */
#define PMBUS_READ_STATUS_AFTER_FAILED_CHECK	BIT(3)

/*
 * PMBUS_NO_WRITE_PROTECT
 *
 * Some PMBus chips respond with invalid data when reading the WRITE_PROTECT
 * register. For such chips, this flag should be set so that the PMBus core
 * driver doesn't use the WRITE_PROTECT command to determine its behavior.
 */
#define PMBUS_NO_WRITE_PROTECT			BIT(4)

/*
 * PMBUS_USE_COEFFICIENTS_CMD
 *
 * When this flag is set the PMBus core driver will use the COEFFICIENTS
 * register to initialize the coefficients for the direct mode format.
 */
#define PMBUS_USE_COEFFICIENTS_CMD		BIT(5)

/*
 * PMBUS_OP_PROTECTED
 * Set if the chip OPERATION command is protected and protection is not
 * determined by the standard WRITE_PROTECT command.
 */
#define PMBUS_OP_PROTECTED			BIT(6)

/*
 * PMBUS_VOUT_PROTECTED
 * Set if the chip VOUT_COMMAND command is protected and protection is not
 * determined by the standard WRITE_PROTECT command.
 */
#define PMBUS_VOUT_PROTECTED			BIT(7)

struct pmbus_platform_data {
	u32 flags;		/* Device specific flags */

	/* regulator support */
	int num_regulators;
	struct regulator_init_data *reg_init_data;
};

#endif /* _PMBUS_H_ */
