/*
 * Copyright (c) 2018 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>

int
fido_get_random(void *buf, size_t len)
{
	arc4random_buf(buf, len);
	return (0);
}
