/* $OpenBSD: acpicpu_x86.c,v 1.2 2025/09/16 12:18:10 hshoexer Exp $ */
/*
 * Copyright (c) 2005 Marco Peereboom <marco@openbsd.org>
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
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

#include <sys/param.h>
#include <sys/kernel.h>		/* for tick */
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/atomic.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/specialreg.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <sys/sensors.h>

int	acpicpu_match(struct device *, void *, void *);
void	acpicpu_attach(struct device *, struct device *, void *);
int	acpicpu_notify(struct aml_node *, int, void *);
void	acpicpu_setperf(int);
void	acpicpu_setperf_ppc_change(struct acpicpu_pss *, int);

#define ACPI_STATE_C0		0x00
#define ACPI_STATE_C1		0x01
#define ACPI_STATE_C2		0x02
#define ACPI_STATE_C3		0x03

#define ACPI_PDC_REVID		0x1
#define ACPI_PDC_SMP		0xa
#define ACPI_PDC_MSR		0x1

/* _PDC/_OSC Intel capabilities flags */
#define ACPI_PDC_P_FFH		0x0001
#define ACPI_PDC_C_C1_HALT	0x0002
#define ACPI_PDC_T_FFH		0x0004
#define ACPI_PDC_SMP_C1PT	0x0008
#define ACPI_PDC_SMP_C2C3	0x0010
#define ACPI_PDC_SMP_P_SWCOORD	0x0020
#define ACPI_PDC_SMP_C_SWCOORD	0x0040
#define ACPI_PDC_SMP_T_SWCOORD	0x0080
#define ACPI_PDC_C_C1_FFH	0x0100
#define ACPI_PDC_C_C2C3_FFH	0x0200
/* reserved			0x0400 */
#define ACPI_PDC_P_HWCOORD	0x0800
#define ACPI_PDC_PPC_NOTIFY	0x1000

#define CST_METH_HALT		0
#define CST_METH_IO_HALT	1
#define CST_METH_MWAIT		2
#define CST_METH_GAS_IO		3

/* flags on Intel's FFH mwait method */
#define CST_FLAG_MWAIT_HW_COORD		0x1
#define CST_FLAG_MWAIT_BM_AVOIDANCE	0x2
#define CST_FLAG_FALLBACK		0x4000	/* fallback for broken _CST */
#define CST_FLAG_SKIP			0x8000	/* state is worse choice */

#define FLAGS_MWAIT_ONLY	0x02
#define FLAGS_BMCHECK		0x04
#define FLAGS_NOTHROTTLE	0x08
#define FLAGS_NOPSS		0x10
#define FLAGS_NOPCT		0x20

#define CPU_THT_EN		(1L << 4)
#define CPU_MAXSTATE(sc)	(1L << (sc)->sc_duty_wid)
#define CPU_STATE(sc,pct)	((pct * CPU_MAXSTATE(sc) / 100) << (sc)->sc_duty_off)
#define CPU_STATEMASK(sc)	((CPU_MAXSTATE(sc) - 1) << (sc)->sc_duty_off)

#define ACPI_MAX_C2_LATENCY	100
#define ACPI_MAX_C3_LATENCY	1000

#define CSD_COORD_SW_ALL	0xFC
#define CSD_COORD_SW_ANY	0xFD
#define CSD_COORD_HW_ALL	0xFE

/* Make sure throttling bits are valid,a=addr,o=offset,w=width */
#define valid_throttle(o,w,a)	(a && w && (o+w)<=31 && (o>4 || (o+w)<=4))

struct acpi_cstate {
	SLIST_ENTRY(acpi_cstate) link;

	u_short		state;
	short		method;		/* CST_METH_* */
	u_short		flags;		/* CST_FLAG_* */
	u_short		latency;
	int		power;
	uint64_t	address;	/* or mwait hint */
};

unsigned long cst_stats[4] = { 0 };

struct acpicpu_softc {
	struct device		sc_dev;
	int			sc_cpu;

	int			sc_duty_wid;
	int			sc_duty_off;
	uint32_t		sc_pblk_addr;
	int			sc_pblk_len;
	int			sc_flags;
	unsigned long		sc_prev_sleep;
	unsigned long		sc_last_itime;

	struct cpu_info		*sc_ci;
	SLIST_HEAD(,acpi_cstate) sc_cstates;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	int			sc_pss_len;	/* XXX */
	int			sc_ppc;
	int			sc_level;
	struct acpicpu_pss	*sc_pss;
	size_t			sc_pssfulllen;

	struct acpicpu_pct	sc_pct;
	/* save compensation for pct access for lying bios' */
	uint32_t		sc_pct_stat_as;
	uint32_t		sc_pct_ctrl_as;
	uint32_t		sc_pct_stat_len;
	uint32_t		sc_pct_ctrl_len;
	/*
	 * XXX: _PPC Change listener
	 * PPC changes can occur when for example a machine is disconnected
	 * from AC power and can no longer support the highest frequency or
	 * voltage when driven from the battery.
	 * Should probably be reimplemented as a list for now we assume only
	 * one listener
	 */
	void			(*sc_notify)(struct acpicpu_pss *, int);
};

void	acpicpu_add_cstatepkg(struct aml_value *, void *);
void	acpicpu_add_cdeppkg(struct aml_value *, void *);
int	acpicpu_getppc(struct acpicpu_softc *);
int	acpicpu_getpct(struct acpicpu_softc *);
int	acpicpu_getpss(struct acpicpu_softc *);
int	acpicpu_getcst(struct acpicpu_softc *);
void	acpicpu_getcst_from_fadt(struct acpicpu_softc *);
void	acpicpu_print_one_cst(struct acpi_cstate *_cx);
void	acpicpu_print_cst(struct acpicpu_softc *_sc);
void	acpicpu_add_cstate(struct acpicpu_softc *_sc, int _state, int _method,
	    int _flags, int _latency, int _power, uint64_t _address);
void	acpicpu_set_pdc(struct acpicpu_softc *);
void	acpicpu_idle(void);
void	acpicpu_suspend(void);

#if 0
void    acpicpu_set_throttle(struct acpicpu_softc *, int);
struct acpi_cstate *acpicpu_find_cstate(struct acpicpu_softc *, int);
#endif

