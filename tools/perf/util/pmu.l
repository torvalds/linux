%option prefix="perf_pmu_"

%{
#include <stdlib.h>
#include <linux/bitops.h>
#include "pmu.h"
#include "pmu-bison.h"

static int value(int base)
{
	long num;

	errno = 0;
	num = strtoul(perf_pmu_text, NULL, base);
	if (errno)
		return PP_ERROR;

	perf_pmu_lval.num = num;
	return PP_VALUE;
}

%}

num_dec         [0-9]+

%%

{num_dec}	{ return value(10); }
config		{ return PP_CONFIG; }
config1		{ return PP_CONFIG1; }
config2		{ return PP_CONFIG2; }
-		{ return '-'; }
:		{ return ':'; }
,		{ return ','; }
.		{ ; }
\n		{ ; }

%%

int perf_pmu_wrap(void)
{
	return 1;
}
