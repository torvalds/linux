#ifndef PERF_UNIT_H
#define PERF_UNIT_H

#include <stddef.h>
#include <linux/types.h>

unsigned long convert_unit(unsigned long value, char *unit);
int unit_number__scnprintf(char *buf, size_t size, u64 n);

#endif /* PERF_UNIT_H */
