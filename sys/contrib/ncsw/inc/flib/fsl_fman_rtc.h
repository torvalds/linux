/*
 * Copyright 2013 Freescale Semiconductor Inc.
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

#ifndef __FSL_FMAN_RTC_H
#define __FSL_FMAN_RTC_H

#include "common/general.h"

/* FM RTC Registers definitions */
#define FMAN_RTC_TMR_CTRL_ALMP1                  0x80000000
#define FMAN_RTC_TMR_CTRL_ALMP2                  0x40000000
#define FMAN_RTC_TMR_CTRL_FS                     0x10000000
#define FMAN_RTC_TMR_CTRL_PP1L                   0x08000000
#define FMAN_RTC_TMR_CTRL_PP2L                   0x04000000
#define FMAN_RTC_TMR_CTRL_TCLK_PERIOD_MASK       0x03FF0000
#define FMAN_RTC_TMR_CTRL_FRD                    0x00004000
#define FMAN_RTC_TMR_CTRL_SLV                    0x00002000
#define FMAN_RTC_TMR_CTRL_ETEP1                  0x00000100
#define FMAN_RTC_TMR_CTRL_COPH                   0x00000080
#define FMAN_RTC_TMR_CTRL_CIPH                   0x00000040
#define FMAN_RTC_TMR_CTRL_TMSR                   0x00000020
#define FMAN_RTC_TMR_CTRL_DBG                    0x00000010
#define FMAN_RTC_TMR_CTRL_BYP                    0x00000008
#define FMAN_RTC_TMR_CTRL_TE                     0x00000004
#define FMAN_RTC_TMR_CTRL_CKSEL_OSC_CLK          0x00000003
#define FMAN_RTC_TMR_CTRL_CKSEL_MAC_CLK          0x00000001
#define FMAN_RTC_TMR_CTRL_CKSEL_EXT_CLK          0x00000000
#define FMAN_RTC_TMR_CTRL_TCLK_PERIOD_SHIFT      16

#define FMAN_RTC_TMR_TEVENT_ETS2                 0x02000000
#define FMAN_RTC_TMR_TEVENT_ETS1                 0x01000000
#define FMAN_RTC_TMR_TEVENT_ALM2                 0x00020000
#define FMAN_RTC_TMR_TEVENT_ALM1                 0x00010000
#define FMAN_RTC_TMR_TEVENT_PP1                  0x00000080
#define FMAN_RTC_TMR_TEVENT_PP2                  0x00000040
#define FMAN_RTC_TMR_TEVENT_PP3                  0x00000020
#define FMAN_RTC_TMR_TEVENT_ALL                  (FMAN_RTC_TMR_TEVENT_ETS2 |\
						FMAN_RTC_TMR_TEVENT_ETS1 |\
						FMAN_RTC_TMR_TEVENT_ALM2 |\
						FMAN_RTC_TMR_TEVENT_ALM1 |\
						FMAN_RTC_TMR_TEVENT_PP1 |\
						FMAN_RTC_TMR_TEVENT_PP2 |\
						FMAN_RTC_TMR_TEVENT_PP3)

#define FMAN_RTC_TMR_PRSC_OCK_MASK               0x0000FFFF

/**************************************************************************//**
 @Description   FM RTC Alarm Polarity Options.
*//***************************************************************************/
enum fman_rtc_alarm_polarity {
    E_FMAN_RTC_ALARM_POLARITY_ACTIVE_HIGH,  /**< Active-high output polarity */
    E_FMAN_RTC_ALARM_POLARITY_ACTIVE_LOW    /**< Active-low output polarity */
};

/**************************************************************************//**
 @Description   FM RTC Trigger Polarity Options.
*//***************************************************************************/
enum fman_rtc_trigger_polarity {
    E_FMAN_RTC_TRIGGER_ON_RISING_EDGE,    /**< Trigger on rising edge */
    E_FMAN_RTC_TRIGGER_ON_FALLING_EDGE    /**< Trigger on falling edge */
};

/**************************************************************************//**
 @Description   IEEE1588 Timer Module FM RTC Optional Clock Sources.
*//***************************************************************************/
enum fman_src_clock {
    E_FMAN_RTC_SOURCE_CLOCK_EXTERNAL,  /**< external high precision timer
						reference clock */
    E_FMAN_RTC_SOURCE_CLOCK_SYSTEM,    /**< MAC system clock */
    E_FMAN_RTC_SOURCE_CLOCK_OSCILATOR  /**< RTC clock oscilator */
};