const struct cfattach acpicpu_ca = {
	sizeof(struct acpicpu_softc), acpicpu_match, acpicpu_attach
};

struct cfdriver acpicpu_cd = {
	NULL, "acpicpu", DV_DULL, CD_COCOVM
};

const char *acpicpu_hids[] = {
	"ACPI0007",
	NULL
};

extern int setperf_prio;

#if 0
void
acpicpu_set_throttle(struct acpicpu_softc *sc, int level)
{
	uint32_t pbval;

	if (sc->sc_flags & FLAGS_NOTHROTTLE)
		return;

	/* Disable throttling control */
	pbval = inl(sc->sc_pblk_addr);
	outl(sc->sc_pblk_addr, pbval & ~CPU_THT_EN);
	if (level < 100) {
		pbval &= ~CPU_STATEMASK(sc);
		pbval |= CPU_STATE(sc, level);
		outl(sc->sc_pblk_addr, pbval & ~CPU_THT_EN);
		outl(sc->sc_pblk_addr, pbval | CPU_THT_EN);
	}
}

struct acpi_cstate *
acpicpu_find_cstate(struct acpicpu_softc *sc, int state)
{
	struct acpi_cstate	*cx;

	SLIST_FOREACH(cx, &sc->sc_cstates, link)
		if (cx->state == state)
			return cx;
	return (NULL);
}
#endif


void
acpicpu_set_pdc(struct acpicpu_softc *sc)
{
	struct aml_value cmd, osc_cmd[4];
	struct aml_value res;
	uint32_t cap;
	uint32_t buf[3];

	/* 4077A616-290C-47BE-9EBD-D87058713953 */
	static uint8_t cpu_oscuuid[16] = { 0x16, 0xA6, 0x77, 0x40, 0x0C, 0x29,
					   0xBE, 0x47, 0x9E, 0xBD, 0xD8, 0x70,
					   0x58, 0x71, 0x39, 0x53 };
	cap = ACPI_PDC_C_C1_HALT | ACPI_PDC_P_FFH | ACPI_PDC_C_C1_FFH
	    | ACPI_PDC_C_C2C3_FFH | ACPI_PDC_SMP_P_SWCOORD | ACPI_PDC_SMP_C2C3
	    | ACPI_PDC_SMP_C1PT;

	if (aml_searchname(sc->sc_devnode, "_OSC")) {
		/* Query _OSC */
		memset(&osc_cmd, 0, sizeof(osc_cmd));
		osc_cmd[0].type = AML_OBJTYPE_BUFFER;
		osc_cmd[0].v_buffer = (uint8_t *)&cpu_oscuuid;
		osc_cmd[0].length = sizeof(cpu_oscuuid);

		osc_cmd[1].type = AML_OBJTYPE_INTEGER;
		osc_cmd[1].v_integer = 1;
		osc_cmd[1].length = 1;

		osc_cmd[2].type = AML_OBJTYPE_INTEGER;
		osc_cmd[2].v_integer = 2;
		osc_cmd[2].length = 1;

		buf[0] = 1;
		buf[1] = cap;
		osc_cmd[3].type = AML_OBJTYPE_BUFFER;
		osc_cmd[3].v_buffer = (int8_t *)&buf;
		osc_cmd[3].length = sizeof(buf);

		aml_evalname(sc->sc_acpi, sc->sc_devnode, "_OSC",
		    4, osc_cmd, &res);

		if (res.type != AML_OBJTYPE_BUFFER || res.length < 8) {
			printf(": unable to query capabilities\n");
			aml_freevalue(&res);
			return;
		}

		/* Evaluate _OSC */
		memset(&osc_cmd, 0, sizeof(osc_cmd));
		osc_cmd[0].type = AML_OBJTYPE_BUFFER;
		osc_cmd[0].v_buffer = (uint8_t *)&cpu_oscuuid;
		osc_cmd[0].length = sizeof(cpu_oscuuid);

		osc_cmd[1].type = AML_OBJTYPE_INTEGER;
		osc_cmd[1].v_integer = 1;
		osc_cmd[1].length = 1;

		osc_cmd[2].type = AML_OBJTYPE_INTEGER;
		osc_cmd[2].v_integer = 2;
		osc_cmd[2].length = 1;

		buf[0] = 0;
		buf[1] = (*(uint32_t *)&res.v_buffer[4]) & cap;
		osc_cmd[3].type = AML_OBJTYPE_BUFFER;
		osc_cmd[3].v_buffer = (int8_t *)&buf;
		osc_cmd[3].length = sizeof(buf);

		aml_freevalue(&res);

		aml_evalname(sc->sc_acpi, sc->sc_devnode, "_OSC",
		    4, osc_cmd, NULL);
	} else {
		/* Evaluate _PDC */
		memset(&cmd, 0, sizeof(cmd));
		cmd.type = AML_OBJTYPE_BUFFER;
		cmd.v_buffer = (uint8_t *)&buf;
		cmd.length = sizeof(buf);

		buf[0] = ACPI_PDC_REVID;
		buf[1] = 1;
		buf[2] = cap;

		aml_evalname(sc->sc_acpi, sc->sc_devnode, "_PDC",
		    1, &cmd, NULL);
	}
}

/*
 * sanity check mwait hints against what cpuid told us
 * ...but because intel screwed up, just check whether cpuid says
 * the given state has _any_ substates.
 */
static int
check_mwait_hints(int state, int hints)
{
	int cstate;
	int num_substates;

	if (cpu_mwait_size == 0)
		return (0);
	cstate = ((hints >> 4) & 0xf) + 1;
	if (cstate == 16)
		cstate = 0;
	else if (cstate > 7) {
		/* out of range of test against CPUID; just trust'em */
		return (1);
	}
	num_substates = (cpu_mwait_states >> (4 * cstate)) & 0xf;
	if (num_substates == 0) {
		printf(": C%d bad (state %d has no substates)", state, cstate);
		return (0);
	}
	return (1);
}

