#ifndef _NET_IPCOMP_H
#define _NET_IPCOMP_H

#include <linux/types.h>

#define IPCOMP_SCRATCH_SIZE     65400

struct crypto_tfm;

struct ipcomp_data {
	u16 threshold;
	struct crypto_tfm **tfms;
};

#endif