/* RTC default values */
#define DEFAULT_SRC_CLOCK                E_FMAN_RTC_SOURCE_CLOCK_SYSTEM
#define DEFAULT_INVERT_INPUT_CLK_PHASE   FALSE
#define DEFAULT_INVERT_OUTPUT_CLK_PHASE  FALSE
#define DEFAULT_ALARM_POLARITY           E_FMAN_RTC_ALARM_POLARITY_ACTIVE_HIGH
#define DEFAULT_TRIGGER_POLARITY         E_FMAN_RTC_TRIGGER_ON_FALLING_EDGE
#define DEFAULT_PULSE_REALIGN            FALSE

#define FMAN_RTC_MAX_NUM_OF_ALARMS 3
#define FMAN_RTC_MAX_NUM_OF_PERIODIC_PULSES 4
#define FMAN_RTC_MAX_NUM_OF_EXT_TRIGGERS 3

/**************************************************************************//**
 @Description FM RTC timer alarm
*//***************************************************************************/
struct t_tmr_alarm{
    uint32_t   tmr_alarm_h;    /**<  */
    uint32_t   tmr_alarm_l;    /**<  */
};

/**************************************************************************//**
 @Description FM RTC timer Ex trigger
*//***************************************************************************/
struct t_tmr_ext_trigger{
    uint32_t   tmr_etts_h;     /**<  */
    uint32_t   tmr_etts_l;     /**<  */
};

struct rtc_regs {
    uint32_t tmr_id;      /* 0x000 Module ID register */
    uint32_t tmr_id2;     /* 0x004 Controller ID register */
    uint32_t reserved0008[30];
    uint32_t tmr_ctrl;    /* 0x0080 timer control register */
    uint32_t tmr_tevent;  /* 0x0084 timer event register */
    uint32_t tmr_temask;  /* 0x0088 timer event mask register */
    uint32_t reserved008c[3];
    uint32_t tmr_cnt_h;   /* 0x0098 timer counter high register */
    uint32_t tmr_cnt_l;   /* 0x009c timer counter low register */
    uint32_t tmr_add;     /* 0x00a0 timer drift compensation addend register */
    uint32_t tmr_acc;     /* 0x00a4 timer accumulator register */
    uint32_t tmr_prsc;    /* 0x00a8 timer prescale */
    uint32_t reserved00ac;
    uint32_t tmr_off_h;    /* 0x00b0 timer offset high */
    uint32_t tmr_off_l;    /* 0x00b4 timer offset low  */
    struct t_tmr_alarm tmr_alarm[FMAN_RTC_MAX_NUM_OF_ALARMS]; /* 0x00b8 timer
								alarm */
    uint32_t tmr_fiper[FMAN_RTC_MAX_NUM_OF_PERIODIC_PULSES]; /* 0x00d0 timer
						fixed period interval */
    struct t_tmr_ext_trigger tmr_etts[FMAN_RTC_MAX_NUM_OF_EXT_TRIGGERS];
			/* 0x00e0 time stamp general purpose external */
    uint32_t reserved00f0[4];
};

struct rtc_cfg {
    enum fman_src_clock            src_clk;
    uint32_t                ext_src_clk_freq;
    uint32_t                rtc_freq_hz;
    bool                    timer_slave_mode;
    bool                    invert_input_clk_phase;
    bool                    invert_output_clk_phase;
    uint32_t                events_mask;
    bool                    bypass; /**< Indicates if frequency compensation
					is bypassed */
    bool                    pulse_realign;
    enum fman_rtc_alarm_polarity    alarm_polarity[FMAN_RTC_MAX_NUM_OF_ALARMS];
    enum fman_rtc_trigger_polarity  trigger_polarity
					[FMAN_RTC_MAX_NUM_OF_EXT_TRIGGERS];
};

/**
 * fman_rtc_defconfig() - Get default RTC configuration
 * @cfg:	pointer to configuration structure.
 *
 * Call this function to obtain a default set of configuration values for
 * initializing RTC.  The user can overwrite any of the values before calling
 * fman_rtc_init(), if specific configuration needs to be applied.
 */
void fman_rtc_defconfig(struct rtc_cfg *cfg);

/**
 * fman_rtc_get_events() - Get the events
 * @regs:		Pointer to RTC register block
 *
 * Returns: The events
 */
uint32_t fman_rtc_get_events(struct rtc_regs *regs);

/**
 * fman_rtc_get_interrupt_mask() - Get the events mask
 * @regs:		Pointer to RTC register block
 *
 * Returns: The events mask
 */
uint32_t fman_rtc_get_interrupt_mask(struct rtc_regs *regs);


