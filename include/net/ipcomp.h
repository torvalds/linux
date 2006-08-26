#ifndef _NET_IPCOMP_H
#define _NET_IPCOMP_H

#include <linux/crypto.h>
#include <linux/types.h>

#define IPCOMP_SCRATCH_SIZE     65400

struct ipcomp_data {
	u16 threshold;
	struct crypto_comp **tfms;
};

#endif
