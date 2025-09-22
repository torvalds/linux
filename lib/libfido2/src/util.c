/*
 * Copyright (c) 2022 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

#include "fido.h"

int
fido_to_uint64(const char *str, int base, uint64_t *out)
{
	char *ep;
	unsigned long long ull;

	errno = 0;
	ull = strtoull(str, &ep, base);
	if (str == ep || *ep != '\0')
		return -1;
	else if (ull == ULLONG_MAX && errno == ERANGE)
		return -1;
	else if (ull > UINT64_MAX)
		return -1;
	*out = (uint64_t)ull;

	return 0;
}
