/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __UAPI_CORESIGHT_STM_H_
#define __UAPI_CORESIGHT_STM_H_

#include <linux/const.h>

#define STM_FLAG_TIMESTAMPED   _BITUL(3)
#define STM_FLAG_MARKED        _BITUL(4)
#define STM_FLAG_GUARANTEED    _BITUL(7)

/*
 * The CoreSight STM supports guaranteed and invariant timing
 * transactions.  Guaranteed transactions are guaranteed to be
 * traced, this might involve stalling the bus or system to
 * ensure the transaction is accepted by the STM.  While invariant
 * timing transactions are not guaranteed to be traced, they
 * will take an invariant amount of time regardless of the
 * state of the STM.
 */
enum {
	STM_OPTION_GUARANTEED = 0,
	STM_OPTION_INVARIANT,
};

#endif