void
acpicpu_add_cstate(struct acpicpu_softc *sc, int state, int method,
    int flags, int latency, int power, uint64_t address)
{
	struct acpi_cstate	*cx;

	dnprintf(10," C%d: latency:.%4x power:%.4x addr:%.16llx\n",
	    state, latency, power, address);

	/* add a new state, or overwrite the fallback C1 state? */
	if (state != ACPI_STATE_C1 ||
	    (cx = SLIST_FIRST(&sc->sc_cstates)) == NULL ||
	    (cx->flags & CST_FLAG_FALLBACK) == 0) {
		cx = malloc(sizeof(*cx), M_DEVBUF, M_WAITOK);
		SLIST_INSERT_HEAD(&sc->sc_cstates, cx, link);
	}

	cx->state = state;
	cx->method = method;
	cx->flags = flags;
	cx->latency = latency;
	cx->power = power;
	cx->address = address;
}

/* Found a _CST object, add new cstate for each entry */
void
acpicpu_add_cstatepkg(struct aml_value *val, void *arg)
{
	struct acpicpu_softc	*sc = arg;
	uint64_t addr;
	struct acpi_grd *grd;
	int state, method, flags;

#if defined(ACPI_DEBUG) && !defined(SMALL_KERNEL)
	aml_showvalue(val);
#endif
	if (val->type != AML_OBJTYPE_PACKAGE || val->length != 4)
		return;

	/* range and sanity checks */
	state = val->v_package[1]->v_integer;
	if (state < 0 || state > 4)
		return;
	if (val->v_package[0]->type != AML_OBJTYPE_BUFFER) {
		printf(": C%d (unexpected ACPI object type %d)",
		    state, val->v_package[0]->type);
		return;
	}
	grd = (struct acpi_grd *)val->v_package[0]->v_buffer;
	if (val->v_package[0]->length != sizeof(*grd) + 2 ||
	    grd->grd_descriptor != LR_GENREGISTER ||
	    grd->grd_length != sizeof(grd->grd_gas) ||
	    val->v_package[0]->v_buffer[sizeof(*grd)] != SRT_ENDTAG) {
		printf(": C%d (bogo buffer)", state);
		return;
	}

	flags = 0;
	switch (grd->grd_gas.address_space_id) {
	case GAS_FUNCTIONAL_FIXED:
		if (grd->grd_gas.register_bit_width == 0) {
			method = CST_METH_HALT;
			addr = 0;
		} else {
			/*
			 * In theory we should only do this for
			 * vendor 1 == Intel but other values crop up,
			 * presumably due to the normal ACPI spec confusion.
			 */
			switch (grd->grd_gas.register_bit_offset) {
			case 0x1:
				method = CST_METH_IO_HALT;
				addr = grd->grd_gas.address;

				/* i386 and amd64 I/O space is 16bits */
				if (addr > 0xffff) {
					printf(": C%d (bogo I/O addr %llx)",
					    state, addr);
					return;
				}
				break;
			case 0x2:
				addr = grd->grd_gas.address;
				if (!check_mwait_hints(state, addr))
					return;
				method = CST_METH_MWAIT;
				flags = grd->grd_gas.access_size;
				break;
			default:
				printf(": C%d (unknown FFH class %d)",
				    state, grd->grd_gas.register_bit_offset);
				return;
			}
		}
		break;

	case GAS_SYSTEM_IOSPACE:
		addr = grd->grd_gas.address;
		if (grd->grd_gas.register_bit_width != 8 ||
		    grd->grd_gas.register_bit_offset != 0) {
			printf(": C%d (unhandled %s spec: %d/%d)", state,
			    "I/O", grd->grd_gas.register_bit_width,
			    grd->grd_gas.register_bit_offset);
			return;
		}
		method = CST_METH_GAS_IO;
		break;

	default:
		/* dump the GAS for analysis */
		{
			int i;
			printf(": C%d (unhandled GAS:", state);
			for (i = 0; i < sizeof(grd->grd_gas); i++)
				printf(" %#x", ((u_char *)&grd->grd_gas)[i]);
			printf(")");

		}
		return;
	}

	acpicpu_add_cstate(sc, state, method, flags,
	    val->v_package[2]->v_integer, val->v_package[3]->v_integer, addr);
}


/* Found a _CSD object, print the dependency  */
void
acpicpu_add_cdeppkg(struct aml_value *val, void *arg)
{
	int64_t	num_proc, coord_type, domain, cindex;

	/*
	 * errors: unexpected object type, bad length, mismatched length,
	 * and bad CSD revision
	 */
	if (val->type != AML_OBJTYPE_PACKAGE || val->length < 6 ||
	    val->length != val->v_package[0]->v_integer ||
	    val->v_package[1]->v_integer != 0) {
#if 1 || defined(ACPI_DEBUG) && !defined(SMALL_KERNEL)
		aml_showvalue(val);
#endif
		printf("bogus CSD\n");
		return;
	}

	/* coordinating 'among' one CPU is trivial, ignore */
	num_proc = val->v_package[4]->v_integer;
	if (num_proc == 1)
		return;

	/* we practically assume the hardware will coordinate, so ignore */
	coord_type = val->v_package[3]->v_integer;
	if (coord_type == CSD_COORD_HW_ALL)
		return;

	domain = val->v_package[2]->v_integer;
	cindex = val->v_package[5]->v_integer;
	printf(": CSD (c=%#llx d=%lld n=%lld i=%lli)",
	    coord_type, domain, num_proc, cindex);
}

