/*
 * Support for viafb GPIO ports.
 *
 * Copyright 2009 Jonathan Corbet <corbet@lwn.net>
 * Distributable under version 2 of the GNU General Public License.
 */

#ifndef __VIA_GPIO_H__
#define __VIA_GPIO_H__

extern int viafb_gpio_lookup(const char *name);
extern int viafb_gpio_init(void);
extern void viafb_gpio_exit(void);
#endif
