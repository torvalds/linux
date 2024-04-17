// SPDX-License-Identifier: GPL-2.0+

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>

#include "dexcr.h"
#include "utils.h"

static unsigned int dexcr;
static unsigned int hdexcr;
static unsigned int effective;

struct dexcr_aspect {
	const char *name;
	const char *desc;
	unsigned int index;
	unsigned long prctl;
	const char *sysctl;
};

static const struct dexcr_aspect aspects[] = {
	{
		.name = "SBHE",
		.desc = "Speculative branch hint enable",
		.index = 0,
		.prctl = PR_PPC_DEXCR_SBHE,
		.sysctl = "speculative_branch_hint_enable",
	},
	{
		.name = "IBRTPD",
		.desc = "Indirect branch recurrent target prediction disable",
		.index = 3,
		.prctl = PR_PPC_DEXCR_IBRTPD,
		.sysctl = "indirect_branch_recurrent_target_prediction_disable",
	},
	{
		.name = "SRAPD",
		.desc = "Subroutine return address prediction disable",
		.index = 4,
		.prctl = PR_PPC_DEXCR_SRAPD,
		.sysctl = "subroutine_return_address_prediction_disable",
	},
	{
		.name = "NPHIE",
		.desc = "Non-privileged hash instruction enable",
		.index = 5,
		.prctl = PR_PPC_DEXCR_NPHIE,
		.sysctl = "nonprivileged_hash_instruction_enable",
	},
	{
		.name = "PHIE",
		.desc = "Privileged hash instruction enable",
		.index = 6,
		.prctl = -1,
		.sysctl = NULL,
	},
};

static void print_list(const char *list[], size_t len)
{
	for (size_t i = 0; i < len; i++) {
		printf("%s", list[i]);
		if (i + 1 < len)
			printf(", ");
	}
}

static void print_dexcr(char *name, unsigned int bits)
{
	const char *enabled_aspects[ARRAY_SIZE(aspects) + 1] = {NULL};
	size_t j = 0;

	printf("%s: 0x%08x", name, bits);

	if (bits == 0) {
		printf("\n");
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(aspects); i++) {
		unsigned int mask = DEXCR_PR_BIT(aspects[i].index);

		if (bits & mask) {
			enabled_aspects[j++] = aspects[i].name;
			bits &= ~mask;
		}
	}

	if (bits)
		enabled_aspects[j++] = "unknown";

	printf(" (");
	print_list(enabled_aspects, j);
	printf(")\n");
}

static void print_aspect(const struct dexcr_aspect *aspect)
{
	const char *attributes[8] = {NULL};
	size_t j = 0;
	unsigned long mask;

	mask = DEXCR_PR_BIT(aspect->index);
	if (dexcr & mask)
		attributes[j++] = "set";
	if (hdexcr & mask)
		attributes[j++] = "set (hypervisor)";
	if (!(effective & mask))
		attributes[j++] = "clear";

	printf("%12s %c (%d): ", aspect->name, effective & mask ? '*' : ' ', aspect->index);
	print_list(attributes, j);
	printf("  \t(%s)\n", aspect->desc);
}

static void print_aspect_config(const struct dexcr_aspect *aspect)
{
	char sysctl_path[128] = "/proc/sys/kernel/dexcr/";
	const char *reason = "unknown";
	const char *reason_hyp = NULL;
	const char *reason_sysctl = "no sysctl";
	const char *reason_prctl = "no prctl";
	bool actual = effective & DEXCR_PR_BIT(aspect->index);
	bool expected = false;

	long sysctl_ctrl = 0;
	int prctl_ctrl = 0;
	int err;

	if (aspect->prctl >= 0) {
		prctl_ctrl = pr_get_dexcr(aspect->prctl);
		if (prctl_ctrl < 0)
			reason_prctl = "(failed to read prctl)";
		else {
			if (prctl_ctrl & PR_PPC_DEXCR_CTRL_SET) {
				reason_prctl = "set by prctl";
				expected = true;
			} else if (prctl_ctrl & PR_PPC_DEXCR_CTRL_CLEAR) {
				reason_prctl = "cleared by prctl";
				expected = false;
			} else
				reason_prctl = "unknown prctl";

			reason = reason_prctl;
		}
	}

	if (aspect->sysctl) {
		strcat(sysctl_path, aspect->sysctl);
		err = read_long(sysctl_path, &sysctl_ctrl, 10);
		if (err)
			reason_sysctl = "(failed to read sysctl)";
		else {
			switch (sysctl_ctrl) {
			case 0:
				reason_sysctl = "cleared by sysctl";
				reason = reason_sysctl;
				expected = false;
				break;
			case 1:
				reason_sysctl = "set by sysctl";
				reason = reason_sysctl;
				expected = true;
				break;
			case 2:
				reason_sysctl = "not modified by sysctl";
				break;
			case 3:
				reason_sysctl = "cleared by sysctl (permanent)";
				reason = reason_sysctl;
				expected = false;
				break;
			case 4:
				reason_sysctl = "set by sysctl (permanent)";
				reason = reason_sysctl;
				expected = true;
				break;
			default:
				reason_sysctl = "unknown sysctl";
				break;
			}
		}
	}


	if (hdexcr & DEXCR_PR_BIT(aspect->index)) {
		reason_hyp = "set by hypervisor";
		reason = reason_hyp;
		expected = true;
	} else
		reason_hyp = "not modified by hypervisor";

	printf("%12s (%d): %-28s (%s, %s, %s)\n",
	       aspect->name,
	       aspect->index,
	       reason,
	       reason_hyp,
	       reason_sysctl,
	       reason_prctl);

	if (actual != expected)
		printf("                : ! actual %s does not match config\n", aspect->name);
}

int main(int argc, char *argv[])
{
	if (!dexcr_exists()) {
		printf("DEXCR not detected on this hardware\n");
		return 1;
	}

	dexcr = get_dexcr(DEXCR);
	hdexcr = get_dexcr(HDEXCR);
	effective = dexcr | hdexcr;

	printf("current status:\n");

	print_dexcr("    DEXCR", dexcr);
	print_dexcr("   HDEXCR", hdexcr);
	print_dexcr("Effective", effective);
	printf("\n");

	for (size_t i = 0; i < ARRAY_SIZE(aspects); i++)
		print_aspect(&aspects[i]);
	printf("\n");

	if (effective & DEXCR_PR_NPHIE) {
		printf("DEXCR[NPHIE] enabled: hashst/hashchk ");
		if (hashchk_triggers())
			printf("working\n");
		else
			printf("failed to trigger\n");
	} else {
		printf("DEXCR[NPHIE] disabled: hashst/hashchk ");
		if (hashchk_triggers())
			printf("unexpectedly triggered\n");
		else
			printf("ignored\n");
	}
	printf("\n");

	printf("configuration:\n");
	for (size_t i = 0; i < ARRAY_SIZE(aspects); i++)
		print_aspect_config(&aspects[i]);
	printf("\n");

	return 0;
}
