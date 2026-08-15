/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * This header provides constants for Renesas RZ/G3L family pinctrl bindings.
 *
 * Copyright (C) 2026 Renesas Electronics Corp.
 *
 */

#ifndef __DT_BINDINGS_PINCTRL_RENESAS_R9A08G046_PINCTRL_H__
#define __DT_BINDINGS_PINCTRL_RENESAS_R9A08G046_PINCTRL_H__

#include <dt-bindings/pinctrl/rzg2l-pinctrl.h>

/* RZG3L_Px = Offset address of PFC_P_mn  - 0x22 */
#define RZG3L_P2	2
#define RZG3L_P3	3
#define RZG3L_P5	5
#define RZG3L_P6	6
#define RZG3L_P7	7
#define RZG3L_P8	8
#define RZG3L_PA	10
#define RZG3L_PB	11
#define RZG3L_PC	12
#define RZG3L_PD	13
#define RZG3L_PE	14
#define RZG3L_PF	15
#define RZG3L_PG	16
#define RZG3L_PH	17
#define RZG3L_PJ	19
#define RZG3L_PK	20
#define RZG3L_PL	21
#define RZG3L_PM	22
#define RZG3L_PS	28

#define RZG3L_PORT_PINMUX(b, p, f)	RZG2L_PORT_PINMUX(RZG3L_P##b, p, f)
#define RZG3L_GPIO(port, pin)		RZG2L_GPIO(RZG3L_P##port, pin)

#endif /* __DT_BINDINGS_PINCTRL_RENESAS_R9A08G046_PINCTRL_H__ */
