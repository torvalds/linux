/*	$OpenBSD: isapnpdebug.c,v 1.4 2022/01/03 09:48:41 jsg Exp $	*/
/*	$NetBSD: isapnpdebug.c,v 1.4 1997/08/03 08:12:23 mikel Exp $	*/

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

#ifdef DEBUG_ISAPNP

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/isa/isapnpreg.h>

#include <dev/isa/isavar.h>

/* isapnp_print_mem():
 *	Print a memory tag
 */
void
isapnp_print_mem(const char *str, const struct isapnp_region *mem)
{
	printf("%sMemory: %s,%sshadowable,decode-%s,%scacheable,%s", str,
	    (mem->flags & ISAPNP_MEMATTR_ROM) ? "ROM," : "RAM,",
	    (mem->flags & ISAPNP_MEMATTR_SHADOWABLE) ? "" : "non-",
	    (mem->flags & ISAPNP_MEMATTR_HIGH_ADDR) ?
		"high-addr," : "range-len,",
	    (mem->flags & ISAPNP_MEMATTR_CACHEABLE) ? "" : "non-",
	    (mem->flags & ISAPNP_MEMATTR_WRITEABLE) ?
		"writable," : "read-only,");

	switch (mem->flags & ISAPNP_MEMWIDTH_MASK) {
	case ISAPNP_MEMWIDTH_8:
		printf("8-bit ");
		break;
	case ISAPNP_MEMWIDTH_16:
		printf("16-bit ");
		break;
	case ISAPNP_MEMWIDTH_8_16:
		printf("8/16-bit ");
		break;
	case ISAPNP_MEMWIDTH_32:
		printf("32-bit ");
		break;
	}

	printf("min 0x%x, max 0x%x, ", mem->minbase, mem->maxbase);
	printf("align 0x%x, length 0x%x\n", mem->align, mem->length);
}


/* isapnp_print_io():
 *	Print an io tag
 */
void
isapnp_print_io(const char *str, const struct isapnp_region *io)
{
	printf("%d %sIO Ports: %d address bits, alignment %d ",
	    io->length, str, (io->flags & ISAPNP_IOFLAGS_16) ? 16 : 10,
	    io->align);

	printf("min 0x%x, max 0x%x\n", io->minbase, io->maxbase);
}


/* isapnp_print_irq():
 *	Print an irq tag
 */
void
isapnp_print_irq(const char *str, const struct isapnp_pin *irq)
{
	int i;

	printf("%sIRQ's supported: ", str);
	for (i = 0; i < 16; i++)
		if (irq->bits & (1 << i))
		    printf("%d ", i);

	if (irq->flags & ISAPNP_IRQTYPE_EDGE_PLUS)
		printf("E+");
	if (irq->flags & ISAPNP_IRQTYPE_EDGE_MINUS)
		printf("E-");
	if (irq->flags & ISAPNP_IRQTYPE_LEVEL_PLUS)
		printf("L+");
	if (irq->flags & ISAPNP_IRQTYPE_LEVEL_MINUS)
		printf("L-");
	printf("\n");
}

/* isapnp_print_drq():
 *	Print a drq tag
 */
void
isapnp_print_drq(const char *str, const struct isapnp_pin *drq)
{
	int i;
	u_char flags = drq->flags;

	printf("%sDRQ's supported: ", str);
	for (i = 0; i < 8; i++)
		if (drq->bits & (1 << i))
		    printf("%d ", i);

	printf("Width: ");
	switch (flags & ISAPNP_DMAWIDTH_MASK) {
	case ISAPNP_DMAWIDTH_8:
		printf("8-bit ");
		break;
	case ISAPNP_DMAWIDTH_8_16:
		printf("8/16-bit ");
		break;
	case ISAPNP_DMAWIDTH_16:
		printf("16-bit ");
		break;
	case ISAPNP_DMAWIDTH_RESERVED:
		printf("Reserved ");
		break;
	}

	printf("Speed: ");
	switch (flags & ISAPNP_DMASPEED_MASK) {
	case ISAPNP_DMASPEED_COMPAT:
		printf("compat ");
		break;
	case ISAPNP_DMASPEED_A:
		printf("A ");
		break;
	case ISAPNP_DMASPEED_B:
		printf("B ");
		break;
	case ISAPNP_DMASPEED_F:
		printf("F ");
		break;
	}

	if (flags & ISAPNP_DMAATTR_MASK)
		printf("Attributes: %s%s%s",
		    (flags & ISAPNP_DMAATTR_BUS_MASTER) ?  "bus master " : "",
		    (flags & ISAPNP_DMAATTR_INCR_8) ?  "incr 8 " : "",
		    (flags & ISAPNP_DMAATTR_INCR_16) ?  "incr 16 " : "");
	printf("\n");
}


