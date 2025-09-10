/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor ipc mediation function definitions.
 *
 * Copyright 2023 Canonical Ltd.
 */

#ifndef __AA_SIGNAL_H
#define __AA_SIGNAL_H

#define SIGUNKNOWN 0
#define MAXMAPPED_SIG 35

#define MAXMAPPED_SIGNAME (MAXMAPPED_SIG + 1)
#define SIGRT_BASE 128

#endif /* __AA_SIGNAL_H */
