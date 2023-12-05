/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Regulator uapi header
 *
 * Author: Naresh Solanki <Naresh.Solanki@9elements.com>
 */

#ifndef _UAPI_REGULATOR_H
#define _UAPI_REGULATOR_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/*
 * Regulator notifier events.
 *
 * UNDER_VOLTAGE  Regulator output is under voltage.
 * OVER_CURRENT   Regulator output current is too high.
 * REGULATION_OUT Regulator output is out of regulation.
 * FAIL           Regulator output has failed.
 * OVER_TEMP      Regulator over temp.
 * FORCE_DISABLE  Regulator forcibly shut down by software.
 * VOLTAGE_CHANGE Regulator voltage changed.
 *                Data passed is old voltage cast to (void *).
 * DISABLE        Regulator was disabled.
 * PRE_VOLTAGE_CHANGE   Regulator is about to have voltage changed.
 *                      Data passed is "struct pre_voltage_change_data"
 * ABORT_VOLTAGE_CHANGE Regulator voltage change failed for some reason.
 *                      Data passed is old voltage cast to (void *).
 * PRE_DISABLE    Regulator is about to be disabled
 * ABORT_DISABLE  Regulator disable failed for some reason
 *
 * NOTE: These events can be OR'ed together when passed into handler.
 */

#define REGULATOR_EVENT_UNDER_VOLTAGE		0x01
#define REGULATOR_EVENT_OVER_CURRENT		0x02
#define REGULATOR_EVENT_REGULATION_OUT		0x04
#define REGULATOR_EVENT_FAIL			0x08
#define REGULATOR_EVENT_OVER_TEMP		0x10
#define REGULATOR_EVENT_FORCE_DISABLE		0x20
#define REGULATOR_EVENT_VOLTAGE_CHANGE		0x40
#define REGULATOR_EVENT_DISABLE			0x80
#define REGULATOR_EVENT_PRE_VOLTAGE_CHANGE	0x100
#define REGULATOR_EVENT_ABORT_VOLTAGE_CHANGE	0x200
#define REGULATOR_EVENT_PRE_DISABLE		0x400
#define REGULATOR_EVENT_ABORT_DISABLE		0x800
#define REGULATOR_EVENT_ENABLE			0x1000
/*
 * Following notifications should be emitted only if detected condition
 * is such that the HW is likely to still be working but consumers should
 * take a recovery action to prevent problems esacalating into errors.
 */
#define REGULATOR_EVENT_UNDER_VOLTAGE_WARN	0x2000
#define REGULATOR_EVENT_OVER_CURRENT_WARN	0x4000
#define REGULATOR_EVENT_OVER_VOLTAGE_WARN	0x8000
#define REGULATOR_EVENT_OVER_TEMP_WARN		0x10000
#define REGULATOR_EVENT_WARN_MASK		0x1E000

struct reg_genl_event {
	char reg_name[32];
	uint64_t event;
};

/* attributes of reg_genl_family */
enum {
	REG_GENL_ATTR_UNSPEC,
	REG_GENL_ATTR_EVENT,	/* reg event info needed by user space */
	__REG_GENL_ATTR_MAX,
};

#define REG_GENL_ATTR_MAX (__REG_GENL_ATTR_MAX - 1)

/* commands supported by the reg_genl_family */
enum {
	REG_GENL_CMD_UNSPEC,
	REG_GENL_CMD_EVENT,	/* kernel->user notifications for reg events */
	__REG_GENL_CMD_MAX,
};

#define REG_GENL_CMD_MAX (__REG_GENL_CMD_MAX - 1)

#define REG_GENL_FAMILY_NAME		"reg_event"
#define REG_GENL_VERSION		0x01
#define REG_GENL_MCAST_GROUP_NAME	"reg_mc_group"

#endif /* _UAPI_REGULATOR_H */
