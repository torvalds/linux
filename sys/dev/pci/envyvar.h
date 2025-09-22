/*	$OpenBSD: envyvar.h,v 1.20 2019/04/30 20:29:46 ratchov Exp $	*/
/*
 * Copyright (c) 2007 Alexandre Ratchov <alex@caoua.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SYS_DEV_PCI_ENVYVAR_H
#define SYS_DEV_PCI_ENVYVAR_H

#include <sys/types.h>
#include <sys/device.h>
#include <sys/time.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>

struct envy_softc;

struct envy_buf {
	bus_dma_segment_t	seg;
	bus_dmamap_t		map;
	caddr_t			addr;
	size_t			size;
	unsigned int		swpos;
	unsigned int		bufsz;
	unsigned int		blksz;
};

struct envy_codec {
	char *name;
	int (*ndev)(struct envy_softc *);
	void (*devinfo)(struct envy_softc *, struct mixer_devinfo *, int);
	void (*get)(struct envy_softc *, struct mixer_ctrl *, int);
	int (*set)(struct envy_softc *, struct mixer_ctrl *, int);
};

struct envy_card {
	int subid;
	char *name;
	int nich;
	struct envy_codec *adc;
	int noch;
	struct envy_codec *dac;
	int nmidi;
	void (*init)(struct envy_softc *);
	void (*codec_write)(struct envy_softc *, int, int, int);
 	void (*set_rate)(struct envy_softc *, int);
	unsigned char *eeprom;
};

struct envy_softc {
	struct device		dev;
	struct device	       *audio;
	int			isht;		/* is a Envy24HT ? */
	int			isac97;		/* is a Envy24HT AC97 ? */
	struct envy_buf		ibuf, obuf;
	pcitag_t		pci_tag;
	pci_chipset_tag_t	pci_pc;
	pci_intr_handle_t      *pci_ih;
	bus_dma_tag_t		pci_dmat;
	bus_space_tag_t		ccs_iot;
	bus_space_handle_t	ccs_ioh;
	bus_size_t		ccs_iosz;
	bus_space_tag_t		mt_iot;
	bus_space_handle_t	mt_ioh;
	bus_size_t		mt_iosz;
	int			iactive;	/* trigger_input called */
	int			oactive;	/* trigger_output called */
	int			ibusy;		/* input DMA started */
	int			obusy;		/* output DMA started */
#ifdef ENVY_DEBUG
	unsigned 		spurious;
	struct timespec		start_ts;
#define ENVY_NINTR		16
	unsigned 		nintr;
	struct envy_intr {
		int ipos, opos, st, mask, ctl, iactive, oactive;
		struct timespec ts;
	} intrs[ENVY_NINTR];
#endif
	struct envy_card       *card;
	unsigned char 		shadow[4][16];
#define ENVY_EEPROM_MAXSZ 32
	unsigned char		eeprom[ENVY_EEPROM_MAXSZ];
	struct ac97_codec_if   *codec_if;
	struct ac97_host_if	host_if;
	enum ac97_host_flags	codec_flags;
	void (*iintr)(void *);
	void *iarg;
	void (*ointr)(void *);
	void *oarg;
#if NMIDI > 0
	void (*midi_in)(void *, int);
	void (*midi_out)(void *);
	void *midi_arg;
	struct device *midi;
	int midi_isopen;
#endif
};

#define ENVY_MIX_CLASSIN	0
#define ENVY_MIX_CLASSOUT	1
#define ENVY_MIX_CLASSMON	2

#define ENVY_MIX_NCLASS		3
#define ENVY_MIX_NOUTSRC	10
#define ENVY_MIX_NMONITOR	20

#define ENVY_MIX_OUTSRC_LINEIN	0
#define ENVY_MIX_OUTSRC_SPDIN	8
#define ENVY_MIX_OUTSRC_DMA	10
#define ENVY_MIX_OUTSRC_MON	11

#endif /* !defined(SYS_DEV_PCI_ENVYVAR_H) */
