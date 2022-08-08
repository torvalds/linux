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

struct pmbus_platform_data {
	u32 flags;		/* Device specific flags */

	/* regulator support */
	int num_regulators;
	struct regulator_init_data *reg_init_data;
};

#endif /* _PMBUS_H_ */
