/*	$OpenBSD: autoconf.h,v 1.13 2024/05/22 05:51:49 jsg Exp $ */

/*
 * Copyright (c) 1997 Per Fogelstrom
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
/*
 * Machine-dependent structures of autoconfiguration
 */

#ifndef _MACHINE_AUTOCONF_H_
#define _MACHINE_AUTOCONF_H_

#include <machine/bus.h>

struct confargs {
	char	*ca_name;
	bus_space_tag_t ca_iot;
	bus_dma_tag_t ca_dmat;
	u_int32_t ca_node;
	int ca_nreg;
	u_int32_t *ca_reg;
	int ca_nintr;
	int32_t *ca_intr;
	u_int ca_baseaddr;

};

int	badaddr(void *, u_int32_t);

typedef int (time_read_t)(time_t *sec);
typedef int (time_write_t)(time_t sec);

extern time_read_t *time_read;
extern time_write_t *time_write;

typedef int mac_intr_handle_t;
typedef void     *(intr_establish_t)(void *, mac_intr_handle_t,
    int, int, int (*func)(void *), void *, const char *);
typedef void     (intr_disestablish_t)(void *, void *);
struct cpu_info;
typedef void     (intr_send_ipi_t)(struct cpu_info *, int);

intr_establish_t mac_intr_establish;
intr_disestablish_t mac_intr_disestablish;
extern intr_establish_t *intr_establish_func;
extern intr_disestablish_t *intr_disestablish_func;
extern intr_send_ipi_t *intr_send_ipi_func;


#endif /* _MACHINE_AUTOCONF_H_ */
