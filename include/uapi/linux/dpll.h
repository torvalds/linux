/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/dpll.yaml */
/* YNL-GEN uapi header */

#ifndef _UAPI_LINUX_DPLL_H
#define _UAPI_LINUX_DPLL_H

#define DPLL_FAMILY_NAME	"dpll"
#define DPLL_FAMILY_VERSION	1

/**
 * enum dpll_mode - working modes a dpll can support, differentiates if and how
 *   dpll selects one of its inputs to syntonize with it, valid values for
 *   DPLL_A_MODE attribute
 * @DPLL_MODE_MANUAL: input can be only selected by sending a request to dpll
 * @DPLL_MODE_AUTOMATIC: highest prio input pin auto selected by dpll
 */
enum dpll_mode {
	DPLL_MODE_MANUAL = 1,
	DPLL_MODE_AUTOMATIC,

	/* private: */
	__DPLL_MODE_MAX,
	DPLL_MODE_MAX = (__DPLL_MODE_MAX - 1)
};

/**
 * enum dpll_lock_status - provides information of dpll device lock status,
 *   valid values for DPLL_A_LOCK_STATUS attribute
 * @DPLL_LOCK_STATUS_UNLOCKED: dpll was not yet locked to any valid input (or
 *   forced by setting DPLL_A_MODE to DPLL_MODE_DETACHED)
 * @DPLL_LOCK_STATUS_LOCKED: dpll is locked to a valid signal, but no holdover
 *   available
 * @DPLL_LOCK_STATUS_LOCKED_HO_ACQ: dpll is locked and holdover acquired
 * @DPLL_LOCK_STATUS_HOLDOVER: dpll is in holdover state - lost a valid lock or
 *   was forced by disconnecting all the pins (latter possible only when dpll
 *   lock-state was already DPLL_LOCK_STATUS_LOCKED_HO_ACQ, if dpll lock-state
 *   was not DPLL_LOCK_STATUS_LOCKED_HO_ACQ, the dpll's lock-state shall remain
 *   DPLL_LOCK_STATUS_UNLOCKED)
 */
enum dpll_lock_status {
	DPLL_LOCK_STATUS_UNLOCKED = 1,
	DPLL_LOCK_STATUS_LOCKED,
	DPLL_LOCK_STATUS_LOCKED_HO_ACQ,
	DPLL_LOCK_STATUS_HOLDOVER,

	/* private: */
	__DPLL_LOCK_STATUS_MAX,
	DPLL_LOCK_STATUS_MAX = (__DPLL_LOCK_STATUS_MAX - 1)
};

/**
 * enum dpll_lock_status_error - if previous status change was done due to a
 *   failure, this provides information of dpll device lock status error. Valid
 *   values for DPLL_A_LOCK_STATUS_ERROR attribute
 * @DPLL_LOCK_STATUS_ERROR_NONE: dpll device lock status was changed without
 *   any error
 * @DPLL_LOCK_STATUS_ERROR_UNDEFINED: dpll device lock status was changed due
 *   to undefined error. Driver fills this value up in case it is not able to
 *   obtain suitable exact error type.
 * @DPLL_LOCK_STATUS_ERROR_MEDIA_DOWN: dpll device lock status was changed
 *   because of associated media got down. This may happen for example if dpll
 *   device was previously locked on an input pin of type
 *   PIN_TYPE_SYNCE_ETH_PORT.
 * @DPLL_LOCK_STATUS_ERROR_FRACTIONAL_FREQUENCY_OFFSET_TOO_HIGH: the FFO
 *   (Fractional Frequency Offset) between the RX and TX symbol rate on the
 *   media got too high. This may happen for example if dpll device was
 *   previously locked on an input pin of type PIN_TYPE_SYNCE_ETH_PORT.
 */
enum dpll_lock_status_error {
	DPLL_LOCK_STATUS_ERROR_NONE = 1,
	DPLL_LOCK_STATUS_ERROR_UNDEFINED,
	DPLL_LOCK_STATUS_ERROR_MEDIA_DOWN,
	DPLL_LOCK_STATUS_ERROR_FRACTIONAL_FREQUENCY_OFFSET_TOO_HIGH,

