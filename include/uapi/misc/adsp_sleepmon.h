/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_ADSPSLEEPMON_H__
#define __QCOM_ADSPSLEEPMON_H__

#include <linux/types.h>

/** Device name for ADSP Sleep monitor */
#define ADSPSLEEPMON_DEVICE_NAME             "/dev/msm_adsp_sleepmon"
/** IOCTL for intimating audio activity */
#define ADSPSLEEPMON_IOCTL_AUDIO_ACTIVITY    _IOWR('R', 1, struct adspsleepmon_ioctl_audio)
/** IOCTL to runtime disable or re-enable panic on ADSP activity anomaly detection */
#define ADSPSLEEPMON_IOCTL_CONFIGURE_PANIC   _IOWR('R', 2, struct adspsleepmon_ioctl_panic)
/** Version used in Audio activity IOCTL */
#define ADSPSLEEPMON_IOCTL_AUDIO_VER_1       1
/** Version used in Panic config IOCTL */
#define ADSPSLEEPMON_IOCTL_CONFIG_PANIC_VER_1   1
/** Reserved fields in the Audio activity IOCTL structure */
#define ADSPSLEEPMON_IOCTL_AUDIO_NUM_RES     3

enum adspsleepmon_ioctl_audio_cmd {
	ADSPSLEEPMON_AUDIO_ACTIVITY_START = 1,
    /**< Activity start of a non-LPI use case */
	ADSPSLEEPMON_AUDIO_ACTIVITY_STOP,
    /**< Activity stop of a non-LPI use case */
	ADSPSLEEPMON_AUDIO_ACTIVITY_LPI_START,
    /**< Activity start of a LPI use case */
	ADSPSLEEPMON_AUDIO_ACTIVITY_LPI_STOP,
    /**< Activity stop of a LPI use case */
	ADSPSLEEPMON_AUDIO_ACTIVITY_RESET,
    /**< Notify no ongoing activity (reset activity trackers) */
	ADSPSLEEPMON_AUDIO_ACTIVITY_MAX,
	/**< Max check for Audio ioctl audio command*/
};

enum adspsleepmon_ioctl_panic_cmd {
	ADSPSLEEPMON_DISABLE_PANIC_LPM = 1,
    /**< Disable panic on detecting ADSP LPM anomaly */
	ADSPSLEEPMON_DISABLE_PANIC_LPI,
    /**< Disable panic on detecting ADSP LPI anomaly */
	ADSPSLEEPMON_RESET_PANIC_LPM,
    /**< Reset panic on detecting ADSP LPM anomaly to default */
	ADSPSLEEPMON_RESET_PANIC_LPI,
    /**< Reset panic on detecting ADSP LPI anomaly to default */
	ADSPSLEEPMON_RESET_PANIC_MAX,
	/**< Max check for Audio ioctl panic command*/
};

/** @struct adspsleepmon_ioctl_audio
 *  Structure to be passed in Audio activity IOCTL
 */
struct adspsleepmon_ioctl_audio {
	__u32 version;
    /**< Version of the interface */
	__u32 command;
    /**< One of the supported commands from adspsleepmon_ioctl_audio_cmd */
	__u32 reserved[ADSPSLEEPMON_IOCTL_AUDIO_NUM_RES];
    /**< Reserved fields for future expansion */
};

struct adspsleepmon_ioctl_panic {
	__u32 version;
    /**< version of the interface */
	__u32 command;
    /**< One of the supported commands from adspsleepmon_ioctl_panic_cmd */
};
#endif /* __QCOM_ADSPSLEEPMON_H__ */