int
acpicpu_getcst(struct acpicpu_softc *sc)
{
	struct aml_value	res;
	struct acpi_cstate	*cx, *next_cx;
	int			use_nonmwait;

	/* delete the existing list */
	while ((cx = SLIST_FIRST(&sc->sc_cstates)) != NULL) {
		SLIST_REMOVE_HEAD(&sc->sc_cstates, link);
		free(cx, M_DEVBUF, sizeof(*cx));
	}

	/* provide a fallback C1-via-halt in case _CST's C1 is bogus */
	acpicpu_add_cstate(sc, ACPI_STATE_C1, CST_METH_HALT,
	    CST_FLAG_FALLBACK, 1, -1, 0);

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "_CST", 0, NULL, &res))
		return (1);

	aml_foreachpkg(&res, 1, acpicpu_add_cstatepkg, sc);
	aml_freevalue(&res);

	/* only have fallback state?  then no _CST objects were understood */
	cx = SLIST_FIRST(&sc->sc_cstates);
	if (cx->flags & CST_FLAG_FALLBACK)
		return (1);

	/*
	 * Skip states >= C2 if the CPU's LAPIC timer stops in deep
	 * states (i.e., it doesn't have the 'ARAT' bit set).
	 * Also keep track if all the states we'll use use mwait.
	 */
	use_nonmwait = 0;
	while ((next_cx = SLIST_NEXT(cx, link)) != NULL) {
		if (cx->state > 1 &&
		    (sc->sc_ci->ci_feature_tpmflags & TPM_ARAT) == 0)
			cx->flags |= CST_FLAG_SKIP;
		else if (cx->method != CST_METH_MWAIT)
			use_nonmwait = 1;
		cx = next_cx;
	}
	if (use_nonmwait)
		sc->sc_flags &= ~FLAGS_MWAIT_ONLY;
	else
		sc->sc_flags |= FLAGS_MWAIT_ONLY;

	if (!aml_evalname(sc->sc_acpi, sc->sc_devnode, "_CSD", 0, NULL, &res)) {
		aml_foreachpkg(&res, 1, acpicpu_add_cdeppkg, sc);
		aml_freevalue(&res);
	}

	return (0);
}

/*
 * old-style fixed C-state info in the FADT.
 * Note that this has extra restrictions on values and flags.
 */
void
acpicpu_getcst_from_fadt(struct acpicpu_softc *sc)
{
	struct acpi_fadt	*fadt = sc->sc_acpi->sc_fadt;
	int flags;

	/* FADT has to set flag to do C2 and higher on MP */
	if ((fadt->flags & FADT_P_LVL2_UP) == 0 && ncpus > 1)
		return;

	/* skip these C2 and C3 states if the CPU doesn't have ARAT */
	flags = (sc->sc_ci->ci_feature_tpmflags & TPM_ARAT)
	    ? 0 : CST_FLAG_SKIP;

	/* Some systems don't export a full PBLK; reduce functionality */
	if (sc->sc_pblk_len >= 5 && fadt->p_lvl2_lat <= ACPI_MAX_C2_LATENCY) {
		acpicpu_add_cstate(sc, ACPI_STATE_C2, CST_METH_GAS_IO, flags,
		    fadt->p_lvl2_lat, -1, sc->sc_pblk_addr + 4);
	}
	if (sc->sc_pblk_len >= 6 && fadt->p_lvl3_lat <= ACPI_MAX_C3_LATENCY)
		acpicpu_add_cstate(sc, ACPI_STATE_C3, CST_METH_GAS_IO, flags,
		    fadt->p_lvl3_lat, -1, sc->sc_pblk_addr + 5);
}


void
acpicpu_print_one_cst(struct acpi_cstate *cx)
{
	const char *meth = "";
	int show_addr = 0;

	switch (cx->method) {
	case CST_METH_IO_HALT:
		show_addr = 1;
		/* fallthrough */
	case CST_METH_HALT:
		meth = " halt";
		break;

	case CST_METH_MWAIT:
		meth = " mwait";
		show_addr = cx->address != 0;
		break;

	case CST_METH_GAS_IO:
		meth = " io";
		show_addr = 1;
		break;

	}

	printf(" %sC%d(", (cx->flags & CST_FLAG_SKIP ? "!" : ""), cx->state);
	if (cx->power != -1)
		printf("%d", cx->power);
	printf("@%d%s", cx->latency, meth);
	if (cx->flags & ~CST_FLAG_SKIP) {
		if (cx->flags & CST_FLAG_FALLBACK)
			printf("!");
		else
			printf(".%x", (cx->flags & ~CST_FLAG_SKIP));
	}
	if (show_addr)
		printf("@0x%llx", cx->address);
	printf(")");
}

void
acpicpu_print_cst(struct acpicpu_softc *sc)
{
	struct acpi_cstate	*cx;
	int i;

	if (!SLIST_EMPTY(&sc->sc_cstates)) {
		printf(":");

		i = 0;
		SLIST_FOREACH(cx, &sc->sc_cstates, link) {
			if (i++)
				printf(",");
			acpicpu_print_one_cst(cx);
		}
	}
}


int
acpicpu_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args	*aa = aux;
	struct cfdata		*cf = match;
	struct acpi_softc	*acpi = (struct acpi_softc *)parent;
	CPU_INFO_ITERATOR	cii;
	struct cpu_info		*ci;
	int64_t			uid;

	if (acpi_matchhids(aa, acpicpu_hids, cf->cf_driver->cd_name) &&
	    aa->aaa_node && aa->aaa_node->value &&
	    aa->aaa_node->value->type == AML_OBJTYPE_DEVICE) {
		/*
		 * Record that we've seen a Device() CPU object,
		 * so we won't attach any Processor() nodes.
		 */
		acpi->sc_skip_processor = 1;

		/* Only match if we can find a CPU with the right ID */
		if (aml_evalinteger(acpi, aa->aaa_node, "_UID", 0,
		    NULL, &uid) == 0)
			CPU_INFO_FOREACH(cii, ci)
				if (ci->ci_acpi_proc_id == uid)
					return (1);

		return (0);
	}

	/* sanity */
	if (aa->aaa_name == NULL ||
	    strcmp(aa->aaa_name, cf->cf_driver->cd_name) != 0 ||
	    aa->aaa_table != NULL)
		return (0);

	return (1);
}

