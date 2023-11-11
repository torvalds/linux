/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright Intel Corporation (C) 2017. All Rights Reserved
 *
 * Reset binding definitions for Altera Arria10 MAX5 System Resource Chip
 *
 * Adapted from altr,rst-mgr-a10.h
 */

#ifndef _DT_BINDINGS_RESET_ALTR_RST_MGR_A10SR_H
#define _DT_BINDINGS_RESET_ALTR_RST_MGR_A10SR_H

/* Peripheral PHY resets */
#define A10SR_RESET_ENET_HPS	0
#define A10SR_RESET_PCIE	1
#define A10SR_RESET_FILE	2
#define A10SR_RESET_BQSPI	3
#define A10SR_RESET_USB		4

#define A10SR_RESET_NUM		5

#endif
