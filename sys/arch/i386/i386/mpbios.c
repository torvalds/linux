/*	$OpenBSD: mpbios.c,v 1.48 2024/10/22 21:50:02 jsg Exp $	*/
/*	$NetBSD: mpbios.c,v 1.2 2002/10/01 12:56:57 fvdl Exp $	*/

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by RedBack Networks Inc.
 *
 * Author: Bill Sommerfeld
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1999 Stefan Grefen
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
 *      This product includes software developed by the NetBSD
 *      Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR AND CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * Derived from FreeBSD's mp_machdep.c
 */
/*
 * Copyright (c) 1996, by Steve Passe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

/*
 * The Intel MP-stuff is just one way of x86 SMP systems
 * so only Intel MP specific stuff is here.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/cpuvar.h>
#include <machine/bus.h>
#include <machine/biosvar.h>
#include <machine/mpbiosreg.h>
#include <machine/mpbiosvar.h>

#include <machine/i82093reg.h>
#include <machine/i82093var.h>
#include <machine/i82489reg.h>
#include <machine/i82489var.h>

#include <dev/isa/isareg.h>
#include <dev/pci/pcivar.h>

#include <dev/eisa/eisavar.h>	/* for ELCR* def'ns */

#include "pci.h"

#include "apm.h"
#include "acpi.h"
#if NAPM > 0 && NACPI > 0
extern int haveacpibutusingapm;
#endif

/* descriptions of MP basetable entries */
struct mpbios_baseentry {
	u_int8_t  	type;
	u_int8_t  	length;
	u_int16_t	count;
	const char    	*name;
};

static const char *loc_where[] = {
	"extended bios data area",
	"last page of base memory",
	"bios"
};

struct mp_map {
	vaddr_t 	baseva;
	int	 	vsize;
	paddr_t 	pa;
	paddr_t 	pg;
	int		psize;
};

int	mp_print(void *, const char *);
int	mp_match(struct device *, void *, void *);
const void *mpbios_search(struct device *, paddr_t, int, struct mp_map *);
static __inline int mpbios_cksum(const void *, int);

void	mp_cfg_special_intr(const struct mpbios_int *, u_int32_t *);
void	mp_print_special_intr(int);

void	mp_cfg_pci_intr(const struct mpbios_int *, u_int32_t *);
void	mp_print_pci_intr(int);

void	mp_cfg_eisa_intr(const struct mpbios_int *, u_int32_t *);
void	mp_print_eisa_intr(int);

void	mp_cfg_isa_intr(const struct mpbios_int *, u_int32_t *);
void	mp_print_isa_intr(int);

void	mpbios_cpu(const u_int8_t *, struct device *);
void	mpbios_bus(const u_int8_t *, struct device *);
void	mpbios_ioapic(const u_int8_t *, struct device *);
int	mpbios_int(const u_int8_t *, struct mp_intr_map *);

const void *mpbios_map(paddr_t, int, struct mp_map *);
void	mpbios_unmap(struct mp_map *);

/*
 * globals to help us bounce our way through parsing the config table.
 */

static struct mp_map mp_cfg_table_map;
static struct mp_map mp_fp_map;
const struct mpbios_cth	*mp_cth;
const struct mpbios_fps	*mp_fps;

#ifdef MPVERBOSE
int mp_verbose = 1;
#else
int mp_verbose = 0;
#endif

int	mpbios_match(struct device *, void *, void *);
void	mpbios_attach(struct device *, struct device *, void *);

const struct cfattach mpbios_ca = {
	sizeof(struct device), mpbios_match, mpbios_attach
};

struct cfdriver mpbios_cd = {
	NULL, "mpbios", DV_DULL
};

int
mpbios_match(struct device *parent, void *match, void *aux)
{
	struct cfdata *cf = match;
	struct bios_attach_args *bia = aux;

	if (strcmp(bia->ba_name, cf->cf_driver->cd_name) == 0)
		return (1);
	return (0);
}

void
mpbios_attach(struct device *parent, struct device *self, void *aux)
{
	mpbios_scan(self);
}

