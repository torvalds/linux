// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Function for comparing two 64-bit signed integers.
 */

#include <linux/export.h>
#include <linux/libgcc.h>

// Compare two 64-bit signed integers
word_type notrace __cmpdi2(long long a, long long b)
{
    // Union to split 64-bit values into high and low 32-bit parts
    const DWunion au = {
        .ll = a
    };
    const DWunion bu = {
        .ll = b
    };

    // Compare the high 32 bits
    if (au.s.high < bu.s.high)
        return 0;  // a < b
    else if (au.s.high > bu.s.high)
        return 2;  // a > b

    // If high parts are equal, compare the low 32 bits
    if ((unsigned int) au.s.low < (unsigned int) bu.s.low)
        return 0;  // a < b
    else if ((unsigned int) au.s.low > (unsigned int) bu.s.low)
        return 2;  // a > b

    // Return 1 if both parts are equal (a == b)
    return 1;  // a == b
}

// Make the function available for use in other modules
EXPORT_SYMBOL(__cmpdi2);
