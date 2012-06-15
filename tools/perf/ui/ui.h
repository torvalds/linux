#ifndef _PERF_UI_H_
#define _PERF_UI_H_ 1

#include <pthread.h>
#include <stdbool.h>

extern pthread_mutex_t ui__lock;

void ui__refresh_dimensions(bool force);

#endif /* _PERF_UI_H_ */
