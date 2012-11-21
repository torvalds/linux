/*
 * Linux WiMax
 * API for user space
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <linux-wimax@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *  - Initial implementation
 *
 *
 * This file declares the user/kernel protocol that is spoken over
 * Generic Netlink, as well as any type declaration that is to be used
 * by kernel and user space.
 *
 * It is intended for user space to clone it verbatim to use it as a
 * primary reference for definitions.
 *
 * Stuff intended for kernel usage as well as full protocol and stack
 * documentation is rooted in include/net/wimax.h.
 */

#ifndef __LINUX__WIMAX_H__
#define __LINUX__WIMAX_H__

#include <linux/types.h>

enum {
	/**
	 * Version of the interface (unsigned decimal, MMm, max 25.5)
	 * M - Major: change if removing or modifying an existing call.
	 * m - minor: change when adding a new call
	 */
	WIMAX_GNL_VERSION = 01,
	/* Generic NetLink attributes */
	WIMAX_GNL_ATTR_INVALID = 0x00,
	WIMAX_GNL_ATTR_MAX = 10,
};


/*
 * Generic NetLink operations
 *
 * Most of these map to an API call; _OP_ stands for operation, _RP_
 * for reply and _RE_ for report (aka: signal).
 */
enum {
	WIMAX_GNL_OP_MSG_FROM_USER,	/* User to kernel message */
	WIMAX_GNL_OP_MSG_TO_USER,	/* Kernel to user message */
	WIMAX_GNL_OP_RFKILL,	/* Run wimax_rfkill() */
	WIMAX_GNL_OP_RESET,	/* Run wimax_rfkill() */
	WIMAX_GNL_RE_STATE_CHANGE,	/* Report: status change */
	WIMAX_GNL_OP_STATE_GET,		/* Request for current state */
};


/* Message from user / to user */
enum {
	WIMAX_GNL_MSG_IFIDX = 1,
	WIMAX_GNL_MSG_PIPE_NAME,
	WIMAX_GNL_MSG_DATA,
};


/*
 * wimax_rfkill()
 *
 * The state of the radio (ON/OFF) is mapped to the rfkill subsystem's
 * switch state (DISABLED/ENABLED).
 */
enum wimax_rf_state {
	WIMAX_RF_OFF = 0,	/* Radio is off, rfkill on/enabled */
	WIMAX_RF_ON = 1,	/* Radio is on, rfkill off/disabled */
	WIMAX_RF_QUERY = 2,
};

/* Attributes */
enum {
	WIMAX_GNL_RFKILL_IFIDX = 1,
	WIMAX_GNL_RFKILL_STATE,
};


/* Attributes for wimax_reset() */
enum {
	WIMAX_GNL_RESET_IFIDX = 1,
};

/* Attributes for wimax_state_get() */
enum {
	WIMAX_GNL_STGET_IFIDX = 1,
};

/*
 * Attributes for the Report State Change
 *
 * For now we just have the old and new states; new attributes might
 * be added later on.
 */
enum {
	WIMAX_GNL_STCH_IFIDX = 1,
	WIMAX_GNL_STCH_STATE_OLD,
	WIMAX_GNL_STCH_STATE_NEW,
};


