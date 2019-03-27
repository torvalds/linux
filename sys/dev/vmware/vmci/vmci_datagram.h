/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* Internal functions in the VMCI Simple Datagram API */

#ifndef _VMCI_DATAGRAM_H_
#define _VMCI_DATAGRAM_H_

#include "vmci_call_defs.h"

/* Datagram API for non-public use. */
int	vmci_datagram_dispatch(vmci_id context_id, struct vmci_datagram *dg);
int	vmci_datagram_invoke_guest_handler(struct vmci_datagram *dg);
int	vmci_datagram_get_priv_flags(struct vmci_handle handle,
	    vmci_privilege_flags *priv_flags);

/* Misc. */
void	vmci_datagram_sync(void);
bool	vmci_datagram_check_host_capabilities(void);

#endif /* !_VMCI_DATAGRAM_H_ */
