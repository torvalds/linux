/******************************************************************************

  Copyright (c) 2001-2017, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/


#include "ixgbe.h"

/************************************************************************
 * ixgbe_bypass_mutex_enter
 *
 *   Mutex support for the bypass feature. Using a dual lock
 *   to facilitate a privileged access to the watchdog update
 *   over other threads.
 ************************************************************************/
static void
ixgbe_bypass_mutex_enter(struct adapter *adapter)
{
	while (atomic_cmpset_int(&adapter->bypass.low, 0, 1) == 0)
		usec_delay(3000);
	while (atomic_cmpset_int(&adapter->bypass.high, 0, 1) == 0)
		usec_delay(3000);
	return;
} /* ixgbe_bypass_mutex_enter */

/************************************************************************
 * ixgbe_bypass_mutex_clear
 ************************************************************************/
static void
ixgbe_bypass_mutex_clear(struct adapter *adapter)
{
	while (atomic_cmpset_int(&adapter->bypass.high, 1, 0) == 0)
		usec_delay(6000);
	while (atomic_cmpset_int(&adapter->bypass.low, 1, 0) == 0)
		usec_delay(6000);
	return;
} /* ixgbe_bypass_mutex_clear */

/************************************************************************
 * ixgbe_bypass_wd_mutex_enter
 *
 *   Watchdog entry is allowed to simply grab the high priority
 ************************************************************************/
static void
ixgbe_bypass_wd_mutex_enter(struct adapter *adapter)
{
	while (atomic_cmpset_int(&adapter->bypass.high, 0, 1) == 0)
		usec_delay(3000);
	return;
} /* ixgbe_bypass_wd_mutex_enter */

/************************************************************************
 * ixgbe_bypass_wd_mutex_clear
 ************************************************************************/
static void
ixgbe_bypass_wd_mutex_clear(struct adapter *adapter)
{
	while (atomic_cmpset_int(&adapter->bypass.high, 1, 0) == 0)
		usec_delay(6000);
	return;
} /* ixgbe_bypass_wd_mutex_clear */

/************************************************************************
 * ixgbe_get_bypass_time
 ************************************************************************/
static void
ixgbe_get_bypass_time(u32 *year, u32 *sec)
{
	struct timespec current;

	*year = 1970;           /* time starts at 01/01/1970 */
	nanotime(&current);
	*sec = current.tv_sec;

	while(*sec > SEC_THIS_YEAR(*year)) {
		*sec -= SEC_THIS_YEAR(*year);
		(*year)++;
	}
} /* ixgbe_get_bypass_time */

/************************************************************************
 * ixgbe_bp_version
 *
 *   Display the feature version
 ************************************************************************/
static int
ixgbe_bp_version(SYSCTL_HANDLER_ARGS)
{
	struct adapter  *adapter = (struct adapter *) arg1;
	struct ixgbe_hw *hw = &adapter->hw;
	int             error = 0;
	static int      version = 0;
	u32             cmd;

	ixgbe_bypass_mutex_enter(adapter);
	cmd = BYPASS_PAGE_CTL2 | BYPASS_WE;
	cmd |= (BYPASS_EEPROM_VER_ADD << BYPASS_CTL2_OFFSET_SHIFT) &
	    BYPASS_CTL2_OFFSET_M;
	if ((error = hw->mac.ops.bypass_rw(hw, cmd, &version) != 0))
		goto err;
	msec_delay(100);
	cmd &= ~BYPASS_WE;
	if ((error = hw->mac.ops.bypass_rw(hw, cmd, &version) != 0))
		goto err;
	ixgbe_bypass_mutex_clear(adapter);
	version &= BYPASS_CTL2_DATA_M;
	error = sysctl_handle_int(oidp, &version, 0, req);
	return (error);
err:
	ixgbe_bypass_mutex_clear(adapter);
	return (error);

} /* ixgbe_bp_version */

