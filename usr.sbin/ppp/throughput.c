/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Brian Somers <brian@Awfulhak.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>

#include "log.h"
#include "timer.h"
#include "throughput.h"
#include "descriptor.h"
#include "prompt.h"


void
throughput_init(struct pppThroughput *t, int period)
{
  t->OctetsIn = t->OctetsOut = t->PacketsIn = t->PacketsOut = 0;
  t->SamplePeriod = period;
  t->in.SampleOctets = (long long *)
    calloc(period, sizeof *t->in.SampleOctets);
  t->in.OctetsPerSecond = 0;
  t->out.SampleOctets = (long long *)
    calloc(period, sizeof *t->out.SampleOctets);
  t->out.OctetsPerSecond = 0;
  t->BestOctetsPerSecond = 0;
  t->nSample = 0;
  time(&t->BestOctetsPerSecondTime);
  memset(&t->Timer, '\0', sizeof t->Timer);
  t->Timer.name = "throughput";
  t->uptime = 0;
  t->downtime = 0;
  t->rolling = 0;
  t->callback.data = NULL;
  t->callback.fn = NULL;
  throughput_stop(t);
}

void
throughput_destroy(struct pppThroughput *t)
{
  if (t && t->in.SampleOctets) {
    throughput_stop(t);
    free(t->in.SampleOctets);
    free(t->out.SampleOctets);
    t->in.SampleOctets = NULL;
    t->out.SampleOctets = NULL;
  }
}

int
throughput_uptime(struct pppThroughput *t)
{
  time_t downat;

  downat = t->downtime ? t->downtime : time(NULL);
  if (t->uptime && downat < t->uptime) {
    /* Euch !  The clock's gone back ! */
    int i;

    for (i = 0; i < t->SamplePeriod; i++)
      t->in.SampleOctets[i] = t->out.SampleOctets[i] = 0;
    t->nSample = 0;
    t->uptime = downat;
  }
  return t->uptime ? downat - t->uptime : 0;
}

void
throughput_disp(struct pppThroughput *t, struct prompt *prompt)
{
  int secs_up, divisor;

  secs_up = throughput_uptime(t);
  prompt_Printf(prompt, "Connect time: %d:%02d:%02d", secs_up / 3600,
                (secs_up / 60) % 60, secs_up % 60);
  if (t->downtime)
    prompt_Printf(prompt, " - down at %s", ctime(&t->downtime));
  else
    prompt_Printf(prompt, "\n");

  divisor = secs_up ? secs_up : 1;
  prompt_Printf(prompt, "%llu octets in, %llu octets out\n",
                t->OctetsIn, t->OctetsOut);
  prompt_Printf(prompt, "%llu packets in, %llu packets out\n",
                t->PacketsIn, t->PacketsOut);
  if (t->rolling) {
    prompt_Printf(prompt, "  overall   %6llu bytes/sec\n",
                  (t->OctetsIn + t->OctetsOut) / divisor);
    prompt_Printf(prompt, "  %s %6llu bytes/sec in, %6llu bytes/sec out "
                  "(over the last %d secs)\n",
                  t->downtime ? "average  " : "currently",
                  t->in.OctetsPerSecond, t->out.OctetsPerSecond,
                  secs_up > t->SamplePeriod ? t->SamplePeriod : secs_up);
    prompt_Printf(prompt, "  peak      %6llu bytes/sec on %s",
                  t->BestOctetsPerSecond, ctime(&t->BestOctetsPerSecondTime));
  } else
    prompt_Printf(prompt, "Overall %llu bytes/sec\n",
                  (t->OctetsIn + t->OctetsOut) / divisor);
}


void
throughput_log(struct pppThroughput *t, int level, const char *title)
{
  if (t->uptime) {
    int secs_up;

    secs_up = throughput_uptime(t);
    if (title == NULL)
      title = "";
    log_Printf(level, "%s%sConnect time: %d secs: %llu octets in, %llu octets"
               " out\n", title, *title ? ": " : "", secs_up, t->OctetsIn,
               t->OctetsOut);
    log_Printf(level, "%s%s%llu packets in, %llu packets out\n",
               title, *title ? ": " : "",  t->PacketsIn, t->PacketsOut);
    if (secs_up == 0)
      secs_up = 1;
    if (t->rolling)
      log_Printf(level, " total %llu bytes/sec, peak %llu bytes/sec on %s",
                 (t->OctetsIn + t->OctetsOut) / secs_up, t->BestOctetsPerSecond,
                 ctime(&t->BestOctetsPerSecondTime));
    else
      log_Printf(level, " total %llu bytes/sec\n",
                 (t->OctetsIn + t->OctetsOut) / secs_up);
  }
}

