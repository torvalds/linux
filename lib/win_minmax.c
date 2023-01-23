// SPDX-License-Identifier: GPL-2.0
/*
 * lib/minmax.c: windowed min/max tracker
 *
 * Kathleen Nichols' algorithm for tracking the minimum (or maximum)
 * value of a data stream over some fixed time interval.  (E.g.,
 * the minimum RTT over the past five minutes.) It uses constant
 * space and constant time per update yet almost always delivers
 * the same minimum as an implementation that has to keep all the
 * data in the window.
 *
 * The algorithm keeps track of the best, 2nd best & 3rd best min
 * values, maintaining an invariant that the measurement time of
 * the n'th best >= n-1'th best. It also makes sure that the three
 * values are widely separated in the time window since that bounds
 * the worse case error when that data is monotonically increasing
 * over the window.
 *
 * Upon getting a new min, we can forget everything earlier because
 * it has no value - the new min is <= everything else in the window
 * by definition and it's the most recent. So we restart fresh on
 * every new min and overwrites 2nd & 3rd choices. The same property
 * holds for 2nd & 3rd best.
 */
#include <linux/module.h>
#include <linux/win_minmax.h>

/* As time advances, update the 1st, 2nd, and 3rd choices. */
static u32 minmax_subwin_update(struct minmax *m, u32 win,
				const struct minmax_sample *val)
{
	u32 dt = val->t - m->s[0].t;

	if (unlikely(dt > win)) {
		/*
		 * Passed entire window without a new val so make 2nd
		 * choice the new val & 3rd choice the new 2nd choice.
		 * we may have to iterate this since our 2nd choice
		 * may also be outside the window (we checked on entry
		 * that the third choice was in the window).
		 */
		m->s[0] = m->s[1];
		m->s[1] = m->s[2];
		m->s[2] = *val;
		if (unlikely(val->t - m->s[0].t > win)) {
			m->s[0] = m->s[1];
			m->s[1] = m->s[2];
			m->s[2] = *val;
		}
	} else if (unlikely(m->s[1].t == m->s[0].t) && dt > win/4) {
		/*
		 * We've passed a quarter of the window without a new val
		 * so take a 2nd choice from the 2nd quarter of the window.
		 */
		m->s[2] = m->s[1] = *val;
	} else if (unlikely(m->s[2].t == m->s[1].t) && dt > win/2) {
		/*
		 * We've passed half the window without finding a new val
		 * so take a 3rd choice from the last half of the window
		 */
		m->s[2] = *val;
	}
	return m->s[0].v;
}

/* Check if new measurement updates the 1st, 2nd or 3rd choice max. */
u32 minmax_running_max(struct minmax *m, u32 win, u32 t, u32 meas)
{
	struct minmax_sample val = { .t = t, .v = meas };

	if (unlikely(val.v >= m->s[0].v) ||	  /* found new max? */
	    unlikely(val.t - m->s[2].t > win))	  /* nothing left in window? */
		return minmax_reset(m, t, meas);  /* forget earlier samples */

	if (unlikely(val.v >= m->s[1].v))
		m->s[2] = m->s[1] = val;
	else if (unlikely(val.v >= m->s[2].v))
		m->s[2] = val;

	return minmax_subwin_update(m, win, &val);
}
EXPORT_SYMBOL(minmax_running_max);

/* Check if new measurement updates the 1st, 2nd or 3rd choice min. */
u32 minmax_running_min(struct minmax *m, u32 win, u32 t, u32 meas)
{
	struct minmax_sample val = { .t = t, .v = meas };

	if (unlikely(val.v <= m->s[0].v) ||	  /* found new min? */
	    unlikely(val.t - m->s[2].t > win))	  /* nothing left in window? */
		return minmax_reset(m, t, meas);  /* forget earlier samples */

	if (unlikely(val.v <= m->s[1].v))
		m->s[2] = m->s[1] = val;
	else if (unlikely(val.v <= m->s[2].v))
		m->s[2] = val;

	return minmax_subwin_update(m, win, &val);
}
