/*	$OpenBSD: isapnpres.c,v 1.9 2014/07/12 18:48:18 tedu Exp $	*/
/*	$NetBSD: isapnpres.c,v 1.7.4.1 1997/11/20 07:46:13 mellon Exp $	*/

/*
 * Copyright (c) 1996 Christos Zoulas.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Christos Zoulas.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Resource parser for Plug and Play cards.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <dev/isa/isapnpreg.h>

#include <dev/isa/isavar.h>

int isapnp_wait_status(struct isapnp_softc *);
struct isa_attach_args *
    isapnp_newdev(struct isa_attach_args *);
struct isa_attach_args *
    isapnp_newconf(struct isa_attach_args *);
void isapnp_merge(struct isa_attach_args *,
    const struct isa_attach_args *);
struct isa_attach_args *
    isapnp_flatten(struct isa_attach_args *);
int isapnp_process_tag(u_char, u_char, u_char *,
    struct isa_attach_args **, struct isa_attach_args **,
    struct isa_attach_args **);

#ifdef DEBUG_ISAPNP
# define DPRINTF(a) printf a
#else
# define DPRINTF(a)
#endif

/* isapnp_wait_status():
 *	Wait for the next byte of resource data to become available
 */
int
isapnp_wait_status(struct isapnp_softc *sc)
{
	int i;

	/* wait up to 1 ms for each resource byte */
	for (i = 0; i < 10; i++) {
		if (isapnp_read_reg(sc, ISAPNP_STATUS) & 1)
			return 0;
		DELAY(100);
	}
	return 1;
}


/* isapnp_newdev():
 *	Add a new logical device to the current card; expand the configuration
 *	resources of the current card if needed.
 */
struct isa_attach_args *
isapnp_newdev(struct isa_attach_args *card)
{
	struct isa_attach_args *ipa, *dev = malloc(sizeof(*dev), M_DEVBUF, M_WAITOK);

	ISAPNP_CLONE_SETUP(dev, card);

	dev->ipa_pref = ISAPNP_DEP_ACCEPTABLE;
	bcopy(card->ipa_devident, dev->ipa_devident,
	    sizeof(card->ipa_devident));

	if (card->ipa_child == NULL)
		card->ipa_child = dev;
	else {
		for (ipa = card->ipa_child; ipa->ipa_sibling != NULL; 
		    ipa = ipa->ipa_sibling)
			continue;
		ipa->ipa_sibling = dev;
	}


	return dev;
}


/* isapnp_newconf():
 *	Add a new alternate configuration to a logical device
 */
struct isa_attach_args *
isapnp_newconf(struct isa_attach_args *dev)
{
	struct isa_attach_args *ipa, *conf = malloc(sizeof(*conf), M_DEVBUF, M_WAITOK);

	ISAPNP_CLONE_SETUP(conf, dev);

	bcopy(dev->ipa_devident, conf->ipa_devident,
	    sizeof(conf->ipa_devident));
	bcopy(dev->ipa_devlogic, conf->ipa_devlogic,
	    sizeof(conf->ipa_devlogic));
	bcopy(dev->ipa_devcompat, conf->ipa_devcompat,
	    sizeof(conf->ipa_devcompat));
	bcopy(dev->ipa_devclass, conf->ipa_devclass,
	    sizeof(conf->ipa_devclass));

	if (dev->ipa_child == NULL)
		dev->ipa_child = conf;
	else {
		for (ipa = dev->ipa_child; ipa->ipa_sibling;
		    ipa = ipa->ipa_sibling)
			continue;
		ipa->ipa_sibling = conf;
	}

	return conf;
}


/* isapnp_merge():
 *	Merge the common device configurations to the subconfigurations
 */