	/* private: */
	__DPLL_LOCK_STATUS_ERROR_MAX,
	DPLL_LOCK_STATUS_ERROR_MAX = (__DPLL_LOCK_STATUS_ERROR_MAX - 1)
};

#define DPLL_TEMP_DIVIDER	1000

/**
 * enum dpll_type - type of dpll, valid values for DPLL_A_TYPE attribute
 * @DPLL_TYPE_PPS: dpll produces Pulse-Per-Second signal
 * @DPLL_TYPE_EEC: dpll drives the Ethernet Equipment Clock
 */
enum dpll_type {
	DPLL_TYPE_PPS = 1,
	DPLL_TYPE_EEC,

	/* private: */
	__DPLL_TYPE_MAX,
	DPLL_TYPE_MAX = (__DPLL_TYPE_MAX - 1)
};

/**
 * enum dpll_pin_type - defines possible types of a pin, valid values for
 *   DPLL_A_PIN_TYPE attribute
 * @DPLL_PIN_TYPE_MUX: aggregates another layer of selectable pins
 * @DPLL_PIN_TYPE_EXT: external input
 * @DPLL_PIN_TYPE_SYNCE_ETH_PORT: ethernet port PHY's recovered clock
 * @DPLL_PIN_TYPE_INT_OSCILLATOR: device internal oscillator
 * @DPLL_PIN_TYPE_GNSS: GNSS recovered clock
 */
enum dpll_pin_type {
	DPLL_PIN_TYPE_MUX = 1,
	DPLL_PIN_TYPE_EXT,
	DPLL_PIN_TYPE_SYNCE_ETH_PORT,
	DPLL_PIN_TYPE_INT_OSCILLATOR,
	DPLL_PIN_TYPE_GNSS,

	/* private: */
	__DPLL_PIN_TYPE_MAX,
	DPLL_PIN_TYPE_MAX = (__DPLL_PIN_TYPE_MAX - 1)
};

/**
 * enum dpll_pin_direction - defines possible direction of a pin, valid values
 *   for DPLL_A_PIN_DIRECTION attribute
 * @DPLL_PIN_DIRECTION_INPUT: pin used as a input of a signal
 * @DPLL_PIN_DIRECTION_OUTPUT: pin used to output the signal
 */
enum dpll_pin_direction {
	DPLL_PIN_DIRECTION_INPUT = 1,
	DPLL_PIN_DIRECTION_OUTPUT,

	/* private: */
	__DPLL_PIN_DIRECTION_MAX,
	DPLL_PIN_DIRECTION_MAX = (__DPLL_PIN_DIRECTION_MAX - 1)
};

#define DPLL_PIN_FREQUENCY_1_HZ		1
#define DPLL_PIN_FREQUENCY_10_KHZ	10000
#define DPLL_PIN_FREQUENCY_77_5_KHZ	77500
#define DPLL_PIN_FREQUENCY_10_MHZ	10000000

/**
 * enum dpll_pin_state - defines possible states of a pin, valid values for
 *   DPLL_A_PIN_STATE attribute
 * @DPLL_PIN_STATE_CONNECTED: pin connected, active input of phase locked loop
 * @DPLL_PIN_STATE_DISCONNECTED: pin disconnected, not considered as a valid
 *   input
 * @DPLL_PIN_STATE_SELECTABLE: pin enabled for automatic input selection
 */
enum dpll_pin_state {
	DPLL_PIN_STATE_CONNECTED = 1,
	DPLL_PIN_STATE_DISCONNECTED,
	DPLL_PIN_STATE_SELECTABLE,

	/* private: */
	__DPLL_PIN_STATE_MAX,
	DPLL_PIN_STATE_MAX = (__DPLL_PIN_STATE_MAX - 1)
};

