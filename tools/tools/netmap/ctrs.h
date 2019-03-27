#ifndef CTRS_H_
#define CTRS_H_

/* $FreeBSD$ */

#include <sys/time.h>

/* counters to accumulate statistics */
struct my_ctrs {
	uint64_t pkts, bytes, events;
	uint64_t drop, drop_bytes;
	uint64_t min_space;
	struct timeval t;
	uint32_t oq_n; /* number of elements in overflow queue (used in lb) */
};

/* very crude code to print a number in normalized form.
 * Caller has to make sure that the buffer is large enough.
 */
static const char *
norm2(char *buf, double val, char *fmt, int normalize)
{
	char *units[] = { "", "K", "M", "G", "T" };
	u_int i;
	if (normalize)
		for (i = 0; val >=1000 && i < sizeof(units)/sizeof(char *) - 1; i++)
			val /= 1000;
	else
		i=0;
	sprintf(buf, fmt, val, units[i]);
	return buf;
}

static __inline const char *
norm(char *buf, double val, int normalize)
{
	if (normalize)
		return norm2(buf, val, "%.3f %s", normalize);
	else
		return norm2(buf, val, "%.0f %s", normalize);
}

static __inline int
timespec_ge(const struct timespec *a, const struct timespec *b)
{

	if (a->tv_sec > b->tv_sec)
		return (1);
	if (a->tv_sec < b->tv_sec)
		return (0);
	if (a->tv_nsec >= b->tv_nsec)
		return (1);
	return (0);
}

static __inline struct timespec
timeval2spec(const struct timeval *a)
{
	struct timespec ts = {
		.tv_sec = a->tv_sec,
		.tv_nsec = a->tv_usec * 1000
	};
	return ts;
}

static __inline struct timeval
timespec2val(const struct timespec *a)
{
	struct timeval tv = {
		.tv_sec = a->tv_sec,
		.tv_usec = a->tv_nsec / 1000
	};
	return tv;
}


static __inline struct timespec
timespec_add(struct timespec a, struct timespec b)
{
	struct timespec ret = { a.tv_sec + b.tv_sec, a.tv_nsec + b.tv_nsec };
	if (ret.tv_nsec >= 1000000000) {
		ret.tv_sec++;
		ret.tv_nsec -= 1000000000;
	}
	return ret;
}

static __inline struct timespec
timespec_sub(struct timespec a, struct timespec b)
{
	struct timespec ret = { a.tv_sec - b.tv_sec, a.tv_nsec - b.tv_nsec };
	if (ret.tv_nsec < 0) {
		ret.tv_sec--;
		ret.tv_nsec += 1000000000;
	}
	return ret;
}

static __inline uint64_t
wait_for_next_report(struct timeval *prev, struct timeval *cur,
		int report_interval)
{
	struct timeval delta;

	delta.tv_sec = report_interval/1000;
	delta.tv_usec = (report_interval%1000)*1000;
	if (select(0, NULL, NULL, NULL, &delta) < 0 && errno != EINTR) {
		perror("select");
		abort();
	}
	gettimeofday(cur, NULL);
	timersub(cur, prev, &delta);
	return delta.tv_sec* 1000000 + delta.tv_usec;
}
#endif /* CTRS_H_ */

