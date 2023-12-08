/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DNS resolver key type
 *
 * Copyright (C) 2010 Wang Lei. All Rights Reserved.
 * Written by Wang Lei (wang840925@gmail.com)
 */

#ifndef _KEYS_DNS_RESOLVER_TYPE_H
#define _KEYS_DNS_RESOLVER_TYPE_H

#include <linux/key-type.h>

extern struct key_type key_type_dns_resolver;

extern int request_dns_resolver_key(const char *description,
				    const char *callout_info,
				    char **data);

#endif /* _KEYS_DNS_RESOLVER_TYPE_H */