void
isapnp_merge(struct isa_attach_args *c, const struct isa_attach_args *d)
{
	int i;

	for (i = 0; i < d->ipa_nio; i++)
		c->ipa_io[c->ipa_nio++] = d->ipa_io[i];

	for (i = 0; i < d->ipa_nmem; i++)
		c->ipa_mem[c->ipa_nmem++] = d->ipa_mem[i];

	for (i = 0; i < d->ipa_nmem32; i++)
		c->ipa_mem32[c->ipa_nmem32++] = d->ipa_mem32[i];

	for (i = 0; i < d->ipa_nirq; i++)
		c->ipa_irq[c->ipa_nirq++] = d->ipa_irq[i];

	for (i = 0; i < d->ipa_ndrq; i++)
		c->ipa_drq[c->ipa_ndrq++] = d->ipa_drq[i];
}


/* isapnp_flatten():
 *	Flatten the tree to a list of config entries.
 */
struct isa_attach_args *
isapnp_flatten(struct isa_attach_args *card)
{
	struct isa_attach_args *dev, *conf, *d, *c, *pa;

	dev = card->ipa_child;
	free(card, M_DEVBUF, 0);

	for (conf = c = NULL, d = dev; d; d = dev) {
		dev = d->ipa_sibling;
		if (d->ipa_child == NULL) {
			/*
			 * No subconfigurations; all configuration info
			 * is in the device node.
			 */
			d->ipa_sibling = NULL;
			pa = d;
		}
		else {
			/*
			 * Push down device configuration info to the
			 * subconfigurations
			 */
			for (pa = d->ipa_child; pa; pa = pa->ipa_sibling)
				isapnp_merge(pa, d);

			pa = d->ipa_child;
			free(d, M_DEVBUF, 0);
		}

		if (c == NULL)
			c = conf = pa;
		else
			c->ipa_sibling = pa;

		while (c->ipa_sibling)
			c = c->ipa_sibling;
	}
	return conf;
}


/* isapnp_process_tag():
 *	Process a resource tag
 */
int
isapnp_process_tag(u_char tag, u_char len, u_char *buf,
    struct isa_attach_args **card, struct isa_attach_args **dev,
    struct isa_attach_args **conf)
{
	char str[64];
	struct isapnp_region *r;
	struct isapnp_pin *p;
	struct isa_attach_args *pa;

#define COPY(a, b) strncpy((a), (b), sizeof(a)), (a)[sizeof(a) - 1] = '\0'

	switch (tag) {
	case ISAPNP_TAG_VERSION_NUM:
		DPRINTF(("PnP version %d.%d, Vendor version %d.%d\n",
		    buf[0] >> 4, buf[0] & 0xf, buf[1] >> 4,  buf[1] & 0xf));
		return 0;

	case ISAPNP_TAG_LOGICAL_DEV_ID:
		(void) isapnp_id_to_vendor(str, buf);
		DPRINTF(("Logical device id %s\n", str));

		*dev = isapnp_newdev(*card);
		COPY((*dev)->ipa_devlogic, str);
		return 0;

	case ISAPNP_TAG_COMPAT_DEV_ID:
		(void) isapnp_id_to_vendor(str, buf);
		DPRINTF(("Compatible device id %s\n", str));

		if (*dev == NULL)
			return -1;

		if (*(*dev)->ipa_devcompat == '\0')
			COPY((*dev)->ipa_devcompat, str);
		return 0;

	case ISAPNP_TAG_DEP_START:
		if (len == 0)
			buf[0] = ISAPNP_DEP_ACCEPTABLE;

		if (*dev == NULL)
			return -1;

		*conf = isapnp_newconf(*dev);
		(*conf)->ipa_pref = buf[0];
#ifdef DEBUG_ISAPNP
		isapnp_print_dep_start(">>> Start dependent function ",
		    (*conf)->ipa_pref);
#endif
		return 0;
		
	case ISAPNP_TAG_DEP_END:
		DPRINTF(("<<<End dependent functions\n"));
		*conf = NULL;
		return 0;

	case ISAPNP_TAG_ANSI_IDENT_STRING:
		buf[len] = '\0';
		DPRINTF(("ANSI Ident: %s\n", buf));
		if (*dev == NULL)
			COPY((*card)->ipa_devident, buf);
		else
			COPY((*dev)->ipa_devclass, buf);
		return 0;

	case ISAPNP_TAG_END:
		*dev = NULL;
		return 0;

	default:
		/* Handled below */
		break;
	}


