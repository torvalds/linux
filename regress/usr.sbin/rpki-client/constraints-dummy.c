/*
 * Public domain
 * dummy shim for some tests.
 */

#include "extern.h"

int
constraints_validate(const char *fn, const struct cert *cert)
{
	return 1;
}