static void
throughput_sampler(void *v)
{
  struct pppThroughput *t = (struct pppThroughput *)v;
  unsigned long long old;
  int uptime, divisor;
  unsigned long long octets;

  timer_Stop(&t->Timer);

  uptime = throughput_uptime(t);
  divisor = uptime < t->SamplePeriod ? uptime + 1 : t->SamplePeriod;

  old = t->in.SampleOctets[t->nSample];
  t->in.SampleOctets[t->nSample] = t->OctetsIn;
  t->in.OctetsPerSecond = (t->in.SampleOctets[t->nSample] - old) / divisor;

  old = t->out.SampleOctets[t->nSample];
  t->out.SampleOctets[t->nSample] = t->OctetsOut;
  t->out.OctetsPerSecond = (t->out.SampleOctets[t->nSample] - old) / divisor;

  octets = t->in.OctetsPerSecond + t->out.OctetsPerSecond;
  if (t->BestOctetsPerSecond < octets) {
    t->BestOctetsPerSecond = octets;
    time(&t->BestOctetsPerSecondTime);
  }

  if (++t->nSample == t->SamplePeriod)
    t->nSample = 0;

  if (t->callback.fn != NULL && uptime >= t->SamplePeriod)
    (*t->callback.fn)(t->callback.data);

  timer_Start(&t->Timer);
}

void
throughput_start(struct pppThroughput *t, const char *name, int rolling)
{
  int i;
  timer_Stop(&t->Timer);

  for (i = 0; i < t->SamplePeriod; i++)
    t->in.SampleOctets[i] = t->out.SampleOctets[i] = 0;
  t->nSample = 0;
  t->OctetsIn = t->OctetsOut = t->PacketsIn = t->PacketsOut = 0;
  t->in.OctetsPerSecond = t->out.OctetsPerSecond = t->BestOctetsPerSecond = 0;
  time(&t->BestOctetsPerSecondTime);
  t->downtime = 0;
  time(&t->uptime);
  throughput_restart(t, name, rolling);
}

void
throughput_restart(struct pppThroughput *t, const char *name, int rolling)
{
  timer_Stop(&t->Timer);
  t->rolling = rolling ? 1 : 0;
  if (t->rolling) {
    t->Timer.load = SECTICKS;
    t->Timer.func = throughput_sampler;
    t->Timer.name = name;
    t->Timer.arg = t;
    timer_Start(&t->Timer);
  } else {
    t->Timer.load = 0;
    t->Timer.func = NULL;
    t->Timer.name = NULL;
    t->Timer.arg = NULL;
  }
}

void
throughput_stop(struct pppThroughput *t)
{
  if (t->Timer.state != TIMER_STOPPED)
    time(&t->downtime);
  timer_Stop(&t->Timer);
}

void
throughput_addin(struct pppThroughput *t, long long n)
{
  t->OctetsIn += n;
  t->PacketsIn++;
}

void
throughput_addout(struct pppThroughput *t, long long n)
{
  t->OctetsOut += n;
  t->PacketsOut++;
}

void
throughput_clear(struct pppThroughput *t, int clear_type, struct prompt *prompt)
{
  if (clear_type & (THROUGHPUT_OVERALL|THROUGHPUT_CURRENT)) {
    int i;

    for (i = 0; i < t->SamplePeriod; i++)
      t->in.SampleOctets[i] = t->out.SampleOctets[i] = 0;
    t->nSample = 0;
  }

  if (clear_type & THROUGHPUT_OVERALL) {
    int divisor;

    if ((divisor = throughput_uptime(t)) == 0)
      divisor = 1;
    prompt_Printf(prompt, "overall cleared (was %6llu bytes/sec)\n",
                  (t->OctetsIn + t->OctetsOut) / divisor);
    t->OctetsIn = t->OctetsOut = t->PacketsIn = t->PacketsOut = 0;
    t->downtime = 0;
    time(&t->uptime);
  }

  if (clear_type & THROUGHPUT_CURRENT) {
    prompt_Printf(prompt, "current cleared (was %6llu bytes/sec in,"
                  " %6llu bytes/sec out)\n",
                  t->in.OctetsPerSecond, t->out.OctetsPerSecond);
    t->in.OctetsPerSecond = t->out.OctetsPerSecond = 0;
  }

  if (clear_type & THROUGHPUT_PEAK) {
    char *time_buf, *last;

    time_buf = ctime(&t->BestOctetsPerSecondTime);
    last = time_buf + strlen(time_buf);
    if (last > time_buf && *--last == '\n')
      *last = '\0';
    prompt_Printf(prompt, "peak    cleared (was %6llu bytes/sec on %s)\n",
                  t->BestOctetsPerSecond, time_buf);
    t->BestOctetsPerSecond = 0;
    time(&t->BestOctetsPerSecondTime);
  }
}

void
throughput_callback(struct pppThroughput *t, void (*fn)(void *), void *data)
{
  t->callback.fn = fn;
  t->callback.data = data;
}
