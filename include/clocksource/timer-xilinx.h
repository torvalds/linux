/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2021 Sean Anderson <sean.anderson@seco.com>
 */

#ifndef XILINX_TIMER_H
#define XILINX_TIMER_H

#include <linux/compiler.h>

#define TCSR0	0x00
#define TLR0	0x04
#define TCR0	0x08
#define TCSR1	0x10
#define TLR1	0x14
#define TCR1	0x18

#define TCSR_MDT	BIT(0)
#define TCSR_UDT	BIT(1)
#define TCSR_GENT	BIT(2)
#define TCSR_CAPT	BIT(3)
#define TCSR_ARHT	BIT(4)
#define TCSR_LOAD	BIT(5)
#define TCSR_ENIT	BIT(6)
#define TCSR_ENT	BIT(7)
#define TCSR_TINT	BIT(8)
#define TCSR_PWMA	BIT(9)
#define TCSR_ENALL	BIT(10)
#define TCSR_CASC	BIT(11)

struct clk;
struct device_node;
struct regmap;

/**
 * struct xilinx_timer_priv - Private data for Xilinx AXI timer drivers
 * @map: Regmap of the device, possibly with an offset
 * @clk: Parent clock
 * @max: Maximum value of the counters
 */
struct xilinx_timer_priv {
	struct regmap *map;
	struct clk *clk;
	u32 max;
};

/**
 * xilinx_timer_tlr_cycles() - Calculate the TLR for a period specified
 *                             in clock cycles
 * @priv: The timer's private data
 * @tcsr: The value of the TCSR register for this counter
 * @cycles: The number of cycles in this period
 *
 * Callers of this function MUST ensure that @cycles is representable as
 * a TLR.
 *
 * Return: The calculated value for TLR
 */
u32 xilinx_timer_tlr_cycles(struct xilinx_timer_priv *priv, u32 tcsr,
			    u64 cycles);

/**
 * xilinx_timer_get_period() - Get the current period of a counter
 * @priv: The timer's private data
 * @tlr: The value of TLR for this counter
 * @tcsr: The value of TCSR for this counter
 *
 * Return: The period, in ns
 */
unsigned int xilinx_timer_get_period(struct xilinx_timer_priv *priv,
				     u32 tlr, u32 tcsr);

#endif /* XILINX_TIMER_H */
