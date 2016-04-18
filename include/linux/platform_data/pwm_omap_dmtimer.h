/*
 * include/linux/platform_data/pwm_omap_dmtimer.h
 *
 * OMAP Dual-Mode Timer PWM platform data
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 * Tarun Kanti DebBarma <tarun.kanti@ti.com>
 * Thara Gopinath <thara@ti.com>
 *
 * Platform device conversion and hwmod support.
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Lauri Leukkunen <lauri.leukkunen@nokia.com>
 * PWM and clock framework support by Timo Teras.
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

#ifndef __PWM_OMAP_DMTIMER_PDATA_H
#define __PWM_OMAP_DMTIMER_PDATA_H

/* trigger types */
#define PWM_OMAP_DMTIMER_TRIGGER_NONE			0x00
#define PWM_OMAP_DMTIMER_TRIGGER_OVERFLOW		0x01
#define PWM_OMAP_DMTIMER_TRIGGER_OVERFLOW_AND_COMPARE	0x02

struct omap_dm_timer;
typedef struct omap_dm_timer pwm_omap_dmtimer;

struct pwm_omap_dmtimer_pdata {
	pwm_omap_dmtimer *(*request_by_node)(struct device_node *np);
	int	(*free)(pwm_omap_dmtimer *timer);

	void	(*enable)(pwm_omap_dmtimer *timer);
	void	(*disable)(pwm_omap_dmtimer *timer);

	struct clk *(*get_fclk)(pwm_omap_dmtimer *timer);

	int	(*start)(pwm_omap_dmtimer *timer);
	int	(*stop)(pwm_omap_dmtimer *timer);

	int	(*set_load)(pwm_omap_dmtimer *timer, int autoreload,
			unsigned int value);
	int	(*set_match)(pwm_omap_dmtimer *timer, int enable,
			unsigned int match);
	int	(*set_pwm)(pwm_omap_dmtimer *timer, int def_on,
			int toggle, int trigger);
	int	(*set_prescaler)(pwm_omap_dmtimer *timer, int prescaler);

	int	(*write_counter)(pwm_omap_dmtimer *timer, unsigned int value);
};

#endif /* __PWM_OMAP_DMTIMER_PDATA_H */
