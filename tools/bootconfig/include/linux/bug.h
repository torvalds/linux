/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SKC_LINUX_BUG_H
#define _SKC_LINUX_BUG_H

#include <stdio.h>
#include <stdlib.h>

#define WARN_ON(cond)	\
	((cond) ? printf("Internal warning(%s:%d, %s): %s\n",	\
			__FILE__, __LINE__, __func__, #cond) : 0)

#endif
