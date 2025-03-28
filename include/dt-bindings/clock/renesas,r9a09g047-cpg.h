/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 */
#ifndef __DT_BINDINGS_CLOCK_RENESAS_R9A09G047_CPG_H__
#define __DT_BINDINGS_CLOCK_RENESAS_R9A09G047_CPG_H__

#include <dt-bindings/clock/renesas-cpg-mssr.h>

/* Core Clock list */
#define R9A09G047_SYS_0_PCLK			0
#define R9A09G047_CA55_0_CORECLK0		1
#define R9A09G047_CA55_0_CORECLK1		2
#define R9A09G047_CA55_0_CORECLK2		3
#define R9A09G047_CA55_0_CORECLK3		4
#define R9A09G047_CA55_0_PERIPHCLK		5
#define R9A09G047_CM33_CLK0			6
#define R9A09G047_CST_0_SWCLKTCK		7
#define R9A09G047_IOTOP_0_SHCLK			8

#endif /* __DT_BINDINGS_CLOCK_RENESAS_R9A09G047_CPG_H__ */
