/*	$OpenBSD: fmsradio.c,v 1.9 2024/05/24 06:02:53 jsg Exp $	*/

/*
 * Copyright (c) 2002 Vladimir Popov <jumbo@narod.ru>
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
 */

/* Device Driver for FM Tuners attached to FM801 */

/* Currently supported tuners:
 *  o SoundForte RadioLink SF64-PCR PCI Radio Card
 *  o SoundForte Quad X-treme SF256-PCP-R PCI Sound Card with FM Radio
 *  o SoundForte Theatre X-treme 5.1 SF256-PCS-R PCI Sound Card with FM Radio
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/audioio.h>
#include <sys/radioio.h>

#include <machine/bus.h>

#include <dev/radio_if.h>

#include <dev/ic/ac97.h>

#include <dev/pci/fmsreg.h>
#include <dev/pci/fmsvar.h>

#include <dev/ic/tea5757.h>

#define TUNER_UNKNOWN		0
#define TUNER_SF256PCPR		1
#define TUNER_SF64PCR		2
#define TUNER_SF256PCS		3

#define SF64PCR_CAPS		RADIO_CAPS_DETECT_STEREO |	\
				RADIO_CAPS_DETECT_SIGNAL |	\
				RADIO_CAPS_SET_MONO |	\
				RADIO_CAPS_HW_SEARCH |	\
				RADIO_CAPS_HW_AFC |	\
				RADIO_CAPS_LOCK_SENSITIVITY

#define SF256PCPR_CAPS		RADIO_CAPS_DETECT_STEREO |	\
				RADIO_CAPS_SET_MONO |	\
				RADIO_CAPS_HW_SEARCH |	\
				RADIO_CAPS_HW_AFC |	\
				RADIO_CAPS_LOCK_SENSITIVITY

#define SF256PCS_CAPS		RADIO_CAPS_SET_MONO |	\
				RADIO_CAPS_HW_SEARCH |	\
				RADIO_CAPS_HW_AFC |	\
				RADIO_CAPS_LOCK_SENSITIVITY

#define PCR_WREN_ON		0
#define PCR_WREN_OFF		FM_IO_PIN1
#define PCR_CLOCK_ON		FM_IO_PIN0
#define PCR_CLOCK_OFF		0
#define PCR_DATA_ON		FM_IO_PIN2
#define PCR_DATA_OFF		0

#define PCR_SIGNAL		0x80
#define PCR_STEREO		0x80
#define PCR_INFO_SIGNAL		(1 << 24)
#define PCR_INFO_STEREO		(1 << 25)

#define PCPR_WREN_ON		0
#define PCPR_WREN_OFF		FM_IO_PIN2
#define PCPR_CLOCK_ON		FM_IO_PIN0
#define PCPR_CLOCK_OFF		0
#define PCPR_DATA_ON		FM_IO_PIN1
#define PCPR_DATA_OFF		0
#define PCPR_INFO_STEREO	0x04

#define PCS_WREN_ON		0
#define PCS_WREN_OFF		FM_IO_PIN2
#define PCS_CLOCK_ON		FM_IO_PIN3
#define PCS_CLOCK_OFF		0
#define PCS_DATA_ON		FM_IO_PIN1
#define PCS_DATA_OFF		0

/*
 * Function prototypes
 */
void	fmsradio_set_mute(struct fms_softc *);

void	sf64pcr_init(bus_space_tag_t, bus_space_handle_t, bus_size_t, u_int32_t);
void	sf64pcr_rset(bus_space_tag_t, bus_space_handle_t, bus_size_t, u_int32_t);
void	sf64pcr_write_bit(bus_space_tag_t, bus_space_handle_t, bus_size_t, int);
u_int32_t	sf64pcr_hw_read(bus_space_tag_t, bus_space_handle_t, bus_size_t);
int sf64pcr_probe(struct fms_softc *);

void	sf256pcpr_init(bus_space_tag_t, bus_space_handle_t, bus_size_t, u_int32_t);
void	sf256pcpr_rset(bus_space_tag_t, bus_space_handle_t, bus_size_t, u_int32_t);
void	sf256pcpr_write_bit(bus_space_tag_t, bus_space_handle_t, bus_size_t, int);
u_int32_t	sf256pcpr_hw_read(bus_space_tag_t, bus_space_handle_t, bus_size_t);
int sf256pcpr_probe(struct fms_softc *);

