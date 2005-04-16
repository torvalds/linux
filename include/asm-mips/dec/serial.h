/*
 *	include/asm-mips/dec/serial.h
 *
 *	Definitions common to all DECstation serial devices.
 *
 *	Copyright (C) 2004  Maciej W. Rozycki
 *
 *	Based on bits extracted from drivers/tc/zs.h for which
 *	the following copyrights apply:
 *
 *	Copyright (C) 1995  David S. Miller (davem@caip.rutgers.edu)
 *	Copyright (C) 1996  Paul Mackerras (Paul.Mackerras@cs.anu.edu.au)
 *	Copyright (C)       Harald Koerfgen
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#ifndef __ASM_MIPS_DEC_SERIAL_H
#define __ASM_MIPS_DEC_SERIAL_H

struct dec_serial_hook {
	int (*init_channel)(void *handle);
	void (*init_info)(void *handle);
	void (*rx_char)(unsigned char ch, unsigned char fl);
	int (*poll_rx_char)(void *handle);
	int (*poll_tx_char)(void *handle, unsigned char ch);
	unsigned int cflags;
};

extern int register_dec_serial_hook(unsigned int channel,
				    struct dec_serial_hook *hook);
extern int unregister_dec_serial_hook(unsigned int channel);

#endif /* __ASM_MIPS_DEC_SERIAL_H */
