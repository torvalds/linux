/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (C) 2022 Schneider-Electric
 *
 * Clément Léger <clement.leger@bootlin.com>
 */

#ifndef _DT_BINDINGS_PCS_RZN1_MIIC
#define _DT_BINDINGS_PCS_RZN1_MIIC

/*
 * Reefer to the datasheet [1] section 8.2.1, Internal Connection of Ethernet
 * Ports to check the available combination
 *
 * [1] REN_r01uh0750ej0140-rzn1-introduction_MAT_20210228.pdf
 */

#define MIIC_GMAC1_PORT			0
#define MIIC_GMAC2_PORT			1
#define MIIC_RTOS_PORT			2
#define MIIC_SERCOS_PORTA		3
#define MIIC_SERCOS_PORTB		4
#define MIIC_ETHERCAT_PORTA		5
#define MIIC_ETHERCAT_PORTB		6
#define MIIC_ETHERCAT_PORTC		7
#define MIIC_SWITCH_PORTA		8
#define MIIC_SWITCH_PORTB		9
#define MIIC_SWITCH_PORTC		10
#define MIIC_SWITCH_PORTD		11
#define MIIC_HSR_PORTA			12
#define MIIC_HSR_PORTB			13

#endif
