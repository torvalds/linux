/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __GH_DBL_H
#define __GH_DBL_H

#include "gh_common.h"

typedef void (*dbl_rx_cb_t)(int irq, void *priv_data);

enum gh_dbl_label {
	GH_DBL_TUI_LABEL,
	GH_DBL_TUI_NEURON_BLK0,
	GH_DBL_TUI_NEURON_BLK1,
	GH_DBL_TUI_QRTR,
	GH_DBL_TEST_TUIVM,
	GH_DBL_TEST_OEMVM,
	GH_DBL_HW_FENCE,
	GH_DBL_TUI_DDUMP,
	GH_DBL_OEMVM_QRTR,
	GH_DBL_VM_PANIC_NOTIFY,
	GH_DBL_LABEL_MAX
};

/* Possible flags to pass for send, set_mask, read, reset */
#define GH_DBL_NONBLOCK		BIT(32)

#if IS_ENABLED(CONFIG_GH_DBL)
void *gh_dbl_tx_register(enum gh_dbl_label label);
void *gh_dbl_rx_register(enum gh_dbl_label label, dbl_rx_cb_t rx_cb,
			 void *priv);

int gh_dbl_tx_unregister(void *dbl_client_desc);
int gh_dbl_rx_unregister(void *dbl_client_desc);

int gh_dbl_send(void *dbl_client_desc, uint64_t *newflags,
		const unsigned long flags);
int gh_dbl_set_mask(void *dbl_client_desc, gh_dbl_flags_t enable_mask,
		    gh_dbl_flags_t ack_mask, const unsigned long flags);
int gh_dbl_read_and_clean(void *dbl_client_desc, gh_dbl_flags_t *clear_flags,
			  const unsigned long flags);
int gh_dbl_reset(void *dbl_client_desc, const unsigned long flags);
int gh_dbl_populate_cap_info(enum gh_dbl_label label, u64 cap_id,
						int direction, int rx_irq);
int gh_dbl_reset_cap_info(enum gh_dbl_label label, int direction, int *irq);
#else
static inline void *gh_dbl_tx_register(enum gh_dbl_label label)
{
	return ERR_PTR(-ENODEV);
}

static inline void *gh_dbl_rx_register(enum gh_dbl_label label,
			 dbl_rx_cb_t rx_cb,
			 void *priv)
{
	return ERR_PTR(-ENODEV);
}

static inline int gh_dbl_tx_unregister(void *dbl_client_desc)
{
	return -EINVAL;
}

static inline int gh_dbl_rx_unregister(void *dbl_client_desc)
{
	return -EINVAL;
}

static inline int gh_dbl_send(void *dbl_client_desc, uint64_t *newflags,
			      const unsigned long flags)
{
	return -EINVAL;
}

static inline int gh_dbl_set_mask(void *dbl_client_desc,
		    gh_dbl_flags_t enable_mask,
		    gh_dbl_flags_t ack_mask,
		    const unsigned long flags)
{
	return -EINVAL;
}

static inline int gh_dbl_read_and_clean(void *dbl_client_desc,
					gh_dbl_flags_t *clear_flags,
					const unsigned long flags)
{
	return -EINVAL;
}

static inline int gh_dbl_reset(void *dbl_client_desc, const unsigned long flags)
{
	return -EINVAL;
}

static inline int gh_dbl_populate_cap_info(enum gh_dbl_label label, u64 cap_id,
						int direction, int rx_irq)
{
	return -EINVAL;
}

static inline
int gh_dbl_reset_cap_info(enum gh_dbl_label label, int direction, int *irq)
{
	return -EINVAL;
}
#endif
#endif
