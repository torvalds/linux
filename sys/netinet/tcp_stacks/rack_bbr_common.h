#ifndef __pacer_timer_h__
#define __pacer_timer_h__
/*-
 * Copyright (c) 2017 Netflix, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * __FBSDID("$FreeBSD$");
 */
/* Common defines and such used by both RACK and BBR */
/* Special values for mss accounting array */
#define TCP_MSS_ACCT_JUSTRET 0
#define TCP_MSS_ACCT_SNDACK  1
#define TCP_MSS_ACCT_PERSIST 2
#define TCP_MSS_ACCT_ATIMER  60
#define TCP_MSS_ACCT_INPACE  61
#define TCP_MSS_ACCT_LATE    62
#define TCP_MSS_SMALL_SIZE_OFF 63	/* Point where small sizes enter */
#define TCP_MSS_ACCT_SIZE    70
#define TCP_MSS_SMALL_MAX_SIZE_DIV (TCP_MSS_ACCT_SIZE - TCP_MSS_SMALL_SIZE_OFF)


/* Magic flags to tell whats cooking on the pacing wheel */
#define PACE_PKT_OUTPUT 0x01	/* Output Packets being paced */
#define PACE_TMR_RACK   0x02	/* RACK timer running */
#define PACE_TMR_TLP    0x04	/* TLP timer running */
#define PACE_TMR_RXT    0x08	/* Retransmit timer running */
#define PACE_TMR_PERSIT 0x10	/* Persists timer running */
#define PACE_TMR_KEEP   0x20	/* Keep alive timer running */
#define PACE_TMR_DELACK 0x40	/* Delayed ack timer running */
#define PACE_TMR_MASK   (PACE_TMR_KEEP|PACE_TMR_PERSIT|PACE_TMR_RXT|PACE_TMR_TLP|PACE_TMR_RACK|PACE_TMR_DELACK)

/* Magic flags for tracing progress events */
#define PROGRESS_DROP   1
#define PROGRESS_UPDATE 2
#define PROGRESS_CLEAR  3
#define PROGRESS_START  4


/* RTT sample methods */
#define USE_RTT_HIGH 0
#define USE_RTT_LOW  1
#define USE_RTT_AVG  2

#ifdef _KERNEL
/* We have only 7 bits in rack so assert its true */
CTASSERT((PACE_TMR_MASK & 0x80) == 0);
#endif
#endif
