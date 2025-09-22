/*
 * Public domain
 * dummy shim for some tests.
 */

#include "extern.h"

void
repo_stat_inc(struct repo *repo, int tal, enum rtype type, enum stype subtype)
{
	return;
}

struct repo *
repo_byid(unsigned int id)
{
	return NULL;
}

unsigned int
repo_id(const struct repo *repo)
{
	return 0;
}
