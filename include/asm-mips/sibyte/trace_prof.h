/*
 * Copyright (C) 2001 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __ASM_SIBYTE_TRACE_PROF_H
#define __ASM_SIBYTE_TRACE_PROF_H

#undef DBG
#if SBPROF_TB_DEBUG
#define DBG(a) a
#else
#define DBG(a)
#endif

#define SBPROF_TB_MAJOR 240
#define DEVNAME "bcm1250_tbprof"

typedef u_int64_t tb_sample_t[6*256];

struct sbprof_tb {
	int          open;
	tb_sample_t *sbprof_tbbuf;
	int          next_tb_sample;

	volatile int tb_enable;
	volatile int tb_armed;

	wait_queue_head_t tb_sync;
	wait_queue_head_t tb_read;
};

#define MAX_SAMPLE_BYTES (24*1024*1024)
#define MAX_TBSAMPLE_BYTES (12*1024*1024)

#define MAX_SAMPLES (MAX_SAMPLE_BYTES/sizeof(u_int32_t))
#define TB_SAMPLE_SIZE (sizeof(tb_sample_t))
#define MAX_TB_SAMPLES (MAX_TBSAMPLE_BYTES/TB_SAMPLE_SIZE)

/* IOCTLs */
#define SBPROF_ZBSTART		_IOW('s', 0, int)
#define SBPROF_ZBSTOP		_IOW('s', 1, int)
#define SBPROF_ZBWAITFULL	_IOW('s', 2, int)

/***************************************************************************
 * Routines for gathering ZBbus profiles using trace buffer
 ***************************************************************************/

/* Requires: Already called zclk_timer_init with a value that won't
	     saturate 40 bits.  No subsequent use of SCD performance counters
	     or trace buffer.
   Effect:   Starts gathering random ZBbus profiles using trace buffer. */
extern int sbprof_zbprof_start(struct file *filp);

/* Effect: Stops collection of ZBbus profiles */
extern int sbprof_zbprof_stop(void);


/***************************************************************************
 * Routines for using 40-bit SCD cycle counter
 *
 * Client responsible for either handling interrupts or making sure
 * the cycles counter never saturates, e.g., by doing
 * zclk_timer_init(0) at least every 2^40 - 1 ZCLKs.
 ***************************************************************************/

/* Configures SCD counter 0 to count ZCLKs starting from val;
   Configures SCD counters1,2,3 to count nothing.
   Must not be called while gathering ZBbus profiles.

unsigned long long val; */
#define zclk_timer_init(val) \
  __asm__ __volatile__ (".set push;" \
			".set mips64;" \
			"la   $8, 0xb00204c0;" /* SCD perf_cnt_cfg */ \
			"sd   %0, 0x10($8);"   /* write val to counter0 */ \
			"sd   %1, 0($8);"      /* config counter0 for zclks*/ \
			".set pop" \
			: /* no outputs */ \
						     /* enable, counter0 */ \
			: /* inputs */ "r"(val), "r" ((1ULL << 33) | 1ULL) \
			: /* modifies */ "$8" )


/* Reads SCD counter 0 and puts result in value
   unsigned long long val; */
#define zclk_get(val) \
  __asm__ __volatile__ (".set push;" \
			".set mips64;" \
			"la   $8, 0xb00204c0;" /* SCD perf_cnt_cfg */ \
			"ld   %0, 0x10($8);"   /* write val to counter0 */ \
			".set pop" \
			: /* outputs */ "=r"(val) \
			: /* inputs */ \
			: /* modifies */ "$8" )

#endif /* __ASM_SIBYTE_TRACE_PROF_H */
