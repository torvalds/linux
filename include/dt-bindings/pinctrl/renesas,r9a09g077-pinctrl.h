/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides constants for Renesas RZ/T2H family pinctrl bindings.
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 */

#ifndef __DT_BINDINGS_PINCTRL_RENESAS_R9A09G077_PINCTRL_H__
#define __DT_BINDINGS_PINCTRL_RENESAS_R9A09G077_PINCTRL_H__

#define RZT2H_PINS_PER_PORT	8

/*
 * Create the pin index from its bank and position numbers and store in
 * the upper 16 bits the alternate function identifier
 */
#define RZT2H_PORT_PINMUX(b, p, f)	((b) * RZT2H_PINS_PER_PORT + (p) | ((f) << 16))

/* Convert a port and pin label to its global pin index */
#define RZT2H_GPIO(port, pin)	((port) * RZT2H_PINS_PER_PORT + (pin))

#endif /* __DT_BINDINGS_PINCTRL_RENESAS_R9A09G077_PINCTRL_H__ */
