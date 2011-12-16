/*
 * Linux WiMAX
 * RF-kill framework integration
 *
 *
 * Copyright (C) 2008 Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * This integrates into the Linux Kernel rfkill susbystem so that the
 * drivers just have to do the bare minimal work, which is providing a
 * method to set the software RF-Kill switch and to report changes in
 * the software and hardware switch status.
 *
 * A non-polled generic rfkill device is embedded into the WiMAX
 * subsystem's representation of a device.
 *
 * FIXME: Need polled support? Let drivers provide a poll routine
 *	  and hand it to rfkill ops then?
 *
 * All device drivers have to do is after wimax_dev_init(), call
 * wimax_report_rfkill_hw() and wimax_report_rfkill_sw() to update
 * initial state and then every time it changes. See wimax.h:struct
 * wimax_dev for more information.
 *
 * ROADMAP
 *
 * wimax_gnl_doit_rfkill()      User space calling wimax_rfkill()
 *   wimax_rfkill()             Kernel calling wimax_rfkill()
 *     __wimax_rf_toggle_radio()
 *
 * wimax_rfkill_set_radio_block()  RF-Kill subsystem calling
 *   __wimax_rf_toggle_radio()
 *
 * __wimax_rf_toggle_radio()
 *   wimax_dev->op_rfkill_sw_toggle() Driver backend
 *   __wimax_state_change()
 *
 * wimax_report_rfkill_sw()     Driver reports state change
 *   __wimax_state_change()
 *
 * wimax_report_rfkill_hw()     Driver reports state change
 *   __wimax_state_change()
 *
 * wimax_rfkill_add()           Initialize/shutdown rfkill support
 * wimax_rfkill_rm()            [called by wimax_dev_add/rm()]
 */

#include <net/wimax.h>
#include <net/genetlink.h>
#include <linux/wimax.h>
#include <linux/security.h>
#include <linux/rfkill.h>
#include <linux/export.h>
#include "wimax-internal.h"

#define D_SUBMODULE op_rfkill
#include "debug-levels.h"

/**
 * wimax_report_rfkill_hw - Reports changes in the hardware RF switch
 *
 * @wimax_dev: WiMAX device descriptor
 *
 * @state: New state of the RF Kill switch. %WIMAX_RF_ON radio on,
 *     %WIMAX_RF_OFF radio off.
 *
 * When the device detects a change in the state of thehardware RF
 * switch, it must call this function to let the WiMAX kernel stack
 * know that the state has changed so it can be properly propagated.
 *
 * The WiMAX stack caches the state (the driver doesn't need to). As
 * well, as the change is propagated it will come back as a request to
 * change the software state to mirror the hardware state.
 *
 * If the device doesn't have a hardware kill switch, just report
 * it on initialization as always on (%WIMAX_RF_ON, radio on).
 */
void wimax_report_rfkill_hw(struct wimax_dev *wimax_dev,
			    enum wimax_rf_state state)
{
	int result;
	struct device *dev = wimax_dev_to_dev(wimax_dev);
	enum wimax_st wimax_state;

	d_fnstart(3, dev, "(wimax_dev %p state %u)\n", wimax_dev, state);
	BUG_ON(state == WIMAX_RF_QUERY);
	BUG_ON(state != WIMAX_RF_ON && state != WIMAX_RF_OFF);

	mutex_lock(&wimax_dev->mutex);
	result = wimax_dev_is_ready(wimax_dev);
	if (result < 0)
		goto error_not_ready;

	if (state != wimax_dev->rf_hw) {
		wimax_dev->rf_hw = state;
		if (wimax_dev->rf_hw == WIMAX_RF_ON &&
		    wimax_dev->rf_sw == WIMAX_RF_ON)
			wimax_state = WIMAX_ST_READY;
		else
			wimax_state = WIMAX_ST_RADIO_OFF;

		result = rfkill_set_hw_state(wimax_dev->rfkill,
					     state == WIMAX_RF_OFF);

		__wimax_state_change(wimax_dev, wimax_state);
	}
error_not_ready:
	mutex_unlock(&wimax_dev->mutex);
	d_fnend(3, dev, "(wimax_dev %p state %u) = void [%d]\n",
		wimax_dev, state, result);
}
EXPORT_SYMBOL_GPL(wimax_report_rfkill_hw);


