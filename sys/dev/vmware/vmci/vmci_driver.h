/*-
 * Copyright (c) 2018 VMware, Inc.
 *
 * SPDX-License-Identifier: (BSD-2-Clause OR GPL-2.0)
 *
 * $FreeBSD$
 */

/* VMCI driver interface. */

#ifndef _VMCI_DRIVER_H_
#define _VMCI_DRIVER_H_

#include <sys/types.h>
#include <sys/syslog.h>
#include <sys/systm.h>

#include "vmci_call_defs.h"
#include "vmci_kernel_if.h"

#ifndef VMCI_DEBUG_LOGGING
#define VMCI_LOG_DEBUG(_args, ...)
#else /* VMCI_DEBUG_LOGGING */
#define VMCI_LOG_DEBUG(_args, ...)					\
	log(LOG_DEBUG, _args, ##__VA_ARGS__)
#endif /* !VMCI_DEBUG_LOGGING */
#define VMCI_LOG_INFO(_args, ...)					\
	log(LOG_INFO, _args, ##__VA_ARGS__)
#define VMCI_LOG_WARNING(_args, ...)					\
	log(LOG_WARNING, _args, ##__VA_ARGS__)
#define VMCI_LOG_ERROR(_args, ...)					\
	log(LOG_ERR, _args, ##__VA_ARGS__)

int	vmci_components_init(void);
void	vmci_components_cleanup(void);
int	vmci_send_datagram(struct vmci_datagram *dg);

void	vmci_util_init(void);
void	vmci_util_exit(void);
bool	vmci_check_host_capabilities(void);
void	vmci_read_datagrams_from_port(vmci_io_handle io_handle,
	    vmci_io_port dg_in_port, uint8_t *dg_in_buffer,
	    size_t dg_in_buffer_size);

#endif /* !_VMCI_DRIVER_H_ */
