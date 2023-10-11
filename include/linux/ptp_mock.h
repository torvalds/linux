/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Mock-up PTP Hardware Clock driver for virtual network devices
 *
 * Copyright 2023 NXP
 */

#ifndef _PTP_MOCK_H_
#define _PTP_MOCK_H_

struct device;
struct mock_phc;

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK_MOCK)

struct mock_phc *mock_phc_create(struct device *dev);
void mock_phc_destroy(struct mock_phc *phc);
int mock_phc_index(struct mock_phc *phc);

#else

static inline struct mock_phc *mock_phc_create(struct device *dev)
{
	return NULL;
}

static inline void mock_phc_destroy(struct mock_phc *phc)
{
}

static inline int mock_phc_index(struct mock_phc *phc)
{
	return -1;
}

#endif

#endif /* _PTP_MOCK_H_ */
