/*
 * This header provides constants specific to AM33XX pinctrl bindings.
 */

#ifndef _DT_BINDINGS_PINCTRL_AM33XX_H
#define _DT_BINDINGS_PINCTRL_AM33XX_H

#include <include/dt-bindings/pinctrl/omap.h>

/* am33xx specific mux bit defines */
#undef PULL_ENA
#undef INPUT_EN

#define PULL_DISABLE		(1 << 3)
#define INPUT_EN		(1 << 5)
#define SLEWCTRL_FAST		(1 << 6)

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

