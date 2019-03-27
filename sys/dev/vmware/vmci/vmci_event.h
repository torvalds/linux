/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* Event code for the vmci guest driver. */

#ifndef _VMCI_EVENT_H_
#define _VMCI_EVENT_H_

#include "vmci_call_defs.h"
#include "vmci_defs.h"

int	vmci_event_init(void);
void	vmci_event_exit(void);
void	vmci_event_sync(void);
int	vmci_event_dispatch(struct vmci_datagram *msg);
bool	vmci_event_check_host_capabilities(void);

#endif /* !_VMCI_EVENT_H_ */
