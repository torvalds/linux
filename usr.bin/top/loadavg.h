/*
 *  Top - a top users display for Berkeley Unix
 *
 *  Defines required to access load average figures.
 *
 *  This include file sets up everything we need to access the load average
 *  values in the kernel in a machine independent way.  First, it sets the
 *  typedef "load_avg" to be either double or long (depending on what is
 *  needed), then it defines these macros appropriately:
 *
 *	loaddouble(la) - convert load_avg to double.
 *	intload(i)     - convert integer to load_avg.
 *
 *	$FreeBSD$
 */

#ifndef LOADAVG_H
#define LOADAVG_H

#include <sys/param.h>

typedef long pctcpu;
#define pctdouble(p) ((double)(p) / FSCALE)

typedef fixpt_t load_avg;
#define loaddouble(la) ((double)(la) / FSCALE)
#endif /* LOADAVG_H */