/************************************************************************
 * ixgbe_bp_set_state
 *
 *   Show/Set the Bypass State:
 *	1 = NORMAL
 *	2 = BYPASS
 *	3 = ISOLATE
 *
 *	With no argument the state is displayed,
 *	passing a value will set it.
 ************************************************************************/
static int
ixgbe_bp_set_state(SYSCTL_HANDLER_ARGS)
{
	struct adapter  *adapter = (struct adapter *) arg1;
	struct ixgbe_hw *hw = &adapter->hw;
	int             error = 0;
	static int      state = 0;

	/* Get the current state */
	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_rw(hw,
	    BYPASS_PAGE_CTL0, &state);
	ixgbe_bypass_mutex_clear(adapter);
	if (error != 0)
		return (error);
	state = (state >> BYPASS_STATUS_OFF_SHIFT) & 0x3;

	error = sysctl_handle_int(oidp, &state, 0, req);
	if ((error != 0) || (req->newptr == NULL))
		return (error);

	/* Sanity check new state */
	switch (state) {
	case BYPASS_NORM:
	case BYPASS_BYPASS:
	case BYPASS_ISOLATE:
		break;
	default:
		return (EINVAL);
	}
	ixgbe_bypass_mutex_enter(adapter);
	if ((error = hw->mac.ops.bypass_set(hw, BYPASS_PAGE_CTL0,
	    BYPASS_MODE_OFF_M, state) != 0))
		goto out;
	/* Set AUTO back on so FW can receive events */
	error = hw->mac.ops.bypass_set(hw, BYPASS_PAGE_CTL0,
	    BYPASS_MODE_OFF_M, BYPASS_AUTO);
out:
	ixgbe_bypass_mutex_clear(adapter);
	usec_delay(6000);
	return (error);
} /* ixgbe_bp_set_state */

/************************************************************************
 * The following routines control the operational
 * "rules" of the feature, what behavior will occur
 * when particular events occur.
 * 	Values are:
 *		0 - no change for the event (NOP)
 *		1 - go to Normal operation
 *		2 - go to Bypass operation
 *		3 - go to Isolate operation
 * Calling the entry with no argument just displays
 * the current rule setting.
 ************************************************************************/

/************************************************************************
 * ixgbe_bp_timeout
 *
 * This is to set the Rule for the watchdog,
 * not the actual watchdog timeout value.
 ************************************************************************/
