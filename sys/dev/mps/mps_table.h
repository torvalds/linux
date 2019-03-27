/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Yahoo! Inc.
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _MPS_TABLE_H
#define _MPS_TABLE_H

struct mps_table_lookup {
	char	*string;
	u_int	code;
};

char * mps_describe_table(struct mps_table_lookup *table, u_int code);
void mps_describe_devinfo(uint32_t devinfo, char *string, int len);

extern struct mps_table_lookup mps_event_names[];
extern struct mps_table_lookup mps_phystatus_names[];
extern struct mps_table_lookup mps_linkrate_names[];
extern struct mps_table_lookup mps_iocstatus_string[];
extern struct mps_table_lookup mps_scsi_status_string[];
extern struct mps_table_lookup mps_scsi_taskmgmt_string[];

void mps_print_iocfacts(struct mps_softc *, MPI2_IOC_FACTS_REPLY *);
void mps_print_portfacts(struct mps_softc *, MPI2_PORT_FACTS_REPLY *);
void mps_print_evt_generic(struct mps_softc *, MPI2_EVENT_NOTIFICATION_REPLY *);
void mps_print_sasdev0(struct mps_softc *, MPI2_CONFIG_PAGE_SAS_DEV_0 *);
void mps_print_evt_sas(struct mps_softc *, MPI2_EVENT_NOTIFICATION_REPLY *);
void mps_print_expander1(struct mps_softc *, MPI2_CONFIG_PAGE_EXPANDER_1 *);
void mps_print_sasphy0(struct mps_softc *, MPI2_CONFIG_PAGE_SAS_PHY_0 *);
void mps_print_sgl(struct mps_softc *, struct mps_command *, int);
void mps_print_scsiio_cmd(struct mps_softc *, struct mps_command *);

#define MPS_DPRINT_PAGE(sc, level, func, buf)		\
do {							\
	if ((sc)->mps_debug & level)			\
		mps_print_##func((sc), buf);		\
} while (0)

#define MPS_DPRINT_EVENT(sc, func, buf)			\
	MPS_DPRINT_PAGE(sc, MPS_EVENT, evt_##func, buf)

#endif
