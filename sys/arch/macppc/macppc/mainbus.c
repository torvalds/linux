/*	$OpenBSD: mainbus.c,v 1.28 2023/03/08 04:43:07 guenther Exp $	*/

/*
 * Copyright (c) 1994, 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <dev/ofw/openfirm.h>

/* Definition of the mainbus driver. */
static int	mbmatch(struct device *, void *, void *);
static void	mbattach(struct device *, struct device *, void *);
static int	mbprint(void *, const char *);

const struct cfattach mainbus_ca = {
	sizeof(struct device), mbmatch, mbattach
};
struct cfdriver mainbus_cd = {
	NULL, "mainbus", DV_DULL
};

#define HH_REG_CONF 	0x90

static int
mbmatch(struct device *parent, void *cfdata, void *aux)
{

	/*
	 * That one mainbus is always here.
	 */
	return(1);
}

static void
mbattach(struct device *parent, struct device *self, void *aux)
{
	struct confargs nca;
	char name[64], *t = NULL;
	int reg[4], cpucnt;
	int node, len, tlen;

	node = OF_peer(0);
	len = OF_getprop(node, "model", name, sizeof(name));
	if (len > 1) {
		name[len] = '\0';
		tlen = strlen(name)+1;
		if ((t = malloc(tlen, M_DEVBUF, M_NOWAIT)) != NULL)
			strlcpy(t, name, tlen);

	}

	len = OF_getprop(node, "compatible", name, sizeof(name));
	if (len > 1) {
		name[len] = '\0';
		/* Old World Macintosh */
		if ((strncmp(name, "AAPL", 4)) == 0) {
			size_t plen;

			hw_vendor = "Apple Computer, Inc.";
			plen = strlen(t) + strlen(name) - 3;
			if ((hw_prod = malloc(plen, M_DEVBUF, M_NOWAIT)) != NULL) {
				snprintf(hw_prod, plen, "%s %s", t, name + 5);
				free(t, M_DEVBUF, tlen);
			}
		} else {
			/* New World Macintosh or Unknown */
			hw_vendor = "Apple Computer, Inc.";
			hw_prod = t;
		}
	}
	printf(": model %s\n", hw_prod);

	/*
	 * Try to find and attach all of the CPUs in the machine.
	 */

	cpucnt = 0;
	ncpusfound = 0;
	node = OF_finddevice("/cpus");
	if (node != -1) {
		for (node = OF_child(node); node != 0; node = OF_peer(node)) {
			u_int32_t cpunum;
			int len;
			len = OF_getprop(node, "reg", &cpunum, sizeof cpunum);
			if (len == 4 && cpucnt == cpunum) {
				nca.ca_name = "cpu";
				nca.ca_reg = reg;
				reg[0] = cpucnt;
				config_found(self, &nca, mbprint);
				ncpusfound++;
				cpucnt++;
			}
		}
	}
	if (cpucnt == 0) {
		nca.ca_name = "cpu";
		nca.ca_reg = reg;
		reg[0] = 0;
		ncpusfound++;
		config_found(self, &nca, mbprint);
	}

	/*
	 * Special hack for SMP old world macs which lack /cpus and only have
	 * one cpu node.
	 */
	node = OF_finddevice("/hammerhead");
	if (node != -1) {
		len = OF_getprop(node, "reg", reg, sizeof(reg));
		if (len >= 2) {
			u_char *hh_base;
			int twoway = 0;

			if ((hh_base = mapiodev(reg[0], reg[1])) != NULL) {
				twoway = in32rb(hh_base + HH_REG_CONF) & 0x02;
				unmapiodev(hh_base, reg[1]);
			}
			if (twoway) {
				nca.ca_name = "cpu";
				nca.ca_reg = reg;
				reg[0] = 1;
				ncpusfound++;
				config_found(self, &nca, mbprint);
			}
		}
	}

	for (node = OF_child(OF_peer(0)); node; node=OF_peer(node)) {
		bzero (name, sizeof(name));
		if (OF_getprop(node, "device_type", name, sizeof(name)) <= 0) {
			if (OF_getprop(node, "name", name, sizeof(name)) <= 0) {
				printf ("name not found on node %x\n", node);
				continue;
			}
		}
		if (strcmp(name, "memory") == 0) {
			nca.ca_name = "mem";
			nca.ca_node = node;
			config_found(self, &nca, mbprint);
		}
		if (strcmp(name, "memory-controller") == 0) {
			nca.ca_name = "memc";
			nca.ca_node = node;
			config_found(self, &nca, mbprint);
		}
		if (strcmp(name, "pci") == 0) {
			nca.ca_name = "mpcpcibr";
			nca.ca_node = node;
			config_found(self, &nca, mbprint);
		}
		if (strcmp(name, "ht") == 0) {
			nca.ca_name = "ht";
			nca.ca_node = node;
			config_found(self, &nca, mbprint);
		}
		if (strcmp(name, "smu") == 0) {
			nca.ca_name = "smu";
			nca.ca_node = node;
			config_found(self, &nca, mbprint);
		}
	}
}

static int
mbprint(void *aux, const char *pnp)
{
	struct confargs *ca = aux;
	if (pnp)
		printf("%s at %s", ca->ca_name, pnp);

	return (UNCONF);
}