/* isapnp_print_dep_start():
 *	Print a start dependencies tag
 */
void
isapnp_print_dep_start(const char *str, const u_char pref)
{

	printf("%sconfig: ", str);
	switch (pref) {
	case ISAPNP_DEP_PREFERRED:
		printf("preferred\n");
		break;
	
	case ISAPNP_DEP_ACCEPTABLE:
		printf("acceptable\n");
		break;

	case ISAPNP_DEP_FUNCTIONAL:
		printf("functional\n");
		break;

	case ISAPNP_DEP_UNSET: 		/* Used internally */
		printf("unset\n");
		break;

	case ISAPNP_DEP_CONFLICTING:	/* Used internally */
		printf("conflicting\n");
		break;

	default:
		printf("invalid\n");
		break;
	}
}

void
isapnp_print_attach(const struct isa_attach_args *pa)
{
	int i;

	printf("Found <%s, %s, %s, %s> ", pa->ipa_devident,
	    pa->ipa_devlogic, pa->ipa_devcompat, pa->ipa_devclass);
	isapnp_print_dep_start("", pa->ipa_pref);

	for (i = 0; i < pa->ipa_nio; i++)
		isapnp_print_io("", &pa->ipa_io[i]);

	for (i = 0; i < pa->ipa_nmem; i++)
		isapnp_print_mem("", &pa->ipa_mem[i]);

	for (i = 0; i < pa->ipa_nirq; i++)
		isapnp_print_irq("", &pa->ipa_irq[i]);

	for (i = 0; i < pa->ipa_ndrq; i++)
		isapnp_print_drq("", &pa->ipa_drq[i]);

	for (i = 0; i < pa->ipa_nmem32; i++)
		isapnp_print_mem("", &pa->ipa_mem32[i]);
}


/* isapnp_get_config():
 *	Get the current configuration of the card
 */
