// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2021 Google LLC
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
 * gki_module_exported.h -- Symbols protected from _export_ by unsigned modules
 * gki_module_protected.h -- Symbols protected from _access_ by unsigned modules
 */
#include "gki_module_protected.h"
#include "gki_module_exported.h"

#define MAX_STRCMP_LEN (max(MAX_PROTECTED_NAME_LEN, MAX_EXPORTED_NAME_LEN))

/* bsearch() comparision callback */
static int cmp_name(const void *sym, const void *protected_sym)
{
	return strncmp(sym, protected_sym, MAX_STRCMP_LEN);
}

/**
 * gki_is_module_protected_symbol - Is a symbol protected from unsigned module?
 *
 * @name:	Symbol being checked against protection from unsigned module
 */
bool gki_is_module_protected_symbol(const char *name)
{
	return bsearch(name, gki_protected_symbols, NO_OF_PROTECTED_SYMBOLS,
		       MAX_PROTECTED_NAME_LEN, cmp_name) != NULL;
}

/**
 * gki_is_module_exported_symbol - Is a symbol exported from a GKI module?
 *
 * @name:	Symbol being checked against exported symbols from GKI modules
 */
bool gki_is_module_exported_symbol(const char *name)
{
	return bsearch(name, gki_exported_symbols, NO_OF_EXPORTED_SYMBOLS,
		       MAX_EXPORTED_NAME_LEN, cmp_name) != NULL;
}
