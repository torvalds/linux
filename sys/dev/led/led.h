/*-
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD$
 */

#ifndef _DEV_LED_H
#define _DEV_LED_H

typedef	void led_t(void *, int);

struct cdev *led_create_state(led_t *, void *, char const *, int);
struct cdev *led_create(led_t *, void *, char const *);
void	led_destroy(struct cdev *);

int	led_set(char const *, char const *);

#endif
