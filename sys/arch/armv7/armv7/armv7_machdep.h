/*	$OpenBSD: armv7_machdep.h,v 1.14 2021/05/16 03:39:28 jsg Exp $	*/
/*
 * Copyright (c) 2013 Sylvestre Gallon <ccna.syl@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __PLATFORMVAR_H__
#define __PLATFORMVAR_H__

void platform_init(void);
void platform_powerdown(void);
void platform_watchdog_reset(void);
void platform_init_cons(void);
void platform_init_mainbus(struct device *);
struct board_dev *platform_board_devs(void);
extern void (*cpuresetfn)(void);
extern void (*powerdownfn)(void);

struct armv7_platform {
	struct board_dev *devs;
	void (*board_init)(void);
	void (*smc_write)(bus_space_tag_t, bus_space_handle_t, bus_size_t,
	    uint32_t, uint32_t);
	void (*init_cons)(void);
	void (*watchdog_reset)(void);
	void (*powerdown)(void);
	void (*init_mainbus)(struct device *);
};

#endif /* __PLATFORMVAR_H__ */
