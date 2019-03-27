/*-
 * Copyright (c) 2016, Hiroki Mori
 * Copyright (c) 2010 Adrian Chadd
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

#include "opt_ddb.h"
#include "opt_ar531x.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/kdb.h>
#include <sys/reboot.h>
 
#include <vm/vm.h>
#include <vm/vm_page.h>
 
#include <net/ethernet.h>
 
#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpuregs.h>
#include <machine/hwfunc.h>
#include <machine/md_var.h>
#include <machine/trap.h>
#include <machine/vmparam.h>
 
#include <mips/atheros/ar531x/ar5315reg.h>
#include <mips/atheros/ar531x/ar5312reg.h>
#include <mips/atheros/ar531x/ar5315_setup.h>

#include <mips/atheros/ar531x/ar5315_cpudef.h>

#include <mips/atheros/ar531x/ar5315_chip.h>
#include <mips/atheros/ar531x/ar5312_chip.h>
#include <mips/atheros/ar724x_chip.h>
#include <mips/atheros/ar91xx_chip.h>

#include <dev/ath/ath_hal/ah_soc.h>

#define	AR5315_SYS_TYPE_LEN		128

static char ar5315_sys_type[AR5315_SYS_TYPE_LEN];
enum ar531x_soc_type ar531x_soc;
struct ar5315_cpu_def * ar5315_cpu_ops = NULL;

void
ar5315_detect_sys_type(void)
{
	char *chip = "????";
	uint32_t ver = 0;
	uint32_t rev = 0;
#if 0
	const uint8_t *ptr, *end;
	static const struct ar531x_boarddata *board = NULL;

	ptr = (const uint8_t *) MIPS_PHYS_TO_KSEG1(AR5315_CONFIG_END
		- 0x1000);

	end = (const uint8_t *)AR5315_CONFIG_BASE;

	for (; ptr > end; ptr -= 0x1000) {
		if (*(const uint32_t *)ptr == AR531X_BD_MAGIC) {
			board = (const struct ar531x_boarddata *) ptr;
			rev = board->major;
			break;
		}
	}
#endif
	int soctype;

#ifdef AR531X_1ST_GENERATION
	soctype = AR_FIRST_GEN;
#else
	soctype = AR_SECOND_GEN;
#endif

	if(soctype == AR_SECOND_GEN) {
		ar5315_cpu_ops = &ar5315_chip_def;

		ver = ATH_READ_REG(AR5315_SYSREG_BASE +
			AR5315_SYSREG_SREV);

		switch (ver) {
		case 0x86:
			ar531x_soc = AR531X_SOC_AR5315;
			chip = "2315";
			break;
		case 0x87:
			ar531x_soc = AR531X_SOC_AR5316;
			chip = "2316";
			break;
		case 0x90:
			ar531x_soc = AR531X_SOC_AR5317;
			chip = "2317";
			break;
		case 0x91:
			ar531x_soc = AR531X_SOC_AR5318;
			chip = "2318";
			break;
		}
	} else {
		ar5315_cpu_ops = &ar5312_chip_def;

		ver = ATH_READ_REG(AR5312_SYSREG_BASE +
			AR5312_SYSREG_REVISION);
		rev = AR5312_REVISION_MINOR(ver);

		switch (AR5312_REVISION_MAJOR(ver)) {
		case AR5312_REVISION_MAJ_AR5311:
			ar531x_soc = AR531X_SOC_AR5311;
			chip = "5311";
			break;
		case AR5312_REVISION_MAJ_AR5312:
			ar531x_soc = AR531X_SOC_AR5312;
			chip = "5312";
			break;
		case AR5312_REVISION_MAJ_AR2313:
			ar531x_soc = AR531X_SOC_AR5313;
			chip = "2313";
			break;
		}
	}

	sprintf(ar5315_sys_type, "Atheros AR%s rev %u", chip, rev);
}

const char *
ar5315_get_system_type(void)
{
	return ar5315_sys_type;
}

