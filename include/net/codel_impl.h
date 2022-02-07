#ifndef __NET_SCHED_CODEL_IMPL_H
#define __NET_SCHED_CODEL_IMPL_H

/*
 * Codel - The Controlled-Delay Active Queue Management algorithm
 *
 *  Copyright (C) 2011-2012 Kathleen Nichols <nichols@pollere.com>
 *  Copyright (C) 2011-2012 Van Jacobson <van@pollere.net>
 *  Copyright (C) 2012 Michael D. Taht <dave.taht@bufferbloat.net>
 *  Copyright (C) 2012,2015 Eric Dumazet <edumazet@google.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

/* Controlling Queue Delay (CoDel) algorithm
 * =========================================
 * Source : Kathleen Nichols and Van Jacobson
 * http://queue.acm.org/detail.cfm?id=2209336
 *
 * Implemented on linux by Dave Taht and Eric Dumazet
 */

#include <net/inet_ecn.h>

static void codel_params_init(struct codel_params *params)
{
	params->interval = MS2TIME(100);
	params->target = MS2TIME(5);
	params->ce_threshold = CODEL_DISABLED_THRESHOLD;
	params->ce_threshold_mask = 0;
	params->ce_threshold_selector = 0;
	params->ecn = false;
}

static void codel_vars_init(struct codel_vars *vars)
{
	memset(vars, 0, sizeof(*vars));
}

static void codel_stats_init(struct codel_stats *stats)
{
	stats->maxpacket = 0;
}

/*
 * http://en.wikipedia.org/wiki/Methods_of_computing_square_roots#Iterative_methods_for_reciprocal_square_roots
 * new_invsqrt = (invsqrt / 2) * (3 - count * invsqrt^2)
 *
 * Here, invsqrt is a fixed point number (< 1.0), 32bit mantissa, aka Q0.32
 */
static void codel_Newton_step(struct codel_vars *vars)
{
	u32 invsqrt = ((u32)vars->rec_inv_sqrt) << REC_INV_SQRT_SHIFT;
	u32 invsqrt2 = ((u64)invsqrt * invsqrt) >> 32;
	u64 val = (3LL << 32) - ((u64)vars->count * invsqrt2);

	val >>= 2; /* avoid overflow in following multiply */
	val = (val * invsqrt) >> (32 - 2 + 1);

	vars->rec_inv_sqrt = val >> REC_INV_SQRT_SHIFT;
}

/*
 * CoDel control_law is t + interval/sqrt(count)
 * We maintain in rec_inv_sqrt the reciprocal value of sqrt(count) to avoid
 * both sqrt() and divide operation.
 */
static codel_time_t codel_control_law(codel_time_t t,
				      codel_time_t interval,
				      u32 rec_inv_sqrt)
{
	return t + reciprocal_scale(interval, rec_inv_sqrt << REC_INV_SQRT_SHIFT);
}

static bool codel_should_drop(const struct sk_buff *skb,
			      void *ctx,
			      struct codel_vars *vars,
			      struct codel_params *params,
			      struct codel_stats *stats,
			      codel_skb_len_t skb_len_func,
			      codel_skb_time_t skb_time_func,
			      u32 *backlog,
			      codel_time_t now)
{
	bool ok_to_drop;
	u32 skb_len;

	if (!skb) {
		vars->first_above_time = 0;
		return false;
	}

	skb_len = skb_len_func(skb);
	vars->ldelay = now - skb_time_func(skb);

	if (unlikely(skb_len > stats->maxpacket))
		stats->maxpacket = skb_len;

	if (codel_time_before(vars->ldelay, params->target) ||
	    *backlog <= params->mtu) {
		/* went below - stay below for at least interval */
		vars->first_above_time = 0;
		return false;
	}
	ok_to_drop = false;
	if (vars->first_above_time == 0) {
		/* just went above from below. If we stay above
		 * for at least interval we'll say it's ok to drop
		 */
		vars->first_above_time = now + params->interval;
	} else if (codel_time_after(now, vars->first_above_time)) {
		ok_to_drop = true;
	}
	return ok_to_drop;
}

