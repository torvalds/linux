/* $NetBSD: pcmcia_cis.c,v 1.17 2000/02/10 09:01:52 chopps Exp $ */
/* $FreeBSD$ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997 Marc Horowitz.  All rights reserved.
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
 *	This product includes software developed by Marc Horowitz.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/queue.h>
#include <sys/types.h>

#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pccard/pccardreg.h>
#include <dev/pccard/pccardvar.h>
#include <dev/pccard/pccardvarp.h>
#include <dev/pccard/pccard_cis.h>

#include "card_if.h"

extern int	pccard_cis_debug;

#define PCCARDCISDEBUG
#ifdef PCCARDCISDEBUG
#define	DPRINTF(arg) do { if (pccard_cis_debug) printf arg; } while (0)
#define	DEVPRINTF(arg) do { if (pccard_cis_debug) device_printf arg; } while (0)
#else
#define	DPRINTF(arg)
#define	DEVPRINTF(arg)
#endif

#define	PCCARD_CIS_SIZE		4096

struct cis_state {
	int	count;
	int	gotmfc;
	struct pccard_config_entry temp_cfe;
	struct pccard_config_entry *default_cfe;
	struct pccard_card *card;
	struct pccard_function *pf;
};

static int pccard_parse_cis_tuple(const struct pccard_tuple *, void *);
static int decode_funce(const struct pccard_tuple *, struct pccard_function *);

void
pccard_read_cis(struct pccard_softc *sc)
{
	struct cis_state state;

	bzero(&state, sizeof state);
	state.card = &sc->card;
	state.card->error = 0;
	state.card->cis1_major = -1;
	state.card->cis1_minor = -1;
	state.card->cis1_info[0] = NULL;
	state.card->cis1_info[1] = NULL;
	state.card->cis1_info[2] = NULL;
	state.card->cis1_info[3] = NULL;
	state.card->manufacturer = PCMCIA_VENDOR_INVALID;
	state.card->product = PCMCIA_PRODUCT_INVALID;
	STAILQ_INIT(&state.card->pf_head);
	state.pf = NULL;

	/*
	 * XXX The following shouldn't be needed, but some slow cards
	 * XXX seem to need it still.  Need to investigate if there's
	 * XXX a way to tell if the card is 'ready' or not rather than
	 * XXX sleeping like this.  We're called just after the power
	 * XXX up of the socket.  The standard timing diagrams don't
	 * XXX seem to indicate that a delay is required.  The old
	 * XXX delay was 1s.  This delay is .1s.
	 */
	pause("pccard", hz / 10);
	if (pccard_scan_cis(device_get_parent(sc->dev), sc->dev,
	    pccard_parse_cis_tuple, &state) == -1)
		state.card->error++;
}

