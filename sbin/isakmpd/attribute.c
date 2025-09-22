/* $OpenBSD: attribute.c,v 1.13 2017/02/03 08:23:46 guenther Exp $	 */
/* $EOM: attribute.c,v 1.10 2000/02/20 19:58:36 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niklas Hallqvist.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <string.h>

#include "attribute.h"
#include "conf.h"
#include "log.h"
#include "isakmp.h"
#include "util.h"

u_int8_t *
attribute_set_basic(u_int8_t *buf, u_int16_t type, u_int16_t value)
{
	SET_ISAKMP_ATTR_TYPE(buf, ISAKMP_ATTR_MAKE(1, type));
	SET_ISAKMP_ATTR_LENGTH_VALUE(buf, value);
	return buf + ISAKMP_ATTR_VALUE_OFF;
}

u_int8_t *
attribute_set_var(u_int8_t *buf, u_int16_t type, u_int8_t *value,
    u_int16_t len)
{
	SET_ISAKMP_ATTR_TYPE(buf, ISAKMP_ATTR_MAKE(0, type));
	SET_ISAKMP_ATTR_LENGTH_VALUE(buf, len);
	memcpy(buf + ISAKMP_ATTR_VALUE_OFF, value, len);
	return buf + ISAKMP_ATTR_VALUE_OFF + len;
}

/*
 * Execute a function FUNC taking an attribute type, value, length and ARG
 * as arguments for each attribute in the area of ISAKMP attributes located
 * at BUF, sized SZ.  If any invocation fails, the processing aborts with a
 * -1 return value.  If all goes well return zero.
 */
int
attribute_map(u_int8_t *buf, size_t sz, int (*func)(u_int16_t, u_int8_t *,
    u_int16_t, void *), void *arg)
{
	u_int8_t       *attr;
	int             fmt;
	u_int16_t       type;
	u_int8_t       *value;
	u_int16_t       len;

	for (attr = buf; attr < buf + sz; attr = value + len) {
		if (attr + ISAKMP_ATTR_VALUE_OFF > buf + sz)
			return -1;
		type = GET_ISAKMP_ATTR_TYPE(attr);
		fmt = ISAKMP_ATTR_FORMAT(type);
		type = ISAKMP_ATTR_TYPE(type);
		value = attr + (fmt ? ISAKMP_ATTR_LENGTH_VALUE_OFF
		    : ISAKMP_ATTR_VALUE_OFF);
		len = (fmt ? ISAKMP_ATTR_LENGTH_VALUE_LEN
		    : GET_ISAKMP_ATTR_LENGTH_VALUE(attr));
		if (value + len > buf + sz)
			return -1;
		if (func(type, value, len, arg))
			return -1;
	}
	return 0;
}

int
attribute_set_constant(char *section, char *tag, struct constant_map *map,
    int attr_class, u_int8_t **attr)
{
	char	*name;
	int	 value;

	name = conf_get_str(section, tag);
	if (!name) {
		LOG_DBG((LOG_MISC, 70,
		    "attribute_set_constant: no %s in the %s section", tag,
		    section));
		return -1;
	}
	value = constant_value(map, name);
	*attr = attribute_set_basic(*attr, attr_class, value);
	return 0;
}
