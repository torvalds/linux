/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2018 Andrew Turner
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "opt_platform.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/pcpu.h>
#include <sys/systm.h>

#include <machine/cpu.h>

#include <dev/psci/smccc.h>

typedef void (cpu_quirk_install)(void);
struct cpu_quirks {
	cpu_quirk_install *quirk_install;
	u_int		midr_mask;
	u_int		midr_value;
};

static enum {
	SSBD_FORCE_ON,
	SSBD_FORCE_OFF,
	SSBD_KERNEL,
} ssbd_method = SSBD_KERNEL;

static cpu_quirk_install install_psci_bp_hardening;
static cpu_quirk_install install_ssbd_workaround;

static struct cpu_quirks cpu_quirks[] = {
	{
		.midr_mask = CPU_IMPL_MASK | CPU_PART_MASK,
		.midr_value = CPU_ID_RAW(CPU_IMPL_ARM, CPU_PART_CORTEX_A57,0,0),
		.quirk_install = install_psci_bp_hardening,
	},
	{
		.midr_mask = CPU_IMPL_MASK | CPU_PART_MASK,
		.midr_value = CPU_ID_RAW(CPU_IMPL_ARM, CPU_PART_CORTEX_A72,0,0),
		.quirk_install = install_psci_bp_hardening,
	},
	{
		.midr_mask = CPU_IMPL_MASK | CPU_PART_MASK,
		.midr_value = CPU_ID_RAW(CPU_IMPL_ARM, CPU_PART_CORTEX_A73,0,0),
		.quirk_install = install_psci_bp_hardening,
	},
	{
		.midr_mask = CPU_IMPL_MASK | CPU_PART_MASK,
		.midr_value = CPU_ID_RAW(CPU_IMPL_ARM, CPU_PART_CORTEX_A75,0,0),
		.quirk_install = install_psci_bp_hardening,
	},
	{
		.midr_mask = CPU_IMPL_MASK | CPU_PART_MASK,
		.midr_value =
		    CPU_ID_RAW(CPU_IMPL_CAVIUM, CPU_PART_THUNDERX2, 0,0),
		.quirk_install = install_psci_bp_hardening,
	},
	{
		.midr_mask = 0,
		.midr_value = 0,
		.quirk_install = install_ssbd_workaround,
	},
};

static void
install_psci_bp_hardening(void)
{

	if (smccc_arch_features(SMCCC_ARCH_WORKAROUND_1) != SMCCC_RET_SUCCESS)
		return;

	PCPU_SET(bp_harden, smccc_arch_workaround_1);
}

static void
install_ssbd_workaround(void)
{
	char *env;

	if (PCPU_GET(cpuid) == 0) {
		env = kern_getenv("kern.cfg.ssbd");
		if (env != NULL) {
			if (strcmp(env, "force-on") == 0) {
				ssbd_method = SSBD_FORCE_ON;
			} else if (strcmp(env, "force-off") == 0) {
				ssbd_method = SSBD_FORCE_OFF;
			}
		}
	}

	/* Enable the workaround on this CPU if it's enabled in the firmware */
	if (smccc_arch_features(SMCCC_ARCH_WORKAROUND_2) != SMCCC_RET_SUCCESS)
		return;

	switch(ssbd_method) {
	case SSBD_FORCE_ON:
		smccc_arch_workaround_2(1);
		break;
	case SSBD_FORCE_OFF:
		smccc_arch_workaround_2(0);
		break;
	case SSBD_KERNEL:
	default:
		PCPU_SET(ssbd, smccc_arch_workaround_2);
		break;
	}
}

void
install_cpu_errata(void)
{
	u_int midr;
	size_t i;

	midr = get_midr();

	for (i = 0; i < nitems(cpu_quirks); i++) {
		if ((midr & cpu_quirks[i].midr_mask) ==
		    cpu_quirks[i].midr_value) {
			cpu_quirks[i].quirk_install();
		}
	}
}
