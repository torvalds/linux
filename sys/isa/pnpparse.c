/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Doug Rabson
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/stdarg.h>

#include <isa/isavar.h>
#include <isa/pnpreg.h>
#include <isa/pnpvar.h>

#define	MAXDEP	8

#define I16(p)	((p)[0] + ((p)[1] << 8))
#define I32(p)	(I16(p) + (I16((p)+2) << 16))

void
pnp_printf(u_int32_t id, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	printf("%s: ", pnp_eisaformat(id));
	vprintf(fmt, ap);
	va_end(ap);
}

/* parse a single descriptor */

static int
pnp_parse_desc(device_t dev, u_char tag, u_char *res, int len,
	       struct isa_config *config, int ldn)
{
	char buf[100];
	u_int32_t id;
	u_int32_t compat_id;
	int temp;

	id = isa_get_logicalid(dev);

	if (PNP_RES_TYPE(tag) == 0) {

		/* Small resource */
		switch (PNP_SRES_NUM(tag)) {

		case PNP_TAG_VERSION:
		case PNP_TAG_VENDOR:
			/* these descriptors are quietly ignored */
			break;

		case PNP_TAG_LOGICAL_DEVICE:
		case PNP_TAG_START_DEPENDANT:
		case PNP_TAG_END_DEPENDANT:
			if (bootverbose)
				pnp_printf(id, "unexpected small tag %d\n",
					   PNP_SRES_NUM(tag));
			/* shouldn't happen; quit now */
			return (1);

		case PNP_TAG_COMPAT_DEVICE:
			/*
			 * Got a compatible device id resource.
			 * Should keep a list of compat ids in the device.
			 */
			bcopy(res, &compat_id, 4);
			if (isa_get_compatid(dev) == 0)
				isa_set_compatid(dev, compat_id);
			break;
	    
		case PNP_TAG_IRQ_FORMAT:
			if (config->ic_nirq == ISA_NIRQ) {
				pnp_printf(id, "too many irqs\n");
				return (1);
			}
			if (I16(res) == 0) {
				/* a null descriptor */
				config->ic_irqmask[config->ic_nirq] = 0;
				config->ic_nirq++;
				break;
			}
			if (bootverbose)
				pnp_printf(id, "adding irq mask %#02x\n",
					   I16(res));
			config->ic_irqmask[config->ic_nirq] = I16(res);
			config->ic_nirq++;
			break;

		case PNP_TAG_DMA_FORMAT:
			if (config->ic_ndrq == ISA_NDRQ) {
				pnp_printf(id, "too many drqs\n");
				return (1);
			}
			if (res[0] == 0) {
				/* a null descriptor */
				config->ic_drqmask[config->ic_ndrq] = 0;
				config->ic_ndrq++;
				break;
			}
			if (bootverbose)
				pnp_printf(id, "adding dma mask %#02x\n",
					   res[0]);
			config->ic_drqmask[config->ic_ndrq] = res[0];
			config->ic_ndrq++;
			break;

		case PNP_TAG_IO_RANGE:
			if (config->ic_nport == ISA_NPORT) {
				pnp_printf(id, "too many ports\n");
				return (1);
			}
			if (res[6] == 0) {
				/* a null descriptor */
				config->ic_port[config->ic_nport].ir_start = 0;
				config->ic_port[config->ic_nport].ir_end = 0;
				config->ic_port[config->ic_nport].ir_size = 0;
				config->ic_port[config->ic_nport].ir_align = 0;
				config->ic_nport++;
				break;
			}
			if (bootverbose) {
				pnp_printf(id, "adding io range "
					   "%#x-%#x, size=%#x, "
					   "align=%#x\n",
					   I16(res + 1),
					   I16(res + 3) + res[6]-1,
					   res[6], res[5]);
			}
			config->ic_port[config->ic_nport].ir_start =
			    I16(res + 1);
			config->ic_port[config->ic_nport].ir_end =
			    I16(res + 3) + res[6] - 1;
			config->ic_port[config->ic_nport].ir_size = res[6];
			if (res[5] == 0) {
			    /* Make sure align is at least one */
			    res[5] = 1;
			}
			config->ic_port[config->ic_nport].ir_align = res[5];
			config->ic_nport++;
			pnp_check_quirks(isa_get_vendorid(dev),
					 isa_get_logicalid(dev), ldn, config);
			break;

		case PNP_TAG_IO_FIXED:
			if (config->ic_nport == ISA_NPORT) {
				pnp_printf(id, "too many ports\n");
				return (1);
			}
			if (res[2] == 0) {
				/* a null descriptor */
				config->ic_port[config->ic_nport].ir_start = 0;
				config->ic_port[config->ic_nport].ir_end = 0;
				config->ic_port[config->ic_nport].ir_size = 0;
				config->ic_port[config->ic_nport].ir_align = 0;
				config->ic_nport++;
				break;
			}
			if (bootverbose) {
				pnp_printf(id, "adding fixed io range "
					   "%#x-%#x, size=%#x, "
					   "align=%#x\n",
					   I16(res),
					   I16(res) + res[2] - 1,
					   res[2], 1);
			}
			config->ic_port[config->ic_nport].ir_start = I16(res);
			config->ic_port[config->ic_nport].ir_end =
			    I16(res) + res[2] - 1;
			config->ic_port[config->ic_nport].ir_size = res[2];
			config->ic_port[config->ic_nport].ir_align = 1;
			config->ic_nport++;
			break;

		case PNP_TAG_END:
			if (bootverbose)
				pnp_printf(id, "end config\n");
			return (1);

		default:
			/* Skip this resource */
			pnp_printf(id, "unexpected small tag %d\n",
				      PNP_SRES_NUM(tag));
			break;
		}
	} else {
		/* Large resource */
		switch (PNP_LRES_NUM(tag)) {

		case PNP_TAG_ID_UNICODE:
		case PNP_TAG_LARGE_VENDOR:
			/* these descriptors are quietly ignored */
			break;

		case PNP_TAG_ID_ANSI:
			if (len > sizeof(buf) - 1)
				len = sizeof(buf) - 1;
			bcopy(res, buf, len);

			/*
			 * Trim trailing spaces and garbage.
			 */
			while (len > 0 && buf[len - 1] <= ' ')
				len--;
			buf[len] = '\0';
			device_set_desc_copy(dev, buf);
			break;
			
		case PNP_TAG_MEMORY_RANGE:
			if (config->ic_nmem == ISA_NMEM) {
				pnp_printf(id, "too many memory ranges\n");
				return (1);
			}
			if (I16(res + 7) == 0) {
				/* a null descriptor */
				config->ic_mem[config->ic_nmem].ir_start = 0;
				config->ic_mem[config->ic_nmem].ir_end = 0;
				config->ic_mem[config->ic_nmem].ir_size = 0;
				config->ic_mem[config->ic_nmem].ir_align = 0;
				config->ic_nmem++;
				break;
			}
			if (bootverbose) {
				temp = I16(res + 7) << 8;
				pnp_printf(id, "adding memory range "
					   "%#x-%#x, size=%#x, "
					   "align=%#x\n",
					   I16(res + 1) << 8,
					   (I16(res + 3) << 8) + temp - 1,
					   temp, I16(res + 5));
			}
			config->ic_mem[config->ic_nmem].ir_start =
			    I16(res + 1) << 8;
			config->ic_mem[config->ic_nmem].ir_end =
			    (I16(res + 3) << 8) + (I16(res + 7) << 8) - 1;
			config->ic_mem[config->ic_nmem].ir_size =
			    I16(res + 7) << 8;
			config->ic_mem[config->ic_nmem].ir_align = I16(res + 5);
			if (!config->ic_mem[config->ic_nmem].ir_align)
				config->ic_mem[config->ic_nmem].ir_align =
				    0x10000;
			config->ic_nmem++;
			break;

		case PNP_TAG_MEMORY32_RANGE:
			if (config->ic_nmem == ISA_NMEM) {
				pnp_printf(id, "too many memory ranges\n");
				return (1);
			}
			if (I32(res + 13) == 0) {
				/* a null descriptor */
				config->ic_mem[config->ic_nmem].ir_start = 0;
				config->ic_mem[config->ic_nmem].ir_end = 0;
				config->ic_mem[config->ic_nmem].ir_size = 0;
				config->ic_mem[config->ic_nmem].ir_align = 0;
				config->ic_nmem++;
				break;
			}
			if (bootverbose) {
				pnp_printf(id, "adding memory32 range "
					   "%#x-%#x, size=%#x, "
					   "align=%#x\n",
					   I32(res + 1),
					   I32(res + 5) + I32(res + 13) - 1,
					   I32(res + 13), I32(res + 9));
			}
			config->ic_mem[config->ic_nmem].ir_start = I32(res + 1);
			config->ic_mem[config->ic_nmem].ir_end =
			    I32(res + 5) + I32(res + 13) - 1;
			config->ic_mem[config->ic_nmem].ir_size = I32(res + 13);
			config->ic_mem[config->ic_nmem].ir_align = I32(res + 9);
			config->ic_nmem++;
			break;

		case PNP_TAG_MEMORY32_FIXED:
			if (config->ic_nmem == ISA_NMEM) {
				pnp_printf(id, "too many memory ranges\n");
				return (1);
			}
			if (I32(res + 5) == 0) {
				/* a null descriptor */
				config->ic_mem[config->ic_nmem].ir_start = 0;
				config->ic_mem[config->ic_nmem].ir_end = 0;
				config->ic_mem[config->ic_nmem].ir_size = 0;
				config->ic_mem[config->ic_nmem].ir_align = 0;
				break;
			}
			if (bootverbose) {
				pnp_printf(id, "adding fixed memory32 range "
					   "%#x-%#x, size=%#x\n",
					   I32(res + 1),
					   I32(res + 1) + I32(res + 5) - 1,
					   I32(res + 5));
			}
			config->ic_mem[config->ic_nmem].ir_start = I32(res + 1);
			config->ic_mem[config->ic_nmem].ir_end =
			    I32(res + 1) + I32(res + 5) - 1;
			config->ic_mem[config->ic_nmem].ir_size = I32(res + 5);
			config->ic_mem[config->ic_nmem].ir_align = 1;
			config->ic_nmem++;
			break;

		default:
			/* Skip this resource */
			pnp_printf(id, "unexpected large tag %d\n",
				   PNP_SRES_NUM(tag));
			break;
		}
	}