/**
 * wimax_report_rfkill_sw - Reports changes in the software RF switch
 *
 * @wimax_dev: WiMAX device descriptor
 *
 * @state: New state of the RF kill switch. %WIMAX_RF_ON radio on,
 *     %WIMAX_RF_OFF radio off.
 *
 * Reports changes in the software RF switch state to the the WiMAX
 * stack.
 *
 * The main use is during initialization, so the driver can query the
 * device for its current software radio kill switch state and feed it
 * to the system.
 *
 * On the side, the device does not change the software state by
 * itself. In practice, this can happen, as the device might decide to
 * switch (in software) the radio off for different reasons.
 */
void wimax_report_rfkill_sw(struct wimax_dev *wimax_dev,
			    enum wimax_rf_state state)
{
	int result;
	struct device *dev = wimax_dev_to_dev(wimax_dev);
	enum wimax_st wimax_state;

	d_fnstart(3, dev, "(wimax_dev %p state %u)\n", wimax_dev, state);
	BUG_ON(state == WIMAX_RF_QUERY);
	BUG_ON(state != WIMAX_RF_ON && state != WIMAX_RF_OFF);

	mutex_lock(&wimax_dev->mutex);
	result = wimax_dev_is_ready(wimax_dev);
	if (result < 0)
		goto error_not_ready;

	if (state != wimax_dev->rf_sw) {
		wimax_dev->rf_sw = state;
		if (wimax_dev->rf_hw == WIMAX_RF_ON &&
		    wimax_dev->rf_sw == WIMAX_RF_ON)
			wimax_state = WIMAX_ST_READY;
		else
			wimax_state = WIMAX_ST_RADIO_OFF;
		__wimax_state_change(wimax_dev, wimax_state);
		rfkill_set_sw_state(wimax_dev->rfkill, state == WIMAX_RF_OFF);
	}
error_not_ready:
	mutex_unlock(&wimax_dev->mutex);
	d_fnend(3, dev, "(wimax_dev %p state %u) = void [%d]\n",
		wimax_dev, state, result);
}
EXPORT_SYMBOL_GPL(wimax_report_rfkill_sw);


/*
 * Callback for the RF Kill toggle operation
 *
 * This function is called by:
 *
 * - The rfkill subsystem when the RF-Kill key is pressed in the
 *   hardware and the driver notifies through
 *   wimax_report_rfkill_hw(). The rfkill subsystem ends up calling back
 *   here so the software RF Kill switch state is changed to reflect
 *   the hardware switch state.
 *
 * - When the user sets the state through sysfs' rfkill/state file
 *
 * - When the user calls wimax_rfkill().
 *
 * This call blocks!
 *
 * WARNING! When we call rfkill_unregister(), this will be called with
 * state 0!
 *
 * WARNING: wimax_dev must be locked
 */
static
int __wimax_rf_toggle_radio(struct wimax_dev *wimax_dev,
			    enum wimax_rf_state state)
{
	int result = 0;
	struct device *dev = wimax_dev_to_dev(wimax_dev);
	enum wimax_st wimax_state;

	might_sleep();
	d_fnstart(3, dev, "(wimax_dev %p state %u)\n", wimax_dev, state);
	if (wimax_dev->rf_sw == state)
		goto out_no_change;
	if (wimax_dev->op_rfkill_sw_toggle != NULL)
		result = wimax_dev->op_rfkill_sw_toggle(wimax_dev, state);
	else if (state == WIMAX_RF_OFF)	/* No op? can't turn off */
		result = -ENXIO;
	else				/* No op? can turn on */
		result = 0;		/* should never happen tho */
	if (result >= 0) {
		result = 0;
		wimax_dev->rf_sw = state;
		wimax_state = state == WIMAX_RF_ON ?
			WIMAX_ST_READY : WIMAX_ST_RADIO_OFF;
		__wimax_state_change(wimax_dev, wimax_state);
	}
out_no_change:
	d_fnend(3, dev, "(wimax_dev %p state %u) = %d\n",
		wimax_dev, state, result);
	return result;
}