void
acpicpu_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpicpu_softc	*sc = (struct acpicpu_softc *)self;
	struct acpi_attach_args *aa = aux;
	struct aml_value	res;
	int64_t			uid;
	int			i;
	uint32_t		status = 0;
	CPU_INFO_ITERATOR	cii;
	struct cpu_info		*ci;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	SLIST_INIT(&sc->sc_cstates);

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode,
	    "_UID", 0, NULL, &uid) == 0)
		sc->sc_cpu = uid;

	if (aml_evalnode(sc->sc_acpi, sc->sc_devnode, 0, NULL, &res) == 0) {
		if (res.type == AML_OBJTYPE_PROCESSOR) {
			sc->sc_cpu = res.v_processor.proc_id;
			sc->sc_pblk_addr = res.v_processor.proc_addr;
			sc->sc_pblk_len = res.v_processor.proc_len;
		}
		aml_freevalue(&res);
	}
	sc->sc_duty_off = sc->sc_acpi->sc_fadt->duty_offset;
	sc->sc_duty_wid = sc->sc_acpi->sc_fadt->duty_width;

	/* link in the matching cpu_info */
	CPU_INFO_FOREACH(cii, ci)
		if (ci->ci_acpi_proc_id == sc->sc_cpu) {
			ci->ci_acpicpudev = self;
			sc->sc_ci = ci;
			break;
		}
	if (ci == NULL) {
		printf(": no cpu matching ACPI ID %d\n", sc->sc_cpu);
		return;
	}

	sc->sc_prev_sleep = 1000000;

	acpicpu_set_pdc(sc);

	if (!valid_throttle(sc->sc_duty_off, sc->sc_duty_wid, sc->sc_pblk_addr))
		sc->sc_flags |= FLAGS_NOTHROTTLE;
#ifdef ACPI_DEBUG
	printf(": %s: ", sc->sc_devnode->name);
	printf("\n: hdr:%x pblk:%x,%x duty:%x,%x pstate:%x "
	       "(%ld throttling states)\n", sc->sc_acpi->sc_fadt->hdr_revision,
		sc->sc_pblk_addr, sc->sc_pblk_len, sc->sc_duty_off,
		sc->sc_duty_wid, sc->sc_acpi->sc_fadt->pstate_cnt,
		CPU_MAXSTATE(sc));
#endif

	/* Get C-States from _CST or FADT */
	if (acpicpu_getcst(sc) || SLIST_EMPTY(&sc->sc_cstates))
		acpicpu_getcst_from_fadt(sc);
	else {
		/* Notify BIOS we use _CST objects */
		if (sc->sc_acpi->sc_fadt->cst_cnt) {
			acpi_write_pmreg(sc->sc_acpi, ACPIREG_SMICMD, 0,
			    sc->sc_acpi->sc_fadt->cst_cnt);
		}
	}
	if (!SLIST_EMPTY(&sc->sc_cstates)) {
		extern uint32_t acpi_force_bm;

		cpu_idle_cycle_fcn = &acpicpu_idle;
		cpu_suspend_cycle_fcn = &acpicpu_suspend;

		/*
		 * C3 (and maybe C2?) needs BM_RLD to be set to
		 * wake the system
		 */
		if (SLIST_FIRST(&sc->sc_cstates)->state > 1 && acpi_force_bm == 0) {
			uint16_t en = acpi_read_pmreg(sc->sc_acpi,
			    ACPIREG_PM1_CNT, 0);
			if ((en & ACPI_PM1_BM_RLD) == 0) {
				acpi_write_pmreg(sc->sc_acpi, ACPIREG_PM1_CNT,
				    0, en | ACPI_PM1_BM_RLD);
				acpi_force_bm = ACPI_PM1_BM_RLD;
			}
		}
	}

	if (acpicpu_getpss(sc)) {
		sc->sc_flags |= FLAGS_NOPSS;
	} else {
#ifdef ACPI_DEBUG
		for (i = 0; i < sc->sc_pss_len; i++) {
			dnprintf(20, "%d %d %d %d %d %d\n",
			    sc->sc_pss[i].pss_core_freq,
			    sc->sc_pss[i].pss_power,
			    sc->sc_pss[i].pss_trans_latency,
			    sc->sc_pss[i].pss_bus_latency,
			    sc->sc_pss[i].pss_ctrl,
			    sc->sc_pss[i].pss_status);
		}
		dnprintf(20, "\n");
#endif
		if (sc->sc_pss_len == 0) {
			/* this should never happen */
			printf("%s: invalid _PSS length\n", DEVNAME(sc));
			sc->sc_flags |= FLAGS_NOPSS;
		}

		acpicpu_getppc(sc);
		if (acpicpu_getpct(sc))
			sc->sc_flags |= FLAGS_NOPCT;
		else if (sc->sc_pss_len > 0) {
			/* Notify BIOS we are handling p-states */
			if (sc->sc_acpi->sc_fadt->pstate_cnt) {
				acpi_write_pmreg(sc->sc_acpi, ACPIREG_SMICMD,
				    0, sc->sc_acpi->sc_fadt->pstate_cnt);
			}

			aml_register_notify(sc->sc_devnode, NULL,
			    acpicpu_notify, sc, ACPIDEV_NOPOLL);

			acpi_gasio(sc->sc_acpi, ACPI_IOREAD,
			    sc->sc_pct.pct_status.grd_gas.address_space_id,
			    sc->sc_pct.pct_status.grd_gas.address,
			    sc->sc_pct_stat_as, sc->sc_pct_stat_as, &status);
			sc->sc_level = (100 / sc->sc_pss_len) *
			    (sc->sc_pss_len - status);
			dnprintf(20, "%s: cpu index %d, percentage %d\n",
			    DEVNAME(sc), status, sc->sc_level);
			if (setperf_prio < 30) {
				cpu_setperf = acpicpu_setperf;
				acpicpu_set_notify(acpicpu_setperf_ppc_change);
				setperf_prio = 30;
				acpi_hasprocfvs = 1;
			}
		}
	}

	/*
	 * Nicely enumerate what power management capabilities
	 * ACPI CPU provides.
	 */
	acpicpu_print_cst(sc);
	if (!(sc->sc_flags & (FLAGS_NOPSS | FLAGS_NOPCT)) ||
	    !(sc->sc_flags & FLAGS_NOPSS)) {
		printf("%c ", SLIST_EMPTY(&sc->sc_cstates) ? ':' : ',');

		/*
		 * If acpicpu is itself providing the capability to transition
		 * states, enumerate them in the fashion that est and powernow
		 * would.
		 */
		if (!(sc->sc_flags & (FLAGS_NOPSS | FLAGS_NOPCT))) {
			printf("FVS, ");
			for (i = 0; i < sc->sc_pss_len - 1; i++)
				printf("%d, ", sc->sc_pss[i].pss_core_freq);
			printf("%d MHz", sc->sc_pss[i].pss_core_freq);
		} else
			printf("PSS");
	}

	printf("\n");
}

