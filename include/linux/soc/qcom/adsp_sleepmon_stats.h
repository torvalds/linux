/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/*
 * This header is for adsp sleepmon stats query API's in drivers.
 */
#ifndef __QCOM_ADSP_SLEEPMON_STATS_H__
#define __QCOM_ADSP_SLEEPMON_STATS_H__

/**
 * adsp_sleepmon_log_master_stats() - * API logs
 * adsp sleepmon stats to dmesg.
 * @arg1: u32, Set Bits to specify stats type
 *          Bit 0: Sysmon stats
 *              1: DSPPM stats
 *              2: Sleep stats (LPI and LPM)
 * @return: SUCCESS (0) if Query is successful
 *          FAILURE (Non-zero) if input param is invalid.
 */
int adsp_sleepmon_log_master_stats(u32 mask);

#endif /* __QCOM_ADSP_SLEEPMON_STATS_H__ */
