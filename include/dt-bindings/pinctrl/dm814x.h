/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides constants specific to DM814X pinctrl bindings.
 */

#ifndef _DT_BINDINGS_PINCTRL_DM814X_H
#define _DT_BINDINGS_PINCTRL_DM814X_H

#include <dt-bindings/pinctrl/omap.h>

#undef INPUT_EN
#undef PULL_UP
#undef PULL_ENA

/*
 * Note that dm814x silicon revision 2.1 and older require input enabled
 * (bit 18 set) for all 3.3V I/Os to avoid cumulative hardware damage. For
 * more info, see errata advisory 2.1.87. We leave bit 18 out of
 * function-mask in dm814x.h and rely on the bootloader for it.
 */
#define INPUT_EN		(1 << 18)
#define PULL_UP			(1 << 17)
#define PULL_DISABLE		(1 << 16)

/* update macro depending on INPUT_EN and PULL_ENA */
#undef PIN_OUTPUT
#undef PIN_OUTPUT_PULLUP
#undef PIN_OUTPUT_PULLDOWN
#undef PIN_INPUT
#undef PIN_INPUT_PULLUP
#undef PIN_INPUT_PULLDOWN

#define PIN_OUTPUT		(PULL_DISABLE)
#define PIN_OUTPUT_PULLUP	(PULL_UP)
#define PIN_OUTPUT_PULLDOWN	0
#define PIN_INPUT		(INPUT_EN | PULL_DISABLE)
#define PIN_INPUT_PULLUP	(INPUT_EN | PULL_UP)
#define PIN_INPUT_PULLDOWN	(INPUT_EN)

/* undef non-existing modes */
#undef PIN_OFF_NONE
#undef PIN_OFF_OUTPUT_HIGH
#undef PIN_OFF_OUTPUT_LOW
#undef PIN_OFF_INPUT_PULLUP
#undef PIN_OFF_INPUT_PULLDOWN
#undef PIN_OFF_WAKEUPENABLE

#endif

