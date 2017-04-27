/*
 * Defines macros and constants for Renesas RZ/A1 pin controller pin
 * muxing functions.
 */
#ifndef __DT_BINDINGS_PINCTRL_RENESAS_RZA1_H
#define __DT_BINDINGS_PINCTRL_RENESAS_RZA1_H

#define RZA1_PINS_PER_PORT	16

/*
 * Create the pin index from its bank and position numbers and store in
 * the upper 16 bits the alternate function identifier
 */
#define RZA1_PINMUX(b, p, f)	((b) * RZA1_PINS_PER_PORT + (p) | (f << 16))

#endif /* __DT_BINDINGS_PINCTRL_RENESAS_RZA1_H */
