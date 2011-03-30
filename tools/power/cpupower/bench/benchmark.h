/*  cpufreq-bench CPUFreq microbenchmark
 *
 *  Copyright (C) 2008 Christian Kornacker <ckornacker@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* load loop, this schould take about 1 to 2ms to complete */
#define ROUNDS(x) {unsigned int rcnt;			       \
		for (rcnt = 0; rcnt< x*1000; rcnt++) { \
			(void)(((int)(pow(rcnt, rcnt) * sqrt(rcnt*7230970)) ^ 7230716) ^ (int)atan2(rcnt, rcnt)); \
		}}							\


void start_benchmark(struct config *config);
