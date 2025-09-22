/*
 * A Killer Adversary for Quicksort
 * M. D. MCILROY
 * http://www.cs.dartmouth.edu/~doug/mdmspe.pdf
 *
 * For comparison:
 *  Bentley & McIlroy: 4096 items, 1645585 compares
 *  random pivot:      4096 items, 8388649 compares
 *  introsort:         4096 items, 151580 compares
 *  heapsort:          4096 items, 48233 compares
 */

#include <stdio.h>
#include <stdlib.h>

static int *val;				/* item values */
static int ncmp;				/* number of comparisons */
static int nsolid;				/* number of solid items */
static int candidate;				/* pivot candidate */
static int gas;					/* gas value */

#define freeze(x)	(val[(x)] = nsolid++)

static int
cmp(const void *px, const void *py)
{
	const int x = *(const int *)px;
	const int y = *(const int *)py;

	ncmp++;
	if (val[x] == gas && val[y] == gas) {
		if (x == candidate)
			freeze(x);
		else
			freeze(y);
	}
	if (val[x] == gas)
		candidate = x;
	else if (val[y] == gas)
		candidate = y;
	return val[x] > val[y] ? 1 : val[x] < val[y] ? -1 : 0;
}

int
antiqsort(int n, int *a, int *ptr)
{
	int i;

	val = a;
	gas = n - 1;
	nsolid = ncmp = candidate = 0;
	for (i = 0; i < n; i++) {
		ptr[i] = i;
		val[i] = gas;
	}
	qsort(ptr, n, sizeof(*ptr), cmp);
	return ncmp;
}