int
pccard_scan_cis(device_t bus, device_t dev, pccard_scan_t fct, void *arg)
{
	struct resource *res;
	int rid;
	struct pccard_tuple tuple;
	int longlink_present;
	int longlink_common;
	u_long longlink_addr;		/* Type suspect */
	int mfc_count;
	int mfc_index;
#ifdef PCCARDCISDEBUG
	int cis_none_cnt = 10;	/* Only report 10 CIS_NONEs */
#endif
	struct {
		int	common;
		u_long	addr;
	} mfc[256 / 5];
	int ret;

	ret = 0;

	/* allocate some memory */

	/*
	 * Some reports from the field suggest that a 64k memory boundary
	 * helps card CIS being able to be read.  Try it here and see what
	 * the results actually are.  I'm not sure I understand why this
	 * would make cards work better, but it is easy enough to test.
	 */
	rid = 0;
	res = bus_alloc_resource_anywhere(dev, SYS_RES_MEMORY, &rid,
	    PCCARD_CIS_SIZE, RF_ACTIVE | rman_make_alignment_flags(64*1024));
	if (res == NULL) {
		device_printf(dev, "can't alloc memory to read attributes\n");
		return -1;
	}
	CARD_SET_RES_FLAGS(bus, dev, SYS_RES_MEMORY, rid, PCCARD_A_MEM_ATTR);
	tuple.memt = rman_get_bustag(res);
	tuple.memh = rman_get_bushandle(res);
	tuple.ptr = 0;

	DPRINTF(("cis mem map %#x (resource: %#jx)\n",
	    (unsigned int) tuple.memh, rman_get_start(res)));

	tuple.mult = 2;

	longlink_present = 1;
	longlink_common = 1;
	longlink_addr = 0;

	mfc_count = 0;
	mfc_index = 0;

	DEVPRINTF((dev, "CIS tuple chain:\n"));

	while (1) {
		while (1) {
			/*
			 * Perform boundary check for insane cards.
			 * If CIS is too long, simulate CIS end.
			 * (This check may not be sufficient for
			 * malicious cards.)
			 */
			if (tuple.mult * tuple.ptr >= PCCARD_CIS_SIZE - 1
			    - 32 /* ad hoc value */ ) {
				printf("CIS is too long -- truncating\n");
				tuple.code = CISTPL_END;
			} else {
				/* get the tuple code */
				tuple.code = pccard_cis_read_1(&tuple, tuple.ptr);
			}

			/* two special-case tuples */

			if (tuple.code == CISTPL_NULL) {
#ifdef PCCARDCISDEBUG
				if (cis_none_cnt > 0)
					DPRINTF(("CISTPL_NONE\n 00\n"));
				else if (cis_none_cnt == 0)
					DPRINTF(("TOO MANY CIS_NONE\n"));
				cis_none_cnt--;
#endif
				if ((*fct)(&tuple, arg)) {
					ret = 1;
					goto done;
				}
				tuple.ptr++;
				continue;
			} else if (tuple.code == CISTPL_END) {
				DPRINTF(("CISTPL_END\n ff\n"));
				/* Call the function for the END tuple, since
				   the CIS semantics depend on it */
				if ((*fct)(&tuple, arg)) {
					ret = 1;
					goto done;
				}
				tuple.ptr++;
				break;
			}
			/* now all the normal tuples */

			tuple.length = pccard_cis_read_1(&tuple, tuple.ptr + 1);
			switch (tuple.code) {
			case CISTPL_LONGLINK_A:
			case CISTPL_LONGLINK_C:
				if ((*fct)(&tuple, arg)) {
					ret = 1;
					goto done;
				}
				if (tuple.length < 4) {
					DPRINTF(("CISTPL_LONGLINK_%s too "
					    "short %d\n",
					    longlink_common ? "C" : "A",
					    tuple.length));
					break;
				}
				longlink_present = 1;
				longlink_common = (tuple.code ==
				    CISTPL_LONGLINK_C) ? 1 : 0;
				longlink_addr = pccard_tuple_read_4(&tuple, 0);
				DPRINTF(("CISTPL_LONGLINK_%s %#lx\n",
				    longlink_common ? "C" : "A",
				    longlink_addr));
				break;
			case CISTPL_NO_LINK:
				if ((*fct)(&tuple, arg)) {
					ret = 1;
					goto done;
				}
				longlink_present = 0;
				DPRINTF(("CISTPL_NO_LINK\n"));
				break;
			case CISTPL_CHECKSUM:
				if ((*fct)(&tuple, arg)) {
					ret = 1;
					goto done;
				}
				if (tuple.length < 5) {
					DPRINTF(("CISTPL_CHECKSUM too "
					    "short %d\n", tuple.length));
					break;
				} {
					int16_t offset;
					u_long addr, length;
					u_int cksum, sum;
					int i;

					offset = (uint16_t)
					    pccard_tuple_read_2(&tuple, 0);
					length = pccard_tuple_read_2(&tuple, 2);
					cksum = pccard_tuple_read_1(&tuple, 4);

					addr = tuple.ptr + offset;

					DPRINTF(("CISTPL_CHECKSUM addr=%#lx "
					    "len=%#lx cksum=%#x",
					    addr, length, cksum));

					/*
					 * XXX do more work to deal with
					 * distant regions
					 */
					if ((addr >= PCCARD_CIS_SIZE) ||
					    ((addr + length) >=
					    PCCARD_CIS_SIZE)) {
						DPRINTF((" skipped, "
						    "too distant\n"));
						break;
					}
					sum = 0;
					for (i = 0; i < length; i++)
						sum +=
						    bus_space_read_1(tuple.memt,
						    tuple.memh,
						    addr + tuple.mult * i);
					if (cksum != (sum & 0xff)) {
						DPRINTF((" failed sum=%#x\n",
						    sum));
						device_printf(dev, 
						    "CIS checksum failed\n");
#if 0
						/*
						 * XXX Some working cards have
						 * XXX bad checksums!!
						 */
						ret = -1;
#endif
					} else {
						DPRINTF((" ok\n"));
					}
				}
				break;
			case CISTPL_LONGLINK_MFC:
				if (tuple.length < 1) {
					DPRINTF(("CISTPL_LONGLINK_MFC too "
					    "short %d\n", tuple.length));
					break;
				}
				if (((tuple.length - 1) % 5) != 0) {
					DPRINTF(("CISTPL_LONGLINK_MFC bogus "
					    "length %d\n", tuple.length));
					break;
				}
				/*
				 * this is kind of ad hoc, as I don't have
				 * any real documentation
				 */
				{
					int i, tmp_count;

					/*
					 * put count into tmp var so that
					 * if we have to bail (because it's
					 * a bogus count) it won't be
					 * remembered for later use.
					 */
					tmp_count =
					    pccard_tuple_read_1(&tuple, 0);

					DPRINTF(("CISTPL_LONGLINK_MFC %d",
					    tmp_count));

					/*
					 * make _sure_ it's the right size;
					 * if too short, it may be a weird
					 * (unknown/undefined) format
					 */
					if (tuple.length != (tmp_count*5 + 1)) {
						DPRINTF((" bogus length %d\n",
						    tuple.length));
						break;
					}
					/*
					 * sanity check for a programming
					 * error which is difficult to find
					 * when debugging.
					 */
					if (tmp_count >
					    howmany(sizeof mfc, sizeof mfc[0]))
						panic("CISTPL_LONGLINK_MFC mfc "
						    "count would blow stack");
                                        mfc_count = tmp_count;
					for (i = 0; i < mfc_count; i++) {
						mfc[i].common =
						    (pccard_tuple_read_1(&tuple,
						    1 + 5 * i) ==
						    PCCARD_MFC_MEM_COMMON) ?
						    1 : 0;
						mfc[i].addr =
						    pccard_tuple_read_4(&tuple,
						    1 + 5 * i + 1);
						DPRINTF((" %s:%#lx",
						    mfc[i].common ? "common" :
						    "attr", mfc[i].addr));
					}
					DPRINTF(("\n"));
				}
				/*
				 * for LONGLINK_MFC, fall through to the
				 * function.  This tuple has structural and
				 * semantic content.
				 */
			default:
				{
					if ((*fct)(&tuple, arg)) {
						ret = 1;
						goto done;
					}
				}
				break;
			}	/* switch */
#ifdef PCCARDCISDEBUG
			/* print the tuple */
			{
				int i;

				DPRINTF((" %#02x %#02x", tuple.code,
				    tuple.length));

				for (i = 0; i < tuple.length; i++) {
					DPRINTF((" %#02x",
					    pccard_tuple_read_1(&tuple, i)));
					if ((i % 16) == 13)
						DPRINTF(("\n"));
				}

				if ((i % 16) != 14)
					DPRINTF(("\n"));
			}
#endif
			/* skip to the next tuple */
			tuple.ptr += 2 + tuple.length;
		}

		/*
		 * the chain is done.  Clean up and move onto the next one,
		 * if any.  The loop is here in the case that there is an MFC
		 * card with no longlink (which defaults to existing, == 0).
		 * In general, this means that if one pointer fails, it will
		 * try the next one, instead of just bailing.
		 */
		while (1) {
			if (longlink_present) {
				CARD_SET_RES_FLAGS(bus, dev, SYS_RES_MEMORY,
				    rid, longlink_common ?
				    PCCARD_A_MEM_COM : PCCARD_A_MEM_ATTR);
				DPRINTF(("cis mem map %#x\n",
				    (unsigned int) tuple.memh));
				tuple.mult = longlink_common ? 1 : 2;
				tuple.ptr = longlink_addr;
				longlink_present = 0;
				longlink_common = 1;
				longlink_addr = 0;
			} else if (mfc_count && (mfc_index < mfc_count)) {
				CARD_SET_RES_FLAGS(bus, dev, SYS_RES_MEMORY,
				    rid, mfc[mfc_index].common ?
				    PCCARD_A_MEM_COM : PCCARD_A_MEM_ATTR);
				DPRINTF(("cis mem map %#x\n",
				    (unsigned int) tuple.memh));
				/* set parse state, and point at the next one */
				tuple.mult = mfc[mfc_index].common ? 1 : 2;
				tuple.ptr = mfc[mfc_index].addr;
				mfc_index++;
			} else {
				goto done;
			}

			/* make sure that the link is valid */
			tuple.code = pccard_cis_read_1(&tuple, tuple.ptr);
			if (tuple.code != CISTPL_LINKTARGET) {
				DPRINTF(("CISTPL_LINKTARGET expected, "
				    "code %#02x observed\n", tuple.code));
				continue;
			}
			tuple.length = pccard_cis_read_1(&tuple, tuple.ptr + 1);
			if (tuple.length < 3) {
				DPRINTF(("CISTPL_LINKTARGET too short %d\n",
				    tuple.length));
				continue;
			}
			if ((pccard_tuple_read_1(&tuple, 0) != 'C') ||
			    (pccard_tuple_read_1(&tuple, 1) != 'I') ||
			    (pccard_tuple_read_1(&tuple, 2) != 'S')) {
				DPRINTF(("CISTPL_LINKTARGET magic "
				    "%02x%02x%02x incorrect\n",
				    pccard_tuple_read_1(&tuple, 0),
				    pccard_tuple_read_1(&tuple, 1),
				    pccard_tuple_read_1(&tuple, 2)));
				continue;
			}
			tuple.ptr += 2 + tuple.length;
			break;
		}
	}

done:
	bus_release_resource(dev, SYS_RES_MEMORY, rid, res);

	return (ret);
}

