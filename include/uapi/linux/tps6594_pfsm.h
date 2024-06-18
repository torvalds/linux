/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Userspace ABI for TPS6594 PMIC Pre-configurable Finite State Machine
 *
 * Copyright (C) 2023 BayLibre Incorporated - https://www.baylibre.com/
 */

#ifndef __TPS6594_PFSM_H
#define __TPS6594_PFSM_H

#include <linux/const.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * struct pmic_state_opt - PMIC state options
 * @gpio_retention: if enabled, power rails associated with GPIO retention remain active
 * @ddr_retention: if enabled, power rails associated with DDR retention remain active
 * @mcu_only_startup_dest: if enabled, startup destination state is MCU_ONLY
 */
struct pmic_state_opt {
	__u8 gpio_retention;
	__u8 ddr_retention;
	__u8 mcu_only_startup_dest;
};

/* Commands */
#define PMIC_BASE			'P'

#define PMIC_GOTO_STANDBY		_IO(PMIC_BASE, 0)
#define PMIC_GOTO_LP_STANDBY		_IO(PMIC_BASE, 1)
#define PMIC_UPDATE_PGM			_IO(PMIC_BASE, 2)
#define PMIC_SET_ACTIVE_STATE		_IO(PMIC_BASE, 3)
#define PMIC_SET_MCU_ONLY_STATE		_IOW(PMIC_BASE, 4, struct pmic_state_opt)
#define PMIC_SET_RETENTION_STATE	_IOW(PMIC_BASE, 5, struct pmic_state_opt)

#endif /*  __TPS6594_PFSM_H */
