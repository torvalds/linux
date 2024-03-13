/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * This header provides constants for binding aspeed,*-gpio.
 *
 * The first cell in Aspeed's GPIO specifier is the GPIO ID. The macros below
 * provide names for this.
 *
 * The second cell contains standard flag values specified in gpio.h.
 */

#ifndef _DT_BINDINGS_GPIO_ASPEED_GPIO_H
#define _DT_BINDINGS_GPIO_ASPEED_GPIO_H

#include <dt-bindings/gpio/gpio.h>

#define ASPEED_GPIO_PORT_A 0
#define ASPEED_GPIO_PORT_B 1
#define ASPEED_GPIO_PORT_C 2
#define ASPEED_GPIO_PORT_D 3
#define ASPEED_GPIO_PORT_E 4
#define ASPEED_GPIO_PORT_F 5
#define ASPEED_GPIO_PORT_G 6
#define ASPEED_GPIO_PORT_H 7
#define ASPEED_GPIO_PORT_I 8
#define ASPEED_GPIO_PORT_J 9
#define ASPEED_GPIO_PORT_K 10
#define ASPEED_GPIO_PORT_L 11
#define ASPEED_GPIO_PORT_M 12
#define ASPEED_GPIO_PORT_N 13
#define ASPEED_GPIO_PORT_O 14
#define ASPEED_GPIO_PORT_P 15
#define ASPEED_GPIO_PORT_Q 16
#define ASPEED_GPIO_PORT_R 17
#define ASPEED_GPIO_PORT_S 18
#define ASPEED_GPIO_PORT_T 19
#define ASPEED_GPIO_PORT_U 20
#define ASPEED_GPIO_PORT_V 21
#define ASPEED_GPIO_PORT_W 22
#define ASPEED_GPIO_PORT_X 23
#define ASPEED_GPIO_PORT_Y 24
#define ASPEED_GPIO_PORT_Z 25
#define ASPEED_GPIO_PORT_AA 26
#define ASPEED_GPIO_PORT_AB 27
#define ASPEED_GPIO_PORT_AC 28

#define ASPEED_GPIO(port, offset) \
	((ASPEED_GPIO_PORT_##port * 8) + offset)

#endif
