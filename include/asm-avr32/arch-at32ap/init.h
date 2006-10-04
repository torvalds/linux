/*
 * AT32AP platform initialization calls.
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_AT32AP_INIT_H__
#define __ASM_AVR32_AT32AP_INIT_H__

void setup_platform(void);
void setup_board(void);

/* Called by setup_platform */
void at32_clock_init(void);
void at32_portmux_init(void);

void at32_setup_serial_console(unsigned int usart_id);

#endif /* __ASM_AVR32_AT32AP_INIT_H__ */