int
mp_print(void *aux, const char *pnp)
{
	struct cpu_attach_args *caa = aux;

	if (pnp)
		printf("%s at %s:", caa->caa_name, pnp);
	return (UNCONF);
}

int
mp_match(struct device *parent, void *cfv, void *aux)
{
	struct cfdata *cf = cfv;
	struct cpu_attach_args *caa = aux;

	if (strcmp(caa->caa_name, cf->cf_driver->cd_name))
		return 0;

	return ((*cf->cf_attach->ca_match)(parent, cf, aux));
}

/*
 * Map a chunk of memory read-only and return an appropriately
 * const'ed pointer.
 */
const void *
mpbios_map(paddr_t pa, int len, struct mp_map *handle)
{
	paddr_t pgpa = trunc_page(pa);
	paddr_t endpa = round_page(pa + len);
	vaddr_t va = (vaddr_t)km_alloc(endpa - pgpa, &kv_any, &kp_none,
	    &kd_nowait);
	vaddr_t retva = va + (pa & PGOFSET);

	handle->pa = pa;
	handle->pg = pgpa;
	handle->psize = len;
	handle->baseva = va;
	handle->vsize = endpa - pgpa;

	do {
		pmap_kenter_pa(va, pgpa, PROT_READ);
		va += PAGE_SIZE;
		pgpa += PAGE_SIZE;
	} while (pgpa < endpa);

	return ((const void *)retva);
}

void
mpbios_unmap(struct mp_map *handle)
{
	pmap_kremove(handle->baseva, handle->vsize);
	km_free((void *)handle->baseva, handle->vsize, &kv_any, &kp_none);
}

/*
 * Look for an Intel MP spec table, indicating SMP capable hardware.
 */
int
mpbios_probe(struct device *self)
{
	paddr_t  	ebda, memtop;

	paddr_t		cthpa;
	int		cthlen;
	const u_int8_t	*mpbios_page;
	int 		scan_loc;

	struct		mp_map t;

#if NAPM > 0 && NACPI > 0
	/*
	 * If we have acpi but chose to use apm, then we really should
	 * not go use mpbios.  Systems with usable acpi typically have
	 * unusable mpbios
	 */
	if (haveacpibutusingapm)
		return (0);
#endif

	/*
	 * Skip probe if someone else (e.g. acpi) already provided the
	 * necessary details.
	 */
	if (mp_busses)
		return (0);

	/* see if EBDA exists */

	mpbios_page = mpbios_map(0, PAGE_SIZE, &t);

	/* XXX Ugly magic constants below. */
	ebda = *(const u_int16_t *)(&mpbios_page[0x40e]);
	ebda <<= 4;

	memtop = *(const u_int16_t *)(&mpbios_page[0x413]);
	memtop <<= 10;

	mpbios_page = NULL;
	mpbios_unmap(&t);

	scan_loc = 0;

	if (ebda && ebda < IOM_BEGIN ) {
		mp_fps = mpbios_search(self, ebda, 1024, &mp_fp_map);
		if (mp_fps != NULL)
			goto found;
	}

	scan_loc = 1;

	if (memtop && memtop <= IOM_BEGIN ) {
		mp_fps = mpbios_search(self, memtop - 1024, 1024, &mp_fp_map);
		if (mp_fps != NULL)
			goto found;
	}

	scan_loc = 2;

	mp_fps = mpbios_search(self, BIOS_BASE, BIOS_COUNT, &mp_fp_map);
	if (mp_fps != NULL)
		goto found;

	/* nothing found */
	return (0);

 found:
	if (mp_verbose)
		printf("%s: MP floating pointer found in %s at 0x%lx\n",
		    self->dv_xname, loc_where[scan_loc], mp_fp_map.pa);

	if (mp_fps->pap == 0) {
		if (mp_fps->mpfb1 == 0)
			printf("%s: MP fps invalid: "
			    "no default config and no configuration table\n",
			    self->dv_xname);
		else
			printf("%s: MP default configuration %d not "
    			    "supported\n", self->dv_xname, mp_fps->mpfb1);
		goto err;
	}

	cthpa = mp_fps->pap;

	mp_cth = mpbios_map(cthpa, sizeof (*mp_cth), &mp_cfg_table_map);
	cthlen = mp_cth->base_len;
	mpbios_unmap(&mp_cfg_table_map);

	mp_cth = mpbios_map(cthpa, cthlen, &mp_cfg_table_map);

	if (mp_verbose)
		printf("%s: MP config table at 0x%lx, %d bytes long\n",
		    self->dv_xname, cthpa, cthlen);

	if (mp_cth->signature != MP_CT_SIG) {
		printf("%s: MP signature mismatch (%x vs %x)\n",
		    self->dv_xname,
		    MP_CT_SIG, mp_cth->signature);
		goto err;
	}

	if (mpbios_cksum(mp_cth, cthlen)) {
		printf ("%s: MP Configuration Table checksum mismatch\n",
		    self->dv_xname);
		goto err;
	}
	return (1);

 err:
	if (mp_fps) {
		mp_fps = NULL;
		mpbios_unmap(&mp_fp_map);
	}
	if (mp_cth) {
		mp_cth = NULL;
		mpbios_unmap(&mp_cfg_table_map);
	}
	return (0);
}