static int
ixgbe_bp_timeout(SYSCTL_HANDLER_ARGS)
{
	struct adapter  *adapter = (struct adapter *) arg1;
	struct ixgbe_hw *hw = &adapter->hw;
	int             error = 0;
	static int      timeout = 0;

	/* Get the current value */
	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_rw(hw, BYPASS_PAGE_CTL0, &timeout);
	ixgbe_bypass_mutex_clear(adapter);
	if (error)
		return (error);
	timeout = (timeout >> BYPASS_WDTIMEOUT_SHIFT) & 0x3;

	error = sysctl_handle_int(oidp, &timeout, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	/* Sanity check on the setting */
	switch (timeout) {
	case BYPASS_NOP:
	case BYPASS_NORM:
	case BYPASS_BYPASS:
	case BYPASS_ISOLATE:
		break;
	default:
		return (EINVAL);
	}

	/* Set the new state */
	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_set(hw, BYPASS_PAGE_CTL0,
	    BYPASS_WDTIMEOUT_M, timeout << BYPASS_WDTIMEOUT_SHIFT);
	ixgbe_bypass_mutex_clear(adapter);
	usec_delay(6000);
	return (error);
} /* ixgbe_bp_timeout */

/************************************************************************
 * ixgbe_bp_main_on
 ************************************************************************/
static int
ixgbe_bp_main_on(SYSCTL_HANDLER_ARGS)
{
	struct adapter  *adapter = (struct adapter *) arg1;
	struct ixgbe_hw *hw = &adapter->hw;
	int             error = 0;
	static int      main_on = 0;

	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_rw(hw, BYPASS_PAGE_CTL0, &main_on);
	main_on = (main_on >> BYPASS_MAIN_ON_SHIFT) & 0x3;
	ixgbe_bypass_mutex_clear(adapter);
	if (error)
		return (error);

	error = sysctl_handle_int(oidp, &main_on, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	/* Sanity check on the setting */
	switch (main_on) {
	case BYPASS_NOP:
	case BYPASS_NORM:
	case BYPASS_BYPASS:
	case BYPASS_ISOLATE:
		break;
	default:
		return (EINVAL);
	}

	/* Set the new state */
	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_set(hw, BYPASS_PAGE_CTL0,
	    BYPASS_MAIN_ON_M, main_on << BYPASS_MAIN_ON_SHIFT);
	ixgbe_bypass_mutex_clear(adapter);
	usec_delay(6000);
	return (error);
} /* ixgbe_bp_main_on */

/************************************************************************
 * ixgbe_bp_main_off
 ************************************************************************/
static int
ixgbe_bp_main_off(SYSCTL_HANDLER_ARGS)
{
	struct adapter  *adapter = (struct adapter *) arg1;
	struct ixgbe_hw *hw = &adapter->hw;
	int             error = 0;
	static int      main_off = 0;

	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_rw(hw, BYPASS_PAGE_CTL0, &main_off);
	ixgbe_bypass_mutex_clear(adapter);
	if (error)
		return (error);
	main_off = (main_off >> BYPASS_MAIN_OFF_SHIFT) & 0x3;

	error = sysctl_handle_int(oidp, &main_off, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	/* Sanity check on the setting */
	switch (main_off) {
	case BYPASS_NOP:
	case BYPASS_NORM:
	case BYPASS_BYPASS:
	case BYPASS_ISOLATE:
		break;
	default:
		return (EINVAL);
	}

	/* Set the new state */
	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_set(hw, BYPASS_PAGE_CTL0,
	    BYPASS_MAIN_OFF_M, main_off << BYPASS_MAIN_OFF_SHIFT);
	ixgbe_bypass_mutex_clear(adapter);
	usec_delay(6000);
	return (error);
} /* ixgbe_bp_main_off */

/************************************************************************
 * ixgbe_bp_aux_on
 ************************************************************************/
static int
ixgbe_bp_aux_on(SYSCTL_HANDLER_ARGS)
{
	struct adapter  *adapter = (struct adapter *) arg1;
	struct ixgbe_hw *hw = &adapter->hw;
	int             error = 0;
	static int      aux_on = 0;

	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_rw(hw, BYPASS_PAGE_CTL0, &aux_on);
	ixgbe_bypass_mutex_clear(adapter);
	if (error)
		return (error);
	aux_on = (aux_on >> BYPASS_AUX_ON_SHIFT) & 0x3;

	error = sysctl_handle_int(oidp, &aux_on, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	/* Sanity check on the setting */
	switch (aux_on) {
	case BYPASS_NOP:
	case BYPASS_NORM:
	case BYPASS_BYPASS:
	case BYPASS_ISOLATE:
		break;
	default:
		return (EINVAL);
	}

	/* Set the new state */
	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_set(hw, BYPASS_PAGE_CTL0,
	    BYPASS_AUX_ON_M, aux_on << BYPASS_AUX_ON_SHIFT);
	ixgbe_bypass_mutex_clear(adapter);
	usec_delay(6000);
	return (error);
} /* ixgbe_bp_aux_on */

/************************************************************************
 * ixgbe_bp_aux_off
 ************************************************************************/
static int
ixgbe_bp_aux_off(SYSCTL_HANDLER_ARGS)
{
	struct adapter  *adapter = (struct adapter *) arg1;
	struct ixgbe_hw *hw = &adapter->hw;
	int             error = 0;
	static int      aux_off = 0;

	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_rw(hw, BYPASS_PAGE_CTL0, &aux_off);
	ixgbe_bypass_mutex_clear(adapter);
	if (error)
		return (error);
	aux_off = (aux_off >> BYPASS_AUX_OFF_SHIFT) & 0x3;

	error = sysctl_handle_int(oidp, &aux_off, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	/* Sanity check on the setting */
	switch (aux_off) {
	case BYPASS_NOP:
	case BYPASS_NORM:
	case BYPASS_BYPASS:
	case BYPASS_ISOLATE:
		break;
	default:
		return (EINVAL);
	}

	/* Set the new state */
	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_set(hw, BYPASS_PAGE_CTL0,
	    BYPASS_AUX_OFF_M, aux_off << BYPASS_AUX_OFF_SHIFT);
	ixgbe_bypass_mutex_clear(adapter);
	usec_delay(6000);
	return (error);
} /* ixgbe_bp_aux_off */

/************************************************************************
 * ixgbe_bp_wd_set - Set the Watchdog timer value
 *
 *   Valid settings are:
 *	- 0 will disable the watchdog
 *	- 1, 2, 3, 4, 8, 16, 32
 *	- anything else is invalid and will be ignored
 ************************************************************************/
static int
ixgbe_bp_wd_set(SYSCTL_HANDLER_ARGS)
{
	struct adapter  *adapter = (struct adapter *) arg1;
	struct ixgbe_hw *hw = &adapter->hw;
	int             error, tmp;
	static int      timeout = 0;
	u32             mask, arg;

	/* Get the current hardware value */
	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_rw(hw, BYPASS_PAGE_CTL0, &tmp);
	ixgbe_bypass_mutex_clear(adapter);
	if (error)
		return (error);
	/*
	 * If armed keep the displayed value,
	 * else change the display to zero.
	 */
	if ((tmp & (0x1 << BYPASS_WDT_ENABLE_SHIFT)) == 0)
		timeout = 0;

	error = sysctl_handle_int(oidp, &timeout, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	arg = 0x1 << BYPASS_WDT_ENABLE_SHIFT;
	mask = BYPASS_WDT_ENABLE_M | BYPASS_WDT_VALUE_M;
	switch (timeout) {
	case 0: /* disables the timer */
		arg = BYPASS_PAGE_CTL0;
		mask = BYPASS_WDT_ENABLE_M;
		break;
	case 1:
		arg |= BYPASS_WDT_1_5 << BYPASS_WDT_TIME_SHIFT;
		break;
	case 2:
		arg |= BYPASS_WDT_2 << BYPASS_WDT_TIME_SHIFT;
		break;
	case 3:
		arg |= BYPASS_WDT_3 << BYPASS_WDT_TIME_SHIFT;
		break;
	case 4:
		arg |= BYPASS_WDT_4 << BYPASS_WDT_TIME_SHIFT;
		break;
	case 8:
		arg |= BYPASS_WDT_8 << BYPASS_WDT_TIME_SHIFT;
		break;
	case 16:
		arg |= BYPASS_WDT_16 << BYPASS_WDT_TIME_SHIFT;
		break;
	case 32:
		arg |= BYPASS_WDT_32 << BYPASS_WDT_TIME_SHIFT;
		break;
	default:
		return (EINVAL);
	}

	/* Set the new watchdog */
	ixgbe_bypass_mutex_enter(adapter);
	error = hw->mac.ops.bypass_set(hw, BYPASS_PAGE_CTL0, mask, arg);
	ixgbe_bypass_mutex_clear(adapter);

	return (error);
} /* ixgbe_bp_wd_set */

/************************************************************************
 * ixgbe_bp_wd_reset - Reset the Watchdog timer
 *
 *    To activate this it must be called with any argument.
 ************************************************************************/
static int
ixgbe_bp_wd_reset(SYSCTL_HANDLER_ARGS)
{
	struct adapter  *adapter = (struct adapter *) arg1;
	struct ixgbe_hw *hw = &adapter->hw;
	u32             sec, year;
	int             cmd, count = 0, error = 0;
	int             reset_wd = 0;

	error = sysctl_handle_int(oidp, &reset_wd, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	cmd = BYPASS_PAGE_CTL1 | BYPASS_WE | BYPASS_CTL1_WDT_PET;

	/* Resync the FW time while writing to CTL1 anyway */
	ixgbe_get_bypass_time(&year, &sec);

	cmd |= (sec & BYPASS_CTL1_TIME_M) | BYPASS_CTL1_VALID;
	cmd |= BYPASS_CTL1_OFFTRST;

	ixgbe_bypass_wd_mutex_enter(adapter);
	error = hw->mac.ops.bypass_rw(hw, cmd, &reset_wd);

	/* Read until it matches what we wrote, or we time out */
	do {
		if (count++ > 10) {
			error = IXGBE_BYPASS_FW_WRITE_FAILURE;
			break;
		}
		error = hw->mac.ops.bypass_rw(hw, BYPASS_PAGE_CTL1, &reset_wd);
		if (error != 0) {
			error = IXGBE_ERR_INVALID_ARGUMENT;
			break;
		}
	} while (!hw->mac.ops.bypass_valid_rd(cmd, reset_wd));

	reset_wd = 0;
	ixgbe_bypass_wd_mutex_clear(adapter);
	return (error);
} /* ixgbe_bp_wd_reset */

/************************************************************************
 * ixgbe_bp_log - Display the bypass log
 *
 *   You must pass a non-zero arg to sysctl
 ************************************************************************/
static int
ixgbe_bp_log(SYSCTL_HANDLER_ARGS)
{
	struct adapter             *adapter = (struct adapter *) arg1;
	struct ixgbe_hw            *hw = &adapter->hw;
	u32                        cmd, base, head;
	u32                        log_off, count = 0;
	static int                 status = 0;
	u8                         data;
	struct ixgbe_bypass_eeprom eeprom[BYPASS_MAX_LOGS];
	int                        i, error = 0;

	error = sysctl_handle_int(oidp, &status, 0, req);
	if ((error) || (req->newptr == NULL))
		return (error);

	/* Keep the log display single-threaded */
	while (atomic_cmpset_int(&adapter->bypass.log, 0, 1) == 0)
		usec_delay(3000);

	ixgbe_bypass_mutex_enter(adapter);

	/* Find Current head of the log eeprom offset */
	cmd = BYPASS_PAGE_CTL2 | BYPASS_WE;
	cmd |= (0x1 << BYPASS_CTL2_OFFSET_SHIFT) & BYPASS_CTL2_OFFSET_M;
	error = hw->mac.ops.bypass_rw(hw, cmd, &status);
	if (error)
		goto unlock_err;

	/* wait for the write to stick */
	msec_delay(100);

	/* Now read the results */
	cmd &= ~BYPASS_WE;
	error = hw->mac.ops.bypass_rw(hw, cmd, &status);
	if (error)
		goto unlock_err;

	ixgbe_bypass_mutex_clear(adapter);

	base = status & BYPASS_CTL2_DATA_M;
	head = (status & BYPASS_CTL2_HEAD_M) >> BYPASS_CTL2_HEAD_SHIFT;

	/* address of the first log */
	log_off = base + (head * 5);

	/* extract all the log entries */
	while (count < BYPASS_MAX_LOGS) {
		eeprom[count].logs = 0;
		eeprom[count].actions = 0;

		/* Log 5 bytes store in on u32 and a u8 */
		for (i = 0; i < 4; i++) {
			ixgbe_bypass_mutex_enter(adapter);
			error = hw->mac.ops.bypass_rd_eep(hw, log_off + i,
			    &data);
			ixgbe_bypass_mutex_clear(adapter);
			if (error)
				return (EINVAL);
			eeprom[count].logs += data << (8 * i);
		}

		ixgbe_bypass_mutex_enter(adapter);
		error = hw->mac.ops.bypass_rd_eep(hw,
		    log_off + i, &eeprom[count].actions);
		ixgbe_bypass_mutex_clear(adapter);
		if (error)
			return (EINVAL);

		/* Quit if not a unread log */
		if (!(eeprom[count].logs & BYPASS_LOG_CLEAR_M))
			break;
		/*
		 * Log looks good so store the address where it's
		 * Unread Log bit is so we can clear it after safely
		 * pulling out all of the log data.
		 */
		eeprom[count].clear_off = log_off;

		count++;
		head = head ? head - 1 : BYPASS_MAX_LOGS;
		log_off = base + (head * 5);
	}

	/* reverse order (oldest first) for output */
	while (count--) {
		int year;
		u32 mon, days, hours, min, sec;
		u32 time = eeprom[count].logs & BYPASS_LOG_TIME_M;
		u32 event = (eeprom[count].logs & BYPASS_LOG_EVENT_M) >>
		    BYPASS_LOG_EVENT_SHIFT;
		u8 action =  eeprom[count].actions & BYPASS_LOG_ACTION_M;
		u16 day_mon[2][13] = {
		  {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
		  {0, 31, 59, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366}
		};
		char *event_str[] = {"unknown", "main on", "aux on",
		    "main off", "aux off", "WDT", "user" };
		char *action_str[] = {"ignore", "normal", "bypass", "isolate",};

		/* verify vaild data  1 - 6 */
		if (event < BYPASS_EVENT_MAIN_ON || event > BYPASS_EVENT_USR)
			event = 0;

		/*
		 * time is in sec's this year, so convert to something
		 * printable.
		 */
		ixgbe_get_bypass_time(&year, &sec);
		days = time / SEC_PER_DAY;
		for (i = 11; days < day_mon[LEAP_YR(year)][i]; i--)
			continue;
		mon = i + 1;    /* display month as 1-12 */
		time -= (day_mon[LEAP_YR(year)][i] * SEC_PER_DAY);
		days = (time / SEC_PER_DAY) + 1;  /* first day is 1 */
		time %= SEC_PER_DAY;
		hours = time / (60 * 60);
		time %= (60 * 60);
		min = time / 60;
		sec = time % 60;
		device_printf(adapter->dev,
		    "UT %02d/%02d %02d:%02d:%02d %8.8s -> %7.7s\n",
		    mon, days, hours, min, sec, event_str[event],
		    action_str[action]);
		cmd = BYPASS_PAGE_CTL2 | BYPASS_WE | BYPASS_CTL2_RW;
		cmd |= ((eeprom[count].clear_off + 3)
		    << BYPASS_CTL2_OFFSET_SHIFT) & BYPASS_CTL2_OFFSET_M;
		cmd |= ((eeprom[count].logs & ~BYPASS_LOG_CLEAR_M) >> 24);

		ixgbe_bypass_mutex_enter(adapter);

		error = hw->mac.ops.bypass_rw(hw, cmd, &status);

		/* wait for the write to stick */
		msec_delay(100);

		ixgbe_bypass_mutex_clear(adapter);

		if (error)
			return (EINVAL);
	}

	status = 0; /* reset */
	/* Another log command can now run */
	while (atomic_cmpset_int(&adapter->bypass.log, 1, 0) == 0)
		usec_delay(3000);
	return (error);

unlock_err:
	ixgbe_bypass_mutex_clear(adapter);
	status = 0; /* reset */
	while (atomic_cmpset_int(&adapter->bypass.log, 1, 0) == 0)
		usec_delay(3000);
	return (EINVAL);
} /* ixgbe_bp_log */

/************************************************************************
 * ixgbe_bypass_init - Set up infrastructure for the bypass feature
 *
 *   Do time and sysctl initialization here.  This feature is
 *   only enabled for the first port of a bypass adapter.
 ************************************************************************/
void
ixgbe_bypass_init(struct adapter *adapter)
{
	struct ixgbe_hw        *hw = &adapter->hw;
	device_t               dev = adapter->dev;
	struct sysctl_oid      *bp_node;
	struct sysctl_oid_list *bp_list;
	u32                    mask, value, sec, year;

	if (!(adapter->feat_cap & IXGBE_FEATURE_BYPASS))
		return;

	/* First set up time for the hardware */
	ixgbe_get_bypass_time(&year, &sec);

	mask = BYPASS_CTL1_TIME_M
	     | BYPASS_CTL1_VALID_M
	     | BYPASS_CTL1_OFFTRST_M;

	value = (sec & BYPASS_CTL1_TIME_M)
	      | BYPASS_CTL1_VALID
	      | BYPASS_CTL1_OFFTRST;

	ixgbe_bypass_mutex_enter(adapter);
	hw->mac.ops.bypass_set(hw, BYPASS_PAGE_CTL1, mask, value);
	ixgbe_bypass_mutex_clear(adapter);

	/* Now set up the SYSCTL infrastructure */

	/*
	 * The log routine is kept separate from the other
	 * children so a general display command like:
	 * `sysctl dev.ix.0.bypass` will not show the log.
	 */
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "bypass_log", CTLTYPE_INT | CTLFLAG_RW,
	    adapter, 0, ixgbe_bp_log, "I", "Bypass Log");

	/* All other setting are hung from the 'bypass' node */
	bp_node = SYSCTL_ADD_NODE(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "bypass", CTLFLAG_RD, NULL, "Bypass");

	bp_list = SYSCTL_CHILDREN(bp_node);

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev), bp_list,
	    OID_AUTO, "version", CTLTYPE_INT | CTLFLAG_RD,
	    adapter, 0, ixgbe_bp_version, "I", "Bypass Version");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev), bp_list,
	    OID_AUTO, "state", CTLTYPE_INT | CTLFLAG_RW,
	    adapter, 0, ixgbe_bp_set_state, "I", "Bypass State");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev), bp_list,
	    OID_AUTO, "timeout", CTLTYPE_INT | CTLFLAG_RW,
	    adapter, 0, ixgbe_bp_timeout, "I", "Bypass Timeout");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev), bp_list,
	    OID_AUTO, "main_on", CTLTYPE_INT | CTLFLAG_RW,
	    adapter, 0, ixgbe_bp_main_on, "I", "Bypass Main On");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev), bp_list,
	    OID_AUTO, "main_off", CTLTYPE_INT | CTLFLAG_RW,
	    adapter, 0, ixgbe_bp_main_off, "I", "Bypass Main Off");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev), bp_list,
	    OID_AUTO, "aux_on", CTLTYPE_INT | CTLFLAG_RW,
	    adapter, 0, ixgbe_bp_aux_on, "I", "Bypass Aux On");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev), bp_list,
	    OID_AUTO, "aux_off", CTLTYPE_INT | CTLFLAG_RW,
	    adapter, 0, ixgbe_bp_aux_off, "I", "Bypass Aux Off");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev), bp_list,
	    OID_AUTO, "wd_set", CTLTYPE_INT | CTLFLAG_RW,
	    adapter, 0, ixgbe_bp_wd_set, "I", "Set BP Watchdog");

	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev), bp_list,
	    OID_AUTO, "wd_reset", CTLTYPE_INT | CTLFLAG_WR,
	    adapter, 0, ixgbe_bp_wd_reset, "S", "Bypass WD Reset");

	adapter->feat_en |= IXGBE_FEATURE_BYPASS;
} /* ixgbe_bypass_init */

