/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* Internal functions in the VMCI Doorbell API. */

#ifndef _VMCI_DOORBELL_H_
#define _VMCI_DOORBELL_H_

#include "vmci_defs.h"

int	vmci_doorbell_init(void);
void	vmci_doorbell_exit(void);
void	vmci_doorbell_hibernate(bool enter_hibernate);
void	vmci_doorbell_sync(void);

int	vmci_doorbell_host_context_notify(vmci_id src_CID,
	    struct vmci_handle handle);
int	vmci_doorbell_get_priv_flags(struct vmci_handle handle,
	    vmci_privilege_flags *priv_flags);

bool	vmci_register_notification_bitmap(PPN bitmap_PPN);
void	vmci_scan_notification_bitmap(uint8_t *bitmap);

#endif /* !_VMCI_DOORBELL_H_ */