/*
 * Simple byte checksum used on config tables.
 */

static __inline int
mpbios_cksum(const void *start, int len)
{
	unsigned char res=0;
	const char *p = start;
	const char *end = p + len;

	while (p < end)
		res += *p++;

	return res;
}


/*
 * Look for the MP floating pointer signature in the given physical
 * address range.
 *
 * We map the memory, scan through it, and unmap it.
 * If we find it, remap the floating pointer structure and return it.
 */

const void *
mpbios_search(struct device *self, paddr_t start, int count, struct mp_map *map)
{
	struct mp_map t;

	int i, len;
	const struct mpbios_fps *m;
	int end = count - sizeof(*m);
	const u_int8_t *base = mpbios_map(start, count, &t);

	if (mp_verbose)
		printf("%s: scanning 0x%lx to 0x%lx for MP signature\n",
		    self->dv_xname, start, start + count - sizeof(*m));

	for (i = 0; i <= end; i += 4) {
		m = (struct mpbios_fps *)&base[i];

		if ((m->signature == MP_FP_SIG) &&
		    ((len = m->length << 4) != 0) &&
		    mpbios_cksum(m, (m->length << 4)) == 0) {
			mpbios_unmap(&t);

			return (mpbios_map(start + i, len, map));
		}
	}
	mpbios_unmap(&t);

	return (0);
}

/*
 * MP configuration table parsing.
 */

static struct mpbios_baseentry mp_conf[] =
{
	{0, 20, 0, "cpu"},
	{1, 8, 0, "bus"},
	{2, 8, 0, "ioapic"},
	{3, 8, 0, "ioint"},
	{4, 8, 0, "lint"},
};

struct mp_bus *mp_busses;
int mp_nbusses;
struct mp_intr_map *mp_intrs;
int mp_nintrs;

struct mp_bus *mp_isa_bus;
struct mp_bus *mp_eisa_bus;

static struct mp_bus extint_bus = {
	"ExtINT",
	-1,
	mp_print_special_intr,
	mp_cfg_special_intr,
	0
};
static struct mp_bus smi_bus = {
	"SMI",
	-1,
	mp_print_special_intr,
	mp_cfg_special_intr,
	0
};
static struct mp_bus nmi_bus = {
	"NMI",
	-1,
	mp_print_special_intr,
	mp_cfg_special_intr,
	0
};


/*
 * 1st pass on BIOS's Intel MP specification table.
 *
 * initializes:
 *	mp_ncpus = 1
 *
 * determines:
 *	cpu_apic_address (common to all CPUs)
 *	ioapic_address[N]
 *	mp_naps
 *	mp_nbusses
 *	mp_napics
 *	nintrs
 */
