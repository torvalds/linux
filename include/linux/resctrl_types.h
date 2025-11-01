/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Arm Ltd.
 * Based on arch/x86/kernel/cpu/resctrl/internal.h
 */

#ifndef __LINUX_RESCTRL_TYPES_H
#define __LINUX_RESCTRL_TYPES_H

#define MAX_MBA_BW			100u
#define MBM_OVERFLOW_INTERVAL		1000

/* Reads to Local DRAM Memory */
#define READS_TO_LOCAL_MEM		BIT(0)

/* Reads to Remote DRAM Memory */
#define READS_TO_REMOTE_MEM		BIT(1)

/* Non-Temporal Writes to Local Memory */
#define NON_TEMP_WRITE_TO_LOCAL_MEM	BIT(2)

/* Non-Temporal Writes to Remote Memory */
#define NON_TEMP_WRITE_TO_REMOTE_MEM	BIT(3)

/* Reads to Local Memory the system identifies as "Slow Memory" */
#define READS_TO_LOCAL_S_MEM		BIT(4)

/* Reads to Remote Memory the system identifies as "Slow Memory" */
#define READS_TO_REMOTE_S_MEM		BIT(5)

/* Dirty Victims to All Types of Memory */
#define DIRTY_VICTIMS_TO_ALL_MEM	BIT(6)

/* Max event bits supported */
#define MAX_EVT_CONFIG_BITS		GENMASK(6, 0)

/* Number of memory transactions that an MBM event can be configured with */
#define NUM_MBM_TRANSACTIONS		7

/* Event IDs */
enum resctrl_event_id {
	/* Must match value of first event below */
	QOS_FIRST_EVENT			= 0x01,

	/*
	 * These values match those used to program IA32_QM_EVTSEL before
	 * reading IA32_QM_CTR on RDT systems.
	 */
	QOS_L3_OCCUP_EVENT_ID		= 0x01,
	QOS_L3_MBM_TOTAL_EVENT_ID	= 0x02,
	QOS_L3_MBM_LOCAL_EVENT_ID	= 0x03,

	/* Must be the last */
	QOS_NUM_EVENTS,
};

#define QOS_NUM_L3_MBM_EVENTS	(QOS_L3_MBM_LOCAL_EVENT_ID - QOS_L3_MBM_TOTAL_EVENT_ID + 1)
#define MBM_STATE_IDX(evt)	((evt) - QOS_L3_MBM_TOTAL_EVENT_ID)

#endif /* __LINUX_RESCTRL_TYPES_H */
