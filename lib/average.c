/*
 * lib/average.c
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#include <linux/module.h>
#include <linux/average.h>
#include <linux/bug.h>

/**
 * DOC: Exponentially Weighted Moving Average (EWMA)
 *
 * These are generic functions for calculating Exponentially Weighted Moving
 * Averages (EWMA). We keep a structure with the EWMA parameters and a scaled
 * up internal representation of the average value to prevent rounding errors.
 * The factor for scaling up and the exponential weight (or decay rate) have to
 * be specified thru the init fuction. The structure should not be accessed
 * directly but only thru the helper functions.
 */

/**
 * ewma_init() - Initialize EWMA parameters
 * @avg: Average structure
 * @factor: Factor to use for the scaled up internal value. The maximum value
 *	of averages can be ULONG_MAX/(factor*weight).
 * @weight: Exponential weight, or decay rate. This defines how fast the
 *	influence of older values decreases. Has to be bigger than 1.
 *
 * Initialize the EWMA parameters for a given struct ewma @avg.
 */
void ewma_init(struct ewma *avg, unsigned long factor, unsigned long weight)
{
	WARN_ON(weight <= 1 || factor == 0);
	avg->internal = 0;
	avg->weight = weight;
	avg->factor = factor;
}
EXPORT_SYMBOL(ewma_init);

/**
 * ewma_add() - Exponentially weighted moving average (EWMA)
 * @avg: Average structure
 * @val: Current value
 *
 * Add a sample to the average.
 */
struct ewma *ewma_add(struct ewma *avg, unsigned long val)
{
	avg->internal = avg->internal  ?
		(((avg->internal * (avg->weight - 1)) +
			(val * avg->factor)) / avg->weight) :
		(val * avg->factor);
	return avg;
}
EXPORT_SYMBOL(ewma_add);
