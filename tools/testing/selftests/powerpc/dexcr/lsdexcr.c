// SPDX-License-Identifier: GPL-2.0+

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "dexcr.h"
#include "utils.h"

static unsigned int dexcr;
static unsigned int hdexcr;
static unsigned int effective;

struct dexcr_aspect {
	const char *name;
	const char *desc;
	unsigned int index;
};

static const struct dexcr_aspect aspects[] = {
	{
		.name = "SBHE",
		.desc = "Speculative branch hint enable",
		.index = 0,
	},
	{
		.name = "IBRTPD",
		.desc = "Indirect branch recurrent target prediction disable",
		.index = 3,
	},
	{
		.name = "SRAPD",
		.desc = "Subroutine return address prediction disable",
		.index = 4,
	},
	{
		.name = "NPHIE",
		.desc = "Non-privileged hash instruction enable",
		.index = 5,
	},
	{
		.name = "PHIE",
		.desc = "Privileged hash instruction enable",
		.index = 6,
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

	printf("%s: %08x", name, bits);

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

int main(int argc, char *argv[])
{
	if (!dexcr_exists()) {
		printf("DEXCR not detected on this hardware\n");
		return 1;
	}

	dexcr = get_dexcr(DEXCR);
	hdexcr = get_dexcr(HDEXCR);
	effective = dexcr | hdexcr;

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

	return 0;
}