/**
 * fman_rtc_set_interrupt_mask() - Set the events mask
 * @regs:		Pointer to RTC register block
 * @mask:		The mask to set
 */
void fman_rtc_set_interrupt_mask(struct rtc_regs *regs, uint32_t mask);

/**
 * fman_rtc_get_event() - Check if specific events occurred
 * @regs:		Pointer to RTC register block
 * @ev_mask:	a mask of the events to check
 *
 * Returns: 0 if the events did not occur. Non zero if one of the events occurred
 */
uint32_t fman_rtc_get_event(struct rtc_regs *regs, uint32_t ev_mask);

/**
 * fman_rtc_check_and_clear_event() - Clear events which are on
 * @regs:		Pointer to RTC register block
 *
 * Returns: A mask of the events which were cleared
 */
uint32_t fman_rtc_check_and_clear_event(struct rtc_regs *regs);

/**
 * fman_rtc_ack_event() - Clear events
 * @regs:		Pointer to RTC register block
 * @events:		The events to disable
 */
void fman_rtc_ack_event(struct rtc_regs *regs, uint32_t events);

/**
 * fman_rtc_enable_interupt() - Enable events interrupts
 * @regs:		Pointer to RTC register block
 * @mask:		The events to disable
 */
void fman_rtc_enable_interupt(struct rtc_regs *regs, uint32_t mask);

/**
 * fman_rtc_disable_interupt() - Disable events interrupts
 * @regs:		Pointer to RTC register block
 * @mask:		The events to disable
 */
void fman_rtc_disable_interupt(struct rtc_regs *regs, uint32_t mask);

/**
 * fman_rtc_get_timer_ctrl() - Get the control register
 * @regs:		Pointer to RTC register block
 *
 * Returns: The control register value
 */
uint32_t fman_rtc_get_timer_ctrl(struct rtc_regs *regs);

/**
 * fman_rtc_set_timer_ctrl() - Set timer control register
 * @regs:		Pointer to RTC register block
 * @val:		The value to set
 */
void fman_rtc_set_timer_ctrl(struct rtc_regs *regs, uint32_t val);

/**
 * fman_rtc_get_frequency_compensation() - Get the frequency compensation
 * @regs:		Pointer to RTC register block
 *
 * Returns: The timer counter
 */
uint32_t fman_rtc_get_frequency_compensation(struct rtc_regs *regs);

/**
 * fman_rtc_set_frequency_compensation() - Set frequency compensation
 * @regs:		Pointer to RTC register block
 * @val:		The value to set
 */
void fman_rtc_set_frequency_compensation(struct rtc_regs *regs, uint32_t val);

/**
 * fman_rtc_get_trigger_stamp() - Get a trigger stamp
 * @regs:		Pointer to RTC register block
 * @id:	The id of the trigger stamp
 *
 * Returns: The time stamp
 */
uint64_t fman_rtc_get_trigger_stamp(struct rtc_regs *regs,  int id);

/**
 * fman_rtc_set_timer_alarm_l() - Set timer alarm low register
 * @regs:		Pointer to RTC register block
 * @index:		The index of alarm to set
 * @val:		The value to set
 */
void fman_rtc_set_timer_alarm_l(struct rtc_regs *regs, int index,
		uint32_t val);

/**
 * fman_rtc_set_timer_alarm() - Set timer alarm
 * @regs:		Pointer to RTC register block
 * @index:		The index of alarm to set
 * @val:		The value to set
 */
void fman_rtc_set_timer_alarm(struct rtc_regs *regs, int index, int64_t val);

/**
 * fman_rtc_set_timer_fiper() - Set timer fiper
 * @regs:		Pointer to RTC register block
 * @index:		The index of fiper to set
 * @val:		The value to set
 */
void fman_rtc_set_timer_fiper(struct rtc_regs *regs, int index, uint32_t val);

/**
 * fman_rtc_set_timer_offset() - Set timer offset
 * @regs:		Pointer to RTC register block
 * @val:		The value to set
 */
void fman_rtc_set_timer_offset(struct rtc_regs *regs, int64_t val);

/**
 * fman_rtc_get_timer() - Get the timer counter
 * @regs:		Pointer to RTC register block
 *
 * Returns: The timer counter
 */
static inline uint64_t fman_rtc_get_timer(struct rtc_regs *regs)
{
	uint64_t time;
    /* TMR_CNT_L must be read first to get an accurate value */
    time = (uint64_t)ioread32be(&regs->tmr_cnt_l);
    time |= ((uint64_t)ioread32be(&regs->tmr_cnt_h) << 32);

    return time;
}

