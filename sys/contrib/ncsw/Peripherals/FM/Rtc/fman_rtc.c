/*
 * Copyright 2008-2013 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "fsl_fman_rtc.h"

void fman_rtc_defconfig(struct rtc_cfg *cfg)
{
	int i;
	cfg->src_clk = DEFAULT_SRC_CLOCK;
	cfg->invert_input_clk_phase = DEFAULT_INVERT_INPUT_CLK_PHASE;
	cfg->invert_output_clk_phase = DEFAULT_INVERT_OUTPUT_CLK_PHASE;
	cfg->pulse_realign = DEFAULT_PULSE_REALIGN;
	for (i = 0; i < FMAN_RTC_MAX_NUM_OF_ALARMS; i++)
		cfg->alarm_polarity[i] = DEFAULT_ALARM_POLARITY;
	for (i = 0; i < FMAN_RTC_MAX_NUM_OF_EXT_TRIGGERS; i++)
		cfg->trigger_polarity[i] = DEFAULT_TRIGGER_POLARITY;
}

uint32_t fman_rtc_get_events(struct rtc_regs *regs)
{
	return ioread32be(&regs->tmr_tevent);
}

uint32_t fman_rtc_get_event(struct rtc_regs *regs, uint32_t ev_mask)
{
	return ioread32be(&regs->tmr_tevent) & ev_mask;
}

uint32_t fman_rtc_get_interrupt_mask(struct rtc_regs *regs)
{
	return ioread32be(&regs->tmr_temask);
}

void fman_rtc_set_interrupt_mask(struct rtc_regs *regs, uint32_t mask)
{
	iowrite32be(mask, &regs->tmr_temask);
}

void fman_rtc_ack_event(struct rtc_regs *regs, uint32_t events)
{
	iowrite32be(events, &regs->tmr_tevent);
}

uint32_t fman_rtc_check_and_clear_event(struct rtc_regs *regs)
{
	uint32_t event;

	event = ioread32be(&regs->tmr_tevent);
	event &= ioread32be(&regs->tmr_temask);

	if (event)
		iowrite32be(event, &regs->tmr_tevent);
	return event;
}

uint32_t fman_rtc_get_frequency_compensation(struct rtc_regs *regs)
{
	return ioread32be(&regs->tmr_add);
}

void fman_rtc_set_frequency_compensation(struct rtc_regs *regs, uint32_t val)
{
	iowrite32be(val, &regs->tmr_add);
}

void fman_rtc_enable_interupt(struct rtc_regs *regs, uint32_t events)
{
	fman_rtc_set_interrupt_mask(regs, fman_rtc_get_interrupt_mask(regs) | events);
}

void fman_rtc_disable_interupt(struct rtc_regs *regs, uint32_t events)
{
	fman_rtc_set_interrupt_mask(regs, fman_rtc_get_interrupt_mask(regs) & ~events);
}

void fman_rtc_set_timer_alarm_l(struct rtc_regs *regs, int index, uint32_t val)
{
	iowrite32be(val, &regs->tmr_alarm[index].tmr_alarm_l);
}

void fman_rtc_set_timer_fiper(struct rtc_regs *regs, int index, uint32_t val)
{
	iowrite32be(val, &regs->tmr_fiper[index]);
}

void fman_rtc_set_timer_alarm(struct rtc_regs *regs, int index, int64_t val)
{
	iowrite32be((uint32_t)val, &regs->tmr_alarm[index].tmr_alarm_l);
	iowrite32be((uint32_t)(val >> 32), &regs->tmr_alarm[index].tmr_alarm_h);
}

void fman_rtc_set_timer_offset(struct rtc_regs *regs, int64_t val)
{
	iowrite32be((uint32_t)val, &regs->tmr_off_l);
	iowrite32be((uint32_t)(val >> 32), &regs->tmr_off_h);
}

uint64_t fman_rtc_get_trigger_stamp(struct rtc_regs *regs, int id)
{
	uint64_t time;
	/* TMR_CNT_L must be read first to get an accurate value */
	time = (uint64_t)ioread32be(&regs->tmr_etts[id].tmr_etts_l);
	time |= ((uint64_t)ioread32be(&regs->tmr_etts[id].tmr_etts_h)
		<< 32);

	return time;
}

