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
 * gki_module_unprotected.h -- Symbols allowed to _access_ by unsigned modules
 */
#include "gki_module_unprotected.h"

/* bsearch() comparision callback */
static int cmp_name(const void *sym, const void *protected_sym)
{
	return strncmp(sym, protected_sym, MAX_UNPROTECTED_NAME_LEN);
}

/**
 * gki_is_module_unprotected_symbol - Is a symbol unprotected for unsigned module?
 *
 * @name:	Symbol being checked in list of unprotected symbols
 */
bool gki_is_module_unprotected_symbol(const char *name)
{
	return bsearch(name, gki_unprotected_symbols, NO_OF_UNPROTECTED_SYMBOLS,
		       MAX_UNPROTECTED_NAME_LEN, cmp_name) != NULL;
}