/**
 * fman_rtc_set_timer() - Set timer counter
 * @regs:		Pointer to RTC register block
 * @val:		The value to set
 */
static inline void fman_rtc_set_timer(struct rtc_regs *regs, int64_t val)
{
	iowrite32be((uint32_t)val, &regs->tmr_cnt_l);
	iowrite32be((uint32_t)(val >> 32), &regs->tmr_cnt_h);
}

/**
 * fman_rtc_timers_soft_reset() - Soft reset
 * @regs:		Pointer to RTC register block
 *
 * Resets all the timer registers and state machines for the 1588 IP and
 * the attached client 1588
 */
void fman_rtc_timers_soft_reset(struct rtc_regs *regs);

/**
 * fman_rtc_clear_external_trigger() - Clear an external trigger
 * @regs:		Pointer to RTC register block
 * @id: The id of the trigger to clear
 */
void fman_rtc_clear_external_trigger(struct rtc_regs *regs, int id);

/**
 * fman_rtc_clear_periodic_pulse() - Clear periodic pulse
 * @regs:		Pointer to RTC register block
 * @id: The id of the fiper to clear
 */
void fman_rtc_clear_periodic_pulse(struct rtc_regs *regs, int id);

/**
 * fman_rtc_enable() - Enable RTC hardware block
 * @regs:		Pointer to RTC register block
 */
void fman_rtc_enable(struct rtc_regs *regs, bool reset_clock);

/**
 * fman_rtc_is_enabled() - Is RTC hardware block enabled
 * @regs:		Pointer to RTC register block
 *
 * Return: TRUE if enabled
 */
bool fman_rtc_is_enabled(struct rtc_regs *regs);

/**
 * fman_rtc_disable() - Disable RTC hardware block
 * @regs:		Pointer to RTC register block
 */
void fman_rtc_disable(struct rtc_regs *regs);

/**
 * fman_rtc_init() - Init RTC hardware block
 * @cfg:		RTC configuration data
 * @regs:		Pointer to RTC register block
 * @num_alarms:		Number of alarms in RTC
 * @num_fipers:		Number of fipers in RTC
 * @num_ext_triggers:	Number of external triggers in RTC
 * @freq_compensation:		Frequency compensation
 * @output_clock_divisor:		Output clock divisor
 *
 * This function initializes RTC and applies basic configuration.
 */
void fman_rtc_init(struct rtc_cfg *cfg, struct rtc_regs *regs, int num_alarms,
		int num_fipers, int num_ext_triggers, bool init_freq_comp,
		uint32_t freq_compensation, uint32_t output_clock_divisor);

/**
 * fman_rtc_set_alarm() - Set an alarm
 * @regs:		Pointer to RTC register block
 * @id:			id of alarm
 * @val:		value to write
 * @enable:		should interrupt be enabled
 */
void fman_rtc_set_alarm(struct rtc_regs *regs, int id, uint32_t val, bool enable);

/**
 * fman_rtc_set_periodic_pulse() - Set an alarm
 * @regs:		Pointer to RTC register block
 * @id:			id of fiper
 * @val:		value to write
 * @enable:		should interrupt be enabled
 */
void fman_rtc_set_periodic_pulse(struct rtc_regs *regs, int id, uint32_t val,
	bool enable);

/**
 * fman_rtc_set_ext_trigger() - Set an external trigger
 * @regs:		Pointer to RTC register block
 * @id:			id of trigger
 * @enable:		should interrupt be enabled
 * @use_pulse_as_input: use the pulse as input
 */
void fman_rtc_set_ext_trigger(struct rtc_regs *regs, int id, bool enable,
	bool use_pulse_as_input);

struct fm_rtc_alarm_params {
	uint8_t alarm_id;            	/**< 0 or 1 */
	uint64_t alarm_time;         	/**< In nanoseconds, the time when the
					alarm should go off - must be a
					multiple of the RTC period */
	void (*f_alarm_callback)(void* app, uint8_t id); /**< This routine will
					be called when RTC reaches alarmTime */
	bool clear_on_expiration;   	/**< TRUE to turn off the alarm once
					expired.*/
};

struct fm_rtc_periodic_pulse_params {
	uint8_t periodic_pulse_id;      /**< 0 or 1 */
	uint64_t periodic_pulse_period; /**< In Nanoseconds. Must be a multiple
					of the RTC period */
	void (*f_periodic_pulse_callback)(void* app, uint8_t id); /**< This
					routine will be called every
					periodicPulsePeriod. */
};

#endif /* __FSL_FMAN_RTC_H */