/*
 * Translate from rfkill state to wimax state
 *
 * NOTE: Special state handling rules here
 *
 *     Just pretend the call didn't happen if we are in a state where
 *     we know for sure it cannot be handled (WIMAX_ST_DOWN or
 *     __WIMAX_ST_QUIESCING). rfkill() needs it to register and
 *     unregister, as it will run this path.
 *
 * NOTE: This call will block until the operation is completed.
 */
static int wimax_rfkill_set_radio_block(void *data, bool blocked)
{
	int result;
	struct wimax_dev *wimax_dev = data;
	struct device *dev = wimax_dev_to_dev(wimax_dev);
	enum wimax_rf_state rf_state;

	d_fnstart(3, dev, "(wimax_dev %p blocked %u)\n", wimax_dev, blocked);
	rf_state = WIMAX_RF_ON;
	if (blocked)
		rf_state = WIMAX_RF_OFF;
	mutex_lock(&wimax_dev->mutex);
	if (wimax_dev->state <= __WIMAX_ST_QUIESCING)
		result = 0;
	else
		result = __wimax_rf_toggle_radio(wimax_dev, rf_state);
	mutex_unlock(&wimax_dev->mutex);
	d_fnend(3, dev, "(wimax_dev %p blocked %u) = %d\n",
		wimax_dev, blocked, result);
	return result;
}

static const struct rfkill_ops wimax_rfkill_ops = {
	.set_block = wimax_rfkill_set_radio_block,
};

/**
 * wimax_rfkill - Set the software RF switch state for a WiMAX device
 *
 * @wimax_dev: WiMAX device descriptor
 *
 * @state: New RF state.
 *
 * Returns:
 *
 * >= 0 toggle state if ok, < 0 errno code on error. The toggle state
 * is returned as a bitmap, bit 0 being the hardware RF state, bit 1
 * the software RF state.
 *
 * 0 means disabled (%WIMAX_RF_ON, radio on), 1 means enabled radio
 * off (%WIMAX_RF_OFF).
 *
 * Description:
 *
 * Called by the user when he wants to request the WiMAX radio to be
 * switched on (%WIMAX_RF_ON) or off (%WIMAX_RF_OFF). With
 * %WIMAX_RF_QUERY, just the current state is returned.
 *
 * NOTE:
 *
 * This call will block until the operation is complete.
 */
int wimax_rfkill(struct wimax_dev *wimax_dev, enum wimax_rf_state state)
{
	int result;
	struct device *dev = wimax_dev_to_dev(wimax_dev);

	d_fnstart(3, dev, "(wimax_dev %p state %u)\n", wimax_dev, state);
	mutex_lock(&wimax_dev->mutex);
	result = wimax_dev_is_ready(wimax_dev);
	if (result < 0) {
		/* While initializing, < 1.4.3 wimax-tools versions use
		 * this call to check if the device is a valid WiMAX
		 * device; so we allow it to proceed always,
		 * considering the radios are all off. */
		if (result == -ENOMEDIUM && state == WIMAX_RF_QUERY)
			result = WIMAX_RF_OFF << 1 | WIMAX_RF_OFF;
		goto error_not_ready;
	}
	switch (state) {
	case WIMAX_RF_ON:
	case WIMAX_RF_OFF:
		result = __wimax_rf_toggle_radio(wimax_dev, state);
		if (result < 0)
			goto error;
		rfkill_set_sw_state(wimax_dev->rfkill, state == WIMAX_RF_OFF);
		break;
	case WIMAX_RF_QUERY:
		break;
	default:
		result = -EINVAL;
		goto error;
	}
	result = wimax_dev->rf_sw << 1 | wimax_dev->rf_hw;
error:
error_not_ready:
	mutex_unlock(&wimax_dev->mutex);
	d_fnend(3, dev, "(wimax_dev %p state %u) = %d\n",
		wimax_dev, state, result);
	return result;
}
EXPORT_SYMBOL(wimax_rfkill);


/*
 * Register a new WiMAX device's RF Kill support
 *
 * WARNING: wimax_dev->mutex must be unlocked
 */
