/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_ARGS_H
#define _LINUX_ARGS_H

/*
 * How do these macros work?
 *
 * In __COUNT_ARGS() _0 to _12 are just placeholders from the start
 * in order to make sure _n is positioned over the correct number
 * from 12 to 0 (depending on X, which is a variadic argument list).
 * They serve no purpose other than occupying a position. Since each
 * macro parameter must have a distinct identifier, those identifiers
 * are as good as any.
 *
 * In COUNT_ARGS() we use actual integers, so __COUNT_ARGS() returns
 * that as _n.
 */

/* This counts to 12. Any more, it will return 13th argument. */
#define __COUNT_ARGS(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _n, X...) _n
#define COUNT_ARGS(X...) __COUNT_ARGS(, ##X, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

/* Concatenate two parameters, but allow them to be expanded beforehand. */
#define __CONCAT(a, b) a ## b
#define CONCATENATE(a, b) __CONCAT(a, b)

#endif	/* _LINUX_ARGS_H */