void
mpbios_scan(struct device *self)
{
	const u_int8_t 	*position, *end;
	int		count;
	int		type;
	int		intr_cnt;
	paddr_t		lapic_base;

	printf(": Intel MP Specification 1.%d\n", mp_fps->spec_rev);

	/*
	 * looks like we've got a MP system.  start setting up
	 * infrastructure..
	 * XXX is this the right place??
	 */

	lapic_base = LAPIC_BASE;
	if (mp_cth != NULL)
		lapic_base = (paddr_t)mp_cth->apic_address;

	lapic_boot_init(lapic_base);

	/*
	 * Walk the table once, counting items
	 */
	for (count = mp_cth->entry_count,
	    position = (const u_int8_t *)mp_cth + sizeof(*mp_cth),
	    end = position + mp_cth->base_len;
	    count-- && position < end;
	    position += mp_conf[type].length) {

		type = *position;
		if (type >= MPS_MCT_NTYPES) {
			printf("%s: unknown entry type %x"
			    " in MP config table\n",
			    self->dv_xname, type);
			end = position;
			break;
		}
		mp_conf[type].count++;
	}

	/*
	 * Walk the table twice, counting int and bus entries
	 */
	for (count = mp_cth->entry_count,
	    intr_cnt = 15,	/* presume all isa irqs missing */
	    position = (const u_int8_t *)mp_cth + sizeof(*mp_cth);
	    count-- && position < end;
	    position += mp_conf[type].length) {
		type = *position;
		if (type == MPS_MCT_BUS) {
			const struct mpbios_bus *bp =
			    (const struct mpbios_bus *)position;
			if (bp->bus_id >= mp_nbusses)
				mp_nbusses = bp->bus_id + 1;
		}

		/*
		 * Count actual interrupt instances.
		 * dst_apic_id of MPS_ALL_APICS means "wired to all
		 * apics of this type".
		 */
		if ((type == MPS_MCT_IOINT) ||
		    (type == MPS_MCT_LINT)) {
			const struct mpbios_int *ie =
			    (const struct mpbios_int *)position;
			if (ie->dst_apic_id != MPS_ALL_APICS)
				intr_cnt++;
			else if (type == MPS_MCT_IOINT)
				intr_cnt +=
				    mp_conf[MPS_MCT_IOAPIC].count;
			else
				intr_cnt += mp_conf[MPS_MCT_CPU].count;
		}
	}

	mp_busses = mallocarray(mp_nbusses, sizeof(struct mp_bus),
	    M_DEVBUF, M_WAITOK|M_ZERO);
	mp_intrs = mallocarray(intr_cnt, sizeof(struct mp_intr_map),
	    M_DEVBUF, M_WAITOK);

	/* re-walk the table, recording info of interest */
	position = (const u_int8_t *)mp_cth + sizeof(*mp_cth);
	count = mp_cth->entry_count;
	mp_nintrs = 0;

	while ((count--) && (position < end)) {
		switch (type = *(u_char *)position) {
		case MPS_MCT_CPU:
			mpbios_cpu(position, self);
			break;
		case MPS_MCT_BUS:
			mpbios_bus(position, self);
			break;
		case MPS_MCT_IOAPIC:
			mpbios_ioapic(position, self);
			break;
		case MPS_MCT_IOINT:
		case MPS_MCT_LINT:
			if (mpbios_int(position,
			    &mp_intrs[mp_nintrs]) == 0)
				mp_nintrs++;
			break;
		default:
			printf("%s: unknown entry type %x "
			    "in MP config table\n",
			    self->dv_xname, type);
			/* NOTREACHED */
			return;
		}

		position += mp_conf[type].length;
	}
	if (mp_verbose && mp_cth->ext_len)
		printf("%s: MP WARNING: %d "
		    "bytes of extended entries not examined\n",
		    self->dv_xname, mp_cth->ext_len);

	/* Clean up. */
	mp_fps = NULL;
	mpbios_unmap(&mp_fp_map);
	if (mp_cth != NULL) {
		mp_cth = NULL;
		mpbios_unmap(&mp_cfg_table_map);
	}

#if NPCI > 0
	if (pci_mode_detect() != 0)
		mpbios_intr_fixup();
#endif
}

