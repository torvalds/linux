/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BRCMNAND_PLAT_DATA_H
#define BRCMNAND_PLAT_DATA_H

struct brcmnand_platform_data {
	int	chip_select;
	const char * const *part_probe_types;
	unsigned int ecc_stepsize;
	unsigned int ecc_strength;
};

#endif /* BRCMNAND_PLAT_DATA_H */
