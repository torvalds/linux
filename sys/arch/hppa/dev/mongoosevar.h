/*	$OpenBSD: mongoosevar.h,v 1.2 2007/05/10 17:59:24 deraadt Exp $	*/

/*
 * Copyright (c) 1998-2003 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

struct hppa_isa_iv {
	const char *iv_name;
	int (*iv_handler)(void *arg);
	void *iv_arg;
	int iv_pri;

	/* don't do sharing, we won't have many slots anyway
	struct hppa_isa_iv *iv_next;
	*/
};

struct mongoose_softc {
	struct  device sc_dev;
	void *sc_ih;

	bus_space_tag_t sc_bt;
	volatile struct mongoose_regs *sc_regs;
	volatile struct mongoose_ctrl *sc_ctrl;
	bus_addr_t sc_iomap;

	/* interrupts section */
	struct hppa_eisa_chipset sc_ec;
	struct hppa_isa_chipset sc_ic;
	struct hppa_isa_iv sc_iv[MONGOOSE_NINTS];

	/* isa/eisa bus guts */
	struct hppa_bus_space_tag sc_eiot;
	struct hppa_bus_space_tag sc_ememt;
	struct hppa_bus_dma_tag sc_edmat;
	struct hppa_bus_space_tag sc_iiot;
	struct hppa_bus_space_tag sc_imemt;
	struct hppa_bus_dma_tag sc_idmat;
};

union mongoose_attach_args {
	char *mongoose_name;
	struct eisabus_attach_args mongoose_eisa;
	struct isabus_attach_args mongoose_isa;
};

int	mgattach_common(struct mongoose_softc *);
int	mgprint(void *, const char *);
int	mg_intr(void *);