int
mpbios_invent(int irq, int type, int bus)
{
	struct mp_intr_map *mip;
	struct mpbios_int e;

	e.type = MPS_MCT_IOINT;
	e.int_type = MPS_INTTYPE_INT;
	switch (type) {
	case IST_EDGE:
		e.int_flags = MPS_INT(MPS_INTPO_ACTHI, MPS_INTTR_EDGE);
		break;

	case IST_LEVEL:
		e.int_flags = MPS_INT(MPS_INTPO_ACTLO, MPS_INTTR_LEVEL);
		break;

	case IST_NONE:
	case IST_PULSE:
		e.int_flags = MPS_INT(MPS_INTPO_DEF, MPS_INTTR_DEF);
		break;
	}
	e.src_bus_id = bus;
	e.src_bus_irq = irq;
	e.dst_apic_id = mp_busses[bus].mb_intrs->ioapic->sc_apicid;
	e.dst_apic_int = irq;

	if (mpbios_int((const u_int8_t *)&e, &mp_intrs[mp_nintrs]) == 0) {
		mip = &mp_intrs[mp_nintrs++];
		return (mip->ioapic_ih | irq);
	}

	return irq;
}

void
mpbios_cpu(const u_int8_t *ent, struct device *self)
{
	const struct mpbios_proc *entry = (const struct mpbios_proc *)ent;
	struct device *mainbus = self->dv_parent->dv_parent;
	struct cpu_attach_args caa;

	/* XXX move this into the CPU attachment goo. */
	/* check for usability */
	if (!(entry->cpu_flags & PROCENTRY_FLAG_EN))
		return;

	/* check for BSP flag */
	if (entry->cpu_flags & PROCENTRY_FLAG_BP)
		caa.cpu_role = CPU_ROLE_BP;
	else {
		caa.cpu_role = CPU_ROLE_AP;
		ncpusfound++;
	}

	caa.caa_name = "cpu";
	caa.cpu_apicid = entry->apic_id;
#ifdef MULTIPROCESSOR
	caa.cpu_func = &mp_cpu_funcs;
#endif
#if 1 /* XXX Will be removed when the real stuff is probed */
	caa.cpu_signature = entry->cpu_signature;

	/*
	 * XXX this is truncated to just contain the low-order 16 bits
	 * of the flags on at least some MP bioses
	 */
	caa.feature_flags = entry->feature_flags;

	/*
	 * XXX some MP bioses don't specify a valid CPU signature; use
	 * the result of the 'cpuid' instruction for the processor
	 * we're running on
	 */
	if ((caa.cpu_signature & 0x00000fff) == 0) {
		caa.cpu_signature = cpu_id;
		caa.feature_flags = cpu_feature;
	}
#endif

	config_found_sm(mainbus, &caa, mp_print, mp_match);
}

/*
 * The following functions conspire to compute base ioapic redirection
 * table entry for a given interrupt line.
 *
 * Fill in: trigger mode, polarity, and possibly delivery mode.
 */
void
mp_cfg_special_intr(const struct mpbios_int *entry, u_int32_t *redir)
{

	/*
	 * All of these require edge triggered, zero vector,
	 * appropriate delivery mode.
	 * see page 13 of the 82093AA datasheet.
	 */
	*redir &= ~IOAPIC_REDLO_DEL_MASK;
	*redir &= ~IOAPIC_REDLO_VECTOR_MASK;
	*redir &= ~IOAPIC_REDLO_LEVEL;

	switch (entry->int_type) {
	case MPS_INTTYPE_NMI:
		*redir |= (IOAPIC_REDLO_DEL_NMI<<IOAPIC_REDLO_DEL_SHIFT);
		break;

	case MPS_INTTYPE_SMI:
		*redir |= (IOAPIC_REDLO_DEL_SMI<<IOAPIC_REDLO_DEL_SHIFT);
		break;
	case MPS_INTTYPE_ExtINT:
		/*
		 * We are using the ioapic in "native" mode.
		 * This indicates where the 8259 is wired to the ioapic
		 * and/or local apic..
		 */
		*redir |= (IOAPIC_REDLO_DEL_EXTINT<<IOAPIC_REDLO_DEL_SHIFT);
		*redir |= (IOAPIC_REDLO_MASK);
		break;
	default:
		panic("unknown MPS interrupt type %d", entry->int_type);
	}
}

