// SPDX-License-Identifier: GPL-2.0
/*
 * helpers to map values in a linear range to range index
 *
 * Original idea borrowed from regulator framework
 *
 * It might be useful if we could support also inversely proportional ranges?
 * Copyright 2020 ROHM Semiconductors
 */

#include <linux/errno.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/linear_range.h>

/**
 * linear_range_values_in_range - return the amount of values in a range
 * @r:		pointer to linear range where values are counted
 *
 * Compute the amount of values in range pointed by @r. Note, values can
 * be all equal - range with selectors 0,...,2 with step 0 still contains
 * 3 values even though they are all equal.
 *
 * Return: the amount of values in range pointed by @r
 */
unsigned int linear_range_values_in_range(const struct linear_range *r)
{
	if (!r)
		return 0;
	return r->max_sel - r->min_sel + 1;
}
EXPORT_SYMBOL_GPL(linear_range_values_in_range);

/**
 * linear_range_values_in_range_array - return the amount of values in ranges
 * @r:		pointer to array of linear ranges where values are counted
 * @ranges:	amount of ranges we include in computation.
 *
 * Compute the amount of values in ranges pointed by @r. Note, values can
 * be all equal - range with selectors 0,...,2 with step 0 still contains
 * 3 values even though they are all equal.
 *
 * Return: the amount of values in first @ranges ranges pointed by @r
 */
unsigned int linear_range_values_in_range_array(const struct linear_range *r,
						int ranges)
{
	int i, values_in_range = 0;

	for (i = 0; i < ranges; i++) {
		int values;

		values = linear_range_values_in_range(&r[i]);
		if (!values)
			return values;

		values_in_range += values;
	}
	return values_in_range;
}
EXPORT_SYMBOL_GPL(linear_range_values_in_range_array);

/**
 * linear_range_get_max_value - return the largest value in a range
 * @r:		pointer to linear range where value is looked from
 *
 * Return: the largest value in the given range
 */
unsigned int linear_range_get_max_value(const struct linear_range *r)
{
	return r->min + (r->max_sel - r->min_sel) * r->step;
}
EXPORT_SYMBOL_GPL(linear_range_get_max_value);

/**
 * linear_range_get_value - fetch a value from given range
 * @r:		pointer to linear range where value is looked from
 * @selector:	selector for which the value is searched
 * @val:	address where found value is updated
 *
 * Search given ranges for value which matches given selector.
 *
 * Return: 0 on success, -EINVAL given selector is not found from any of the
 * ranges.
 */
int linear_range_get_value(const struct linear_range *r, unsigned int selector,
			   unsigned int *val)
{
	if (r->min_sel > selector || r->max_sel < selector)
		return -EINVAL;

	*val = r->min + (selector - r->min_sel) * r->step;

	return 0;
}
EXPORT_SYMBOL_GPL(linear_range_get_value);

/**
 * linear_range_get_value_array - fetch a value from array of ranges
 * @r:		pointer to array of linear ranges where value is looked from
 * @ranges:	amount of ranges in an array
 * @selector:	selector for which the value is searched
 * @val:	address where found value is updated
 *
 * Search through an array of ranges for value which matches given selector.
 *
 * Return: 0 on success, -EINVAL given selector is not found from any of the
 * ranges.
 */
int linear_range_get_value_array(const struct linear_range *r, int ranges,
				 unsigned int selector, unsigned int *val)
{
	int i;

	for (i = 0; i < ranges; i++)
		if (r[i].min_sel <= selector && r[i].max_sel >= selector)
			return linear_range_get_value(&r[i], selector, val);

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(linear_range_get_value_array);

/**
 * linear_range_get_selector_low - return linear range selector for value
 * @r:		pointer to linear range where selector is looked from
 * @val:	value for which the selector is searched
 * @selector:	address where found selector value is updated
 * @found:	flag to indicate that given value was in the range
 *
 * Return selector which which range value is closest match for given
 * input value. Value is matching if it is equal or smaller than given
 * value. If given value is in the range, then @found is set true.
 *
 * Return: 0 on success, -EINVAL if range is invalid or does not contain
 * value smaller or equal to given value
 */
int linear_range_get_selector_low(const struct linear_range *r,
				  unsigned int val, unsigned int *selector,
				  bool *found)
{
	*found = false;

	if (r->min > val)
		return -EINVAL;

	if (linear_range_get_max_value(r) < val) {
		*selector = r->max_sel;
		return 0;
	}

	*found = true;

	if (r->step == 0)
		*selector = r->min_sel;
	else
		*selector = (val - r->min) / r->step + r->min_sel;

	return 0;
}
EXPORT_SYMBOL_GPL(linear_range_get_selector_low);

/**
 * linear_range_get_selector_low_array - return linear range selector for value
 * @r:		pointer to array of linear ranges where selector is looked from
 * @ranges:	amount of ranges to scan from array
 * @val:	value for which the selector is searched
 * @selector:	address where found selector value is updated
 * @found:	flag to indicate that given value was in the range
 *
 * Scan array of ranges for selector which which range value matches given
 * input value. Value is matching if it is equal or smaller than given
 * value. If given value is found to be in a range scanning is stopped and
 * @found is set true. If a range with values smaller than given value is found
 * but the range max is being smaller than given value, then the ranges
 * biggest selector is updated to @selector but scanning ranges is continued
 * and @found is set to false.
 *
 * Return: 0 on success, -EINVAL if range array is invalid or does not contain
 * range with a value smaller or equal to given value
 */
int linear_range_get_selector_low_array(const struct linear_range *r,
					int ranges, unsigned int val,
					unsigned int *selector, bool *found)
{
	int i;
	int ret = -EINVAL;

	for (i = 0; i < ranges; i++) {
		int tmpret;

		tmpret = linear_range_get_selector_low(&r[i], val, selector,
						       found);
		if (!tmpret)
			ret = 0;

		if (*found)
			break;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(linear_range_get_selector_low_array);

/**
 * linear_range_get_selector_high - return linear range selector for value
 * @r:		pointer to linear range where selector is looked from
 * @val:	value for which the selector is searched
 * @selector:	address where found selector value is updated
 * @found:	flag to indicate that given value was in the range
 *
 * Return selector which which range value is closest match for given
 * input value. Value is matching if it is equal or higher than given
 * value. If given value is in the range, then @found is set true.
 *
 * Return: 0 on success, -EINVAL if range is invalid or does not contain
 * value greater or equal to given value
 */
int linear_range_get_selector_high(const struct linear_range *r,
				   unsigned int val, unsigned int *selector,
				   bool *found)
{
	*found = false;

	if (linear_range_get_max_value(r) < val)
		return -EINVAL;

	if (r->min > val) {
		*selector = r->min_sel;
		return 0;
	}

	*found = true;

	if (r->step == 0)
		*selector = r->max_sel;
	else
		*selector = DIV_ROUND_UP(val - r->min, r->step) + r->min_sel;

	return 0;
}
EXPORT_SYMBOL_GPL(linear_range_get_selector_high);