static struct sk_buff *codel_dequeue(void *ctx,
				     u32 *backlog,
				     struct codel_params *params,
				     struct codel_vars *vars,
				     struct codel_stats *stats,
				     codel_skb_len_t skb_len_func,
				     codel_skb_time_t skb_time_func,
				     codel_skb_drop_t drop_func,
				     codel_skb_dequeue_t dequeue_func)
{
	struct sk_buff *skb = dequeue_func(vars, ctx);
	codel_time_t now;
	bool drop;

	if (!skb) {
		vars->dropping = false;
		return skb;
	}
	now = codel_get_time();
	drop = codel_should_drop(skb, ctx, vars, params, stats,
				 skb_len_func, skb_time_func, backlog, now);
	if (vars->dropping) {
		if (!drop) {
			/* sojourn time below target - leave dropping state */
			vars->dropping = false;
		} else if (codel_time_after_eq(now, vars->drop_next)) {
			/* It's time for the next drop. Drop the current
			 * packet and dequeue the next. The dequeue might
			 * take us out of dropping state.
			 * If not, schedule the next drop.
			 * A large backlog might result in drop rates so high
			 * that the next drop should happen now,
			 * hence the while loop.
			 */
			while (vars->dropping &&
			       codel_time_after_eq(now, vars->drop_next)) {
				vars->count++; /* dont care of possible wrap
						* since there is no more divide
						*/
				codel_Newton_step(vars);
				if (params->ecn && INET_ECN_set_ce(skb)) {
					stats->ecn_mark++;
					vars->drop_next =
						codel_control_law(vars->drop_next,
								  params->interval,
								  vars->rec_inv_sqrt);
					goto end;
				}
				stats->drop_len += skb_len_func(skb);
				drop_func(skb, ctx);
				stats->drop_count++;
				skb = dequeue_func(vars, ctx);
				if (!codel_should_drop(skb, ctx,
						       vars, params, stats,
						       skb_len_func,
						       skb_time_func,
						       backlog, now)) {
					/* leave dropping state */
					vars->dropping = false;
				} else {
					/* and schedule the next drop */
					vars->drop_next =
						codel_control_law(vars->drop_next,
								  params->interval,
								  vars->rec_inv_sqrt);
				}
			}
		}
	} else if (drop) {
		u32 delta;

		if (params->ecn && INET_ECN_set_ce(skb)) {
			stats->ecn_mark++;
		} else {
			stats->drop_len += skb_len_func(skb);
			drop_func(skb, ctx);
			stats->drop_count++;

			skb = dequeue_func(vars, ctx);
			drop = codel_should_drop(skb, ctx, vars, params,
						 stats, skb_len_func,
						 skb_time_func, backlog, now);
		}
		vars->dropping = true;
		/* if min went above target close to when we last went below it
		 * assume that the drop rate that controlled the queue on the
		 * last cycle is a good starting point to control it now.
		 */
		delta = vars->count - vars->lastcount;
		if (delta > 1 &&
		    codel_time_before(now - vars->drop_next,
				      16 * params->interval)) {
			vars->count = delta;
			/* we dont care if rec_inv_sqrt approximation
			 * is not very precise :
			 * Next Newton steps will correct it quadratically.
			 */
			codel_Newton_step(vars);
		} else {
			vars->count = 1;
			vars->rec_inv_sqrt = ~0U >> REC_INV_SQRT_SHIFT;
		}
		vars->lastcount = vars->count;
		vars->drop_next = codel_control_law(now, params->interval,
						    vars->rec_inv_sqrt);
	}
end:
	if (skb && codel_time_after(vars->ldelay, params->ce_threshold)) {
		bool set_ce = true;

		if (params->ce_threshold_mask) {
			int dsfield = skb_get_dsfield(skb);

			set_ce = (dsfield >= 0 &&
				  (((u8)dsfield & params->ce_threshold_mask) ==
				   params->ce_threshold_selector));
		}
		if (set_ce && INET_ECN_set_ce(skb))
			stats->ce_mark++;
	}
	return skb;
}

#endif
