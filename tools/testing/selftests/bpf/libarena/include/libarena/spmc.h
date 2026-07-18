/* SPDX-License-Identifier: LGPL-2.1 OR BSD-2-Clause */

#pragma once

struct spmc_arr;

#define SPMC_ARR_BASESZ 128
#define SPMC_ARR_ORDERS 10

struct spmc_arr {
	u64 __arena *data;
	u64 order;
};

struct spmc {
	volatile struct spmc_arr __arena *cur;
	volatile u64 top;
	volatile u64 bottom;
	struct spmc_arr arr[SPMC_ARR_ORDERS];
};

int spmc_owned_add(struct spmc __arena *spmc, u64 val);
int spmc_owned_remove(struct spmc __arena *spmc, u64 *val);
int spmc_steal(struct spmc __arena *spmc, u64 *val);

struct spmc __arena *spmc_create(void);
int spmc_destroy(struct spmc __arena *spmc);
