/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Counter interface enum functions
 * Copyright (C) 2018 William Breathitt Gray
 */
#ifndef _COUNTER_ENUM_H_
#define _COUNTER_ENUM_H_

#include <linux/types.h>

struct counter_device;
struct counter_signal;
struct counter_count;

ssize_t counter_signal_enum_read(struct counter_device *counter,
				 struct counter_signal *signal, void *priv,
				 char *buf);
ssize_t counter_signal_enum_write(struct counter_device *counter,
				  struct counter_signal *signal, void *priv,
				  const char *buf, size_t len);

ssize_t counter_signal_enum_available_read(struct counter_device *counter,
					   struct counter_signal *signal,
					   void *priv, char *buf);

ssize_t counter_count_enum_read(struct counter_device *counter,
				struct counter_count *count, void *priv,
				char *buf);
ssize_t counter_count_enum_write(struct counter_device *counter,
				 struct counter_count *count, void *priv,
				 const char *buf, size_t len);

ssize_t counter_count_enum_available_read(struct counter_device *counter,
					  struct counter_count *count,
					  void *priv, char *buf);

ssize_t counter_device_enum_read(struct counter_device *counter, void *priv,
				 char *buf);
ssize_t counter_device_enum_write(struct counter_device *counter, void *priv,
				  const char *buf, size_t len);

ssize_t counter_device_enum_available_read(struct counter_device *counter,
					   void *priv, char *buf);

#endif /* _COUNTER_ENUM_H_ */
