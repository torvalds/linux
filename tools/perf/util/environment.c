/*
 * We put all the perf config variables in this same object
 * file, so that programs can link against the config parser
 * without having to link against all the rest of perf.
 */
#include "cache.h"

int pager_use_color = 1;
