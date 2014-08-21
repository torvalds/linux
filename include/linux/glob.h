#ifndef _LINUX_GLOB_H
#define _LINUX_GLOB_H

#include <linux/types.h>	/* For bool */
#include <linux/compiler.h>	/* For __pure */

bool __pure glob_match(char const *pat, char const *str);

#endif	/* _LINUX_GLOB_H */
