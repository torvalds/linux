// SPDX-License-Identifier: GPL-2.0
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>

#include "futex.h"

#ifndef PR_FUTEX_HASH
#define PR_FUTEX_HASH                   78
# define PR_FUTEX_HASH_SET_SLOTS        1
# define PR_FUTEX_HASH_GET_SLOTS        2
#endif // PR_FUTEX_HASH

void futex_set_nbuckets_param(struct bench_futex_parameters *params)
{
	int ret;

	if (params->nbuckets < 0)
		return;

	ret = prctl(PR_FUTEX_HASH, PR_FUTEX_HASH_SET_SLOTS, params->nbuckets, 0);
	if (ret) {
		printf("Requesting %d hash buckets failed: %d/%m\n",
		       params->nbuckets, ret);
		err(EXIT_FAILURE, "prctl(PR_FUTEX_HASH)");
	}
}

void futex_print_nbuckets(struct bench_futex_parameters *params)
{
	char *futex_hash_mode;
	int ret;

	ret = prctl(PR_FUTEX_HASH, PR_FUTEX_HASH_GET_SLOTS);
	if (params->nbuckets >= 0) {
		if (ret != params->nbuckets) {
			if (ret < 0) {
				printf("Can't query number of buckets: %m\n");
				err(EXIT_FAILURE, "prctl(PR_FUTEX_HASH)");
			}
			printf("Requested number of hash buckets does not currently used.\n");
			printf("Requested: %d in usage: %d\n", params->nbuckets, ret);
			err(EXIT_FAILURE, "prctl(PR_FUTEX_HASH)");
		}
		if (params->nbuckets == 0)
			ret = asprintf(&futex_hash_mode, "Futex hashing: global hash");
		else
			ret = asprintf(&futex_hash_mode, "Futex hashing: %d hash buckets",
				       params->nbuckets);
	} else {
		if (ret <= 0) {
			ret = asprintf(&futex_hash_mode, "Futex hashing: global hash");
		} else {
			ret = asprintf(&futex_hash_mode, "Futex hashing: auto resized to %d buckets",
				       ret);
		}
	}
	if (ret < 0)
		err(EXIT_FAILURE, "ENOMEM, futex_hash_mode");
	printf("%s\n", futex_hash_mode);
	free(futex_hash_mode);
}