void	sf256pcs_init(bus_space_tag_t, bus_space_handle_t, bus_size_t, u_int32_t);
void	sf256pcs_rset(bus_space_tag_t, bus_space_handle_t, bus_size_t, u_int32_t);
void	sf256pcs_write_bit(bus_space_tag_t, bus_space_handle_t, bus_size_t, int);
u_int32_t	sf256pcs_hw_read(bus_space_tag_t, bus_space_handle_t, bus_size_t);
int	sf256pcs_probe(struct fms_softc *);

int	fmsradio_get_info(void *, struct radio_info *);
int	fmsradio_set_info(void *, struct radio_info *);
int	fmsradio_search(void *, int);

const struct radio_hw_if fmsradio_hw_if = {
	NULL,   /* open */
	NULL,   /* close */
	fmsradio_get_info,
	fmsradio_set_info,
	fmsradio_search
};

struct fmsradio_if {
	int			type; /* Card type */

	int			mute;
	u_int8_t		vol;
	u_int32_t		freq;
	u_int32_t		stereo;
	u_int32_t		lock;

	struct tea5757_t	tea;
};

int
fmsradio_attach(struct fms_softc *sc)
{
	struct fmsradio_if *r;

	r = malloc(sizeof(struct fmsradio_if), M_DEVBUF, M_NOWAIT);
	if (r == NULL) {
		printf("%s: cannot allocate memory for FM tuner config\n",
				sc->sc_dev.dv_xname);
		return TUNER_UNKNOWN;
	}

	sc->radio = r;
	r->tea.iot = sc->sc_iot;
	r->tea.ioh = sc->sc_ioh;
	r->tea.offset = FM_IO_CTL;
	r->tea.flags = sc->sc_dev.dv_cfdata->cf_flags;
	r->vol = 0;
	r->mute = 0;
	r->freq = MIN_FM_FREQ;
	r->stereo = TEA5757_STEREO;
	r->lock = TEA5757_S030;

	r->type = TUNER_UNKNOWN;
	if ((r->type = sf64pcr_probe(sc)) == TUNER_SF64PCR)
		printf("%s: SF64-PCR FM Radio\n", sc->sc_dev.dv_xname);
	else if ((r->type = sf256pcpr_probe(sc)) == TUNER_SF256PCPR)
		printf("%s: SF256-PCP-R FM Radio\n", sc->sc_dev.dv_xname);
	else if ((r->type = sf256pcs_probe(sc)) == TUNER_SF256PCS)
		printf("%s: SF256-PCS-R FM Radio\n", sc->sc_dev.dv_xname);
	else
		return TUNER_UNKNOWN;

	fmsradio_set_mute(sc);
	radio_attach_mi(&fmsradio_hw_if, sc, &sc->sc_dev);
	return r->type;
}

/* SF256-PCS specific routines */
int
sf256pcs_probe(struct fms_softc *sc)
{
	struct fmsradio_if *radio = (struct fmsradio_if *)sc->radio;
	u_int32_t freq;

	radio->tea.init = sf256pcs_init;
	radio->tea.rset = sf256pcs_rset;
	radio->tea.write_bit = sf256pcs_write_bit;
	radio->tea.read = sf256pcs_hw_read;

	tea5757_set_freq(&radio->tea, radio->stereo,
	    radio->lock, radio->freq);
	freq = tea5757_decode_freq(sf256pcs_hw_read(radio->tea.iot,
	    radio->tea.ioh, radio->tea.offset),
	    radio->tea.flags & TEA5757_TEA5759);
	if (freq != radio->freq)
		return TUNER_UNKNOWN;

	return TUNER_SF256PCS;
}

u_int32_t
sf256pcs_hw_read(bus_space_tag_t iot, bus_space_handle_t ioh, bus_size_t offset)
{
	u_int32_t res = 0ul;
	u_int16_t i, d;

	d  = FM_IO_GPIO(FM_IO_PIN1 | FM_IO_PIN2 | FM_IO_PIN3) | PCS_WREN_OFF;

	/* Now read data in */
	d |= FM_IO_GPIO_IN(PCS_DATA_ON) | PCS_DATA_ON;

	bus_space_write_2(iot, ioh, offset, d | PCS_CLOCK_OFF);

	/* Read the register */
	i = 24;
	while (i--) {
		res <<= 1;
		bus_space_write_2(iot, ioh, offset, d | PCS_CLOCK_ON);
		bus_space_write_2(iot, ioh, offset, d | PCS_CLOCK_OFF);
		res |= bus_space_read_2(iot, ioh, offset) &
		   PCS_DATA_ON ? 1 : 0;
	}

	return (res & (TEA5757_DATA | TEA5757_FREQ));
}

