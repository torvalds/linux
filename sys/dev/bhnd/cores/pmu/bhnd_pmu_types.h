/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Landon Fuller under sponsorship from
 * the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _BHND_CORES_PMU_BHND_PMU_TYPES_H_
#define _BHND_CORES_PMU_BHND_PMU_TYPES_H_

#include <sys/types.h>

/**
 * bhnd_pmu(4) regulators.
 */ 
typedef enum bhnd_pmu_regulator {
	BHND_REGULATOR_PAREF_LDO	= 0,	/**< PA reference LDO */
} bhnd_pmu_regulator;

/**
 * bhnd_pmu(4) spurious signal avoidance modes.
 */
typedef enum bhnd_pmu_spuravoid {
	BHND_PMU_SPURAVOID_NONE	= 0,	/**< spur avoidance disabled */
	BHND_PMU_SPURAVOID_M1	= 1,	/**< chipset-specific mode 1 */
	BHND_PMU_SPURAVOID_M2	= 2,	/**< chipset-specific mode 2 */
} bhnd_pmu_spuravoid;

#endif /* _BHND_CORES_PMU_BHND_PMU_TYPES_H_ */
