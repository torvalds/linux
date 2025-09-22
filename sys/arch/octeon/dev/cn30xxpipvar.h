/*	$OpenBSD: cn30xxpipvar.h,v 1.6 2024/05/20 23:13:33 jsg Exp $	*/

/*
 * Copyright (c) 2007 Internet Initiative Japan, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _CN30XXPIPVAR_H_
#define _CN30XXPIPVAR_H_

#include <sys/kstat.h>

#include "kstat.h"

/* XXX */
struct cn30xxpip_softc {
	int			sc_port;
	bus_space_tag_t		sc_regt;
	bus_space_handle_t	sc_regh;
	bus_space_handle_t	sc_regh_stat;
	int			sc_tag_type;
	int			sc_receive_group;
	size_t			sc_ip_offset;
};

/* XXX */
struct cn30xxpip_attach_args {
	int			aa_port;
	bus_space_tag_t		aa_regt;
	int			aa_tag_type;
	int			aa_receive_group;
	size_t			aa_ip_offset;
};

void			cn30xxpip_init(struct cn30xxpip_attach_args *,
			    struct cn30xxpip_softc **);
int			cn30xxpip_port_config(struct cn30xxpip_softc *);
void			cn30xxpip_prt_cfg_enable(struct cn30xxpip_softc *,
			    uint64_t, int);
void			cn30xxpip_stats_init(struct cn30xxpip_softc *);
#if NKSTAT > 0
void			cn30xxpip_kstat_read(struct cn30xxpip_softc *,
			    struct kstat_kv *);
#endif

#endif /* !_CN30XXPIPVAR_H_ */
