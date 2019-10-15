#include <stdio.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include "spark.h"
#include "stat.h"

#define SPARK_SHIFT 8

/* Print spark lines on outf for numval values in val. */
int print_spark(char *bf, int size, unsigned long *val, int numval)
{
	static const char *ticks[NUM_SPARKS] = {
		"▁",  "▂", "▃", "▄", "▅", "▆", "▇", "█"
	};
	int i, printed = 0;
	unsigned long min = ULONG_MAX, max = 0, f;

	for (i = 0; i < numval; i++) {
		if (val[i] < min)
			min = val[i];
		if (val[i] > max)
			max = val[i];
	}
	f = ((max - min) << SPARK_SHIFT) / (NUM_SPARKS - 1);
	if (f < 1)
		f = 1;
	for (i = 0; i < numval; i++) {
		printed += scnprintf(bf + printed, size - printed, "%s",
				     ticks[((val[i] - min) << SPARK_SHIFT) / f]);
	}

	return printed;
}
