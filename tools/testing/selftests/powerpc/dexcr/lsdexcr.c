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
	const char *reason = NULL;
	const char *reason_hyp = NULL;
	const char *reason_prctl = "no prctl";
	bool actual = effective & DEXCR_PR_BIT(aspect->index);
	bool expected = actual;  /* Assume it's fine if we don't expect a specific set/clear value */

	if (actual)
		reason = "set by unknown";
	else
		reason = "cleared by unknown";

	if (aspect->prctl != -1) {
		int ctrl = pr_get_dexcr(aspect->prctl);

		if (ctrl < 0) {
			reason_prctl = "failed to read prctl";
		} else {
			if (ctrl & PR_PPC_DEXCR_CTRL_SET) {
				reason_prctl = "set by prctl";
				expected = true;
			} else if (ctrl & PR_PPC_DEXCR_CTRL_CLEAR) {
				reason_prctl = "cleared by prctl";
				expected = false;
			} else {
				reason_prctl = "unknown prctl";
			}

			reason = reason_prctl;
		}
	}

	if (hdexcr & DEXCR_PR_BIT(aspect->index)) {
		reason_hyp = "set by hypervisor";
		reason = reason_hyp;
		expected = true;
	} else {
		reason_hyp = "not modified by hypervisor";
	}

	printf("%12s (%d): %-28s (%s, %s)\n",
	       aspect->name,
	       aspect->index,
	       reason,
	       reason_hyp,
	       reason_prctl);

	/*
	 * The checks are not atomic, so this can technically trigger if the
	 * hypervisor makes a change while we are checking each source. It's
	 * far more likely to be a bug if we see this though.
	 */
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
