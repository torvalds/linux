/*
 * ng_hci_misc.h
 */

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) Maksim Yevmenkin <m_evmenkin@yahoo.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: ng_hci_misc.h,v 1.3 2003/09/08 18:57:51 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_HCI_MISC_H_
#define _NETGRAPH_HCI_MISC_H_

void              ng_hci_mtap                   (ng_hci_unit_p, struct mbuf *);
void              ng_hci_node_is_up             (node_p, hook_p, void *, int);
void              ng_hci_unit_clean             (ng_hci_unit_p, int);

ng_hci_neighbor_p ng_hci_new_neighbor           (ng_hci_unit_p);
void              ng_hci_free_neighbor          (ng_hci_neighbor_p);
void              ng_hci_flush_neighbor_cache   (ng_hci_unit_p);
ng_hci_neighbor_p ng_hci_get_neighbor           (ng_hci_unit_p, bdaddr_p, int);
int               ng_hci_neighbor_stale         (ng_hci_neighbor_p);

ng_hci_unit_con_p ng_hci_new_con                (ng_hci_unit_p, int);
void              ng_hci_free_con               (ng_hci_unit_con_p);
ng_hci_unit_con_p ng_hci_con_by_handle          (ng_hci_unit_p, int);
ng_hci_unit_con_p ng_hci_con_by_bdaddr          (ng_hci_unit_p, bdaddr_p, int);

int               ng_hci_command_timeout        (ng_hci_unit_p);
int               ng_hci_command_untimeout      (ng_hci_unit_p);
int               ng_hci_con_timeout            (ng_hci_unit_con_p);
int               ng_hci_con_untimeout          (ng_hci_unit_con_p);

#endif /* ndef _NETGRAPH_HCI_MISC_H_ */

