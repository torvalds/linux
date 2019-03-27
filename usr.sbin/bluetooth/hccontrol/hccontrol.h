/*-
 * hccontrol.h
 *
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001-2002 Maksim Yevmenkin <m_evmenkin@yahoo.com>
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
 * $Id: hccontrol.h,v 1.2 2003/05/19 17:29:29 max Exp $
 * $FreeBSD$
 */

#ifndef _HCCONTROL_H_
#define _HCCONTROL_H_

#define	OK			0	/* everything was OK */
#define	ERROR			1	/* could not execute command */
#define	FAILED			2	/* error was reported */
#define	USAGE			3	/* invalid parameters */

#define	MAX_NODE_NUM		16	/* max number of nodes */

struct hci_command {
	char const		*command;
	char const		*description;
	int			(*handler)(int, int, char **);
};

extern int			 timeout;
extern int			 verbose;
extern struct hci_command	 link_control_commands[];
extern struct hci_command	 link_policy_commands[];
extern struct hci_command	 host_controller_baseband_commands[];
extern struct hci_command	 info_commands[];
extern struct hci_command	 status_commands[];
extern struct hci_command	 node_commands[];
extern struct hci_command	 le_commands[];
 
int                hci_request         (int, int, char const *, int, char *, int *);
int                hci_simple_request  (int, int, char *, int *);
int                hci_send            (int, char const *, int);
int                hci_recv            (int, char *, int *);

char const *	hci_link2str        (int);
char const *	hci_pin2str         (int);
char const *	hci_scan2str        (int);
char const *	hci_encrypt2str     (int, int);
char const *	hci_coding2str      (int);
char const *	hci_vdata2str       (int);
char const *	hci_hmode2str       (int, char *, int);
char const *	hci_ver2str         (int);
char const *	hci_lmpver2str      (int);
char const *	hci_manufacturer2str(int);
char const *	hci_features2str    (uint8_t *, char *, int);
char const *	hci_cc2str          (int);
char const *	hci_con_state2str   (int);
char const *	hci_status2str      (int);
char const *	hci_bdaddr2str      (bdaddr_t const *);

#endif /* _HCCONTROL_H_ */