/**
 * enum dpll_pin_capabilities - defines possible capabilities of a pin, valid
 *   flags on DPLL_A_PIN_CAPABILITIES attribute
 * @DPLL_PIN_CAPABILITIES_DIRECTION_CAN_CHANGE: pin direction can be changed
 * @DPLL_PIN_CAPABILITIES_PRIORITY_CAN_CHANGE: pin priority can be changed
 * @DPLL_PIN_CAPABILITIES_STATE_CAN_CHANGE: pin state can be changed
 */
enum dpll_pin_capabilities {
	DPLL_PIN_CAPABILITIES_DIRECTION_CAN_CHANGE = 1,
	DPLL_PIN_CAPABILITIES_PRIORITY_CAN_CHANGE = 2,
	DPLL_PIN_CAPABILITIES_STATE_CAN_CHANGE = 4,
};

#define DPLL_PHASE_OFFSET_DIVIDER	1000

enum dpll_a {
	DPLL_A_ID = 1,
	DPLL_A_MODULE_NAME,
	DPLL_A_PAD,
	DPLL_A_CLOCK_ID,
	DPLL_A_MODE,
	DPLL_A_MODE_SUPPORTED,
	DPLL_A_LOCK_STATUS,
	DPLL_A_TEMP,
	DPLL_A_TYPE,
	DPLL_A_LOCK_STATUS_ERROR,

	__DPLL_A_MAX,
	DPLL_A_MAX = (__DPLL_A_MAX - 1)
};

enum dpll_a_pin {
	DPLL_A_PIN_ID = 1,
	DPLL_A_PIN_PARENT_ID,
	DPLL_A_PIN_MODULE_NAME,
	DPLL_A_PIN_PAD,
	DPLL_A_PIN_CLOCK_ID,
	DPLL_A_PIN_BOARD_LABEL,
	DPLL_A_PIN_PANEL_LABEL,
	DPLL_A_PIN_PACKAGE_LABEL,
	DPLL_A_PIN_TYPE,
	DPLL_A_PIN_DIRECTION,
	DPLL_A_PIN_FREQUENCY,
	DPLL_A_PIN_FREQUENCY_SUPPORTED,
	DPLL_A_PIN_FREQUENCY_MIN,
	DPLL_A_PIN_FREQUENCY_MAX,
	DPLL_A_PIN_PRIO,
	DPLL_A_PIN_STATE,
	DPLL_A_PIN_CAPABILITIES,
	DPLL_A_PIN_PARENT_DEVICE,
	DPLL_A_PIN_PARENT_PIN,
	DPLL_A_PIN_PHASE_ADJUST_MIN,
	DPLL_A_PIN_PHASE_ADJUST_MAX,
	DPLL_A_PIN_PHASE_ADJUST,
	DPLL_A_PIN_PHASE_OFFSET,
	DPLL_A_PIN_FRACTIONAL_FREQUENCY_OFFSET,
	DPLL_A_PIN_ESYNC_FREQUENCY,
	DPLL_A_PIN_ESYNC_FREQUENCY_SUPPORTED,
	DPLL_A_PIN_ESYNC_PULSE,

	__DPLL_A_PIN_MAX,
	DPLL_A_PIN_MAX = (__DPLL_A_PIN_MAX - 1)
};

enum dpll_cmd {
	DPLL_CMD_DEVICE_ID_GET = 1,
	DPLL_CMD_DEVICE_GET,
	DPLL_CMD_DEVICE_SET,
	DPLL_CMD_DEVICE_CREATE_NTF,
	DPLL_CMD_DEVICE_DELETE_NTF,
	DPLL_CMD_DEVICE_CHANGE_NTF,
	DPLL_CMD_PIN_ID_GET,
	DPLL_CMD_PIN_GET,
	DPLL_CMD_PIN_SET,
	DPLL_CMD_PIN_CREATE_NTF,
	DPLL_CMD_PIN_DELETE_NTF,
	DPLL_CMD_PIN_CHANGE_NTF,

	__DPLL_CMD_MAX,
	DPLL_CMD_MAX = (__DPLL_CMD_MAX - 1)
};

#define DPLL_MCGRP_MONITOR	"monitor"

#endif /* _UAPI_LINUX_DPLL_H */
