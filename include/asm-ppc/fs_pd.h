/*
 * Platform information definitions.
 *
 * 2006 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef FS_PD_H
#define FS_PD_H

static inline int uart_baudrate(void)
{
	int baud;
	bd_t *bd = (bd_t *) __res;

	if (bd->bi_baudrate)
		baud = bd->bi_baudrate;
	else
		baud = -1;
	return baud;
}

static inline int uart_clock(void)
{
	return (((bd_t *) __res)->bi_intfreq);
}

#define cpm2_map(member)	(&cpm2_immr->member)
#define cpm2_map_size(member, size)	(&cpm2_immr->member)
#define cpm2_unmap(addr)        do {} while(0)

#endif
