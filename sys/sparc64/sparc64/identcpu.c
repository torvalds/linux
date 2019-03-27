/*-
 * Initial implementation:
 * Copyright (c) 2001 Robert Drehmel
 * All rights reserved.
 *
 * As long as the above copyright statement and this notice remain
 * unchanged, you can do what ever you want with this file.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>

#include <machine/md_var.h>
#include <machine/ver.h>

char machine[] = MACHINE;
SYSCTL_STRING(_hw, HW_MACHINE, machine, CTLFLAG_RD,
    machine, 0, "Machine class");

static char cpu_model[128];
SYSCTL_STRING(_hw, HW_MODEL, model, CTLFLAG_RD,
    cpu_model, 0, "Machine model");

static SYSCTL_NODE(_hw, OID_AUTO, freq, CTLFLAG_RD, 0, "");

static u_int cpu_count;
static u_int cpu_freq;
SYSCTL_UINT(_hw_freq, OID_AUTO, cpu, CTLFLAG_RD, &cpu_freq, 0,
    "CPU clock frequency");

void
cpu_identify(u_long vers, u_int freq, u_int id)
{
	const char *manus;
	const char *impls;

	switch (VER_MANUF(vers)) {
	case 0x04:
		manus = "HAL/Fujitsu";
		break;
	case 0x13:
	case 0x17:
	case 0x22:
	case 0x3e:
		manus = "Sun Microsystems";
		break;
	default:
		manus = NULL;
		break;
	}
	switch (VER_IMPL(vers)) {
	case CPU_IMPL_SPARC64:
		impls = "SPARC64";
		break;
	case CPU_IMPL_SPARC64II:
		impls = "SPARC64-II";
		break;
	case CPU_IMPL_SPARC64III:
		impls = "SPARC64-III";
		break;
	case CPU_IMPL_SPARC64IV:
		impls = "SPARC64-IV";
		break;
	case CPU_IMPL_SPARC64V:
		impls = "SPARC64-V";
		break;
	case CPU_IMPL_SPARC64VI:
		impls = "SPARC64-VI";
		break;
	case CPU_IMPL_SPARC64VII:
		impls = "SPARC64-VII";
		break;
	case CPU_IMPL_SPARC64VIIIfx:
		impls = "SPARC64-VIIIfx";
		break;
	case CPU_IMPL_ULTRASPARCI:
		impls = "UltraSparc-I";
		break;
	case CPU_IMPL_ULTRASPARCII:
		impls = "UltraSparc-II";
		break;
	case CPU_IMPL_ULTRASPARCIIi:
		impls = "UltraSparc-IIi";
		break;
	case CPU_IMPL_ULTRASPARCIIe:
		impls = "UltraSparc-IIe";
		break;
	case CPU_IMPL_ULTRASPARCIII:
		impls = "UltraSparc-III";
		break;
	case CPU_IMPL_ULTRASPARCIIIp:
		impls = "UltraSparc-III+";
		break;
	case CPU_IMPL_ULTRASPARCIIIi:
		impls = "UltraSparc-IIIi";
		break;
	case CPU_IMPL_ULTRASPARCIV:
		impls = "UltraSparc-IV";
		break;
	case CPU_IMPL_ULTRASPARCIVp:
		impls = "UltraSparc-IV+";
		break;
	case CPU_IMPL_ULTRASPARCIIIip:
		impls = "UltraSparc-IIIi+";
		break;
	default:
		impls = NULL;
		break;
	}
	if (manus == NULL || impls == NULL) {
		printf(
		    "CPU: unknown; please e-mail the following value together\n"
		    "     with the exact name of your processor to "
		    "<freebsd-sparc64@FreeBSD.org>.\n"
		    "     version register: <0x%lx>\n", vers);
		return;
	}

	snprintf(cpu_model, sizeof(cpu_model), "%s %s", manus, impls);
	printf("cpu%d: %s %s Processor (%d.%02d MHz CPU)\n", id, manus, impls,
	    (freq + 4999) / 1000000, ((freq + 4999) / 10000) % 100);
	if (bootverbose) {
		printf("  mask=0x%lx maxtl=%ld maxwin=%ld\n", VER_MASK(vers),
		    VER_MAXTL(vers), VER_MAXWIN(vers));
	}

	/*
	 * Calculate the average CPU frequency.
	 */
	freq = (freq + 500000ul) / 1000000ul;
	cpu_freq = (cpu_freq * cpu_count + freq) / (cpu_count + 1);
	cpu_count++;
}