int wimax_rfkill_add(struct wimax_dev *wimax_dev)
{
	int result;
	struct rfkill *rfkill;
	struct device *dev = wimax_dev_to_dev(wimax_dev);

	d_fnstart(3, dev, "(wimax_dev %p)\n", wimax_dev);
	/* Initialize RF Kill */
	result = -ENOMEM;
	rfkill = rfkill_alloc(wimax_dev->name, dev, RFKILL_TYPE_WIMAX,
			      &wimax_rfkill_ops, wimax_dev);
	if (rfkill == NULL)
		goto error_rfkill_allocate;

	d_printf(1, dev, "rfkill %p\n", rfkill);

	wimax_dev->rfkill = rfkill;

	rfkill_init_sw_state(rfkill, 1);
	result = rfkill_register(wimax_dev->rfkill);
	if (result < 0)
		goto error_rfkill_register;

	/* If there is no SW toggle op, SW RFKill is always on */
	if (wimax_dev->op_rfkill_sw_toggle == NULL)
		wimax_dev->rf_sw = WIMAX_RF_ON;

	d_fnend(3, dev, "(wimax_dev %p) = 0\n", wimax_dev);
	return 0;

error_rfkill_register:
	rfkill_destroy(wimax_dev->rfkill);
error_rfkill_allocate:
	d_fnend(3, dev, "(wimax_dev %p) = %d\n", wimax_dev, result);
	return result;
}


/*
 * Deregister a WiMAX device's RF Kill support
 *
 * Ick, we can't call rfkill_free() after rfkill_unregister()...oh
 * well.
 *
 * WARNING: wimax_dev->mutex must be unlocked
 */
void wimax_rfkill_rm(struct wimax_dev *wimax_dev)
{
	struct device *dev = wimax_dev_to_dev(wimax_dev);
	d_fnstart(3, dev, "(wimax_dev %p)\n", wimax_dev);
	rfkill_unregister(wimax_dev->rfkill);
	rfkill_destroy(wimax_dev->rfkill);
	d_fnend(3, dev, "(wimax_dev %p)\n", wimax_dev);
}


/*
 * Exporting to user space over generic netlink
 *
 * Parse the rfkill command from user space, return a combination
 * value that describe the states of the different toggles.
 *
 * Only one attribute: the new state requested (on, off or no change,
 * just query).
 */

static const struct nla_policy wimax_gnl_rfkill_policy[WIMAX_GNL_ATTR_MAX + 1] = {
	[WIMAX_GNL_RFKILL_IFIDX] = {
		.type = NLA_U32,
	},
	[WIMAX_GNL_RFKILL_STATE] = {
		.type = NLA_U32		/* enum wimax_rf_state */
	},
};


static
int wimax_gnl_doit_rfkill(struct sk_buff *skb, struct genl_info *info)
{
	int result, ifindex;
	struct wimax_dev *wimax_dev;
	struct device *dev;
	enum wimax_rf_state new_state;

	d_fnstart(3, NULL, "(skb %p info %p)\n", skb, info);
	result = -ENODEV;
	if (info->attrs[WIMAX_GNL_RFKILL_IFIDX] == NULL) {
		printk(KERN_ERR "WIMAX_GNL_OP_RFKILL: can't find IFIDX "
			"attribute\n");
		goto error_no_wimax_dev;
	}
	ifindex = nla_get_u32(info->attrs[WIMAX_GNL_RFKILL_IFIDX]);
	wimax_dev = wimax_dev_get_by_genl_info(info, ifindex);
	if (wimax_dev == NULL)
		goto error_no_wimax_dev;
	dev = wimax_dev_to_dev(wimax_dev);
	result = -EINVAL;
	if (info->attrs[WIMAX_GNL_RFKILL_STATE] == NULL) {
		dev_err(dev, "WIMAX_GNL_RFKILL: can't find RFKILL_STATE "
			"attribute\n");
		goto error_no_pid;
	}
	new_state = nla_get_u32(info->attrs[WIMAX_GNL_RFKILL_STATE]);

	/* Execute the operation and send the result back to user space */
	result = wimax_rfkill(wimax_dev, new_state);
error_no_pid:
	dev_put(wimax_dev->net_dev);
error_no_wimax_dev:
	d_fnend(3, NULL, "(skb %p info %p) = %d\n", skb, info, result);
	return result;
}


struct genl_ops wimax_gnl_rfkill = {
	.cmd = WIMAX_GNL_OP_RFKILL,
	.flags = GENL_ADMIN_PERM,
	.policy = wimax_gnl_rfkill_policy,
	.doit = wimax_gnl_doit_rfkill,
	.dumpit = NULL,
};