/* XXX this is incredibly verbose.  Not sure what trt is */

void
pccard_print_cis(device_t dev)
{
	struct pccard_softc *sc = PCCARD_SOFTC(dev);
	struct pccard_card *card = &sc->card;
	struct pccard_function *pf;
	struct pccard_config_entry *cfe;
	int i;

	device_printf(dev, "CIS version ");
	if (card->cis1_major == 4) {
		if (card->cis1_minor == 0)
			printf("PCCARD 1.0\n");
		else if (card->cis1_minor == 1)
			printf("PCCARD 2.0 or 2.1\n");
	} else if (card->cis1_major >= 5)
		printf("PC Card Standard %d.%d\n", card->cis1_major, card->cis1_minor);
	else
		printf("unknown (major=%d, minor=%d)\n",
		    card->cis1_major, card->cis1_minor);

	device_printf(dev, "CIS info: ");
	for (i = 0; i < 4; i++) {
		if (card->cis1_info[i] == NULL)
			break;
		if (i)
			printf(", ");
		printf("%s", card->cis1_info[i]);
	}
	printf("\n");

	device_printf(dev, "Manufacturer code %#x, product %#x\n",
	    card->manufacturer, card->product);

	STAILQ_FOREACH(pf, &card->pf_head, pf_list) {
		device_printf(dev, "function %d: ", pf->number);

		switch (pf->function) {
		case PCCARD_FUNCTION_UNSPEC:
			printf("unspecified");
			break;
		case PCCARD_FUNCTION_MULTIFUNCTION:
			printf("multi-function");
			break;
		case PCCARD_FUNCTION_MEMORY:
			printf("memory");
			break;
		case PCCARD_FUNCTION_SERIAL:
			printf("serial port");
			break;
		case PCCARD_FUNCTION_PARALLEL:
			printf("parallel port");
			break;
		case PCCARD_FUNCTION_DISK:
			printf("fixed disk");
			break;
		case PCCARD_FUNCTION_VIDEO:
			printf("video adapter");
			break;
		case PCCARD_FUNCTION_NETWORK:
			printf("network adapter");
			break;
		case PCCARD_FUNCTION_AIMS:
			printf("auto incrementing mass storage");
			break;
		case PCCARD_FUNCTION_SCSI:
			printf("SCSI bridge");
			break;
		case PCCARD_FUNCTION_SECURITY:
			printf("Security services");
			break;
		case PCCARD_FUNCTION_INSTRUMENT:
			printf("Instrument");
			break;
		default:
			printf("unknown (%d)", pf->function);
			break;
		}

		printf(", ccr addr %#x mask %#x\n", pf->ccr_base, pf->ccr_mask);

		STAILQ_FOREACH(cfe, &pf->cfe_head, cfe_list) {
			device_printf(dev, "function %d, config table entry "
			    "%d: ", pf->number, cfe->number);

			switch (cfe->iftype) {
			case PCCARD_IFTYPE_MEMORY:
				printf("memory card");
				break;
			case PCCARD_IFTYPE_IO:
				printf("I/O card");
				break;
			default:
				printf("card type unknown");
				break;
			}

			printf("; irq mask %#x", cfe->irqmask);

			if (cfe->num_iospace) {
				printf("; iomask %#lx, iospace", cfe->iomask);

				for (i = 0; i < cfe->num_iospace; i++) {
					printf(" %#jx", cfe->iospace[i].start);
					if (cfe->iospace[i].length)
						printf("-%#jx",
						    cfe->iospace[i].start +
						    cfe->iospace[i].length - 1);
				}
			}
			if (cfe->num_memspace) {
				printf("; memspace");

				for (i = 0; i < cfe->num_memspace; i++) {
					printf(" %#jx",
					    cfe->memspace[i].cardaddr);
					if (cfe->memspace[i].length)
						printf("-%#jx",
						    cfe->memspace[i].cardaddr +
						    cfe->memspace[i].length - 1);
					if (cfe->memspace[i].hostaddr)
						printf("@%#jx",
						    cfe->memspace[i].hostaddr);
				}
			}
			if (cfe->maxtwins)
				printf("; maxtwins %d", cfe->maxtwins);

			printf(";");

			if (cfe->flags & PCCARD_CFE_MWAIT_REQUIRED)
				printf(" mwait_required");
			if (cfe->flags & PCCARD_CFE_RDYBSY_ACTIVE)
				printf(" rdybsy_active");
			if (cfe->flags & PCCARD_CFE_WP_ACTIVE)
				printf(" wp_active");
			if (cfe->flags & PCCARD_CFE_BVD_ACTIVE)
				printf(" bvd_active");
			if (cfe->flags & PCCARD_CFE_IO8)
				printf(" io8");
			if (cfe->flags & PCCARD_CFE_IO16)
				printf(" io16");
			if (cfe->flags & PCCARD_CFE_IRQSHARE)
				printf(" irqshare");
			if (cfe->flags & PCCARD_CFE_IRQPULSE)
				printf(" irqpulse");
			if (cfe->flags & PCCARD_CFE_IRQLEVEL)
				printf(" irqlevel");
			if (cfe->flags & PCCARD_CFE_POWERDOWN)
				printf(" powerdown");
			if (cfe->flags & PCCARD_CFE_READONLY)
				printf(" readonly");
			if (cfe->flags & PCCARD_CFE_AUDIO)
				printf(" audio");

			printf("\n");
		}
	}

	if (card->error)
		device_printf(dev, "%d errors found while parsing CIS\n",
		    card->error);
}