void
sf256pcs_write_bit(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t off, int bit)
{
	u_int16_t data, wren;

	wren  = FM_IO_GPIO(FM_IO_PIN1 | FM_IO_PIN2 | FM_IO_PIN3);
	wren |= PCS_WREN_ON;
	data = bit ? PCPR_DATA_ON : PCS_DATA_OFF;

	bus_space_write_2(iot, ioh, off, PCS_CLOCK_OFF | wren | data);
	bus_space_write_2(iot, ioh, off, PCS_CLOCK_ON  | wren | data);
	bus_space_write_2(iot, ioh, off, PCS_CLOCK_OFF | wren | data);
}

void
sf256pcs_init(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t offset, u_int32_t d)
{
	d  = FM_IO_GPIO(FM_IO_PIN1 | FM_IO_PIN2 | FM_IO_PIN3);
	d |= PCS_WREN_ON | PCS_DATA_OFF | PCS_CLOCK_OFF;

	bus_space_write_2(iot, ioh, offset, d);
	bus_space_write_2(iot, ioh, offset, d);
}

void
sf256pcs_rset(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t offset, u_int32_t d)
{
	d  = FM_IO_GPIO(FM_IO_PIN1 | FM_IO_PIN2 | FM_IO_PIN3);
	d |= PCS_WREN_OFF | PCS_DATA_OFF | PCS_CLOCK_OFF;

	bus_space_write_2(iot, ioh, offset, d);
	bus_space_write_2(iot, ioh, offset, d);
}

/* SF256-PCP-R specific routines */
int
sf256pcpr_probe(struct fms_softc *sc)
{
	struct fmsradio_if *radio = (struct fmsradio_if *)sc->radio;
	u_int32_t freq;

	radio->tea.init = sf256pcpr_init;
	radio->tea.rset = sf256pcpr_rset;
	radio->tea.write_bit = sf256pcpr_write_bit;
	radio->tea.read = sf256pcpr_hw_read;

	tea5757_set_freq(&radio->tea, radio->stereo,
	    radio->lock, radio->freq);
	freq = tea5757_decode_freq(sf256pcpr_hw_read(radio->tea.iot,
	    radio->tea.ioh, radio->tea.offset),
	    radio->tea.flags & TEA5757_TEA5759);
	if (freq != radio->freq)
		return TUNER_UNKNOWN;

	return TUNER_SF256PCPR;
}

u_int32_t
sf256pcpr_hw_read(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t offset)
{
	u_int32_t res = 0ul;
	u_int16_t i, d;

	d  = FM_IO_GPIO_ALL | FM_IO_GPIO_IN(PCPR_DATA_ON | FM_IO_PIN3);

	/* Now read data in */
	d |= PCPR_WREN_OFF | PCPR_DATA_ON;

	bus_space_write_2(iot, ioh, offset, d | PCPR_CLOCK_OFF);

	/* Read the register */
	i = 24;
	while (i--) {
		res <<= 1;
		bus_space_write_2(iot, ioh, offset, d | PCPR_CLOCK_ON);
		bus_space_write_2(iot, ioh, offset, d | PCPR_CLOCK_OFF);
		res |= bus_space_read_2(iot, ioh, offset) &
		    PCPR_DATA_ON ? 1 : 0;
	}

	return (res & (TEA5757_DATA | TEA5757_FREQ));
}

void
sf256pcpr_write_bit(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t off, int bit)
{
	u_int16_t data, wren;

	wren  = FM_IO_GPIO_ALL | FM_IO_GPIO_IN(FM_IO_PIN3) | PCPR_WREN_ON;
	data = bit ? PCPR_DATA_ON : PCPR_DATA_OFF;

	bus_space_write_2(iot, ioh, off, PCPR_CLOCK_OFF | wren | data);
	bus_space_write_2(iot, ioh, off, PCPR_CLOCK_ON  | wren | data);
	bus_space_write_2(iot, ioh, off, PCPR_CLOCK_OFF | wren | data);
}