	/*
	 * Decide which configuration we add the tag to
	 */
	if (*conf)
		pa = *conf;
	else if (*dev)
		pa = *dev;
	else
		/* error */
		return -1;

	switch (tag) {
	case ISAPNP_TAG_IRQ_FORMAT:
		if (len < 2)
			break;

		if (len != 3)
			buf[2] = ISAPNP_IRQTYPE_EDGE_PLUS;

		p = &pa->ipa_irq[pa->ipa_nirq++];
		p->bits = buf[0] | (buf[1] << 8);
		p->flags = buf[2];
#ifdef DEBUG_ISAPNP
		isapnp_print_irq("", p);
#endif
		break;

	case ISAPNP_TAG_DMA_FORMAT:
		if (buf[0] == 0)
			break;

		p = &pa->ipa_drq[pa->ipa_ndrq++];
		p->bits = buf[0];
		p->flags = buf[1];
#ifdef DEBUG_ISAPNP
		isapnp_print_drq("", p);
#endif
		break;


	case ISAPNP_TAG_IO_PORT_DESC:
		r = &pa->ipa_io[pa->ipa_nio++];
		r->flags = buf[0];
		r->minbase = (buf[2] << 8) | buf[1];
		r->maxbase = (buf[4] << 8) | buf[3];
		r->align = buf[5];
		r->length = buf[6];
#ifdef DEBUG_ISAPNP
		isapnp_print_io("", r);
#endif
		break;

	case ISAPNP_TAG_FIXED_IO_PORT_DESC:
		r = &pa->ipa_io[pa->ipa_nio++];
		r->flags = 0;
		r->minbase = (buf[1] << 8) | buf[0];
		r->maxbase = r->minbase;
		r->align = 1;
		r->length = buf[2];
#ifdef DEBUG_ISAPNP
		isapnp_print_io("FIXED ", r);
#endif
		break;

	case ISAPNP_TAG_VENDOR_DEF:
		DPRINTF(("Vendor defined (short)\n"));
		break;

	case ISAPNP_TAG_MEM_RANGE_DESC:
		r = &pa->ipa_mem[pa->ipa_nmem++];
		r->flags = buf[0];
		r->minbase = (buf[2] << 16) | (buf[1] << 8);
		r->maxbase = (buf[4] << 16) | (buf[3] << 8);
		r->align = (buf[6] << 8) | buf[5];
		r->length = (buf[8] << 16) | (buf[7] << 8);
#ifdef DEBUG_ISAPNP
		isapnp_print_mem("", r);
#endif
		break;


	case ISAPNP_TAG_UNICODE_IDENT_STRING:
		DPRINTF(("Unicode Ident\n"));
		break;

	case ISAPNP_TAG_VENDOR_DEFINED:
		DPRINTF(("Vendor defined (long)\n"));
		break;

	case ISAPNP_TAG_MEM32_RANGE_DESC:
		r = &pa->ipa_mem32[pa->ipa_nmem32++];
		r->flags = buf[0];
		r->minbase = (buf[4] << 24) | (buf[3] << 16) |
		    (buf[2] << 8) | buf[1];
		r->maxbase = (buf[8] << 24) | (buf[7] << 16) |
		    (buf[6] << 8) | buf[5];
		r->align = (buf[12] << 24) | (buf[11] << 16) | 
		    (buf[10] << 8) | buf[9];
		r->length = (buf[16] << 24) | (buf[15] << 16) |
		    (buf[14] << 8) | buf[13];
#ifdef DEBUG_ISAPNP
		isapnp_print_mem("32-bit ", r);
#endif
		break;

	case ISAPNP_TAG_FIXED_MEM32_RANGE_DESC:
		r = &pa->ipa_mem32[pa->ipa_nmem32++];
		r->flags = buf[0];
		r->minbase = (buf[4] << 24) | (buf[3] << 16) |
		    (buf[2] << 8) | buf[1];
		r->maxbase = r->minbase;
		r->align = 1;
		r->length = (buf[8] << 24) | (buf[7] << 16) |
		    (buf[6] << 8) | buf[5];
#ifdef DEBUG_ISAPNP
		isapnp_print_mem("FIXED 32-bit ", r);
#endif
		break;

	default:
#ifdef DEBUG_ISAPNP
		{
			int i;
			printf("tag %.2x, len %d: ", tag, len);
			for (i = 0; i < len; i++)
				printf("%.2x ", buf[i]);
			printf("\n");
		}
#endif
		break;
	}
	return 0;
}


