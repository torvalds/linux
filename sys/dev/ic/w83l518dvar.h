/*	$OpenBSD: w83l518dvar.h,v 1.1 2009/10/03 19:51:53 kettenis Exp $	*/
/*	$NetBSD: w83l518dvar.h,v 1.1 2009/09/30 20:44:50 jmcneill Exp $ */

/*
 * Copyright (c) 2009 Jared D. McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SYS_DEV_IC_W83L518DVAR_H
#define _SYS_DEV_IC_W83L518DVAR_H

struct wb_softc {
	struct device		wb_dev;

	/* to be filled in by bus driver */
	bus_space_tag_t		wb_iot;
	bus_space_handle_t	wb_ioh;
	uint8_t			wb_type;
	uint16_t		wb_base;
	uint8_t			wb_irq;

	/* private */
	struct device		*wb_sdmmc_dev;
	int			wb_sdmmc_width;
	uint8_t			wb_sdmmc_intsts;
	struct timeout		wb_sdmmc_to;
};

void	wb_attach(struct wb_softc *);
int	wb_detach(struct wb_softc *, int);
int	wb_intr(void *);

uint8_t	wb_read(struct wb_softc *, uint8_t);
void	wb_write(struct wb_softc *, uint8_t, uint8_t);
uint8_t	wb_idx_read(struct wb_softc *, uint8_t);
void	wb_idx_write(struct wb_softc *, uint8_t, uint8_t);

void	wb_led(struct wb_softc *, int);

#endif
