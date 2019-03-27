/*-
 * Copyright (c) 2014, Neville-Neil Consulting
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
 *
 * Author: George V. Neville-Neil
 *
 */

/*
 * Calculate the time overhead of starting, stopping, and recording
 * pmc counters.
 *
 * The only argument is a counter name, such as "instruction-retired"
 * which is CPU dependent and can be found with pmmcontrol(8) using
 * pmccontrol -L.
 *
 * The start, stop, read and write operations are timed using the
 * rdtsc() macro which reads the Time Stamp Counter on the CPU.
 */

#include <stdio.h>
#include <err.h>
#include <sysexits.h>
#include <sys/types.h>
#include <machine/cpufunc.h>
#include <pmc.h>

int
main(int argc, char **argv)
{
	pmc_id_t pmcid;
	pmc_value_t read_value;
	pmc_value_t read_clear_value;
	uint64_t tsc1, write_cyc, start_cyc, read_cyc, stop_cyc;
	char *counter_name;
	
	if (argc != 2)
		err(EX_USAGE, "counter-name required");

	counter_name = argv[1];
	
	if (pmc_init() != 0)
		err(EX_OSERR, "hwpmc(4) not loaded, kldload or update your kernel");

	if (pmc_allocate(counter_name, PMC_MODE_SC, 0, 0, &pmcid, 64*1024) < 0)
		err(EX_OSERR, "failed to allocate %s as a system counter in counting mode",
		    counter_name);

	tsc1 = rdtsc();
	if (pmc_write(pmcid, 0) < 0)
		err(EX_OSERR, "failed to zero counter %s", counter_name);
	write_cyc = rdtsc() - tsc1;

	tsc1 = rdtsc();
	if (pmc_start(pmcid) < 0)
		err(EX_OSERR, "failed to start counter %s", counter_name);
	start_cyc = rdtsc() - tsc1;

	tsc1 = rdtsc();
	if (pmc_read(pmcid, &read_value) < 0)
		err(EX_OSERR, "failed to read counter %s", counter_name);
	read_cyc = rdtsc() - tsc1;

	tsc1 = rdtsc();
	if (pmc_stop(pmcid) < 0)
		err(EX_OSERR, "failed to stop counter %s", counter_name);
	stop_cyc = rdtsc() - tsc1;
	
	if (pmc_rw(pmcid, 0, &read_clear_value))
		err(EX_OSERR, "failed to read and zero %s", counter_name);

	if (pmc_release(pmcid) < 0)
		err(EX_OSERR, "failed to release %s as a system counter in counting mode",
		    counter_name);

	printf("Counter %s, read value %ld, read/clear value %ld\n",
	    counter_name, read_value, read_clear_value);
	printf("Cycles to start: %ld\tstop: %ld\tread: %ld\twrite: %ld\n",
	    start_cyc, stop_cyc, read_cyc, stop_cyc);

	return(0);
}