/* isapnp_get_resource():
 *	Read the resources for card c
 */
struct isa_attach_args *
isapnp_get_resource(struct isapnp_softc *sc, int c,
    struct isa_attach_args *template)
{
	u_char d, tag;
	u_short len;
	int i;
	int warned = 0;
	struct isa_attach_args *card, *dev = NULL, *conf = NULL;
	u_char buf[ISAPNP_MAX_TAGSIZE], *p;

	bzero(buf, sizeof(buf));

	card = malloc(sizeof(*card), M_DEVBUF, M_WAITOK);
	ISAPNP_CLONE_SETUP(card, template);

#define NEXT_BYTE \
		if (isapnp_wait_status(sc)) \
			goto bad; \
		d = isapnp_read_reg(sc, ISAPNP_RESOURCE_DATA)

	for (i = 0; i < ISAPNP_SERIAL_SIZE; i++) {
		NEXT_BYTE;

		if (d != sc->sc_id[c][i] && i != ISAPNP_SERIAL_SIZE - 1) {
			if (!warned) {
				printf("%s: card %d violates PnP spec; byte %d\n",
				    sc->sc_dev.dv_xname, c + 1, i);
				warned++;
			}
			if (i == 0) {
				/*
				 * Magic! If this is the first byte, we
				 * assume that the tag data begins here.
				 */
				goto parse;
			}
		}
	}

	do {
		NEXT_BYTE;
parse:

		if (d & ISAPNP_LARGE_TAG) {
			tag = d;
			NEXT_BYTE;
			buf[0] = d;
			NEXT_BYTE;
			buf[1] = d;
			len = (buf[1] << 8) | buf[0];
		}
		else {
			tag = (d >> 3) & 0xf;
			len = d & 0x7;
		}

		for (p = buf, i = 0; i < len; i++) {
			NEXT_BYTE;
			if (i < ISAPNP_MAX_TAGSIZE)
				*p++ = d;
		}

		if (len >= ISAPNP_MAX_TAGSIZE) {
			printf("%s: Maximum tag size exceeded, card %d\n",
			    sc->sc_dev.dv_xname, c + 1);
			len = ISAPNP_MAX_TAGSIZE;
			if (++warned == 10)
				goto bad;
		}

		if (isapnp_process_tag(tag, len, buf, &card, &dev, &conf) == -1) {
			printf("%s: No current device for tag, card %d\n",
			    sc->sc_dev.dv_xname, c + 1);
			if (++warned == 10)
				goto bad;
		}
	}
	while (tag != ISAPNP_TAG_END);
	return isapnp_flatten(card);

bad:
	for (card = isapnp_flatten(card); card; ) {
		dev = card->ipa_sibling;
		free(card, M_DEVBUF, 0);
		card = dev;
	}
	printf("%s: %s, card %d\n", sc->sc_dev.dv_xname,
	    warned >= 10 ? "Too many tag errors" : "Resource timeout", c + 1);
	return NULL;
}
