// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2022 Google LLC
 * Author: ramjiyani@google.com (Ramji Jiyani)
 */

#include <linux/bsearch.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/string.h>

/*
 * Build time generated header files
 *
 * gki_module_protected_exports.h -- Symbols protected from _export_ by unsigned modules
 * gki_module_unprotected.h -- Symbols allowed to _access_ by unsigned modules
 */
#include "gki_module_protected_exports.h"
#include "gki_module_unprotected.h"

#define MAX_STRCMP_LEN (max(MAX_UNPROTECTED_NAME_LEN, MAX_PROTECTED_EXPORTS_NAME_LEN))

/* bsearch() comparision callback */
static int cmp_name(const void *sym, const void *protected_sym)
{
	return strncmp(sym, protected_sym, MAX_STRCMP_LEN);
}

/**
 * gki_is_module_protected_export - Is a symbol exported from a protected GKI module?
 *
 * @name:	Symbol being checked against exported symbols from protected GKI modules
 */
bool gki_is_module_protected_export(const char *name)
{
	if (NR_UNPROTECTED_SYMBOLS) {
		return bsearch(name, gki_protected_exports_symbols, NR_PROTECTED_EXPORTS_SYMBOLS,
		       MAX_PROTECTED_EXPORTS_NAME_LEN, cmp_name) != NULL;
	} else {
		/*
		 * If there are no symbols in unprotected list; We don't need to
		 * protect exports as there is no KMI enforcement.
		 * Treat everything exportable in this case.
		 */
		return false;
	}
}

/**
 * gki_is_module_unprotected_symbol - Is a symbol unprotected for unsigned module?
 *
 * @name:	Symbol being checked in list of unprotected symbols
 */
bool gki_is_module_unprotected_symbol(const char *name)
{
	if (NR_UNPROTECTED_SYMBOLS) {
		return bsearch(name, gki_unprotected_symbols, NR_UNPROTECTED_SYMBOLS,
				MAX_UNPROTECTED_NAME_LEN, cmp_name) != NULL;
	} else {
		/*
		 * If there are no symbols in unprotected list;
		 * there isn't a KMI enforcement for the kernel.
		 * Treat everything accessible in this case.
		 */
		return true;
	}
}
