/* Public domain. */

#ifndef _LINUX_MATH_H
#define _LINUX_MATH_H

#define mult_frac(x, n, d) (((x) * (n)) / (d))

#define round_up(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#define round_down(x, y) (((x) / (y)) * (y)) /* y is power of two */
#define rounddown(x, y) (((x) / (y)) * (y)) /* arbitrary y */
#define DIV_ROUND_UP(x, y)	(((x) + ((y) - 1)) / (y))
#define DIV_ROUND_UP_ULL(x, y)	DIV_ROUND_UP(x, y)
#define DIV_ROUND_DOWN(x, y)	((x) / (y))
#define DIV_ROUND_DOWN_ULL(x, y)	DIV_ROUND_DOWN(x, y)
#define DIV_ROUND_CLOSEST(x, y)	(((x) + ((y) / 2)) / (y))
#define DIV_ROUND_CLOSEST_ULL(x, y)	DIV_ROUND_CLOSEST(x, y)

#endif
