/*-
 * Copyright (c) 2017 Adrian Chadd <adrian@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#include "opt_ah.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/pcpu.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/conf.h>

/*
 * This implements the "old" style ath(4) module behaviour, which loaded the
 * driver, HAL and PCI glue.
 */

static int
ath_modevent(module_t mod __unused, int type, void *data __unused)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		printf("[ath] loaded\n");
		break;

	case MOD_UNLOAD:
		printf("[ath] unloaded\n");
		break;

	case MOD_SHUTDOWN:
		break;

	default:
		error = EOPNOTSUPP;
		break;

	}
	return (error);
}

DEV_MODULE(if_ath, ath_modevent, NULL);

MODULE_VERSION(if_ath, 1);
MODULE_DEPEND(if_ath, ath_hal, 1, 1, 1);
MODULE_DEPEND(if_ath, ath_hal_ar5210, 1, 1, 1);
MODULE_DEPEND(if_ath, ath_hal_ar5211, 1, 1, 1);
MODULE_DEPEND(if_ath, ath_hal_ar5212, 1, 1, 1);
MODULE_DEPEND(if_ath, ath_hal_ar5416, 1, 1, 1);
MODULE_DEPEND(if_ath, ath_hal_ar9300, 1, 1, 1);
MODULE_DEPEND(if_ath, ath_rate, 1, 1, 1);
MODULE_DEPEND(if_ath, ath_dfs, 1, 1, 1);
MODULE_DEPEND(if_ath, wlan, 1, 1, 1);
MODULE_DEPEND(if_ath, ath_main, 1, 1, 1);
MODULE_DEPEND(if_ath, if_ath_pci, 1, 1, 1);
