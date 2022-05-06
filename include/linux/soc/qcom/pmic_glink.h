/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _PMIC_GLINK_H
#define _PMIC_GLINK_H

#include <linux/types.h>

struct pmic_glink_client;
struct device;

enum pmic_glink_state {
	PMIC_GLINK_STATE_DOWN,
	PMIC_GLINK_STATE_UP,
};

/**
 * struct pmic_glink_client_data - pmic_glink client data
 * @name:	Client name
 * @id:		Unique id for client for communication
 * @priv:	private data for client
 * @msg_cb:	callback function for client to receive the messages that
 *		are intended to be delivered to it over PMIC Glink
 * @state_cb:	callback function to notify pmic glink state in the event of
 *		a subsystem restart (SSR) or a protection domain restart (PDR)
 */
struct pmic_glink_client_data {
	const char	*name;
	u32		id;
	void		*priv;
	int		(*msg_cb)(void *priv, void *data, size_t len);
	void		(*state_cb)(void *priv, enum pmic_glink_state state);
};

/**
 * struct pmic_glink_hdr - PMIC Glink message header
 * @owner:	message owner for a client
 * @type:	message type
 * @opcode:	message opcode
 */
struct pmic_glink_hdr {
	u32 owner;
	u32 type;
	u32 opcode;
};

#if IS_ENABLED(CONFIG_QTI_PMIC_GLINK)
struct pmic_glink_client *pmic_glink_register_client(struct device *dev,
			const struct pmic_glink_client_data *client_data);
int pmic_glink_unregister_client(struct pmic_glink_client *client);
int pmic_glink_write(struct pmic_glink_client *client, void *data,
			size_t len);
#else
static inline struct pmic_glink_client *pmic_glink_register_client(
			struct device *dev,
			const struct pmic_glink_client_data *client_data)
{
	return ERR_PTR(-ENODEV);
}

static inline int pmic_glink_unregister_client(struct pmic_glink_client *client)
{
	return -ENODEV;
}

static inline int pmic_glink_write(struct pmic_glink_client *client, void *data,
				size_t len)
{
	return -ENODEV;
}
#endif

#endif