/* XXX too much duplicated code here. */

void
mp_cfg_pci_intr(const struct mpbios_int *entry, u_int32_t *redir)
{
	int mpspo = (entry->int_flags >> MPS_INTPO_SHIFT) & MPS_INTPO_MASK;
	int mpstrig = (entry->int_flags >> MPS_INTTR_SHIFT) & MPS_INTTR_MASK;

	*redir &= ~IOAPIC_REDLO_DEL_MASK;
	switch (mpspo) {
	case MPS_INTPO_ACTHI:
		*redir &= ~IOAPIC_REDLO_ACTLO;
		break;
	case MPS_INTPO_DEF:
	case MPS_INTPO_ACTLO:
		*redir |= IOAPIC_REDLO_ACTLO;
		break;
	default:
		panic("unknown MPS interrupt polarity %d", mpspo);
	}

	if (entry->int_type != MPS_INTTYPE_INT) {
		mp_cfg_special_intr(entry, redir);
		return;
	}
	*redir |= (IOAPIC_REDLO_DEL_LOPRI<<IOAPIC_REDLO_DEL_SHIFT);

	switch (mpstrig) {
	case MPS_INTTR_DEF:
	case MPS_INTTR_LEVEL:
		*redir |= IOAPIC_REDLO_LEVEL;
		break;
	case MPS_INTTR_EDGE:
		*redir &= ~IOAPIC_REDLO_LEVEL;
		break;
	default:
		panic("unknown MPS interrupt trigger %d", mpstrig);
	}
}

void
mp_cfg_eisa_intr(const struct mpbios_int *entry, u_int32_t *redir)
{
	int mpspo = (entry->int_flags >> MPS_INTPO_SHIFT) & MPS_INTPO_MASK;
	int mpstrig = (entry->int_flags >> MPS_INTTR_SHIFT) & MPS_INTTR_MASK;

	*redir &= ~IOAPIC_REDLO_DEL_MASK;
	switch (mpspo) {
	case MPS_INTPO_DEF:
	case MPS_INTPO_ACTHI:
		*redir &= ~IOAPIC_REDLO_ACTLO;
		break;
	case MPS_INTPO_ACTLO:
		*redir |= IOAPIC_REDLO_ACTLO;
		break;
	default:
		panic("unknown MPS interrupt polarity %d", mpspo);
	}

	if (entry->int_type != MPS_INTTYPE_INT) {
		mp_cfg_special_intr(entry, redir);
		return;
	}
	*redir |= (IOAPIC_REDLO_DEL_LOPRI<<IOAPIC_REDLO_DEL_SHIFT);

	switch (mpstrig) {
	case MPS_INTTR_LEVEL:
		*redir |= IOAPIC_REDLO_LEVEL;
		break;
	case MPS_INTTR_EDGE:
		*redir &= ~IOAPIC_REDLO_LEVEL;
		break;
	case MPS_INTTR_DEF:
		/*
		 * Set "default" setting based on ELCR value snagged
		 * earlier.
		 */
		if (mp_busses[entry->src_bus_id].mb_data &
		    (1<<entry->src_bus_irq)) {
			*redir |= IOAPIC_REDLO_LEVEL;
		} else {
			*redir &= ~IOAPIC_REDLO_LEVEL;
		}
		break;
	default:
		panic("unknown MPS interrupt trigger %d", mpstrig);
	}
}


void
mp_cfg_isa_intr(const struct mpbios_int *entry, u_int32_t *redir)
{
	int mpspo = (entry->int_flags >> MPS_INTPO_SHIFT) & MPS_INTPO_MASK;
	int mpstrig = (entry->int_flags >> MPS_INTTR_SHIFT) & MPS_INTTR_MASK;

	*redir &= ~IOAPIC_REDLO_DEL_MASK;
	switch (mpspo) {
	case MPS_INTPO_DEF:
	case MPS_INTPO_ACTHI:
		*redir &= ~IOAPIC_REDLO_ACTLO;
		break;
	case MPS_INTPO_ACTLO:
		*redir |= IOAPIC_REDLO_ACTLO;
		break;
	default:
		panic("unknown MPS interrupt polarity %d", mpspo);
	}

	if (entry->int_type != MPS_INTTYPE_INT) {
		mp_cfg_special_intr(entry, redir);
		return;
	}
	*redir |= (IOAPIC_REDLO_DEL_LOPRI << IOAPIC_REDLO_DEL_SHIFT);

	switch (mpstrig) {
	case MPS_INTTR_LEVEL:
		*redir |= IOAPIC_REDLO_LEVEL;
		break;
	case MPS_INTTR_DEF:
	case MPS_INTTR_EDGE:
		*redir &= ~IOAPIC_REDLO_LEVEL;
		break;
	default:
		panic("unknown MPS interrupt trigger %d", mpstrig);
	}
}