void
sf256pcpr_init(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t offset, u_int32_t d)
{
	d  = FM_IO_GPIO_ALL | FM_IO_GPIO_IN(FM_IO_PIN3);
	d |= PCPR_WREN_ON | PCPR_DATA_OFF | PCPR_CLOCK_OFF;

	bus_space_write_2(iot, ioh, offset, d);
	bus_space_write_2(iot, ioh, offset, d);
}

void
sf256pcpr_rset(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t offset, u_int32_t d)
{
	d  = FM_IO_GPIO_ALL | FM_IO_GPIO_IN(FM_IO_PIN3);
	d |= PCPR_WREN_OFF | PCPR_DATA_OFF | PCPR_CLOCK_OFF;

	bus_space_write_2(iot, ioh, offset, d);
	bus_space_write_2(iot, ioh, offset, d);
}

/* SF64-PCR specific routines */
int
sf64pcr_probe(struct fms_softc *sc)
{
	struct fmsradio_if *radio = (struct fmsradio_if *)sc->radio;
	u_int32_t freq;

	radio->tea.init = sf64pcr_init;
	radio->tea.rset = sf64pcr_rset;
	radio->tea.write_bit = sf64pcr_write_bit;
	radio->tea.read = sf64pcr_hw_read;

	tea5757_set_freq(&radio->tea, radio->stereo,
	    radio->lock, radio->freq);
	freq = tea5757_decode_freq(sf64pcr_hw_read(radio->tea.iot,
	    radio->tea.ioh, radio->tea.offset),
	    radio->tea.flags & TEA5757_TEA5759);
	if (freq != radio->freq)
		return TUNER_UNKNOWN;

	return TUNER_SF64PCR;
}

u_int32_t
sf64pcr_hw_read(bus_space_tag_t iot, bus_space_handle_t ioh, bus_size_t offset)
{
	u_int32_t res = 0ul;
	u_int16_t d, i, ind = 0;

	d  = FM_IO_GPIO_ALL | FM_IO_GPIO_IN(PCR_DATA_ON | FM_IO_PIN3);

	/* Now read data in */
	d |= PCR_WREN_OFF | PCR_DATA_ON;

	bus_space_write_2(iot, ioh, offset, d | PCR_CLOCK_OFF);
	DELAY(4);

	/* Read the register */
	i = 23;
	while (i--) {
		bus_space_write_2(iot, ioh, offset, d | PCR_CLOCK_ON);
		DELAY(4);

		bus_space_write_2(iot, ioh, offset, d | PCR_CLOCK_OFF);
		DELAY(4);

		res |= bus_space_read_2(iot, ioh, offset) & PCR_DATA_ON ? 1 : 0;
		res <<= 1;
	}

	bus_space_write_2(iot, ioh, offset, d | PCR_CLOCK_ON);
	DELAY(4);

	i = bus_space_read_1(iot, ioh, offset);
	ind = i & PCR_SIGNAL ? (1 << 1) : (0 << 1); /* Tuning */

	bus_space_write_2(iot, ioh, offset, d | PCR_CLOCK_OFF);

	i = bus_space_read_2(iot, ioh, offset);
	ind |= i & PCR_STEREO ? (1 << 0) : (0 << 0); /* Mono */
	res |= i & PCR_DATA_ON ? (1 << 0) : (0 << 0);

	return (res & (TEA5757_DATA | TEA5757_FREQ)) | (ind << 24);
}

void
sf64pcr_write_bit(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t off, int bit)
{
	u_int16_t data, wren;

	wren  = FM_IO_GPIO_ALL | FM_IO_GPIO_IN(FM_IO_PIN3) | PCR_WREN_ON;
	data = bit ? PCR_DATA_ON : PCR_DATA_OFF;

	bus_space_write_2(iot, ioh, off, PCR_CLOCK_OFF | wren | data);
	DELAY(4);
	bus_space_write_2(iot, ioh, off, PCR_CLOCK_ON | wren | data);
	DELAY(4);
	bus_space_write_2(iot, ioh, off, PCR_CLOCK_OFF | wren | data);
	DELAY(4);
}

void
sf64pcr_init(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t offset, u_int32_t d)
{
	d  = FM_IO_GPIO_ALL | FM_IO_GPIO_IN(FM_IO_PIN3);
	d |= PCR_WREN_ON | PCR_DATA_ON | PCR_CLOCK_OFF;

	bus_space_write_2(iot, ioh, offset, d);
	DELAY(4);
}