	return (0);
}

/*
 * Parse a single "dependent" resource combination.
 */

u_char
*pnp_parse_dependant(device_t dev, u_char *resources, int len,
		     struct isa_config *config, int ldn)
{

	return pnp_scan_resources(dev, resources, len, config, ldn,
				  pnp_parse_desc);
}

static void
pnp_merge_resources(device_t dev, struct isa_config *from,
		    struct isa_config *to)
{
	device_t parent;
	int i;

	parent = device_get_parent(dev);
	for (i = 0; i < from->ic_nmem; i++) {
		if (to->ic_nmem == ISA_NMEM) {
			device_printf(parent, "too many memory ranges\n");
			return;
		}
		to->ic_mem[to->ic_nmem] = from->ic_mem[i];
		to->ic_nmem++;
	}
	for (i = 0; i < from->ic_nport; i++) {
		if (to->ic_nport == ISA_NPORT) {
			device_printf(parent, "too many port ranges\n");
			return;
		}
		to->ic_port[to->ic_nport] = from->ic_port[i];
		to->ic_nport++;
	}
	for (i = 0; i < from->ic_nirq; i++) {
		if (to->ic_nirq == ISA_NIRQ) {
			device_printf(parent, "too many irq ranges\n");
			return;
		}
		to->ic_irqmask[to->ic_nirq] = from->ic_irqmask[i];
		to->ic_nirq++;
	}
	for (i = 0; i < from->ic_ndrq; i++) {
		if (to->ic_ndrq == ISA_NDRQ) {
			device_printf(parent, "too many drq ranges\n");
			return;
		}
		to->ic_drqmask[to->ic_ndrq] = from->ic_drqmask[i];
		to->ic_ndrq++;
	}
}

