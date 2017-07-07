#ifndef JEVENTS_H
#define JEVENTS_H 1

int json_events(const char *fn,
		int (*func)(void *data, char *name, char *event, char *desc,
				char *long_desc,
				char *pmu,
				char *unit, char *perpkg, char *metric_expr,
				char *metric_name),
		void *data);
char *get_cpu_str(void);

#ifndef min
#define min(x, y) ({                            \
	typeof(x) _min1 = (x);                  \
	typeof(y) _min2 = (y);                  \
	(void) (&_min1 == &_min2);              \
	_min1 < _min2 ? _min1 : _min2; })
#endif

#endif
