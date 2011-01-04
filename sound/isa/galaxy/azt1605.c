/*
 * Aztech AZT1605 Driver
 * Copyright (C) 2007,2010  Rene Herman
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define AZT1605

#define CRD_NAME "Aztech AZT1605"
#define DRV_NAME "AZT1605"
#define DEV_NAME "azt1605"

#define GALAXY_DSP_MAJOR		2
#define GALAXY_DSP_MINOR		1

#define GALAXY_CONFIG_SIZE		3

/*
 * 24-bit config register
 */

#define GALAXY_CONFIG_SBA_220		(0 << 0)
#define GALAXY_CONFIG_SBA_240		(1 << 0)
#define GALAXY_CONFIG_SBA_260		(2 << 0)
#define GALAXY_CONFIG_SBA_280		(3 << 0)
#define GALAXY_CONFIG_SBA_MASK		GALAXY_CONFIG_SBA_280

#define GALAXY_CONFIG_MPUA_300		(0 << 2)
#define GALAXY_CONFIG_MPUA_330		(1 << 2)

#define GALAXY_CONFIG_MPU_ENABLE	(1 << 3)

#define GALAXY_CONFIG_GAME_ENABLE	(1 << 4)

#define GALAXY_CONFIG_CD_PANASONIC	(1 << 5)
#define GALAXY_CONFIG_CD_MITSUMI	(1 << 6)
#define GALAXY_CONFIG_CD_MASK		(\
	GALAXY_CONFIG_CD_PANASONIC | GALAXY_CONFIG_CD_MITSUMI)

#define GALAXY_CONFIG_UNUSED		(1 << 7)
#define GALAXY_CONFIG_UNUSED_MASK	GALAXY_CONFIG_UNUSED

#define GALAXY_CONFIG_SBIRQ_2		(1 << 8)
#define GALAXY_CONFIG_SBIRQ_3		(1 << 9)
#define GALAXY_CONFIG_SBIRQ_5		(1 << 10)
#define GALAXY_CONFIG_SBIRQ_7		(1 << 11)

#define GALAXY_CONFIG_MPUIRQ_2		(1 << 12)
#define GALAXY_CONFIG_MPUIRQ_3		(1 << 13)
#define GALAXY_CONFIG_MPUIRQ_5		(1 << 14)
#define GALAXY_CONFIG_MPUIRQ_7		(1 << 15)

#define GALAXY_CONFIG_WSSA_530		(0 << 16)
#define GALAXY_CONFIG_WSSA_604		(1 << 16)
#define GALAXY_CONFIG_WSSA_E80		(2 << 16)
#define GALAXY_CONFIG_WSSA_F40		(3 << 16)

#define GALAXY_CONFIG_WSS_ENABLE	(1 << 18)

#define GALAXY_CONFIG_CDIRQ_11		(1 << 19)
#define GALAXY_CONFIG_CDIRQ_12		(1 << 20)
#define GALAXY_CONFIG_CDIRQ_15		(1 << 21)
#define GALAXY_CONFIG_CDIRQ_MASK	(\
	GALAXY_CONFIG_CDIRQ_11 | GALAXY_CONFIG_CDIRQ_12 |\
	GALAXY_CONFIG_CDIRQ_15)

#define GALAXY_CONFIG_CDDMA_DISABLE	(0 << 22)
#define GALAXY_CONFIG_CDDMA_0		(1 << 22)
#define GALAXY_CONFIG_CDDMA_1		(2 << 22)
#define GALAXY_CONFIG_CDDMA_3		(3 << 22)
#define GALAXY_CONFIG_CDDMA_MASK	GALAXY_CONFIG_CDDMA_3

#define GALAXY_CONFIG_MASK		(\
	GALAXY_CONFIG_SBA_MASK | GALAXY_CONFIG_CD_MASK |\
	GALAXY_CONFIG_UNUSED_MASK | GALAXY_CONFIG_CDIRQ_MASK |\
	GALAXY_CONFIG_CDDMA_MASK)

#include "galaxy.c"
