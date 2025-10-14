/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) 2025 Renesas Electronics Corporation.
 */

#ifndef _DT_BINDINGS_RENASAS_R9A09G077_PCS_MIIC_H
#define _DT_BINDINGS_RENASAS_R9A09G077_PCS_MIIC_H

/*
 * Media Interface Connection Matrix
 * ===========================================================
 *
 * Selects the function of the Media interface of the MAC to be used
 *
 * SW_MODE[2:0] | Port 0      | Port 1      | Port 2      | Port 3
 * -------------|-------------|-------------|-------------|-------------
 * 000b         | ETHSW Port0 | ETHSW Port1 | ETHSW Port2 | GMAC1
 * 001b         | ESC Port0   | ESC Port1   | GMAC2       | GMAC1
 * 010b         | ESC Port0   | ESC Port1   | ETHSW Port2 | GMAC1
 * 011b         | ESC Port0   | ESC Port1   | ESC Port2   | GMAC1
 * 100b         | ETHSW Port0 | ESC Port1   | ESC Port2   | GMAC1
 * 101b         | ETHSW Port0 | ESC Port1   | ETHSW Port2 | GMAC1
 * 110b         | ETHSW Port0 | ETHSW Port1 | GMAC2       | GMAC1
 * 111b         | GMAC0       | GMAC1       | GMAC2       | -
 */
#define ETHSS_GMAC0_PORT		0
#define ETHSS_GMAC1_PORT		1
#define ETHSS_GMAC2_PORT		2
#define ETHSS_ESC_PORT0			3
#define ETHSS_ESC_PORT1			4
#define ETHSS_ESC_PORT2			5
#define ETHSS_ETHSW_PORT0		6
#define ETHSS_ETHSW_PORT1		7
#define ETHSS_ETHSW_PORT2		8

#endif
