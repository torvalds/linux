/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Exynos mailbox message.
 *
 * Copyright 2024 Linaro Ltd.
 */

#ifndef _LINUX_EXYNOS_MESSAGE_H_
#define _LINUX_EXYNOS_MESSAGE_H_

#define EXYNOS_MBOX_CHAN_TYPE_DOORBELL		0
#define EXYNOS_MBOX_CHAN_TYPE_DATA		1

struct exynos_mbox_msg {
	unsigned int chan_id;
	unsigned int chan_type;
};

#endif /* _LINUX_EXYNOS_MESSAGE_H_ */