uint32_t fman_rtc_get_timer_ctrl(struct rtc_regs *regs)
{
	return ioread32be(&regs->tmr_ctrl);
}

void fman_rtc_set_timer_ctrl(struct rtc_regs *regs, uint32_t val)
{
	iowrite32be(val, &regs->tmr_ctrl);
}

void fman_rtc_timers_soft_reset(struct rtc_regs *regs)
{
	fman_rtc_set_timer_ctrl(regs, FMAN_RTC_TMR_CTRL_TMSR);
	DELAY(10);
	fman_rtc_set_timer_ctrl(regs, 0);
}

void fman_rtc_init(struct rtc_cfg *cfg, struct rtc_regs *regs, int num_alarms,
		int num_fipers, int num_ext_triggers, bool init_freq_comp,
		uint32_t freq_compensation, uint32_t output_clock_divisor)
{
	uint32_t            tmr_ctrl;
	int			i;

	fman_rtc_timers_soft_reset(regs);

	/* Set the source clock */
	switch (cfg->src_clk) {
	case E_FMAN_RTC_SOURCE_CLOCK_SYSTEM:
		tmr_ctrl = FMAN_RTC_TMR_CTRL_CKSEL_MAC_CLK;
		break;
	case E_FMAN_RTC_SOURCE_CLOCK_OSCILATOR:
		tmr_ctrl = FMAN_RTC_TMR_CTRL_CKSEL_OSC_CLK;
		break;
	default:
		/* Use a clock from the External TMR reference clock.*/
		tmr_ctrl = FMAN_RTC_TMR_CTRL_CKSEL_EXT_CLK;
		break;
	}

	/* whatever period the user picked, the timestamp will advance in '1'
	* every time the period passed. */
	tmr_ctrl |= ((1 << FMAN_RTC_TMR_CTRL_TCLK_PERIOD_SHIFT) &
				FMAN_RTC_TMR_CTRL_TCLK_PERIOD_MASK);

	if (cfg->invert_input_clk_phase)
		tmr_ctrl |= FMAN_RTC_TMR_CTRL_CIPH;
	if (cfg->invert_output_clk_phase)
		tmr_ctrl |= FMAN_RTC_TMR_CTRL_COPH;

	for (i = 0; i < num_alarms; i++) {
		if (cfg->alarm_polarity[i] ==
			E_FMAN_RTC_ALARM_POLARITY_ACTIVE_LOW)
			tmr_ctrl |= (FMAN_RTC_TMR_CTRL_ALMP1 >> i);
	}

	for (i = 0; i < num_ext_triggers; i++)
		if (cfg->trigger_polarity[i] ==
			E_FMAN_RTC_TRIGGER_ON_FALLING_EDGE)
			tmr_ctrl |= (FMAN_RTC_TMR_CTRL_ETEP1 << i);

	if (!cfg->timer_slave_mode && cfg->bypass)
		tmr_ctrl |= FMAN_RTC_TMR_CTRL_BYP;

	fman_rtc_set_timer_ctrl(regs, tmr_ctrl);
	if (init_freq_comp)
		fman_rtc_set_frequency_compensation(regs, freq_compensation);

	/* Clear TMR_ALARM registers */
	for (i = 0; i < num_alarms; i++)
		fman_rtc_set_timer_alarm(regs, i, 0xFFFFFFFFFFFFFFFFLL);

	/* Clear TMR_TEVENT */
	fman_rtc_ack_event(regs, FMAN_RTC_TMR_TEVENT_ALL);

	/* Initialize TMR_TEMASK */
	fman_rtc_set_interrupt_mask(regs, 0);

	/* Clear TMR_FIPER registers */
	for (i = 0; i < num_fipers; i++)
		fman_rtc_set_timer_fiper(regs, i, 0xFFFFFFFF);

	/* Initialize TMR_PRSC */
	iowrite32be(output_clock_divisor, &regs->tmr_prsc);

	/* Clear TMR_OFF */
	fman_rtc_set_timer_offset(regs, 0);
}

bool fman_rtc_is_enabled(struct rtc_regs *regs)
{
	return (bool)(fman_rtc_get_timer_ctrl(regs) & FMAN_RTC_TMR_CTRL_TE);
}

