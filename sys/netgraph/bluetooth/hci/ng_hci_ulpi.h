/*
 * ng_hci_ulpi.h
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
 * $Id: ng_hci_ulpi.h,v 1.2 2003/04/26 22:35:21 max Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_HCI_ULPI_H_
#define _NETGRAPH_HCI_ULPI_H_

/*
 * LP_xxx event handlers
 */

int  ng_hci_lp_con_req                   (ng_hci_unit_p, item_p, hook_p);
int  ng_hci_lp_discon_req                (ng_hci_unit_p, item_p, hook_p);
int  ng_hci_lp_con_cfm                   (ng_hci_unit_con_p, int);
int  ng_hci_lp_con_ind                   (ng_hci_unit_con_p, u_int8_t *);
int  ng_hci_lp_con_rsp                   (ng_hci_unit_p, item_p, hook_p);
int  ng_hci_lp_discon_ind                (ng_hci_unit_con_p, int);
int  ng_hci_lp_qos_req                   (ng_hci_unit_p, item_p, hook_p);
int  ng_hci_lp_qos_cfm                   (ng_hci_unit_con_p, int);
int  ng_hci_lp_qos_ind                   (ng_hci_unit_con_p);
int  ng_hci_lp_enc_change                (ng_hci_unit_con_p, int);

void ng_hci_process_con_timeout          (node_p, hook_p, void *, int);

#endif /* ndef _NETGRAPH_HCI_ULPI_H_ */