/**
 * enum wimax_st - The different states of a WiMAX device
 * @__WIMAX_ST_NULL: The device structure has been allocated and zeroed,
 *     but still wimax_dev_add() hasn't been called. There is no state.
 *
 * @WIMAX_ST_DOWN: The device has been registered with the WiMAX and
 *     networking stacks, but it is not initialized (normally that is
 *     done with 'ifconfig DEV up' [or equivalent], which can upload
 *     firmware and enable communications with the device).
 *     In this state, the device is powered down and using as less
 *     power as possible.
 *     This state is the default after a call to wimax_dev_add(). It
 *     is ok to have drivers move directly to %WIMAX_ST_UNINITIALIZED
 *     or %WIMAX_ST_RADIO_OFF in _probe() after the call to
 *     wimax_dev_add().
 *     It is recommended that the driver leaves this state when
 *     calling 'ifconfig DEV up' and enters it back on 'ifconfig DEV
 *     down'.
 *
 * @__WIMAX_ST_QUIESCING: The device is being torn down, so no API
 *     operations are allowed to proceed except the ones needed to
 *     complete the device clean up process.
 *
 * @WIMAX_ST_UNINITIALIZED: [optional] Communication with the device
 *     is setup, but the device still requires some configuration
 *     before being operational.
 *     Some WiMAX API calls might work.
 *
 * @WIMAX_ST_RADIO_OFF: The device is fully up; radio is off (wether
 *     by hardware or software switches).
 *     It is recommended to always leave the device in this state
 *     after initialization.
 *
 * @WIMAX_ST_READY: The device is fully up and radio is on.
 *
 * @WIMAX_ST_SCANNING: [optional] The device has been instructed to
 *     scan. In this state, the device cannot be actively connected to
 *     a network.
 *
 * @WIMAX_ST_CONNECTING: The device is connecting to a network. This
 *     state exists because in some devices, the connect process can
 *     include a number of negotiations between user space, kernel
 *     space and the device. User space needs to know what the device
 *     is doing. If the connect sequence in a device is atomic and
 *     fast, the device can transition directly to CONNECTED
 *
 * @WIMAX_ST_CONNECTED: The device is connected to a network.
 *
 * @__WIMAX_ST_INVALID: This is an invalid state used to mark the
 *     maximum numeric value of states.
 *
 * Description:
 *
 * Transitions from one state to another one are atomic and can only
 * be caused in kernel space with wimax_state_change(). To read the
 * state, use wimax_state_get().
 *
 * States starting with __ are internal and shall not be used or
 * referred to by drivers or userspace. They look ugly, but that's the
 * point -- if any use is made non-internal to the stack, it is easier
 * to catch on review.
 *
 * All API operations [with well defined exceptions] will take the
 * device mutex before starting and then check the state. If the state
 * is %__WIMAX_ST_NULL, %WIMAX_ST_DOWN, %WIMAX_ST_UNINITIALIZED or
 * %__WIMAX_ST_QUIESCING, it will drop the lock and quit with
 * -%EINVAL, -%ENOMEDIUM, -%ENOTCONN or -%ESHUTDOWN.
 *
 * The order of the definitions is important, so we can do numerical
 * comparisons (eg: < %WIMAX_ST_RADIO_OFF means the device is not ready
 * to operate).
 */
/*
 * The allowed state transitions are described in the table below
 * (states in rows can go to states in columns where there is an X):
 *
 *                                  UNINI   RADIO READY SCAN CONNEC CONNEC
 *             NULL DOWN QUIESCING TIALIZED  OFF        NING  TING   TED
 * NULL         -    x
 * DOWN              -      x        x       x
 * QUIESCING         x      -
 * UNINITIALIZED            x        -       x
 * RADIO_OFF                x                -     x
 * READY                    x                x     -     x     x      x
 * SCANNING                 x                x     x     -     x      x
 * CONNECTING               x                x     x     x     -      x
 * CONNECTED                x                x     x                  -
 *
 * This table not available in kernel-doc because the formatting messes it up.
 */
 enum wimax_st {
	__WIMAX_ST_NULL = 0,
	WIMAX_ST_DOWN,
	__WIMAX_ST_QUIESCING,
	WIMAX_ST_UNINITIALIZED,
	WIMAX_ST_RADIO_OFF,
	WIMAX_ST_READY,
	WIMAX_ST_SCANNING,
	WIMAX_ST_CONNECTING,
	WIMAX_ST_CONNECTED,
	__WIMAX_ST_INVALID			/* Always keep last */
};


#endif /* #ifndef __LINUX__WIMAX_H__ */