/*
 * Parse resource data for Logical Devices, make a list of available
 * resource configurations, and add them to the device.
 *
 * This function exits as soon as it gets an error reading *ANY*
 * Resource Data or it reaches the end of Resource Data.
 */

void
pnp_parse_resources(device_t dev, u_char *resources, int len, int ldn)
{
	struct isa_config *configs;
	struct isa_config *config;
	device_t parent;
	int priorities[1 + MAXDEP];
	u_char *start;
	u_char *p;
	u_char tag;
	u_int32_t id;
	int ncfgs;
	int l;
	int i;

	parent = device_get_parent(dev);
	id = isa_get_logicalid(dev);

	configs = (struct isa_config *)malloc(sizeof(*configs)*(1 + MAXDEP),
					      M_DEVBUF, M_NOWAIT | M_ZERO);
	if (configs == NULL) {
		device_printf(parent, "No memory to parse PNP data\n");
		return;
	}
	config = &configs[0];
	priorities[0] = 0;
	ncfgs = 1;

	p = resources;
	start = NULL;
	while (len > 0) {
		tag = *p++;
		len--;
		if (PNP_RES_TYPE(tag) == 0) {
			/* Small resource */
			l = PNP_SRES_LEN(tag);
			if (len < l) {
				len = 0;
				continue;
			}
			len -= l;

			switch (PNP_SRES_NUM(tag)) {

			case PNP_TAG_START_DEPENDANT:
				if (start != NULL) {
					/*
					 * Copy the common resources first,
					 * then parse the "dependent" resources.
					 */
					pnp_merge_resources(dev, &configs[0],
							    config);
					pnp_parse_dependant(dev, start,
							    p - start - 1,
							    config, ldn);
				}
				start = p + l;
				if (ncfgs > MAXDEP) {
					device_printf(parent, "too many dependent configs (%d)\n", MAXDEP);
					len = 0;
					break;
				}
				config = &configs[ncfgs];
				/*
				 * If the priority is not specified,
				 * then use the default of 'acceptable'
				 */
				if (l > 0)
					priorities[ncfgs] = p[0];
				else
					priorities[ncfgs] = 1;
				if (bootverbose)
					pnp_printf(id, "start dependent (%d)\n",
						   priorities[ncfgs]);
				ncfgs++;
				break;

			case PNP_TAG_END_DEPENDANT:
				if (start == NULL) {
					device_printf(parent,
						      "malformed resources\n");
					len = 0;
					break;
				}
				/*
				 * Copy the common resources first,
				 * then parse the "dependent" resources.
				 */
				pnp_merge_resources(dev, &configs[0], config);
				pnp_parse_dependant(dev, start, p - start - 1,
						    config, ldn);
				start = NULL;
				if (bootverbose)
					pnp_printf(id, "end dependent\n");
				/*
				 * Back to the common part; clear it
				 * as its contents has already been copied
				 * to each dependent.
				 */
				config = &configs[0];
				bzero(config, sizeof(*config));
				break;

			case PNP_TAG_END:
				if (start != NULL) {
					device_printf(parent,
						      "malformed resources\n");
				}
				len = 0;
				break;

			default:
				if (start != NULL)
					/* defer parsing a dependent section */
					break;
				if (pnp_parse_desc(dev, tag, p, l, config, ldn))
					len = 0;
				break;
			}
			p += l;
		} else {
			/* Large resource */
			if (len < 2) {
				len = 0;
				break;
			}
			l = I16(p);
			p += 2;
			len -= 2;
			if (len < l) {
				len = 0;
				break;
			}
			len -= l;
			if (start == NULL &&
			    pnp_parse_desc(dev, tag, p, l, config, ldn)) {
				len = 0;
				break;
			}
			p += l;
		}
	}

	if (ncfgs == 1) {
		/* Single config without dependants */
		ISA_ADD_CONFIG(parent, dev, priorities[0], &configs[0]);
		free(configs, M_DEVBUF);
		return;
	}

	for (i = 1; i < ncfgs; i++) {
		/*
		 * Merge the remaining part of the common resources,
		 * if any. Strictly speaking, there shouldn't be common/main
		 * resources after the END_DEPENDENT tag.
		 */
		pnp_merge_resources(dev, &configs[0], &configs[i]);
		ISA_ADD_CONFIG(parent, dev, priorities[i], &configs[i]);
	}

	free(configs, M_DEVBUF);
}

u_char
*pnp_scan_resources(device_t dev, u_char *resources, int len,
		    struct isa_config *config, int ldn, pnp_scan_cb *cb)
{
	u_char *p;
	u_char tag;
	int l;

	p = resources;
	while (len > 0) {
		tag = *p++;
		len--;
		if (PNP_RES_TYPE(tag) == 0) {
			/* small resource */
			l = PNP_SRES_LEN(tag);
			if (len < l)
				break;
			if ((*cb)(dev, tag, p, l, config, ldn))
				return (p + l);
			if (PNP_SRES_NUM(tag) == PNP_TAG_END)
				return (p + l);
		} else {
			/* large resource */
			if (len < 2)
				break;
			l = I16(p);
			p += 2;
			len -= 2;
			if (len < l)
				break;
			if ((*cb)(dev, tag, p, l, config, ldn))
				return (p + l);
		}
		p += l;
		len -= l;
	}
	return NULL;
}