void
mp_print_special_intr(int intr)
{
}

void
mp_print_pci_intr(int intr)
{
	printf(" device %d INT_%c", (intr >> 2) & 0x1f, 'A' + (intr & 0x3));
}

void
mp_print_isa_intr(int intr)
{
	printf(" irq %d", intr);
}

void
mp_print_eisa_intr(int intr)
{
	printf(" EISA irq %d", intr);
}

void
mpbios_bus(const u_int8_t *ent, struct device *self)
{
	const struct mpbios_bus *entry = (const struct mpbios_bus *)ent;
	int bus_id = entry->bus_id;

	printf("%s: bus %d is type %6.6s\n", self->dv_xname,
	    bus_id, entry->bus_type);

#ifdef DIAGNOSTIC
	/*
	 * This "should not happen" unless the table changes out
	 * from underneath us
	 */
	if (bus_id >= mp_nbusses) {
		panic("%s: bus number %d out of range?? (type %6.6s)",
		    self->dv_xname, bus_id, entry->bus_type);
	}
#endif

	mp_busses[bus_id].mb_intrs = NULL;

	if (memcmp(entry->bus_type, "PCI   ", 6) == 0) {
		mp_busses[bus_id].mb_name = "pci";
		mp_busses[bus_id].mb_idx = bus_id;
		mp_busses[bus_id].mb_intr_print = mp_print_pci_intr;
		mp_busses[bus_id].mb_intr_cfg = mp_cfg_pci_intr;
	} else if (memcmp(entry->bus_type, "EISA  ", 6) == 0) {
		mp_busses[bus_id].mb_name = "eisa";
		mp_busses[bus_id].mb_idx = bus_id;
		mp_busses[bus_id].mb_intr_print = mp_print_eisa_intr;
		mp_busses[bus_id].mb_intr_cfg = mp_cfg_eisa_intr;

		mp_busses[bus_id].mb_data = inb(ELCR0) | (inb(ELCR1) << 8);

		if (mp_eisa_bus)
			printf("%s: multiple eisa busses?\n",
			    self->dv_xname);
		else
			mp_eisa_bus = &mp_busses[bus_id];
	} else if (memcmp(entry->bus_type, "ISA   ", 6) == 0) {
		mp_busses[bus_id].mb_name = "isa";
		mp_busses[bus_id].mb_idx = bus_id;
		mp_busses[bus_id].mb_intr_print = mp_print_isa_intr;
		mp_busses[bus_id].mb_intr_cfg = mp_cfg_isa_intr;
		if (mp_isa_bus)
			printf("%s: multiple isa busses?\n",
			    self->dv_xname);
		else
			mp_isa_bus = &mp_busses[bus_id];
	} else {
		printf("%s: unsupported bus type %6.6s\n", self->dv_xname,
		    entry->bus_type);
	}
}


void
mpbios_ioapic(const u_int8_t *ent, struct device *self)
{
	const struct mpbios_ioapic *entry = (const struct mpbios_ioapic *)ent;
	struct device *mainbus = self->dv_parent->dv_parent;
	struct apic_attach_args aaa;

	/* XXX let flags checking happen in ioapic driver.. */
	if (!(entry->apic_flags & IOAPICENTRY_FLAG_EN))
		return;

	aaa.aaa_name = "ioapic";
	aaa.apic_id = entry->apic_id;
	aaa.apic_version = entry->apic_version;
	aaa.apic_address = (u_int32_t)entry->apic_address;
	aaa.apic_vecbase = -1;
	aaa.flags = (mp_fps->mpfb2 & 0x80) ? IOAPIC_PICMODE : IOAPIC_VWIRE;

	config_found_sm(mainbus, &aaa, mp_print, mp_match);
}

