#ifndef _TOOLS_MATH_H
#define _TOOLS_MATH_H

/*
 * This looks more complex than it should be. But we need to
 * get the type for the ~ right in round_down (it needs to be
 * as wide as the result!), and we want to evaluate the macro
 * arguments just once each.
 */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

#ifndef roundup
#define roundup(x, y) (                                \
{                                                      \
	const typeof(y) __y = y;		       \
	(((x) + (__y - 1)) / __y) * __y;	       \
}                                                      \
)
#endif

#endif
