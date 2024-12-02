/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides constants for Renesas RZ/V2M pinctrl bindings.
 *
 * Copyright (C) 2022 Renesas Electronics Corp.
 *
 */

#ifndef __DT_BINDINGS_RZV2M_PINCTRL_H
#define __DT_BINDINGS_RZV2M_PINCTRL_H

#define RZV2M_PINS_PER_PORT	16

/*
 * Create the pin index from its bank and position numbers and store in
 * the upper 16 bits the alternate function identifier
 */
#define RZV2M_PORT_PINMUX(b, p, f)	((b) * RZV2M_PINS_PER_PORT + (p) | ((f) << 16))

/* Convert a port and pin label to its global pin index */
#define RZV2M_GPIO(port, pin)	((port) * RZV2M_PINS_PER_PORT + (pin))

#endif /* __DT_BINDINGS_RZV2M_PINCTRL_H */