int
mpbios_int(const u_int8_t *ent, struct mp_intr_map *mpi)
{
	const struct mpbios_int *entry = (const struct mpbios_int *)ent;
	struct mpbios_int rw_entry = *entry;
	struct ioapic_softc *sc = NULL, *sc2;

	struct mp_intr_map *altmpi;
	struct mp_bus *mpb;

	u_int32_t id = entry->dst_apic_id;
	u_int32_t pin = entry->dst_apic_int;
	u_int32_t bus = entry->src_bus_id;
	u_int32_t dev = entry->src_bus_irq;
	u_int32_t type = entry->int_type;
	u_int32_t flags = entry->int_flags;

	rw_entry.dst_apic_id = id;

	switch (type) {
	case MPS_INTTYPE_INT:
		mpb = &(mp_busses[bus]);
		break;
	case MPS_INTTYPE_ExtINT:
		mpb = &extint_bus;
		break;
	case MPS_INTTYPE_SMI:
		mpb = &smi_bus;
		break;
	case MPS_INTTYPE_NMI:
		mpb = &nmi_bus;
		break;
	default:
		panic("unknown MPS interrupt type %d", entry->int_type);
	}
	mpi->bus = mpb;
	mpi->bus_pin = dev;

	mpi->ioapic_ih = APIC_INT_VIA_APIC |
	    ((id << APIC_INT_APIC_SHIFT) | ((pin << APIC_INT_PIN_SHIFT)));

	mpi->type = type;
	mpi->flags = flags;
	mpi->redir = 0;
	if (mpb->mb_intr_cfg == NULL) {
		printf("mpbios: can't find bus %d for apic %d pin %d\n",
		    bus, id, pin);
		return (1);
	}

	(*mpb->mb_intr_cfg)(&rw_entry, &mpi->redir);

	if (entry->type == MPS_MCT_IOINT) {
		sc = ioapic_find(id);
		if (sc == NULL) {
			printf("mpbios: can't find ioapic %d\n", id);
			return (1);
		}

		/*
		 * XXX workaround for broken BIOSs that put the ACPI
		 * global interrupt number in the entry, not the pin
		 * number.
		 */
		if (pin >= sc->sc_apic_sz) {
			sc2 = ioapic_find_bybase(pin);
			if (sc2 != sc) {
				printf("mpbios: bad pin %d for apic %d\n",
				    pin, id);
				return (1);
			}
			printf("mpbios: WARNING: pin %d for apic %d too high; "
			       "assuming ACPI global int value\n", pin, id);
			pin -= sc->sc_apic_vecbase;
		}

		mpi->ioapic = sc;
		mpi->ioapic_pin = pin;

		altmpi = sc->sc_pins[pin].ip_map;

		if (altmpi != NULL) {
			if ((altmpi->type != type) ||
			    (altmpi->flags != flags)) {
				printf(
				    "%s: conflicting map entries for pin %d\n",
				    sc->sc_pic.pic_dev.dv_xname, pin);
			}
		} else {
			sc->sc_pins[pin].ip_map = mpi;
		}
	} else {
		if (pin >= 2)
			printf("pin %d of local apic doesn't exist!\n", pin);
		else {
			mpi->ioapic = NULL;
			mpi->ioapic_pin = pin;
			mpi->cpu_id = id;
		}
	}

	if (mp_verbose) {
		printf("%s: int%d attached to %s",
		    sc ? sc->sc_pic.pic_dev.dv_xname : "local apic", pin,
		    mpb->mb_name);
		if (mpb->mb_idx != -1)
			printf("%d", mpb->mb_idx);

		(*(mpb->mb_intr_print))(dev);

		printf(" (type 0x%x flags 0x%x)\n", type, flags);
	}

	mpi->next = mpb->mb_intrs;
	mpb->mb_intrs = mpi;

	return (0);
}
