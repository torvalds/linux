
#ifdef __KERNEL__
# include <linux/slab.h>
#else
# include <stdlib.h>
# include <assert.h>
# define kfree(x) do { if (x) free(x); } while (0)
# define BUG_ON(x) assert(!(x))
#endif

#include <linux/crush/crush.h>

const char *crush_bucket_alg_name(int alg)
{
	switch (alg) {
	case CRUSH_BUCKET_UNIFORM: return "uniform";
	case CRUSH_BUCKET_LIST: return "list";
	case CRUSH_BUCKET_TREE: return "tree";
	case CRUSH_BUCKET_STRAW: return "straw";
	default: return "unknown";
	}
}

/**
 * crush_get_bucket_item_weight - Get weight of an item in given bucket
 * @b: bucket pointer
 * @p: item index in bucket
 */
int crush_get_bucket_item_weight(const struct crush_bucket *b, int p)
{
	if ((__u32)p >= b->size)
		return 0;

	switch (b->alg) {
	case CRUSH_BUCKET_UNIFORM:
		return ((struct crush_bucket_uniform *)b)->item_weight;
	case CRUSH_BUCKET_LIST:
		return ((struct crush_bucket_list *)b)->item_weights[p];
	case CRUSH_BUCKET_TREE:
		if (p & 1)
			return ((struct crush_bucket_tree *)b)->node_weights[p];
		return 0;
	case CRUSH_BUCKET_STRAW:
		return ((struct crush_bucket_straw *)b)->item_weights[p];
	}
	return 0;
}

/**
 * crush_calc_parents - Calculate parent vectors for the given crush map.
 * @map: crush_map pointer
 */
void crush_calc_parents(struct crush_map *map)
{
	int i, b, c;

	for (b = 0; b < map->max_buckets; b++) {
		if (map->buckets[b] == NULL)
			continue;
		for (i = 0; i < map->buckets[b]->size; i++) {
			c = map->buckets[b]->items[i];
			BUG_ON(c >= map->max_devices ||
			       c < -map->max_buckets);
			if (c >= 0)
				map->device_parents[c] = map->buckets[b]->id;
			else
				map->bucket_parents[-1-c] = map->buckets[b]->id;
		}
	}
}

void crush_destroy_bucket_uniform(struct crush_bucket_uniform *b)
{
	kfree(b->h.perm);
	kfree(b->h.items);
	kfree(b);
}

void crush_destroy_bucket_list(struct crush_bucket_list *b)
{
	kfree(b->item_weights);
	kfree(b->sum_weights);
	kfree(b->h.perm);
	kfree(b->h.items);
	kfree(b);
}

void crush_destroy_bucket_tree(struct crush_bucket_tree *b)
{
	kfree(b->node_weights);
	kfree(b);
}

void crush_destroy_bucket_straw(struct crush_bucket_straw *b)
{
	kfree(b->straws);
	kfree(b->item_weights);
	kfree(b->h.perm);
	kfree(b->h.items);
	kfree(b);
}

void crush_destroy_bucket(struct crush_bucket *b)
{
	switch (b->alg) {
	case CRUSH_BUCKET_UNIFORM:
		crush_destroy_bucket_uniform((struct crush_bucket_uniform *)b);
		break;
	case CRUSH_BUCKET_LIST:
		crush_destroy_bucket_list((struct crush_bucket_list *)b);
		break;
	case CRUSH_BUCKET_TREE:
		crush_destroy_bucket_tree((struct crush_bucket_tree *)b);
		break;
	case CRUSH_BUCKET_STRAW:
		crush_destroy_bucket_straw((struct crush_bucket_straw *)b);
		break;
	}
}

/**
 * crush_destroy - Destroy a crush_map
 * @map: crush_map pointer
 */
void crush_destroy(struct crush_map *map)
{
	/* buckets */
	if (map->buckets) {
		__s32 b;
		for (b = 0; b < map->max_buckets; b++) {
			if (map->buckets[b] == NULL)
				continue;
			crush_destroy_bucket(map->buckets[b]);
		}
		kfree(map->buckets);
	}

	/* rules */
	if (map->rules) {
		__u32 b;
		for (b = 0; b < map->max_rules; b++)
			kfree(map->rules[b]);
		kfree(map->rules);
	}

	kfree(map->bucket_parents);
	kfree(map->device_parents);
	kfree(map);
}


