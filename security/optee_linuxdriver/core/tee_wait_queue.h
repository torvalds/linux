/*
 * Copyright (c) 2015, Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef TEE_WAIT_QUEUE_H
#define TEE_WAIT_QUEUE_H

#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/device.h>

struct tee_wait_queue_private {
	struct mutex mu;
	struct list_head db;
};

void tee_wait_queue_init(struct tee_wait_queue_private *priv);
void tee_wait_queue_exit(struct tee_wait_queue_private *priv);
void tee_wait_queue_sleep(struct device *dev,
			struct tee_wait_queue_private *priv, u32 key);
void tee_wait_queue_wakeup(struct device *dev,
			struct tee_wait_queue_private *priv, u32 key);

#endif /*TEE_WAIT_QUEUE_H*/