static int
pccard_parse_cis_tuple(const struct pccard_tuple *tuple, void *arg)
{
	/* most of these are educated guesses */
	static struct pccard_config_entry init_cfe = {
		-1, PCCARD_CFE_RDYBSY_ACTIVE | PCCARD_CFE_WP_ACTIVE |
		PCCARD_CFE_BVD_ACTIVE, PCCARD_IFTYPE_MEMORY,
	};

	struct cis_state *state = arg;

	switch (tuple->code) {
	case CISTPL_END:
		/* if we've seen a LONGLINK_MFC, and this is the first
		 * END after it, reset the function list.  
		 *
		 * XXX This might also be the right place to start a
		 * new function, but that assumes that a function
		 * definition never crosses any longlink, and I'm not
		 * sure about that.  This is probably safe for MFC
		 * cards, but what we have now isn't broken, so I'd
		 * rather not change it.
		 */
		if (state->gotmfc == 1) {
			struct pccard_function *pf, *pfnext;

			for (pf = STAILQ_FIRST(&state->card->pf_head); 
			     pf != NULL; pf = pfnext) {
				pfnext = STAILQ_NEXT(pf, pf_list);
				free(pf, M_DEVBUF);
			}

			STAILQ_INIT(&state->card->pf_head);

			state->count = 0;
			state->gotmfc = 2;
			state->pf = NULL;
		}
		break;
	case CISTPL_LONGLINK_MFC:
		/*
		 * this tuple's structure was dealt with in scan_cis.  here,
		 * record the fact that the MFC tuple was seen, so that
		 * functions declared before the MFC link can be cleaned
		 * up.
		 */
		state->gotmfc = 1;
		break;
#ifdef PCCARDCISDEBUG
	case CISTPL_DEVICE:
	case CISTPL_DEVICE_A:
		{
			u_int reg, dtype, dspeed;

			reg = pccard_tuple_read_1(tuple, 0);
			dtype = reg & PCCARD_DTYPE_MASK;
			dspeed = reg & PCCARD_DSPEED_MASK;

			DPRINTF(("CISTPL_DEVICE%s type=",
			(tuple->code == CISTPL_DEVICE) ? "" : "_A"));
			switch (dtype) {
			case PCCARD_DTYPE_NULL:
				DPRINTF(("null"));
				break;
			case PCCARD_DTYPE_ROM:
				DPRINTF(("rom"));
				break;
			case PCCARD_DTYPE_OTPROM:
				DPRINTF(("otprom"));
				break;
			case PCCARD_DTYPE_EPROM:
				DPRINTF(("eprom"));
				break;
			case PCCARD_DTYPE_EEPROM:
				DPRINTF(("eeprom"));
				break;
			case PCCARD_DTYPE_FLASH:
				DPRINTF(("flash"));
				break;
			case PCCARD_DTYPE_SRAM:
				DPRINTF(("sram"));
				break;
			case PCCARD_DTYPE_DRAM:
				DPRINTF(("dram"));
				break;
			case PCCARD_DTYPE_FUNCSPEC:
				DPRINTF(("funcspec"));
				break;
			case PCCARD_DTYPE_EXTEND:
				DPRINTF(("extend"));
				break;
			default:
				DPRINTF(("reserved"));
				break;
			}
			DPRINTF((" speed="));
			switch (dspeed) {
			case PCCARD_DSPEED_NULL:
				DPRINTF(("null"));
				break;
			case PCCARD_DSPEED_250NS:
				DPRINTF(("250ns"));
				break;
			case PCCARD_DSPEED_200NS:
				DPRINTF(("200ns"));
				break;
			case PCCARD_DSPEED_150NS:
				DPRINTF(("150ns"));
				break;
			case PCCARD_DSPEED_100NS:
				DPRINTF(("100ns"));
				break;
			case PCCARD_DSPEED_EXT:
				DPRINTF(("ext"));
				break;
			default:
				DPRINTF(("reserved"));
				break;
			}
		}
		DPRINTF(("\n"));
		break;
#endif
	case CISTPL_VERS_1:
		if (tuple->length < 6) {
			DPRINTF(("CISTPL_VERS_1 too short %d\n",
			    tuple->length));
			break;
		} {
			int start, i, ch, count;

			state->card->cis1_major = pccard_tuple_read_1(tuple, 0);
			state->card->cis1_minor = pccard_tuple_read_1(tuple, 1);

			for (count = 0, start = 0, i = 0;
			    (count < 4) && ((i + 4) < 256); i++) {
				ch = pccard_tuple_read_1(tuple, 2 + i);
				if (ch == 0xff)
					break;
				state->card->cis1_info_buf[i] = ch;
				if (ch == 0) {
					state->card->cis1_info[count] =
					    state->card->cis1_info_buf + start;
					start = i + 1;
					count++;
				}
			}
			DPRINTF(("CISTPL_VERS_1\n"));
		}
		break;
	case CISTPL_MANFID:
		if (tuple->length < 4) {
			DPRINTF(("CISTPL_MANFID too short %d\n",
			    tuple->length));
			break;
		}
		state->card->manufacturer = pccard_tuple_read_2(tuple, 0);
		state->card->product = pccard_tuple_read_2(tuple, 2);
		/*
		 * This is for xe driver. But not limited to that driver.
		 * In PC Card Standard,
		 * Manufacturer ID: 2byte.
		 * Product ID: typically 2bytes, but there's no limit on its
		 * size.  prodext is a two byte field, so maybe we should
		 * also handle the '6' case.  So far no cards have surfaced
		 * with a length of '6'.
		 */
		if (tuple->length == 5 )
			state->card->prodext = pccard_tuple_read_1(tuple, 4);
		DPRINTF(("CISTPL_MANFID\n"));
		break;
	case CISTPL_FUNCID:
		if (tuple->length < 1) {
			DPRINTF(("CISTPL_FUNCID too short %d\n",
			    tuple->length));
			break;
		}
		if ((state->pf == NULL) || (state->gotmfc == 2)) {
			state->pf = malloc(sizeof(*state->pf), M_DEVBUF,
			    M_NOWAIT | M_ZERO);
			state->pf->number = state->count++;
			state->pf->last_config_index = -1;
			STAILQ_INIT(&state->pf->cfe_head);

			STAILQ_INSERT_TAIL(&state->card->pf_head, state->pf,
			    pf_list);
		}
		state->pf->function = pccard_tuple_read_1(tuple, 0);

		DPRINTF(("CISTPL_FUNCID\n"));
		break;
        case CISTPL_FUNCE:
                if (state->pf == NULL || state->pf->function <= 0) {
                        DPRINTF(("CISTPL_FUNCE is not followed by "
                                "valid CISTPL_FUNCID\n"));
                        break;
                }
                if (tuple->length >= 2)
                        decode_funce(tuple, state->pf);
                DPRINTF(("CISTPL_FUNCE\n"));
                break;
	case CISTPL_CONFIG:
		if (tuple->length < 3) {
			DPRINTF(("CISTPL_CONFIG too short %d\n",
			    tuple->length));
			break;
		} {
			u_int reg, rasz, rmsz, rfsz;
			int i;

			reg = pccard_tuple_read_1(tuple, 0);
			rasz = 1 + ((reg & PCCARD_TPCC_RASZ_MASK) >>
			    PCCARD_TPCC_RASZ_SHIFT);
			rmsz = 1 + ((reg & PCCARD_TPCC_RMSZ_MASK) >>
			    PCCARD_TPCC_RMSZ_SHIFT);
			rfsz = ((reg & PCCARD_TPCC_RFSZ_MASK) >>
			    PCCARD_TPCC_RFSZ_SHIFT);

			if (tuple->length < (rasz + rmsz + rfsz)) {
				DPRINTF(("CISTPL_CONFIG (%d,%d,%d) too "
				    "short %d\n", rasz, rmsz, rfsz,
				    tuple->length));
				break;
			}
			if (state->pf == NULL) {
				state->pf = malloc(sizeof(*state->pf),
				    M_DEVBUF, M_NOWAIT | M_ZERO);
				state->pf->number = state->count++;
				state->pf->last_config_index = -1;
				STAILQ_INIT(&state->pf->cfe_head);

				STAILQ_INSERT_TAIL(&state->card->pf_head,
				    state->pf, pf_list);

				state->pf->function = PCCARD_FUNCTION_UNSPEC;
			}
			state->pf->last_config_index =
			    pccard_tuple_read_1(tuple, 1);

			state->pf->ccr_base = 0;
			for (i = 0; i < rasz; i++)
				state->pf->ccr_base |=
				    ((pccard_tuple_read_1(tuple, 2 + i)) <<
				    (i * 8));

			state->pf->ccr_mask = 0;
			for (i = 0; i < rmsz; i++)
				state->pf->ccr_mask |=
				    ((pccard_tuple_read_1(tuple,
				    2 + rasz + i)) << (i * 8));

			/* skip the reserved area and subtuples */

			/* reset the default cfe for each cfe list */
			state->temp_cfe = init_cfe;
			state->default_cfe = &state->temp_cfe;
		}
		DPRINTF(("CISTPL_CONFIG\n"));
		break;
	case CISTPL_CFTABLE_ENTRY:
		{
			int idx, i;
			u_int reg, reg2;
			u_int intface, def, num;
			u_int power, timing, iospace, irq, memspace, misc;
			struct pccard_config_entry *cfe;

			idx = 0;

			reg = pccard_tuple_read_1(tuple, idx++);
			intface = reg & PCCARD_TPCE_INDX_INTFACE;
			def = reg & PCCARD_TPCE_INDX_DEFAULT;
			num = reg & PCCARD_TPCE_INDX_NUM_MASK;

			/*
			 * this is a little messy.  Some cards have only a
			 * cfentry with the default bit set.  So, as we go
			 * through the list, we add new indexes to the queue,
			 * and keep a pointer to the last one with the
			 * default bit set.  if we see a record with the same
			 * index, as the default, we stash the default and
			 * replace the queue entry. otherwise, we just add
			 * new entries to the queue, pointing the default ptr
			 * at them if the default bit is set.  if we get to
			 * the end with the default pointer pointing at a
			 * record which hasn't had a matching index, that's
			 * ok; it just becomes a cfentry like any other.
			 */

			/*
			 * if the index in the cis differs from the default
			 * cis, create new entry in the queue and start it
			 * with the current default
			 */
			if (num != state->default_cfe->number) {
				cfe = (struct pccard_config_entry *)
				    malloc(sizeof(*cfe), M_DEVBUF, M_NOWAIT);
				if (cfe == NULL) {
					DPRINTF(("no memory for config entry\n"));
					goto abort_cfe;
				}
				*cfe = *state->default_cfe;

				STAILQ_INSERT_TAIL(&state->pf->cfe_head,
				    cfe, cfe_list);

				cfe->number = num;

				/*
				 * if the default bit is set in the cis, then
				 * point the new default at whatever is being
				 * filled in
				 */
				if (def)
					state->default_cfe = cfe;
			} else {
				/*
				 * the cis index matches the default index,
				 * fill in the default cfentry.  It is
				 * assumed that the cfdefault index is in the
				 * queue.  For it to be otherwise, the cis
				 * index would have to be -1 (initial
				 * condition) which is not possible, or there
				 * would have to be a preceding cis entry
				 * which had the same cis index and had the
				 * default bit unset. Neither condition
				 * should happen.  If it does, this cfentry
				 * is lost (written into temp space), which
				 * is an acceptable failure mode.
				 */

				cfe = state->default_cfe;

				/*
				 * if the cis entry does not have the default
				 * bit set, copy the default out of the way
				 * first.
				 */
				if (!def) {
					state->temp_cfe = *state->default_cfe;
					state->default_cfe = &state->temp_cfe;
				}
			}

			if (intface) {
				reg = pccard_tuple_read_1(tuple, idx++);
				cfe->flags &= ~(PCCARD_CFE_MWAIT_REQUIRED
				    | PCCARD_CFE_RDYBSY_ACTIVE
				    | PCCARD_CFE_WP_ACTIVE
				    | PCCARD_CFE_BVD_ACTIVE);
				if (reg & PCCARD_TPCE_IF_MWAIT)
					cfe->flags |= PCCARD_CFE_MWAIT_REQUIRED;
				if (reg & PCCARD_TPCE_IF_RDYBSY)
					cfe->flags |= PCCARD_CFE_RDYBSY_ACTIVE;
				if (reg & PCCARD_TPCE_IF_WP)
					cfe->flags |= PCCARD_CFE_WP_ACTIVE;
				if (reg & PCCARD_TPCE_IF_BVD)
					cfe->flags |= PCCARD_CFE_BVD_ACTIVE;
				cfe->iftype = reg & PCCARD_TPCE_IF_IFTYPE;
			}
			reg = pccard_tuple_read_1(tuple, idx++);

			power = reg & PCCARD_TPCE_FS_POWER_MASK;
			timing = reg & PCCARD_TPCE_FS_TIMING;
			iospace = reg & PCCARD_TPCE_FS_IOSPACE;
			irq = reg & PCCARD_TPCE_FS_IRQ;
			memspace = reg & PCCARD_TPCE_FS_MEMSPACE_MASK;
			misc = reg & PCCARD_TPCE_FS_MISC;

			if (power) {
				/* skip over power, don't save */
				/* for each parameter selection byte */
				for (i = 0; i < power; i++) {
					reg = pccard_tuple_read_1(tuple, idx++);
					for (; reg; reg >>= 1)
					{
						/* set bit -> read */
						if ((reg & 1) == 0)
							continue;
						/* skip over bytes */
						do {
							reg2 = pccard_tuple_read_1(tuple, idx++);
							/*
							 * until non-extension
							 * byte
							 */
						} while (reg2 & 0x80);
					}
				}
			}
			if (timing) {
				/* skip over timing, don't save */
				reg = pccard_tuple_read_1(tuple, idx++);

				if ((reg & PCCARD_TPCE_TD_RESERVED_MASK) !=
				    PCCARD_TPCE_TD_RESERVED_MASK)
					idx++;
				if ((reg & PCCARD_TPCE_TD_RDYBSY_MASK) !=
				    PCCARD_TPCE_TD_RDYBSY_MASK)
					idx++;
				if ((reg & PCCARD_TPCE_TD_WAIT_MASK) !=
				    PCCARD_TPCE_TD_WAIT_MASK)
					idx++;
			}
			if (iospace) {
				if (tuple->length <= idx) {
					DPRINTF(("ran out of space before TCPE_IO\n"));
					goto abort_cfe;
				}

				reg = pccard_tuple_read_1(tuple, idx++);
				cfe->flags &=
				    ~(PCCARD_CFE_IO8 | PCCARD_CFE_IO16);
				if (reg & PCCARD_TPCE_IO_BUSWIDTH_8BIT)
					cfe->flags |= PCCARD_CFE_IO8;
				if (reg & PCCARD_TPCE_IO_BUSWIDTH_16BIT)
					cfe->flags |= PCCARD_CFE_IO16;
				cfe->iomask =
				    reg & PCCARD_TPCE_IO_IOADDRLINES_MASK;

				if (reg & PCCARD_TPCE_IO_HASRANGE) {
					reg = pccard_tuple_read_1(tuple, idx++);
					cfe->num_iospace = 1 + (reg &
					    PCCARD_TPCE_IO_RANGE_COUNT);

					if (cfe->num_iospace >
					    (sizeof(cfe->iospace) /
					     sizeof(cfe->iospace[0]))) {
						DPRINTF(("too many io "
						    "spaces %d",
						    cfe->num_iospace));
						state->card->error++;
						break;
					}
					for (i = 0; i < cfe->num_iospace; i++) {
						switch (reg & PCCARD_TPCE_IO_RANGE_ADDRSIZE_MASK) {
						case PCCARD_TPCE_IO_RANGE_ADDRSIZE_ONE:
							cfe->iospace[i].start =
								pccard_tuple_read_1(tuple, idx++);
							break;
						case PCCARD_TPCE_IO_RANGE_ADDRSIZE_TWO:
							cfe->iospace[i].start =
								pccard_tuple_read_2(tuple, idx);
							idx += 2;
							break;
						case PCCARD_TPCE_IO_RANGE_ADDRSIZE_FOUR:
							cfe->iospace[i].start =
								pccard_tuple_read_4(tuple, idx);
							idx += 4;
							break;
						}
						switch (reg &
							PCCARD_TPCE_IO_RANGE_LENGTHSIZE_MASK) {
						case PCCARD_TPCE_IO_RANGE_LENGTHSIZE_ONE:
							cfe->iospace[i].length =
								pccard_tuple_read_1(tuple, idx++);
							break;
						case PCCARD_TPCE_IO_RANGE_LENGTHSIZE_TWO:
							cfe->iospace[i].length =
								pccard_tuple_read_2(tuple, idx);
							idx += 2;
							break;
						case PCCARD_TPCE_IO_RANGE_LENGTHSIZE_FOUR:
							cfe->iospace[i].length =
								pccard_tuple_read_4(tuple, idx);
							idx += 4;
							break;
						}
						cfe->iospace[i].length++;
					}
				} else {
					cfe->num_iospace = 1;
					cfe->iospace[0].start = 0;
					cfe->iospace[0].length =
					    (1 << cfe->iomask);
				}
			}
			if (irq) {
				if (tuple->length <= idx) {
					DPRINTF(("ran out of space before TCPE_IR\n"));
					goto abort_cfe;
				}

				reg = pccard_tuple_read_1(tuple, idx++);
				cfe->flags &= ~(PCCARD_CFE_IRQSHARE
				    | PCCARD_CFE_IRQPULSE
				    | PCCARD_CFE_IRQLEVEL);
				if (reg & PCCARD_TPCE_IR_SHARE)
					cfe->flags |= PCCARD_CFE_IRQSHARE;
				if (reg & PCCARD_TPCE_IR_PULSE)
					cfe->flags |= PCCARD_CFE_IRQPULSE;
				if (reg & PCCARD_TPCE_IR_LEVEL)
					cfe->flags |= PCCARD_CFE_IRQLEVEL;

				if (reg & PCCARD_TPCE_IR_HASMASK) {
					/*
					 * it's legal to ignore the
					 * special-interrupt bits, so I will
					 */

					cfe->irqmask =
					    pccard_tuple_read_2(tuple, idx);
					idx += 2;
				} else {
					cfe->irqmask =
					    (1 << (reg & PCCARD_TPCE_IR_IRQ));
				}
			} else {
				cfe->irqmask = 0xffff;
			}
			if (memspace) {
				if (tuple->length <= idx) {
					DPRINTF(("ran out of space before TCPE_MS\n"));
					goto abort_cfe;
				}

				if (memspace == PCCARD_TPCE_FS_MEMSPACE_LENGTH) {
					cfe->num_memspace = 1;
					cfe->memspace[0].length = 256 *
					    pccard_tuple_read_2(tuple, idx);
					idx += 2;
					cfe->memspace[0].cardaddr = 0;
					cfe->memspace[0].hostaddr = 0;
				} else if (memspace ==
				    PCCARD_TPCE_FS_MEMSPACE_LENGTHADDR) {
					cfe->num_memspace = 1;
					cfe->memspace[0].length = 256 *
					    pccard_tuple_read_2(tuple, idx);
					idx += 2;
					cfe->memspace[0].cardaddr = 256 *
					    pccard_tuple_read_2(tuple, idx);
					idx += 2;
					cfe->memspace[0].hostaddr = cfe->memspace[0].cardaddr;
				} else {
					int lengthsize;
					int cardaddrsize;
					int hostaddrsize;

					reg = pccard_tuple_read_1(tuple, idx++);
					cfe->num_memspace = (reg &
					    PCCARD_TPCE_MS_COUNT) + 1;
					if (cfe->num_memspace >
					    (sizeof(cfe->memspace) /
					     sizeof(cfe->memspace[0]))) {
						DPRINTF(("too many mem "
						    "spaces %d",
						    cfe->num_memspace));
						state->card->error++;
						break;
					}
					lengthsize =
						((reg & PCCARD_TPCE_MS_LENGTH_SIZE_MASK) >>
						 PCCARD_TPCE_MS_LENGTH_SIZE_SHIFT);
					cardaddrsize =
						((reg & PCCARD_TPCE_MS_CARDADDR_SIZE_MASK) >>
						 PCCARD_TPCE_MS_CARDADDR_SIZE_SHIFT);
					hostaddrsize =
						(reg & PCCARD_TPCE_MS_HOSTADDR) ? cardaddrsize : 0;

					if (lengthsize == 0) {
						DPRINTF(("cfe memspace "
						    "lengthsize == 0\n"));
					}
					for (i = 0; i < cfe->num_memspace; i++) {
						if (lengthsize) {
							cfe->memspace[i].length =
								256 * pccard_tuple_read_n(tuple, lengthsize,
								       idx);
							idx += lengthsize;
						} else {
							cfe->memspace[i].length = 0;
						}
						if (cfe->memspace[i].length == 0) {
							DPRINTF(("cfe->memspace[%d].length == 0\n",
								 i));
						}
						if (cardaddrsize) {
							cfe->memspace[i].cardaddr =
								256 * pccard_tuple_read_n(tuple, cardaddrsize,
								       idx);
							idx += cardaddrsize;
						} else {
							cfe->memspace[i].cardaddr = 0;
						}
						if (hostaddrsize) {
							cfe->memspace[i].hostaddr =
								256 * pccard_tuple_read_n(tuple, hostaddrsize,
								       idx);
							idx += hostaddrsize;
						} else {
							cfe->memspace[i].hostaddr = 0;
						}
					}
				}
			} else
				cfe->num_memspace = 0;
			if (misc) {
				if (tuple->length <= idx) {
					DPRINTF(("ran out of space before TCPE_MI\n"));
					goto abort_cfe;
				}

				reg = pccard_tuple_read_1(tuple, idx++);
				cfe->flags &= ~(PCCARD_CFE_POWERDOWN
				    | PCCARD_CFE_READONLY
				    | PCCARD_CFE_AUDIO);
				if (reg & PCCARD_TPCE_MI_PWRDOWN)
					cfe->flags |= PCCARD_CFE_POWERDOWN;
				if (reg & PCCARD_TPCE_MI_READONLY)
					cfe->flags |= PCCARD_CFE_READONLY;
				if (reg & PCCARD_TPCE_MI_AUDIO)
					cfe->flags |= PCCARD_CFE_AUDIO;
				cfe->maxtwins = reg & PCCARD_TPCE_MI_MAXTWINS;

				while (reg & PCCARD_TPCE_MI_EXT) {
					reg = pccard_tuple_read_1(tuple, idx++);
				}
			}
			/* skip all the subtuples */
		}

	abort_cfe:
		DPRINTF(("CISTPL_CFTABLE_ENTRY\n"));
		break;
	default:
		DPRINTF(("unhandled CISTPL %#x\n", tuple->code));
		break;
	}

	return (0);
}

static int
decode_funce(const struct pccard_tuple *tuple, struct pccard_function *pf)
{
	int i;
	int len;
	int type = pccard_tuple_read_1(tuple, 0);

	switch (pf->function) {
	case PCCARD_FUNCTION_DISK:
		if (type == PCCARD_TPLFE_TYPE_DISK_DEVICE_INTERFACE) {
			pf->pf_funce_disk_interface
				= pccard_tuple_read_1(tuple, 1);
			pf->pf_funce_disk_power
				= pccard_tuple_read_1(tuple, 2);
		}
		break;
	case PCCARD_FUNCTION_NETWORK:
		if (type == PCCARD_TPLFE_TYPE_LAN_NID) {
			len = pccard_tuple_read_1(tuple, 1);
			if (tuple->length < 2 + len || len > 8) {
				/* tuple length not enough or nid too long */
				break;
                        }
			for (i = 0; i < len; i++) {
				pf->pf_funce_lan_nid[i]
					= pccard_tuple_read_1(tuple, i + 2);
			}
			pf->pf_funce_lan_nidlen = len;
		}
		break;
	default:
		break;
	}
	return 0;
}
