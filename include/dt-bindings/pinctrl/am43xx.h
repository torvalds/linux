/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This header provides constants specific to AM43XX pinctrl bindings.
 */

#ifndef _DT_BINDINGS_PINCTRL_AM43XX_H
#define _DT_BINDINGS_PINCTRL_AM43XX_H

#define MUX_MODE0	0
#define MUX_MODE1	1
#define MUX_MODE2	2
#define MUX_MODE3	3
#define MUX_MODE4	4
#define MUX_MODE5	5
#define MUX_MODE6	6
#define MUX_MODE7	7
#define MUX_MODE8	8
#define MUX_MODE9	9

#define PULL_DISABLE		(1 << 16)
#define PULL_UP			(1 << 17)
#define INPUT_EN		(1 << 18)
#define SLEWCTRL_SLOW		(1 << 19)
#define SLEWCTRL_FAST		0
#define DS0_FORCE_OFF_MODE	(1 << 24)
#define DS0_INPUT		(1 << 25)
#define DS0_FORCE_OUT_HIGH	(1 << 26)
#define DS0_PULL_UP_DOWN_EN	(1 << 27)
#define DS0_PULL_UP_SEL		(1 << 28)
#define WAKEUP_ENABLE		(1 << 29)

#define DS0_PIN_OUTPUT		(DS0_FORCE_OFF_MODE)
#define DS0_PIN_OUTPUT_HIGH	(DS0_FORCE_OFF_MODE | DS0_FORCE_OUT_HIGH)
#define DS0_PIN_OUTPUT_PULLUP	(DS0_FORCE_OFF_MODE | DS0_PULL_UP_DOWN_EN | DS0_PULL_UP_SEL)
#define DS0_PIN_OUTPUT_PULLDOWN	(DS0_FORCE_OFF_MODE | DS0_PULL_UP_DOWN_EN)
#define DS0_PIN_INPUT		(DS0_FORCE_OFF_MODE | DS0_INPUT)
#define DS0_PIN_INPUT_PULLUP	(DS0_FORCE_OFF_MODE | DS0_INPUT | DS0_PULL_UP_DOWN_EN | DS0_PULL_UP_SEL)
#define DS0_PIN_INPUT_PULLDOWN	(DS0_FORCE_OFF_MODE | DS0_INPUT | DS0_PULL_UP_DOWN_EN)

#define PIN_OUTPUT		(PULL_DISABLE)
#define PIN_OUTPUT_PULLUP	(PULL_UP)
#define PIN_OUTPUT_PULLDOWN	0
#define PIN_INPUT		(INPUT_EN | PULL_DISABLE)
#define PIN_INPUT_PULLUP	(INPUT_EN | PULL_UP)
#define PIN_INPUT_PULLDOWN	(INPUT_EN)

/*
 * Macro to allow using the absolute physical address instead of the
 * padconf registers instead of the offset from padconf base.
 */
#define AM4372_IOPAD(pa, val)	(((pa) & 0xffff) - 0x0800) (val)

#endif

