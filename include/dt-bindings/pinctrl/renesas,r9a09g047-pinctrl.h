/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides constants for Renesas RZ/G3E family pinctrl bindings.
 *
 * Copyright (C) 2024 Renesas Electronics Corp.
 *
 */

#ifndef __DT_BINDINGS_PINCTRL_RENESAS_R9A09G047_PINCTRL_H__
#define __DT_BINDINGS_PINCTRL_RENESAS_R9A09G047_PINCTRL_H__

#include <dt-bindings/pinctrl/rzg2l-pinctrl.h>

/* RZG3E_Px = Offset address of PFC_P_mn  - 0x20 */
#define RZG3E_P0	0
#define RZG3E_P1	1
#define RZG3E_P2	2
#define RZG3E_P3	3
#define RZG3E_P4	4
#define RZG3E_P5	5
#define RZG3E_P6	6
#define RZG3E_P7	7
#define RZG3E_P8	8
#define RZG3E_PA	10
#define RZG3E_PB	11
#define RZG3E_PC	12
#define RZG3E_PD	13
#define RZG3E_PE	14
#define RZG3E_PF	15
#define RZG3E_PG	16
#define RZG3E_PH	17
#define RZG3E_PJ	19
#define RZG3E_PK	20
#define RZG3E_PL	21
#define RZG3E_PM	22
#define RZG3E_PS	28

#define RZG3E_PORT_PINMUX(b, p, f)	RZG2L_PORT_PINMUX(RZG3E_P##b, p, f)
#define RZG3E_GPIO(port, pin)		RZG2L_GPIO(RZG3E_P##port, pin)

#endif /* __DT_BINDINGS_PINCTRL_RENESAS_R9A09G047_PINCTRL_H__ */
