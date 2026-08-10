/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_GLOB_H
#define _LINUX_GLOB_H

#include <linux/types.h>	/* For bool */
#include <linux/compiler.h>	/* For __pure */

bool __pure glob_match(char const *pat, char const *str);
bool __pure glob_match_len(char const *pat, char const *str, size_t len);

#endif	/* _LINUX_GLOB_H */
