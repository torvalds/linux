/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 */

#ifndef __ALTMODE_H__
#define __ALTMODE_H__

#include <linux/types.h>

/**
 * struct altmode_client_data
 *	Uniquely define altmode client while registering with altmode framework.
 *
 * @svid:		Unique ID for Type-C Altmode client devices
 * @name:		Short descriptive name to identify client
 * @priv:		Pointer to client driver's internal top level structure
 * @callback:		Callback function to receive PMIC GLINK message data
 */
struct altmode_client_data {
	u16		svid;
	const char	*name;
	void		*priv;
	int		(*callback)(void *priv, void *data, size_t len);
};

struct altmode_client;

enum altmode_send_msg_type {
	ALTMODE_PAN_EN = 0x10,
	ALTMODE_PAN_ACK,
};

struct altmode_pan_ack_msg {
	u32 cmd_type;
	u8 port_index;
};

#if IS_ENABLED(CONFIG_QTI_ALTMODE_GLINK)

struct device;

int altmode_register_notifier(struct device *client_dev, void (*cb)(void *),
			      void *priv);
int altmode_deregister_notifier(struct device *client_dev, void *priv);
struct altmode_client *altmode_register_client(struct device *dev,
		const struct altmode_client_data *client_data);
int altmode_deregister_client(struct altmode_client *client);
int altmode_send_data(struct altmode_client *client, void *data, size_t len);

#else

static inline int altmode_register_notifier(struct device *client_dev,
					    void (*cb)(void *), void *priv)
{
	return -ENODEV;
}

static inline int altmode_deregister_notifier(struct device *client_dev,
					      void *priv)
{
	return -ENODEV;
}

static inline struct altmode_client *altmode_register_client(struct device *dev,
		const struct altmode_client_data *client_data)
{
	return ERR_PTR(-ENODEV);
}

static inline int altmode_deregister_client(struct altmode_client *client)
{
	return -ENODEV;
}

static inline int altmode_send_data(struct altmode_client *client, void *data,
				    size_t len)
{
	return -ENODEV;
}

#endif

#endif