int
acpicpu_getppc(struct acpicpu_softc *sc)
{
	struct aml_value	res;

	sc->sc_ppc = 0;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "_PPC", 0, NULL, &res)) {
		dnprintf(10, "%s: no _PPC\n", DEVNAME(sc));
		return (1);
	}

	sc->sc_ppc = aml_val2int(&res);
	dnprintf(10, "%s: _PPC: %d\n", DEVNAME(sc), sc->sc_ppc);
	aml_freevalue(&res);

	return (0);
}

int
acpicpu_getpct(struct acpicpu_softc *sc)
{
	struct aml_value	res;
	int			rv = 1;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "_PCT", 0, NULL, &res)) {
		dnprintf(20, "%s: no _PCT\n", DEVNAME(sc));
		return (1);
	}

	if (res.length != 2) {
		dnprintf(20, "%s: %s: invalid _PCT length\n", DEVNAME(sc),
		    sc->sc_devnode->name);
		return (1);
	}

	memcpy(&sc->sc_pct.pct_ctrl, res.v_package[0]->v_buffer,
	    sizeof sc->sc_pct.pct_ctrl);
	if (sc->sc_pct.pct_ctrl.grd_gas.address_space_id ==
	    GAS_FUNCTIONAL_FIXED) {
		dnprintf(20, "CTRL GASIO is functional fixed hardware.\n");
		goto ffh;
	}

	memcpy(&sc->sc_pct.pct_status, res.v_package[1]->v_buffer,
	    sizeof sc->sc_pct.pct_status);
	if (sc->sc_pct.pct_status.grd_gas.address_space_id ==
	    GAS_FUNCTIONAL_FIXED) {
		dnprintf(20, "CTRL GASIO is functional fixed hardware.\n");
		goto ffh;
	}

	dnprintf(10, "_PCT(ctrl)  : %02x %04x %02x %02x %02x %02x %016llx\n",
	    sc->sc_pct.pct_ctrl.grd_descriptor,
	    sc->sc_pct.pct_ctrl.grd_length,
	    sc->sc_pct.pct_ctrl.grd_gas.address_space_id,
	    sc->sc_pct.pct_ctrl.grd_gas.register_bit_width,
	    sc->sc_pct.pct_ctrl.grd_gas.register_bit_offset,
	    sc->sc_pct.pct_ctrl.grd_gas.access_size,
	    sc->sc_pct.pct_ctrl.grd_gas.address);

	dnprintf(10, "_PCT(status): %02x %04x %02x %02x %02x %02x %016llx\n",
	    sc->sc_pct.pct_status.grd_descriptor,
	    sc->sc_pct.pct_status.grd_length,
	    sc->sc_pct.pct_status.grd_gas.address_space_id,
	    sc->sc_pct.pct_status.grd_gas.register_bit_width,
	    sc->sc_pct.pct_status.grd_gas.register_bit_offset,
	    sc->sc_pct.pct_status.grd_gas.access_size,
	    sc->sc_pct.pct_status.grd_gas.address);

	/* if not set assume single 32 bit access */
	sc->sc_pct_stat_as = sc->sc_pct.pct_status.grd_gas.register_bit_width
	    / 8;
	if (sc->sc_pct_stat_as == 0)
		sc->sc_pct_stat_as = 4;
	sc->sc_pct_ctrl_as = sc->sc_pct.pct_ctrl.grd_gas.register_bit_width / 8;
	if (sc->sc_pct_ctrl_as == 0)
		sc->sc_pct_ctrl_as = 4;
	sc->sc_pct_stat_len = sc->sc_pct.pct_status.grd_gas.access_size;
	if (sc->sc_pct_stat_len == 0)
		sc->sc_pct_stat_len = sc->sc_pct_stat_as;
	sc->sc_pct_ctrl_len = sc->sc_pct.pct_ctrl.grd_gas.access_size;
	if (sc->sc_pct_ctrl_len == 0)
		sc->sc_pct_ctrl_len = sc->sc_pct_ctrl_as;

	rv = 0;
ffh:
	aml_freevalue(&res);
	return (rv);
}

