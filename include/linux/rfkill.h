#ifndef __RFKILL_H
#define __RFKILL_H

/*
 * Copyright (C) 2006 - 2007 Ivo van Doorn
 * Copyright (C) 2007 Dmitry Torokhov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/leds.h>

/**
 * enum rfkill_type - type of rfkill switch.
 * RFKILL_TYPE_WLAN: switch is on a 802.11 wireless network device.
 * RFKILL_TYPE_BLUETOOTH: switch is on a bluetooth device.
 * RFKILL_TYPE_UWB: switch is on a ultra wideband device.
 * RFKILL_TYPE_WIMAX: switch is on a WiMAX device.
 * RFKILL_TYPE_WWAN: switch is on a wireless WAN device.
 */
enum rfkill_type {
	RFKILL_TYPE_WLAN ,
	RFKILL_TYPE_BLUETOOTH,
	RFKILL_TYPE_UWB,
	RFKILL_TYPE_WIMAX,
	RFKILL_TYPE_WWAN,
	RFKILL_TYPE_MAX,
};

enum rfkill_state {
	RFKILL_STATE_SOFT_BLOCKED = 0,	/* Radio output blocked */
	RFKILL_STATE_UNBLOCKED    = 1,	/* Radio output allowed */
	RFKILL_STATE_HARD_BLOCKED = 2,	/* Output blocked, non-overrideable */
	RFKILL_STATE_MAX,		/* marker for last valid state */
};

/*
 * These are DEPRECATED, drivers using them should be verified to
 * comply with the rfkill usage guidelines in Documentation/rfkill.txt
 * and then converted to use the new names for rfkill_state
 */
#define RFKILL_STATE_OFF RFKILL_STATE_SOFT_BLOCKED
#define RFKILL_STATE_ON  RFKILL_STATE_UNBLOCKED

/**
 * struct rfkill - rfkill control structure.
 * @name: Name of the switch.
 * @type: Radio type which the button controls, the value stored
 *	here should be a value from enum rfkill_type.
 * @state: State of the switch, "UNBLOCKED" means radio can operate.
 * @user_claim_unsupported: Whether the hardware supports exclusive
 *	RF-kill control by userspace. Set this before registering.
 * @user_claim: Set when the switch is controlled exlusively by userspace.
 * @mutex: Guards switch state transitions.  It serializes callbacks
 *	and also protects the state.
 * @data: Pointer to the RF button drivers private data which will be
 *	passed along when toggling radio state.
 * @toggle_radio(): Mandatory handler to control state of the radio.
 *	only RFKILL_STATE_SOFT_BLOCKED and RFKILL_STATE_UNBLOCKED are
 *	valid parameters.
 * @get_state(): handler to read current radio state from hardware,
 *      may be called from atomic context, should return 0 on success.
 *      Either this handler OR judicious use of rfkill_force_state() is
 *      MANDATORY for any driver capable of RFKILL_STATE_HARD_BLOCKED.
 * @led_trigger: A LED trigger for this button's LED.
 * @dev: Device structure integrating the switch into device tree.
 * @node: Used to place switch into list of all switches known to the
 *	the system.
 *
 * This structure represents a RF switch located on a network device.
 */
struct rfkill {
	const char *name;
	enum rfkill_type type;

	bool user_claim_unsupported;
	bool user_claim;

	/* the mutex serializes callbacks and also protects
	 * the state */
	struct mutex mutex;
	enum rfkill_state state;
	void *data;
	int (*toggle_radio)(void *data, enum rfkill_state state);
	int (*get_state)(void *data, enum rfkill_state *state);

#ifdef CONFIG_RFKILL_LEDS
	struct led_trigger led_trigger;
#endif

	struct device dev;
	struct list_head node;
};
#define to_rfkill(d)	container_of(d, struct rfkill, dev)

struct rfkill * __must_check rfkill_allocate(struct device *parent,
					     enum rfkill_type type);
void rfkill_free(struct rfkill *rfkill);
int __must_check rfkill_register(struct rfkill *rfkill);
void rfkill_unregister(struct rfkill *rfkill);

int rfkill_force_state(struct rfkill *rfkill, enum rfkill_state state);
int rfkill_set_default(enum rfkill_type type, enum rfkill_state state);

/**
 * rfkill_state_complement - return complementar state
 * @state: state to return the complement of
 *
 * Returns RFKILL_STATE_SOFT_BLOCKED if @state is RFKILL_STATE_UNBLOCKED,
 * returns RFKILL_STATE_UNBLOCKED otherwise.
 */
static inline enum rfkill_state rfkill_state_complement(enum rfkill_state state)
{
	return (state == RFKILL_STATE_UNBLOCKED) ?
		RFKILL_STATE_SOFT_BLOCKED : RFKILL_STATE_UNBLOCKED;
}

/**
 * rfkill_get_led_name - Get the LED trigger name for the button's LED.
 * This function might return a NULL pointer if registering of the
 * LED trigger failed.
 * Use this as "default_trigger" for the LED.
 */
static inline char *rfkill_get_led_name(struct rfkill *rfkill)
{
#ifdef CONFIG_RFKILL_LEDS
	return (char *)(rfkill->led_trigger.name);
#else
	return NULL;
#endif
}

/* rfkill notification chain */
#define RFKILL_STATE_CHANGED		0x0001	/* state of a normal rfkill
						   switch has changed */

int register_rfkill_notifier(struct notifier_block *nb);
int unregister_rfkill_notifier(struct notifier_block *nb);

#endif /* RFKILL_H */