void
sf64pcr_rset(bus_space_tag_t iot, bus_space_handle_t ioh,
    bus_size_t offset, u_int32_t d)
{
	/* Do nothing */
	return;
}


/* Common tuner routines */
/*
 * Mute/unmute
 */
void
fmsradio_set_mute(struct fms_softc *sc)
{
	struct fmsradio_if *radio = (struct fmsradio_if *)sc->radio;
	u_int16_t v, mute, unmute;

	switch (radio->type) {
	case TUNER_SF256PCS:
		mute = FM_IO_GPIO(FM_IO_PIN1 | FM_IO_PIN2 | FM_IO_PIN3);
		unmute = mute | PCS_WREN_OFF;
		break;
	case TUNER_SF256PCPR:
		mute  = FM_IO_GPIO_ALL | FM_IO_GPIO_IN(FM_IO_PIN3);
		unmute = mute | PCPR_WREN_OFF;
		break;
	case TUNER_SF64PCR:
		mute  = FM_IO_GPIO_ALL | FM_IO_GPIO_IN(FM_IO_PIN3);
		unmute = mute | PCR_WREN_OFF;
		break;
	default:
		return;
	}
	v = (radio->mute || !radio->vol) ? mute : unmute;
	bus_space_write_2(radio->tea.iot, radio->tea.ioh,
	    radio->tea.offset, v);
	DELAY(64);
	bus_space_write_2(radio->tea.iot, radio->tea.ioh,
	    radio->tea.offset, v);
}

int
fmsradio_get_info(void *v, struct radio_info *ri)
{
	struct fms_softc *sc = v;
	struct fmsradio_if *radio = (struct fmsradio_if *)sc->radio;
	u_int32_t buf;

	ri->mute = radio->mute;
	ri->volume = radio->vol ? 255 : 0;
	ri->stereo = radio->stereo == TEA5757_STEREO ? 1 : 0;
	ri->lock = tea5757_decode_lock(radio->lock);

	switch (radio->type) {
	case TUNER_SF256PCS:
		ri->caps = SF256PCS_CAPS;
		buf = sf256pcs_hw_read(radio->tea.iot, radio->tea.ioh,
		    radio->tea.offset);
		ri->info = 0; /* UNSUPPORTED */
		break;
	case TUNER_SF256PCPR:
		ri->caps = SF256PCPR_CAPS;
		buf = sf256pcpr_hw_read(radio->tea.iot, radio->tea.ioh,
		    radio->tea.offset);
		ri->info = bus_space_read_2(radio->tea.iot, radio->tea.ioh,
			FM_VOLUME) == PCPR_INFO_STEREO ?
			RADIO_INFO_STEREO : 0;
		break;
	case TUNER_SF64PCR:
		ri->caps = SF64PCR_CAPS;
		buf = sf64pcr_hw_read(radio->tea.iot, radio->tea.ioh,
		    radio->tea.offset);
		ri->info  = buf & PCR_INFO_SIGNAL ? 0 : RADIO_INFO_SIGNAL;
		ri->info |= buf & PCR_INFO_STEREO ? 0 : RADIO_INFO_STEREO;
		break;
	default:
		return EINVAL;
	}

	ri->freq = radio->freq = tea5757_decode_freq(buf,
			sc->sc_dev.dv_cfdata->cf_flags & TEA5757_TEA5759);

	fmsradio_set_mute(sc);

	/* UNSUPPORTED */
	ri->rfreq = 0;

	return (0);
}

int
fmsradio_set_info(void *v, struct radio_info *ri)
{
	struct fms_softc *sc = v;
	struct fmsradio_if *radio = (struct fmsradio_if *)sc->radio;

	radio->mute = ri->mute ? 1 : 0;
	radio->vol = ri->volume ? 255 : 0;
	radio->stereo = ri->stereo ? TEA5757_STEREO: TEA5757_MONO;
	radio->lock = tea5757_encode_lock(ri->lock);
	ri->freq = radio->freq = tea5757_set_freq(&radio->tea,
		radio->lock, radio->stereo, ri->freq);
	fmsradio_set_mute(sc);

	return (0);
}

int
fmsradio_search(void *v, int f)
{
	struct fms_softc *sc = v;
	struct fmsradio_if *radio = (struct fmsradio_if *)sc->radio;

	tea5757_search(&radio->tea, radio->lock,
			radio->stereo, f);
	fmsradio_set_mute(sc);

	return (0);
}
