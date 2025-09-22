/*	$OpenBSD: asp.c,v 1.16 2025/06/28 13:24:21 miod Exp $	*/

/*
 * Copyright (c) 1998-2003 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * References:
 *
 * 1. Cobra/Coral I/O Subsystem External Reference Specification
 *    Hewlett-Packard
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/reboot.h>

#include <machine/bus.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>
#include <machine/cpufunc.h>

#include <hppa/dev/cpudevs.h>

#include <hppa/gsc/gscbusvar.h>

struct asp_hwr {
	u_int8_t asp_reset;
	u_int8_t asp_resv[31];
	u_int8_t asp_version;
	u_int8_t asp_resv1[15];
	u_int8_t asp_scsidsync;
	u_int8_t asp_resv2[15];
	u_int8_t asp_error;
};

struct asp_trs {
	struct gscbus_ic asp_ic;
	u_int8_t  asp_cled;
	u_int8_t  asp_resv[3];
	struct {
		u_int		:20,
			asp_spu	: 3,	/* SPU ID board jumper */
#define	ASP_SPUCOBRA	0
#define	ASP_SPUCORAL	1
#define	ASP_SPUBUSH	2
#define	ASP_SPUHARDBALL	3
#define	ASP_SPUSCORPIO	4
#define	ASP_SPUCORAL2	5
			asp_sw	: 1,	/* front switch is normal */
			asp_clk : 1,	/* SCSI clock is doubled */
			asp_lan : 2,	/* LAN iface selector */
#define	ASP_LANINVAL	0
#define	ASP_LANAUI	1
#define	ASP_LANTHIN	2
#define	ASP_LANMISS	3
			asp_lanf: 1,	/* LAN AUI fuse is ok */
			asp_spwr: 1,	/* SCSI power ok */
			asp_scsi: 3;	/* SCSI ctrl ID */
	} _asp_ios;
#define	asp_spu		_asp_ios.asp_spu
#define	asp_sw		_asp_ios.asp_sw
#define	asp_clk		_asp_ios.asp_clk
#define	asp_lan		_asp_ios.asp_lan
#define	asp_lanf	_asp_ios.asp_lanf
#define	asp_spwr	_asp_ios.asp_spwr
#define	asp_scsi	_asp_ios.asp_scsi
};

const struct asp_spus_tag {
	char	name[12];
	int	ledword;
} asp_spus[] = {
	{ "Cobra", 0 },
	{ "Coral", 0 },
	{ "Bushmaster", 0 },
	{ "Hardball", 1 },
	{ "Scorpio", 0 },
	{ "Coral II", 1 },
	{ "#6", 0 },
	{ "#7", 0 }
};

#define	ASP_IOMASK	0xfe000000
/* ASP "Primary Controller" HPA */
#define	ASP_CHPA	0xF0800000

int	aspmatch(struct device *, void *, void *);
void	aspattach(struct device *, struct device *, void *);

const struct cfattach asp_ca = {
	sizeof(struct device), aspmatch, aspattach
};

struct cfdriver asp_cd = {
	NULL, "asp", DV_DULL
};

int
aspmatch(struct device *parent, void *cfdata, void *aux)
{
	struct confargs *ca = aux;
	/* struct cfdata *cf = cfdata; */

	if (ca->ca_type.iodc_type != HPPA_TYPE_BHA ||
	    ca->ca_type.iodc_sv_model != HPPA_BHA_ASP)
		return 0;

	return 1;
}

void
aspattach(struct device *parent, struct device *self, void *aux)
{
	struct confargs *ca = aux;
	volatile struct asp_trs *trs;
	volatile struct asp_hwr *hw;
	struct gscbus_ic *ic;
	struct gsc_attach_args ga;
	bus_space_handle_t ioh;
	int s;

	if (bus_space_map(ca->ca_iot, ca->ca_hpa, IOMOD_HPASIZE, 0, &ioh)) {
		printf(": can't map IO space\n");
		return;
	}

	hw = (struct asp_hwr *)ca->ca_hpa;
	trs = (struct asp_trs *)ASP_CHPA;
	ic = (struct gscbus_ic *)&trs->asp_ic;

#ifdef USELEDS
	machine_ledaddr = &trs->asp_cled;
	machine_ledword = asp_spus[trs->asp_spu].ledword;
#endif

	/* reset ASP */
	/* hw->asp_reset = 1; */
	/* delay(400000); */

	s = splhigh();
	ic->imr = ~0;
	(void)ic->irr;
	ic->imr = 0;
	splx(s);

	printf (": %s rev %d, lan %d scsi %d\n",
	    asp_spus[trs->asp_spu].name, hw->asp_version,
	    trs->asp_lan, trs->asp_scsi);

	ga.ga_ca = *ca;	/* clone from us */
	ga.ga_dp.dp_bc[0] = ga.ga_dp.dp_bc[1];
	ga.ga_dp.dp_bc[1] = ga.ga_dp.dp_bc[2];
	ga.ga_dp.dp_bc[2] = ga.ga_dp.dp_bc[3];
	ga.ga_dp.dp_bc[3] = ga.ga_dp.dp_bc[4];
	ga.ga_dp.dp_bc[4] = ga.ga_dp.dp_bc[5];
	ga.ga_dp.dp_bc[5] = ga.ga_dp.dp_mod;
	ga.ga_dp.dp_mod = 0;
	ga.ga_hpamask = ASP_IOMASK;
	ga.ga_name = "gsc";
	ga.ga_parent = gsc_asp;
	ga.ga_ic = ic;
	config_found(self, &ga, gscprint);
}
