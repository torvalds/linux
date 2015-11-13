/*
 * This header provides constants for DRA pinctrl bindings.
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 * Author: Rajendra Nayak <rnayak@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _DT_BINDINGS_PINCTRL_DRA_H
#define _DT_BINDINGS_PINCTRL_DRA_H

/* DRA7 mux mode options for each pin. See TRM for options */
#define MUX_MODE0	0x0
#define MUX_MODE1	0x1
#define MUX_MODE2	0x2
#define MUX_MODE3	0x3
#define MUX_MODE4	0x4
#define MUX_MODE5	0x5
#define MUX_MODE6	0x6
#define MUX_MODE7	0x7
#define MUX_MODE8	0x8
#define MUX_MODE9	0x9
#define MUX_MODE10	0xa
#define MUX_MODE11	0xb
#define MUX_MODE12	0xc
#define MUX_MODE13	0xd
#define MUX_MODE14	0xe
#define MUX_MODE15	0xf

/* Certain pins need virtual mode, but note: they may glitch */
#define MUX_VIRTUAL_MODE0	(MODE_SELECT | (0x0 << 4))
#define MUX_VIRTUAL_MODE1	(MODE_SELECT | (0x1 << 4))
#define MUX_VIRTUAL_MODE2	(MODE_SELECT | (0x2 << 4))
#define MUX_VIRTUAL_MODE3	(MODE_SELECT | (0x3 << 4))
#define MUX_VIRTUAL_MODE4	(MODE_SELECT | (0x4 << 4))
#define MUX_VIRTUAL_MODE5	(MODE_SELECT | (0x5 << 4))
#define MUX_VIRTUAL_MODE6	(MODE_SELECT | (0x6 << 4))
#define MUX_VIRTUAL_MODE7	(MODE_SELECT | (0x7 << 4))
#define MUX_VIRTUAL_MODE8	(MODE_SELECT | (0x8 << 4))
#define MUX_VIRTUAL_MODE9	(MODE_SELECT | (0x9 << 4))
#define MUX_VIRTUAL_MODE10	(MODE_SELECT | (0xa << 4))
#define MUX_VIRTUAL_MODE11	(MODE_SELECT | (0xb << 4))
#define MUX_VIRTUAL_MODE12	(MODE_SELECT | (0xc << 4))
#define MUX_VIRTUAL_MODE13	(MODE_SELECT | (0xd << 4))
#define MUX_VIRTUAL_MODE14	(MODE_SELECT | (0xe << 4))
#define MUX_VIRTUAL_MODE15	(MODE_SELECT | (0xf << 4))

#define MODE_SELECT		(1 << 8)

#define PULL_ENA		(0 << 16)
#define PULL_DIS		(1 << 16)
#define PULL_UP			(1 << 17)
#define INPUT_EN		(1 << 18)
#define SLEWCONTROL		(1 << 19)
#define WAKEUP_EN		(1 << 24)
#define WAKEUP_EVENT		(1 << 25)

/* Active pin states */
#define PIN_OUTPUT		(0 | PULL_DIS)
#define PIN_OUTPUT_PULLUP	(PULL_UP)
#define PIN_OUTPUT_PULLDOWN	(0)
#define PIN_INPUT		(INPUT_EN | PULL_DIS)
#define PIN_INPUT_SLEW		(INPUT_EN | SLEWCONTROL)
#define PIN_INPUT_PULLUP	(PULL_ENA | INPUT_EN | PULL_UP)
#define PIN_INPUT_PULLDOWN	(PULL_ENA | INPUT_EN)

/*
 * Macro to allow using the absolute physical address instead of the
 * padconf registers instead of the offset from padconf base.
 */
#define DRA7XX_CORE_IOPAD(pa, val)	(((pa) & 0xffff) - 0x3400) (val)

#endif

