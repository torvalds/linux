/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ARRAY_SIZE_H
#define ARRAY_SIZE_H

/**
 * ARRAY_SIZE - get the number of elements in array @arr
 * @arr: array to be sized
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif /* ARRAY_SIZE_H */
