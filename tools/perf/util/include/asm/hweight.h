#ifndef PERF_HWEIGHT_H
#define PERF_HWEIGHT_H

#include <linux/types.h>
unsigned int hweight32(unsigned int w);
unsigned long hweight64(__u64 w);

#endif /* PERF_HWEIGHT_H */
