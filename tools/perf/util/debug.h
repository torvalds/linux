/* For debugging general purposes */

extern int verbose;
extern int dump_trace;

int eprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int dump_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void trace_event(event_t *event);
