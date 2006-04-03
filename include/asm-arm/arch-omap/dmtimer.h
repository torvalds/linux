/*
 * linux/include/asm-arm/arm/arch-omap/dmtimer.h
 *
 * OMAP Dual-Mode Timers
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Lauri Leukkunen <lauri.leukkunen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_ARCH_TIMER_H
#define __ASM_ARCH_TIMER_H

#include <linux/list.h>

#define OMAP_TIMER_SRC_ARMXOR		0x00
#define OMAP_TIMER_SRC_32_KHZ		0x01
#define OMAP_TIMER_SRC_EXT_CLK		0x02

/* timer control reg bits */
#define OMAP_TIMER_CTRL_CAPTMODE	(1 << 13)
#define OMAP_TIMER_CTRL_PT		(1 << 12)
#define OMAP_TIMER_CTRL_TRG_OVERFLOW	(0x1 << 10)
#define OMAP_TIMER_CTRL_TRG_OFANDMATCH	(0x2 << 10)
#define OMAP_TIMER_CTRL_TCM_LOWTOHIGH	(0x1 << 8)
#define OMAP_TIMER_CTRL_TCM_HIGHTOLOW	(0x2 << 8)
#define OMAP_TIMER_CTRL_TCM_BOTHEDGES	(0x3 << 8)
#define OMAP_TIMER_CTRL_SCPWM		(1 << 7)
#define OMAP_TIMER_CTRL_CE		(1 << 6)	/* compare enable */
#define OMAP_TIMER_CTRL_PRE		(1 << 5)	/* prescaler enable */
#define OMAP_TIMER_CTRL_PTV_SHIFT	2		/* how much to shift the prescaler value */
#define OMAP_TIMER_CTRL_AR		(1 << 1)	/* auto-reload enable */
#define OMAP_TIMER_CTRL_ST		(1 << 0)	/* start timer */

/* timer interrupt enable bits */
#define OMAP_TIMER_INT_CAPTURE		(1 << 2)
#define OMAP_TIMER_INT_OVERFLOW		(1 << 1)
#define OMAP_TIMER_INT_MATCH		(1 << 0)


struct omap_dm_timer {
	struct list_head timer_list;

	u32 base;
	unsigned int irq;
};

u32 omap_dm_timer_read_reg(struct omap_dm_timer *timer, int reg);
void omap_dm_timer_write_reg(struct omap_dm_timer *timer, int reg, u32 value);

struct omap_dm_timer * omap_dm_timer_request(void);
void omap_dm_timer_free(struct omap_dm_timer *timer);
void omap_dm_timer_set_source(struct omap_dm_timer *timer, int source);

void omap_dm_timer_set_int_enable(struct omap_dm_timer *timer, unsigned int value);
void omap_dm_timer_set_trigger(struct omap_dm_timer *timer, unsigned int value);
void omap_dm_timer_enable_compare(struct omap_dm_timer *timer);
void omap_dm_timer_enable_autoreload(struct omap_dm_timer *timer);

void omap_dm_timer_trigger(struct omap_dm_timer *timer);
void omap_dm_timer_start(struct omap_dm_timer *timer);
void omap_dm_timer_stop(struct omap_dm_timer *timer);

void omap_dm_timer_set_load(struct omap_dm_timer *timer, unsigned int load);
void omap_dm_timer_set_match(struct omap_dm_timer *timer, unsigned int match);

unsigned int omap_dm_timer_read_status(struct omap_dm_timer *timer);
void omap_dm_timer_write_status(struct omap_dm_timer *timer, unsigned int value);

unsigned int omap_dm_timer_read_counter(struct omap_dm_timer *timer);
void omap_dm_timer_reset_counter(struct omap_dm_timer *timer);

int omap_dm_timers_active(void);
u32 omap_dm_timer_modify_idlect_mask(u32 inputmask);

#endif /* __ASM_ARCH_TIMER_H */
