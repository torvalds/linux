/*	$OpenBSD: w83l518d.c,v 1.1 2009/10/03 19:51:53 kettenis Exp $	*/
/*	$NetBSD: w83l518d.c,v 1.1 2009/09/30 20:44:50 jmcneill Exp $ */

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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/proc.h>

#include <machine/bus.h>

#include <dev/ic/w83l518dreg.h>
#include <dev/ic/w83l518dvar.h>
#include <dev/ic/w83l518d_sdmmc.h>

uint8_t
wb_idx_read(struct wb_softc *wb, uint8_t reg)
{
	bus_space_write_1(wb->wb_iot, wb->wb_ioh, WB_SD_INDEX, reg);
	return bus_space_read_1(wb->wb_iot, wb->wb_ioh, WB_SD_DATA);
}

void
wb_idx_write(struct wb_softc *wb, uint8_t reg, uint8_t val)
{
	bus_space_write_1(wb->wb_iot, wb->wb_ioh, WB_SD_INDEX, reg);
	bus_space_write_1(wb->wb_iot, wb->wb_ioh, WB_SD_DATA, val);
}

uint8_t
wb_read(struct wb_softc *wb, uint8_t reg)
{
	return bus_space_read_1(wb->wb_iot, wb->wb_ioh, reg);
}

void
wb_write(struct wb_softc *wb, uint8_t reg, uint8_t val)
{
	bus_space_write_1(wb->wb_iot, wb->wb_ioh, reg, val);
}

void
wb_led(struct wb_softc *wb, int enable)
{
	uint8_t val;

	val = wb_read(wb, WB_SD_CSR);
	if (enable)
		val |= WB_CSR_MS_LED;
	else
		val &= ~WB_CSR_MS_LED;
	wb_write(wb, WB_SD_CSR, val);
}

void
wb_attach(struct wb_softc *wb)
{
	switch (wb->wb_type) {
	case WB_DEVNO_SD:
		wb_sdmmc_attach(wb);
		break;
	case WB_DEVNO_MS:
		break;
	case WB_DEVNO_SC:
		break;
	case WB_DEVNO_GPIO:
		break;
	}
}

int
wb_detach(struct wb_softc *wb, int flags)
{
	switch (wb->wb_type) {
	case WB_DEVNO_SD:
		wb_sdmmc_detach(wb, flags);
		break;
	}

	return 0;
}

/*
 * intr handler 
 */
int
wb_intr(void *opaque)
{
	struct wb_softc *wb = opaque;

	switch (wb->wb_type) {
	case WB_DEVNO_SD:
		return wb_sdmmc_intr(wb);
		break;
	}

	return 0;
}