void fman_rtc_enable(struct rtc_regs *regs, bool reset_clock)
{
	uint32_t tmr_ctrl = fman_rtc_get_timer_ctrl(regs);

	/* TODO check that no timestamping MACs are working in this stage. */
	if (reset_clock) {
		fman_rtc_set_timer_ctrl(regs, (tmr_ctrl | FMAN_RTC_TMR_CTRL_TMSR));

		DELAY(10);
		/* Clear TMR_OFF */
		fman_rtc_set_timer_offset(regs, 0);
	}

	fman_rtc_set_timer_ctrl(regs, (tmr_ctrl | FMAN_RTC_TMR_CTRL_TE));
}

void fman_rtc_disable(struct rtc_regs *regs)
{
	fman_rtc_set_timer_ctrl(regs, (fman_rtc_get_timer_ctrl(regs)
					& ~(FMAN_RTC_TMR_CTRL_TE)));
}

void fman_rtc_clear_periodic_pulse(struct rtc_regs *regs, int id)
{
	uint32_t tmp_reg;
	if (id == 0)
		tmp_reg = FMAN_RTC_TMR_TEVENT_PP1;
	else
		tmp_reg = FMAN_RTC_TMR_TEVENT_PP2;
	fman_rtc_disable_interupt(regs, tmp_reg);

	tmp_reg = fman_rtc_get_timer_ctrl(regs);
	if (tmp_reg & FMAN_RTC_TMR_CTRL_FS)
		fman_rtc_set_timer_ctrl(regs, tmp_reg & ~FMAN_RTC_TMR_CTRL_FS);

	fman_rtc_set_timer_fiper(regs, id, 0xFFFFFFFF);
}

void fman_rtc_clear_external_trigger(struct rtc_regs *regs, int id)
{
	uint32_t    tmpReg, tmp_ctrl;

	if (id == 0)
		tmpReg = FMAN_RTC_TMR_TEVENT_ETS1;
	else
		tmpReg = FMAN_RTC_TMR_TEVENT_ETS2;
	fman_rtc_disable_interupt(regs, tmpReg);

	if (id == 0)
		tmpReg = FMAN_RTC_TMR_CTRL_PP1L;
	else
		tmpReg = FMAN_RTC_TMR_CTRL_PP2L;
	tmp_ctrl = fman_rtc_get_timer_ctrl(regs);
	if (tmp_ctrl & tmpReg)
		fman_rtc_set_timer_ctrl(regs, tmp_ctrl & ~tmpReg);
}

void fman_rtc_set_alarm(struct rtc_regs *regs, int id, uint32_t val, bool enable)
{
	uint32_t    tmpReg;
	fman_rtc_set_timer_alarm(regs, id, val);
	if (enable) {
		if (id == 0)
			tmpReg = FMAN_RTC_TMR_TEVENT_ALM1;
		else
			tmpReg = FMAN_RTC_TMR_TEVENT_ALM2;
		fman_rtc_enable_interupt(regs, tmpReg);
	}
}

void fman_rtc_set_periodic_pulse(struct rtc_regs *regs, int id, uint32_t val,
						bool enable)
{
	uint32_t    tmpReg;
	fman_rtc_set_timer_fiper(regs, id, val);
	if (enable) {
		if (id == 0)
			tmpReg = FMAN_RTC_TMR_TEVENT_PP1;
		else
			tmpReg = FMAN_RTC_TMR_TEVENT_PP2;
		fman_rtc_enable_interupt(regs, tmpReg);
	}
}

void fman_rtc_set_ext_trigger(struct rtc_regs *regs, int id, bool enable,
						bool use_pulse_as_input)
{
	uint32_t    tmpReg;
	if (enable) {
		if (id == 0)
			tmpReg = FMAN_RTC_TMR_TEVENT_ETS1;
		else
			tmpReg = FMAN_RTC_TMR_TEVENT_ETS2;
		fman_rtc_enable_interupt(regs, tmpReg);
	}
	if (use_pulse_as_input)	{
		if (id == 0)
			tmpReg = FMAN_RTC_TMR_CTRL_PP1L;
		else
			tmpReg = FMAN_RTC_TMR_CTRL_PP2L;
		fman_rtc_set_timer_ctrl(regs, fman_rtc_get_timer_ctrl(regs) | tmpReg);
	}
}
