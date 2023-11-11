/* SPDX-License-Identifier: GPL-2.0+ OR MIT */
/*
 * This header provides constants for Apple pinctrl bindings.
 */

#ifndef _DT_BINDINGS_PINCTRL_APPLE_H
#define _DT_BINDINGS_PINCTRL_APPLE_H

#define APPLE_PINMUX(pin, func) ((pin) | ((func) << 16))
#define APPLE_PIN(pinmux) ((pinmux) & 0xffff)
#define APPLE_FUNC(pinmux) ((pinmux) >> 16)

#endif /* _DT_BINDINGS_PINCTRL_APPLE_H */