int
acpicpu_getpss(struct acpicpu_softc *sc)
{
	struct aml_value	res;
	int			i, c, cf;

	if (aml_evalname(sc->sc_acpi, sc->sc_devnode, "_PSS", 0, NULL, &res)) {
		dprintf("%s: no _PSS\n", DEVNAME(sc));
		return (1);
	}

	free(sc->sc_pss, M_DEVBUF, sc->sc_pssfulllen);

	sc->sc_pss = mallocarray(res.length, sizeof(*sc->sc_pss), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	sc->sc_pssfulllen = res.length * sizeof(*sc->sc_pss);

	c = 0;
	for (i = 0; i < res.length; i++) {
		cf = aml_val2int(res.v_package[i]->v_package[0]);

		/* This heuristic comes from FreeBSDs
		 * dev/acpica/acpi_perf.c to weed out invalid PSS entries.
		 */
		if (cf == sc->sc_pss[c].pss_core_freq) {
			printf("%s: struck PSS entry, core frequency equals "
			    " last\n", sc->sc_dev.dv_xname);
			continue;
		}

		if (cf == 0xFFFF || cf == 0x9999 || cf == 99999 || cf == 0) {
			printf("%s: struck PSS entry, inappropriate core "
			    "frequency value\n", sc->sc_dev.dv_xname);
			continue;
		}

		sc->sc_pss[c].pss_core_freq = cf;
		sc->sc_pss[c].pss_power = aml_val2int(
		    res.v_package[i]->v_package[1]);
		sc->sc_pss[c].pss_trans_latency = aml_val2int(
		    res.v_package[i]->v_package[2]);
		sc->sc_pss[c].pss_bus_latency = aml_val2int(
		    res.v_package[i]->v_package[3]);
		sc->sc_pss[c].pss_ctrl = aml_val2int(
		    res.v_package[i]->v_package[4]);
		sc->sc_pss[c].pss_status = aml_val2int(
		    res.v_package[i]->v_package[5]);
		c++;
	}
	sc->sc_pss_len = c;

	aml_freevalue(&res);

	return (0);
}

int
acpicpu_fetch_pss(struct acpicpu_pss **pss)
{
	struct acpicpu_softc	*sc;

	/*
	 * XXX: According to the ACPI spec in an SMP system all processors
	 * are supposed to support the same states. For now we pray
	 * the bios ensures this...
	 */

	sc = (struct acpicpu_softc *)cpu_info_primary.ci_acpicpudev;
	if (!sc)
		return 0;
	*pss = sc->sc_pss;

	return (sc->sc_pss_len);
}

int
acpicpu_notify(struct aml_node *node, int notify_type, void *arg)
{
	struct acpicpu_softc	*sc = arg;

	dnprintf(10, "acpicpu_notify: %.2x %s\n", notify_type,
	    sc->sc_devnode->name);

	switch (notify_type) {
	case 0x80:	/* _PPC changed, retrieve new values */
		acpicpu_getppc(sc);
		acpicpu_getpss(sc);
		if (sc->sc_notify)
			sc->sc_notify(sc->sc_pss, sc->sc_pss_len);
		break;

	case 0x81:	/* _CST changed, retrieve new values */
		acpicpu_getcst(sc);
		printf("%s: notify", DEVNAME(sc));
		acpicpu_print_cst(sc);
		printf("\n");
		break;

	default:
		printf("%s: unhandled cpu event %x\n", DEVNAME(sc),
		    notify_type);
		break;
	}

	return (0);
}

void
acpicpu_set_notify(void (*func)(struct acpicpu_pss *, int))
{
	struct acpicpu_softc    *sc;

	sc = (struct acpicpu_softc *)cpu_info_primary.ci_acpicpudev;
	if (sc != NULL)
		sc->sc_notify = func;
}

void
acpicpu_setperf_ppc_change(struct acpicpu_pss *pss, int npss)
{
	struct acpicpu_softc    *sc;

	sc = (struct acpicpu_softc *)cpu_info_primary.ci_acpicpudev;

	if (sc != NULL)
		cpu_setperf(sc->sc_level);
}

void
acpicpu_setperf(int level)
{
	struct acpicpu_softc	*sc;
	struct acpicpu_pss	*pss = NULL;
	int			idx, len;
	uint32_t		status = 0;

	sc = (struct acpicpu_softc *)curcpu()->ci_acpicpudev;

	dnprintf(10, "%s: acpicpu setperf level %d\n",
	    sc->sc_devnode->name, level);

	if (level < 0 || level > 100) {
		dnprintf(10, "%s: acpicpu setperf illegal percentage\n",
		    sc->sc_devnode->name);
		return;
	}

	/*
	 * XXX this should be handled more gracefully and it needs to also do
	 * the duty cycle method instead of pss exclusively
	 */
	if (sc->sc_flags & FLAGS_NOPSS || sc->sc_flags & FLAGS_NOPCT) {
		dnprintf(10, "%s: acpicpu no _PSS or _PCT\n",
		    sc->sc_devnode->name);
		return;
	}

	if (sc->sc_ppc)
		len = sc->sc_ppc;
	else
		len = sc->sc_pss_len;
	idx = (len - 1) - (level / (100 / len));
	if (idx < 0)
		idx = 0;

	if (sc->sc_ppc)
		idx += sc->sc_pss_len - sc->sc_ppc;

	if (idx > sc->sc_pss_len)
		idx = sc->sc_pss_len - 1;

	dnprintf(10, "%s: acpicpu setperf index %d pss_len %d ppc %d\n",
	    sc->sc_devnode->name, idx, sc->sc_pss_len, sc->sc_ppc);

	pss = &sc->sc_pss[idx];

#ifdef ACPI_DEBUG
	/* keep this for now since we will need this for debug in the field */
	printf("0 status: %x %llx %u %u ctrl: %x %llx %u %u\n",
	    sc->sc_pct.pct_status.grd_gas.address_space_id,
	    sc->sc_pct.pct_status.grd_gas.address,
	    sc->sc_pct_stat_as, sc->sc_pct_stat_len,
	    sc->sc_pct.pct_ctrl.grd_gas.address_space_id,
	    sc->sc_pct.pct_ctrl.grd_gas.address,
	    sc->sc_pct_ctrl_as, sc->sc_pct_ctrl_len);
#endif
	acpi_gasio(sc->sc_acpi, ACPI_IOREAD,
	    sc->sc_pct.pct_status.grd_gas.address_space_id,
	    sc->sc_pct.pct_status.grd_gas.address, sc->sc_pct_stat_as,
	    sc->sc_pct_stat_len, &status);
	dnprintf(20, "1 status: %u <- %u\n", status, pss->pss_status);

	/* Are we already at the requested frequency? */
	if (status == pss->pss_status)
		return;

	acpi_gasio(sc->sc_acpi, ACPI_IOWRITE,
	    sc->sc_pct.pct_ctrl.grd_gas.address_space_id,
	    sc->sc_pct.pct_ctrl.grd_gas.address, sc->sc_pct_ctrl_as,
	    sc->sc_pct_ctrl_len, &pss->pss_ctrl);
	dnprintf(20, "pss_ctrl: %x\n", pss->pss_ctrl);

	acpi_gasio(sc->sc_acpi, ACPI_IOREAD,
	    sc->sc_pct.pct_status.grd_gas.address_space_id,
	    sc->sc_pct.pct_status.grd_gas.address, sc->sc_pct_stat_as,
	    sc->sc_pct_stat_as, &status);
	dnprintf(20, "2 status: %d\n", status);

	/* Did the transition succeed? */
	if (status == pss->pss_status) {
		cpuspeed = pss->pss_core_freq;
		sc->sc_level = level;
	} else
		printf("%s: acpicpu setperf failed to alter frequency\n",
		    sc->sc_devnode->name);
}

void
acpicpu_idle(void)
{
	struct cpu_info *ci = curcpu();
	struct acpicpu_softc *sc = (struct acpicpu_softc *)ci->ci_acpicpudev;
	struct acpi_cstate *best, *cx;
	unsigned long itime;

	if (sc == NULL) {
		__asm volatile("sti");
		panic("null acpicpu");
	}

	/* possibly update the MWAIT_ONLY flag in cpu_info */
	if (sc->sc_flags & FLAGS_MWAIT_ONLY) {
		if ((ci->ci_mwait & MWAIT_ONLY) == 0)
			atomic_setbits_int(&ci->ci_mwait, MWAIT_ONLY);
	} else if (ci->ci_mwait & MWAIT_ONLY)
		atomic_clearbits_int(&ci->ci_mwait, MWAIT_ONLY);

	/*
	 * Find the first state with a latency we'll accept, ignoring
	 * states marked skippable
	 */
	best = cx = SLIST_FIRST(&sc->sc_cstates);
	while ((cx->flags & CST_FLAG_SKIP) ||
	    cx->latency * 3 > sc->sc_prev_sleep) {
		if ((cx = SLIST_NEXT(cx, link)) == NULL)
			break;
		best = cx;
	}

	if (best->state >= 3 &&
	    (best->flags & CST_FLAG_MWAIT_BM_AVOIDANCE) &&
	    acpi_read_pmreg(acpi_softc, ACPIREG_PM1_STS, 0) & ACPI_PM1_BM_STS) {
		/* clear it and back off */
		acpi_write_pmreg(acpi_softc, ACPIREG_PM1_STS, 0,
		    ACPI_PM1_BM_STS);
		while ((cx = SLIST_NEXT(cx, link)) != NULL) {
			if (cx->flags & CST_FLAG_SKIP)
				continue;
			if (cx->state < 3 ||
			    (cx->flags & CST_FLAG_MWAIT_BM_AVOIDANCE) == 0)
				break;
		}
		best = cx;
	}


	atomic_inc_long(&cst_stats[best->state]);

	itime = tick / 2;
	switch (best->method) {
	default:
	case CST_METH_HALT:
		__asm volatile("sti; hlt");
		break;

	case CST_METH_IO_HALT:
		inb((u_short)best->address);
		__asm volatile("sti; hlt");
		break;

	case CST_METH_MWAIT:
		{
		struct timeval start, stop;
		unsigned int hints;

#ifdef __LP64__
		if ((read_rflags() & PSL_I) == 0)
			panic("idle with interrupts blocked!");
#else
		if ((read_eflags() & PSL_I) == 0)
			panic("idle with interrupts blocked!");
#endif

		/* something already queued? */
		if (!cpu_is_idle(ci))
			return;

		/*
		 * About to idle; setting the MWAIT_IN_IDLE bit tells
		 * cpu_unidle() that it can't be a no-op and tells cpu_kick()
		 * that it doesn't need to use an IPI.  We also set the
		 * MWAIT_KEEP_IDLING bit: those routines clear it to stop
		 * the mwait.  Once they're set, we do a final check of the
		 * queue, in case another cpu called setrunqueue() and added
		 * something to the queue and called cpu_unidle() between
		 * the check in sched_idle() and here.
		 */
		hints = (unsigned)best->address;
		microuptime(&start);
		atomic_setbits_int(&ci->ci_mwait, MWAIT_IDLING);
		if (cpu_is_idle(ci)) {
			/* intel errata AAI65: cflush before monitor */
			if (ci->ci_cflushsz != 0 &&
			    strcmp(cpu_vendor, "GenuineIntel") == 0) {
				membar_sync();
				clflush((unsigned long)&ci->ci_mwait);
				membar_sync();
			}

			monitor(&ci->ci_mwait, 0, 0);
			if ((ci->ci_mwait & MWAIT_IDLING) == MWAIT_IDLING)
				mwait(0, hints);
		}

		microuptime(&stop);
		timersub(&stop, &start, &stop);
		itime = stop.tv_sec * 1000000 + stop.tv_usec;

		/* done idling; let cpu_kick() know that an IPI is required */
		atomic_clearbits_int(&ci->ci_mwait, MWAIT_IDLING);
		break;
		}

	case CST_METH_GAS_IO:
		inb((u_short)best->address);
		/* something harmless to give system time to change state */
		acpi_read_pmreg(acpi_softc, ACPIREG_PM1_STS, 0);
		break;

	}

	sc->sc_last_itime = itime;
	itime >>= 1;
	sc->sc_prev_sleep = (sc->sc_prev_sleep + (sc->sc_prev_sleep >> 1)
	    + itime) >> 1;
}

void
acpicpu_suspend(void)
{
	extern int cpu_suspended;
	struct cpu_info *ci = curcpu();
	struct acpicpu_softc *sc = (struct acpicpu_softc *)ci->ci_acpicpudev;
	struct acpi_cstate *best, *cx;

	if (sc == NULL) {
		__asm volatile("sti");
		panic("null acpicpu");
	}

	/*
	 * Find the lowest usable state.
	 */
	best = cx = SLIST_FIRST(&sc->sc_cstates);
	while ((cx->flags & CST_FLAG_SKIP)) {
		if ((cx = SLIST_NEXT(cx, link)) == NULL)
			break;
		best = cx;
	}

	switch (best->method) {
	default:
	case CST_METH_HALT:
		__asm volatile("sti; hlt");
		break;

	case CST_METH_IO_HALT:
		inb((u_short)best->address);
		__asm volatile("sti; hlt");
		break;

	case CST_METH_MWAIT:
		{
		unsigned int hints;

		hints = (unsigned)best->address;
		/* intel errata AAI65: cflush before monitor */
		if (ci->ci_cflushsz != 0 &&
		    strcmp(cpu_vendor, "GenuineIntel") == 0) {
			membar_sync();
			clflush((unsigned long)&cpu_suspended);
			membar_sync();
		}

		monitor(&cpu_suspended, 0, 0);
		if (cpu_suspended || !CPU_IS_PRIMARY(ci))
			mwait(0, hints);

		break;
		}

	case CST_METH_GAS_IO:
		inb((u_short)best->address);
		/* something harmless to give system time to change state */
		acpi_read_pmreg(acpi_softc, ACPIREG_PM1_STS, 0);
		break;

	}
}
