/*
 * AT32 portmux interface.
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_AT32_PORTMUX_H__
#define __ASM_AVR32_AT32_PORTMUX_H__

void portmux_set_func(unsigned int portmux_id, unsigned int pin_id,
		      unsigned int function_id);

#endif /* __ASM_AVR32_AT32_PORTMUX_H__ */
