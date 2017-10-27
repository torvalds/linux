#ifndef CEPH_CRUSH_MAPPER_H
#define CEPH_CRUSH_MAPPER_H

/*
 * CRUSH functions for find rules and then mapping an input to an
 * output set.
 *
 * LGPL2
 */

#include "crush.h"

extern int crush_find_rule(const struct crush_map *map, int ruleset, int type, int size);
int crush_do_rule(const struct crush_map *map,
		  int ruleno, int x, int *result, int result_max,
		  const __u32 *weight, int weight_max,
		  void *cwin, const struct crush_choose_arg *choose_args);

/*
 * Returns the exact amount of workspace that will need to be used
 * for a given combination of crush_map and result_max. The caller can
 * then allocate this much on its own, either on the stack, in a
 * per-thread long-lived buffer, or however it likes.
 */
static inline size_t crush_work_size(const struct crush_map *map,
				     int result_max)
{
	return map->working_size + result_max * 3 * sizeof(__u32);
}

void crush_init_workspace(const struct crush_map *map, void *v);

#endif
