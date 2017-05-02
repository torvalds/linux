#include "units.h"
#include <inttypes.h>
#include <linux/kernel.h>
#include <linux/time64.h>

unsigned long convert_unit(unsigned long value, char *unit)
{
	*unit = ' ';

	if (value > 1000) {
		value /= 1000;
		*unit = 'K';
	}

	if (value > 1000) {
		value /= 1000;
		*unit = 'M';
	}

	if (value > 1000) {
		value /= 1000;
		*unit = 'G';
	}

	return value;
}

int unit_number__scnprintf(char *buf, size_t size, u64 n)
{
	char unit[4] = "BKMG";
	int i = 0;

	while (((n / 1024) > 1) && (i < 3)) {
		n /= 1024;
		i++;
	}

	return scnprintf(buf, size, "%" PRIu64 "%c", n, unit[i]);
}
