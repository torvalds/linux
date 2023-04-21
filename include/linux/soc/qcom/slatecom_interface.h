/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef SLATECOM_INTERFACE_H
#define SLATECOM_INTERFACE_H

/*
 * slate_soft_reset() - soft reset Slate
 * Return 0 on success or -Ve on error
 */
int slate_soft_reset(void);

/*
 * is_twm_exit()
 * Return true if device is booting up on TWM exit.
 * value is auto cleared once read.
 */
bool is_twm_exit(void);

/*
 * is_slate_running()
 * Return true if slate is running.
 * value is auto cleared once read.
 */
bool is_slate_running(void);

/*
 * set_slate_dsp_state()
 * Set slate dsp state
 */
void set_slate_dsp_state(bool status);

/*
 * set_slate_bt_state()
 * Set slate bt state
 */
void set_slate_bt_state(bool status);

struct subsys_state_ops {
	void (*set_dsp_state)(bool status);
	void (*set_bt_state)(bool status);
};

void slatecom_state_init(void (*fn1)(bool), void (*fn2)(bool));


/*
 * Message header type - generic header structure
 */
struct msg_header_t {
	uint32_t opcode;
	uint32_t payload_size;
};

/**
 * Opcodes to be received on slate-control channel.
 */
enum WMSlateCtrlChnlOpcode {
	/*
	 * Command to slate to enter TWM mode
	 */
	GMI_MGR_ENTER_TWM = 1,

	/*
	 * Notification to slate about Modem Processor Sub System
	 * is down due to a subsystem reset.
	 */
	GMI_MGR_SSR_MPSS_DOWN_NOTIFICATION = 2,

	/*
	 * Notification to slate about Modem Processor Sub System
	 * being brought up after a subsystem reset.
	 */
	GMI_MGR_SSR_MPSS_UP_NOTIFICATION = 3,

	/*
	 * Notification to slate about ADSP Sub System
	 * is down due to a subsystem reset.
	 */
	GMI_MGR_SSR_ADSP_DOWN_INDICATION = 8,

	/*
	 * Notification to slate about Modem Processor
	 * Sub System being brought up after a subsystem reset.
	 */
	GMI_MGR_SSR_ADSP_UP_INDICATION = 9,

	/*
	 * Notification to MSM for generic wakeup in tracker mode
	 */
	GMI_MGR_WAKE_UP_NO_REASON = 10,

	/*
	 * Notification to Slate About Entry to Tracker-DS
	 */
	GMI_MGR_ENTER_TRACKER_DS = 11,

	/*
	 * Notification to Slate About Entry to Tracker-DS
	 */
	GMI_MGR_EXIT_TRACKER_DS = 12,

	/*
	 * Notification to Slate About Time-sync update
	 */
	GMI_MGR_TIME_SYNC_UPDATE = 13,	/* payload struct: time_sync_t*/

	/*
	 * Notification to Slate About Timeval UTC
	 */
	GMI_MGR_TIMEVAL_UTC = 14,		/* payload struct: timeval_utc_t*/

	/*
	 * Notification to Slate About Daylight saving time
	 */
	GMI_MGR_DST = 15,			/* payload struct: dst_t*/

	/*
	 * Notification to slate about WLAN boot init
	 */
	GMI_MGR_WLAN_BOOT_INIT = 16,

	/*
	 * Notification to slate about boot complete
	 */
	GMI_MGR_WLAN_BOOT_COMPLETE = 17,

	GMI_WLAN_5G_CONNECT = 18,

	GMI_WLAN_5G_DISCONNECT  = 19,

	/*
	 * DEBUG Opcodes
	 */
	GMI_MGR_ENABLE_QCLI = 91,		/* Enable QCLI */

	GMI_MGR_DISABLE_QCLI = 92,		/* Disable QCLI */

	GMI_MGR_ENABLE_PMIC_RTC = 93,		/* Enable PMIC RTC */

	GMI_MGR_DISABLE_PMIC_RTC = 94,		/* Disable PMIC RTC */
};

/*
 * Notification to slate about WLAN state
 */
#if IS_ENABLED(CONFIG_MSM_SLATECOM_INTERFACE)

int send_wlan_state(enum WMSlateCtrlChnlOpcode type);

#else
static inline int send_wlan_state(enum WMSlateCtrlChnlOpcode type)
{
	return 0;
}
#endif

#ifdef CONFIG_COMPAT
long compat_slate_com_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#else
#define compat_slate_com_ioctl NULL
#endif

#endif /* SLATECOM_INTERFACE_H */