void
isapnp_get_config(struct isapnp_softc *sc, struct isa_attach_args *pa)
{
	int i;
	u_char v0, v1, v2, v3;
	static u_char isapnp_mem_range[] = ISAPNP_MEM_DESC;
	static u_char isapnp_io_range[] = ISAPNP_IO_DESC;
	static u_char isapnp_irq_range[] = ISAPNP_IRQ_DESC;
	static u_char isapnp_drq_range[] = ISAPNP_DRQ_DESC;
	static u_char isapnp_mem32_range[] = ISAPNP_MEM32_DESC;
	struct isapnp_region *r;
	struct isapnp_pin *p;

	bzero(pa, sizeof(*pa));

	for (i = 0; i < sizeof(isapnp_io_range); i++) {
		r = &pa->ipa_io[i];
		v0 = isapnp_read_reg(sc,
		    isapnp_io_range[i] + ISAPNP_IO_BASE_15_8);
		v1 = isapnp_read_reg(sc,
		    isapnp_io_range[i] + ISAPNP_IO_BASE_7_0);
		r->base = (v0 << 8) | v1;
		if (r->base == 0)
			break;
	}
	pa->ipa_nio = i;

	for (i = 0; i < sizeof(isapnp_mem_range); i++) {
		r = &pa->ipa_mem[i];
		v0 = isapnp_read_reg(sc,
		    isapnp_mem_range[i] + ISAPNP_MEM_BASE_23_16);
		v1 = isapnp_read_reg(sc,
		    isapnp_mem_range[i] + ISAPNP_MEM_BASE_15_8);
		r->base = (v0 << 16) | (v1 << 8);
		if (r->base == 0)
			break;

		v0 = isapnp_read_reg(sc,
		    isapnp_mem_range[i] + ISAPNP_MEM_LRANGE_23_16);
		v1 = isapnp_read_reg(sc,
		    isapnp_mem_range[i] + ISAPNP_MEM_LRANGE_15_8);
		r->length = (v0 << 16) | (v1 << 8);
		v0 = isapnp_read_reg(sc,
		    isapnp_mem_range[i] + ISAPNP_MEM_CONTROL);
		r->flags = 0;
		if (v0 & ISAPNP_MEM_CONTROL_LIMIT)
			r->flags |= ISAPNP_MEMATTR_HIGH_ADDR;
		if (v0 & ISAPNP_MEM_CONTROL_16)
			r->flags |= ISAPNP_MEMWIDTH_16;
	}
	pa->ipa_nmem = i;

	for (i = 0; i < sizeof(isapnp_irq_range); i++) {
		v0 = isapnp_read_reg(sc,
		    isapnp_irq_range[i] + ISAPNP_IRQ_NUMBER);
		p = &pa->ipa_irq[i];
		p->num = v0 & 0xf;
		if (p->num == 0)
			break;

		switch (v0 & (ISAPNP_IRQ_LEVEL|ISAPNP_IRQ_HIGH)) {
		case ISAPNP_IRQ_LEVEL|ISAPNP_IRQ_HIGH:
		    p->flags = ISAPNP_IRQTYPE_LEVEL_PLUS;
		    break;
		case ISAPNP_IRQ_HIGH:
		    p->flags = ISAPNP_IRQTYPE_EDGE_PLUS;
		    break;
		case ISAPNP_IRQ_LEVEL:
		    p->flags = ISAPNP_IRQTYPE_LEVEL_MINUS;
		    break;
		default:
		    p->flags = ISAPNP_IRQTYPE_EDGE_MINUS;
		    break;
		}
	}
	pa->ipa_nirq = i;

	for (i = 0; i < sizeof(isapnp_drq_range); i++) {
		v0 = isapnp_read_reg(sc, isapnp_drq_range[i]);
		p = &pa->ipa_drq[i];
		p->num = v0 & 0xf;
		if (p->num == 4)
			break;
	}
	pa->ipa_ndrq = i;

	for (i = 0; i < sizeof(isapnp_mem32_range); i++) {
		r = &pa->ipa_mem32[i];
		v0 = isapnp_read_reg(sc,
		    isapnp_mem32_range[i] + ISAPNP_MEM32_BASE_31_24);
		v1 = isapnp_read_reg(sc,
		    isapnp_mem32_range[i] + ISAPNP_MEM32_BASE_23_16);
		v2 = isapnp_read_reg(sc,
		    isapnp_mem32_range[i] + ISAPNP_MEM32_BASE_15_8);
		v3 = isapnp_read_reg(sc,
		    isapnp_mem32_range[i] + ISAPNP_MEM32_BASE_7_0);
		r->base = (v0 << 24) | (v1 << 16) | (v2 << 8) | v3;
		if (r->base == 0)
			break;

		v0 = isapnp_read_reg(sc,
		    isapnp_mem32_range[i] + ISAPNP_MEM32_LRANGE_31_24);
		v1 = isapnp_read_reg(sc,
		    isapnp_mem32_range[i] + ISAPNP_MEM32_LRANGE_23_16);
		v2 = isapnp_read_reg(sc,
		    isapnp_mem32_range[i] + ISAPNP_MEM32_LRANGE_15_8);
		v3 = isapnp_read_reg(sc,
		    isapnp_mem32_range[i] + ISAPNP_MEM32_LRANGE_7_0);
		r->length = (v0 << 24) | (v1 << 16) | (v2 << 8) | v3;
		v0 = isapnp_read_reg(sc,
		    isapnp_mem_range[i] + ISAPNP_MEM_CONTROL);
		r->flags = v0;
	}
	pa->ipa_nmem32 = i;
}


/* isapnp_print_config():
 *	Print the current configuration of the card
 */
void
isapnp_print_config(const struct isa_attach_args *pa)
{
	int i;
	const struct isapnp_region *r;
	const struct isapnp_pin *p;

	printf("Register configuration:\n");
	if (pa->ipa_nio)
		for (i = 0; i < pa->ipa_nio; i++) {
			r = &pa->ipa_io[i];
			printf("io[%d]: 0x%x/%d\n", i, r->base, r->length);
		}

	if (pa->ipa_nmem)
		for (i = 0; i < pa->ipa_nmem; i++) {
			r = &pa->ipa_mem[i];
			printf("mem[%d]: 0x%x/%d\n", i, r->base, r->length);
		}

	if (pa->ipa_nirq)
		for (i = 0; i < pa->ipa_nirq; i++) {
			p = &pa->ipa_irq[i];
			printf("irq[%d]: %d\n", i, p->num);
		}

	if (pa->ipa_ndrq)
		for (i = 0; i < pa->ipa_ndrq; i++) {
			p = &pa->ipa_drq[i];
			printf("drq[%d]: %d\n", i, p->num);
		}

	if (pa->ipa_nmem32)
		for (i = 0; i < pa->ipa_nmem32; i++) {
			r = &pa->ipa_mem32[i];
			printf("mem32[%d]: 0x%x/%d\n", i, r->base, r->length);
		}
}

#endif
