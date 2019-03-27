/*-
 * Copyright (c) 2016 Jared McNeill <jmcneill@invisible.ca>
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

#ifndef __DWC_HDMI_H__
#define	__DWC_HDMI_H__

struct dwc_hdmi_softc {
	device_t		sc_dev;
	struct resource		*sc_mem_res;
	int			sc_mem_rid;
	uint32_t		sc_reg_shift;
	device_t		(*sc_get_i2c_dev)(device_t);

	uint8_t			*sc_edid;
	uint8_t			sc_edid_len;
	struct intr_config_hook	sc_mode_hook;
	struct videomode	sc_mode;

	struct edid_info	sc_edid_info;
	int			sc_has_audio;
};

static inline uint8_t
RD1(struct dwc_hdmi_softc *sc, bus_size_t off)
{
	return (bus_read_1(sc->sc_mem_res, off << sc->sc_reg_shift));
}

static inline void
WR1(struct dwc_hdmi_softc *sc, bus_size_t off, uint8_t val)
{
	bus_write_1(sc->sc_mem_res, off << sc->sc_reg_shift, val);
}

int	dwc_hdmi_get_edid(device_t, uint8_t **, uint32_t *);
int	dwc_hdmi_set_videomode(device_t, const struct videomode *);
int	dwc_hdmi_init(device_t);

#endif	/* __DWC_HDMI_H__ */
