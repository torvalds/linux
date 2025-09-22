/*	$OpenBSD: mpconfig.h,v 1.8 2011/10/21 20:48:11 kettenis Exp $	*/
/*	$NetBSD: mpconfig.h,v 1.2 2003/05/11 00:05:52 fvdl Exp $	*/

/*
 * Definitions originally from the mpbios code, but now used for ACPI
 * MP config as well.
 */

#ifndef _AMD64_MPCONFIG_H
#define _AMD64_MPCONFIG_H

#ifndef _LOCORE

struct mpbios_int;

struct mp_bus {
	char *mb_name;		/* XXX bus name */
	int mb_idx;		/* XXX bus index */
	void (*mb_intr_print)(int);
	void (*mb_intr_cfg)(const struct mpbios_int *, u_int32_t *);
	struct mp_intr_map *mb_intrs;
	u_int32_t mb_data;	/* random bus-specific datum. */
};

struct mp_intr_map {
	struct mp_intr_map *next;
	struct mp_bus *bus;
	int bus_pin;
	struct ioapic_softc *ioapic;
	int ioapic_pin;
	int ioapic_ih;		/* int handle, for apic_intr_est */
	int type;		/* from mp spec intr record */
 	int flags;		/* from mp spec intr record */
	u_int32_t redir;
	int cpu_id;
};

#if defined(_KERNEL)
extern int mp_verbose;
extern struct mp_bus *mp_busses;
extern int mp_nbusses;
extern struct mp_intr_map *mp_intrs;
extern int mp_nintrs;
extern struct mp_bus *mp_isa_bus;
extern struct mp_bus *mp_eisa_bus;
#endif
#endif

#endif /* _AMD64_MPCONFIG_H */
