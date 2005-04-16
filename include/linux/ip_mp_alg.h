/* ip_mp_alg.h: IPV4 multipath algorithm support, user-visible values.
 *
 * Copyright (C) 2004, 2005 Einar Lueck <elueck@de.ibm.com>
 * Copyright (C) 2005 David S. Miller <davem@davemloft.net>
 */

#ifndef _LINUX_IP_MP_ALG_H
#define _LINUX_IP_MP_ALG_H

enum ip_mp_alg {
	IP_MP_ALG_NONE,
	IP_MP_ALG_RR,
	IP_MP_ALG_DRR,
	IP_MP_ALG_RANDOM,
	IP_MP_ALG_WRANDOM,
	__IP_MP_ALG_MAX
};

#define IP_MP_ALG_MAX (__IP_MP_ALG_MAX - 1)

#endif /* _LINUX_IP_MP_ALG_H */

