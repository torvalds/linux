/*-
 * Copyright 2014 Luiz Otavio O Souza <loos@freebsd.org>
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

#ifndef _TI_ADCVAR_H_
#define _TI_ADCVAR_H_

#define	TI_ADC_NPINS	8

#define	ADC_READ4(_sc, reg)	bus_read_4((_sc)->sc_mem_res, reg)
#define	ADC_WRITE4(_sc, reg, value)	\
	bus_write_4((_sc)->sc_mem_res, reg, value)

struct ti_adc_softc {
	device_t		sc_dev;
	int			sc_last_state;
	struct mtx		sc_mtx;
	struct resource		*sc_mem_res;
	struct resource		*sc_irq_res;
	void			*sc_intrhand;
	int			sc_tsc_wires;
	int			sc_tsc_wire_config[TI_ADC_NPINS];
	int			sc_coord_readouts;
	int			sc_x_plate_resistance;
	int			sc_charge_delay;
	int			sc_adc_nchannels;
	int			sc_adc_channels[TI_ADC_NPINS];
	int			sc_xp_bit, sc_xp_inp;
	int			sc_xn_bit, sc_xn_inp;
	int			sc_yp_bit, sc_yp_inp;
	int			sc_yn_bit, sc_yn_inp;
	uint32_t		sc_tsc_enabled;
	int			sc_pen_down;
#ifdef EVDEV_SUPPORT
	int			sc_x;
	int			sc_y;
	struct evdev_dev *sc_evdev;
#endif
};

struct ti_adc_input {
	int32_t			enable;		/* input enabled */
	int32_t			samples;	/* samples average */
	int32_t			input;		/* input number */
	int32_t			value;		/* raw converted value */
	uint32_t		stepconfig;	/* step config register */
	uint32_t		stepdelay;	/* step delay register */
	struct ti_adc_softc	*sc;		/* pointer to adc softc */
};

#define	TI_ADC_LOCK(_sc)		\
	mtx_lock(&(_sc)->sc_mtx)
#define	TI_ADC_UNLOCK(_sc)		\
	mtx_unlock(&(_sc)->sc_mtx)
#define	TI_ADC_LOCK_INIT(_sc)	\
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->sc_dev), \
	    "ti_adc", MTX_DEF)
#define	TI_ADC_LOCK_DESTROY(_sc)	\
	mtx_destroy(&_sc->sc_mtx);
#define	TI_ADC_LOCK_ASSERT(_sc)	\
	mtx_assert(&(_sc)->sc_mtx, MA_OWNED)

#endif /* _TI_ADCVAR_H_ */
