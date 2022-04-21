// SPDX-License-Identifier: GPL-2.0
/*
 * Randomized tests for eBPF longest-prefix-match maps
 *
 * This program runs randomized tests against the lpm-bpf-map. It implements a
 * "Trivial Longest Prefix Match" (tlpm) based on simple, linear, singly linked
 * lists. The implementation should be pretty straightforward.
 *
 * Based on tlpm, this inserts randomized data into bpf-lpm-maps and verifies
 * the trie-based bpf-map implementation behaves the same way as tlpm.
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <linux/bpf.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <bpf/bpf.h>

#include "bpf_util.h"
#include "bpf_rlimit.h"

struct tlpm_node {
	struct tlpm_node *next;
	size_t n_bits;
	uint8_t key[];
};

static struct tlpm_node *tlpm_match(struct tlpm_node *list,
				    const uint8_t *key,
				    size_t n_bits);

static struct tlpm_node *tlpm_add(struct tlpm_node *list,
				  const uint8_t *key,
				  size_t n_bits)
{
	struct tlpm_node *node;
	size_t n;

	n = (n_bits + 7) / 8;

	/* 'overwrite' an equivalent entry if one already exists */
	node = tlpm_match(list, key, n_bits);
	if (node && node->n_bits == n_bits) {
		memcpy(node->key, key, n);
		return list;
	}

	/* add new entry with @key/@n_bits to @list and return new head */

	node = malloc(sizeof(*node) + n);
	assert(node);

	node->next = list;
	node->n_bits = n_bits;
	memcpy(node->key, key, n);

	return node;
}

static void tlpm_clear(struct tlpm_node *list)
{
	struct tlpm_node *node;

	/* free all entries in @list */

	while ((node = list)) {
		list = list->next;
		free(node);
	}
}

static struct tlpm_node *tlpm_match(struct tlpm_node *list,
				    const uint8_t *key,
				    size_t n_bits)
{
	struct tlpm_node *best = NULL;
	size_t i;

	/* Perform longest prefix-match on @key/@n_bits. That is, iterate all
	 * entries and match each prefix against @key. Remember the "best"
	 * entry we find (i.e., the longest prefix that matches) and return it
	 * to the caller when done.
	 */

	for ( ; list; list = list->next) {
		for (i = 0; i < n_bits && i < list->n_bits; ++i) {
			if ((key[i / 8] & (1 << (7 - i % 8))) !=
			    (list->key[i / 8] & (1 << (7 - i % 8))))
				break;
		}

		if (i >= list->n_bits) {
			if (!best || i > best->n_bits)
				best = list;
		}
	}

	return best;
}

static struct tlpm_node *tlpm_delete(struct tlpm_node *list,
				     const uint8_t *key,
				     size_t n_bits)
{
	struct tlpm_node *best = tlpm_match(list, key, n_bits);
	struct tlpm_node *node;

	if (!best || best->n_bits != n_bits)
		return list;

	if (best == list) {
		node = best->next;
		free(best);
		return node;
	}

	for (node = list; node; node = node->next) {
		if (node->next == best) {
			node->next = best->next;
			free(best);
			return list;
		}
	}
	/* should never get here */
	assert(0);
	return list;
}

static void test_lpm_basic(void)
{
	struct tlpm_node *list = NULL, *t1, *t2;

	/* very basic, static tests to verify tlpm works as expected */

	assert(!tlpm_match(list, (uint8_t[]){ 0xff }, 8));

	t1 = list = tlpm_add(list, (uint8_t[]){ 0xff }, 8);
	assert(t1 == tlpm_match(list, (uint8_t[]){ 0xff }, 8));
	assert(t1 == tlpm_match(list, (uint8_t[]){ 0xff, 0xff }, 16));
	assert(t1 == tlpm_match(list, (uint8_t[]){ 0xff, 0x00 }, 16));
	assert(!tlpm_match(list, (uint8_t[]){ 0x7f }, 8));
	assert(!tlpm_match(list, (uint8_t[]){ 0xfe }, 8));
	assert(!tlpm_match(list, (uint8_t[]){ 0xff }, 7));

	t2 = list = tlpm_add(list, (uint8_t[]){ 0xff, 0xff }, 16);
	assert(t1 == tlpm_match(list, (uint8_t[]){ 0xff }, 8));
	assert(t2 == tlpm_match(list, (uint8_t[]){ 0xff, 0xff }, 16));
	assert(t1 == tlpm_match(list, (uint8_t[]){ 0xff, 0xff }, 15));
	assert(!tlpm_match(list, (uint8_t[]){ 0x7f, 0xff }, 16));

	list = tlpm_delete(list, (uint8_t[]){ 0xff, 0xff }, 16);
	assert(t1 == tlpm_match(list, (uint8_t[]){ 0xff }, 8));
	assert(t1 == tlpm_match(list, (uint8_t[]){ 0xff, 0xff }, 16));

	list = tlpm_delete(list, (uint8_t[]){ 0xff }, 8);
	assert(!tlpm_match(list, (uint8_t[]){ 0xff }, 8));

	tlpm_clear(list);
}

static void test_lpm_order(void)
{
	struct tlpm_node *t1, *t2, *l1 = NULL, *l2 = NULL;
	size_t i, j;

	/* Verify the tlpm implementation works correctly regardless of the
	 * order of entries. Insert a random set of entries into @l1, and copy
	 * the same data in reverse order into @l2. Then verify a lookup of
	 * random keys will yield the same result in both sets.
	 */

	for (i = 0; i < (1 << 12); ++i)
		l1 = tlpm_add(l1, (uint8_t[]){
					rand() % 0xff,
					rand() % 0xff,
				}, rand() % 16 + 1);

	for (t1 = l1; t1; t1 = t1->next)
		l2 = tlpm_add(l2, t1->key, t1->n_bits);

	for (i = 0; i < (1 << 8); ++i) {
		uint8_t key[] = { rand() % 0xff, rand() % 0xff };

		t1 = tlpm_match(l1, key, 16);
		t2 = tlpm_match(l2, key, 16);

		assert(!t1 == !t2);
		if (t1) {
			assert(t1->n_bits == t2->n_bits);
			for (j = 0; j < t1->n_bits; ++j)
				assert((t1->key[j / 8] & (1 << (7 - j % 8))) ==
				       (t2->key[j / 8] & (1 << (7 - j % 8))));
		}
	}

	tlpm_clear(l1);
	tlpm_clear(l2);
}

static void test_lpm_map(int keysize)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts, .map_flags = BPF_F_NO_PREALLOC);
	volatile size_t n_matches, n_matches_after_delete;
	size_t i, j, n_nodes, n_lookups;
	struct tlpm_node *t, *list = NULL;
	struct bpf_lpm_trie_key *key;
	uint8_t *data, *value;
	int r, map;

	/* Compare behavior of tlpm vs. bpf-lpm. Create a randomized set of
	 * prefixes and insert it into both tlpm and bpf-lpm. Then run some
	 * randomized lookups and verify both maps return the same result.
	 */

	n_matches = 0;
	n_matches_after_delete = 0;
	n_nodes = 1 << 8;
	n_lookups = 1 << 16;

	data = alloca(keysize);
	memset(data, 0, keysize);

	value = alloca(keysize + 1);
	memset(value, 0, keysize + 1);

	key = alloca(sizeof(*key) + keysize);
	memset(key, 0, sizeof(*key) + keysize);

	map = bpf_map_create(BPF_MAP_TYPE_LPM_TRIE, NULL,
			     sizeof(*key) + keysize,
			     keysize + 1,
			     4096,
			     &opts);
	assert(map >= 0);

	for (i = 0; i < n_nodes; ++i) {
		for (j = 0; j < keysize; ++j)
			value[j] = rand() & 0xff;
		value[keysize] = rand() % (8 * keysize + 1);

		list = tlpm_add(list, value, value[keysize]);

		key->prefixlen = value[keysize];
		memcpy(key->data, value, keysize);
		r = bpf_map_update_elem(map, key, value, 0);
		assert(!r);
	}

	for (i = 0; i < n_lookups; ++i) {
		for (j = 0; j < keysize; ++j)
			data[j] = rand() & 0xff;

		t = tlpm_match(list, data, 8 * keysize);

		key->prefixlen = 8 * keysize;
		memcpy(key->data, data, keysize);
		r = bpf_map_lookup_elem(map, key, value);
		assert(!r || errno == ENOENT);
		assert(!t == !!r);

		if (t) {
			++n_matches;
			assert(t->n_bits == value[keysize]);
			for (j = 0; j < t->n_bits; ++j)
				assert((t->key[j / 8] & (1 << (7 - j % 8))) ==
				       (value[j / 8] & (1 << (7 - j % 8))));
		}
	}

	/* Remove the first half of the elements in the tlpm and the
	 * corresponding nodes from the bpf-lpm.  Then run the same
	 * large number of random lookups in both and make sure they match.
	 * Note: we need to count the number of nodes actually inserted
	 * since there may have been duplicates.
	 */
	for (i = 0, t = list; t; i++, t = t->next)
		;
	for (j = 0; j < i / 2; ++j) {
		key->prefixlen = list->n_bits;
		memcpy(key->data, list->key, keysize);
		r = bpf_map_delete_elem(map, key);
		assert(!r);
		list = tlpm_delete(list, list->key, list->n_bits);
		assert(list);
	}
	for (i = 0; i < n_lookups; ++i) {
		for (j = 0; j < keysize; ++j)
			data[j] = rand() & 0xff;

		t = tlpm_match(list, data, 8 * keysize);

		key->prefixlen = 8 * keysize;
		memcpy(key->data, data, keysize);
		r = bpf_map_lookup_elem(map, key, value);
		assert(!r || errno == ENOENT);
		assert(!t == !!r);

		if (t) {
			++n_matches_after_delete;
			assert(t->n_bits == value[keysize]);
			for (j = 0; j < t->n_bits; ++j)
				assert((t->key[j / 8] & (1 << (7 - j % 8))) ==
				       (value[j / 8] & (1 << (7 - j % 8))));
		}
	}

	close(map);
	tlpm_clear(list);

	/* With 255 random nodes in the map, we are pretty likely to match
	 * something on every lookup. For statistics, use this:
	 *
	 *     printf("          nodes: %zu\n"
	 *            "        lookups: %zu\n"
	 *            "        matches: %zu\n"
	 *            "matches(delete): %zu\n",
	 *            n_nodes, n_lookups, n_matches, n_matches_after_delete);
	 */
}

/* Test the implementation with some 'real world' examples */

static void test_lpm_ipaddr(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts, .map_flags = BPF_F_NO_PREALLOC);
	struct bpf_lpm_trie_key *key_ipv4;
	struct bpf_lpm_trie_key *key_ipv6;
	size_t key_size_ipv4;
	size_t key_size_ipv6;
	int map_fd_ipv4;
	int map_fd_ipv6;
	__u64 value;

	key_size_ipv4 = sizeof(*key_ipv4) + sizeof(__u32);
	key_size_ipv6 = sizeof(*key_ipv6) + sizeof(__u32) * 4;
	key_ipv4 = alloca(key_size_ipv4);
	key_ipv6 = alloca(key_size_ipv6);

	map_fd_ipv4 = bpf_map_create(BPF_MAP_TYPE_LPM_TRIE, NULL,
				     key_size_ipv4, sizeof(value),
				     100, &opts);
	assert(map_fd_ipv4 >= 0);

	map_fd_ipv6 = bpf_map_create(BPF_MAP_TYPE_LPM_TRIE, NULL,
				     key_size_ipv6, sizeof(value),
				     100, &opts);
	assert(map_fd_ipv6 >= 0);

	/* Fill data some IPv4 and IPv6 address ranges */
	value = 1;
	key_ipv4->prefixlen = 16;
	inet_pton(AF_INET, "192.168.0.0", key_ipv4->data);
	assert(bpf_map_update_elem(map_fd_ipv4, key_ipv4, &value, 0) == 0);

	value = 2;
	key_ipv4->prefixlen = 24;
	inet_pton(AF_INET, "192.168.0.0", key_ipv4->data);
	assert(bpf_map_update_elem(map_fd_ipv4, key_ipv4, &value, 0) == 0);

	value = 3;
	key_ipv4->prefixlen = 24;
	inet_pton(AF_INET, "192.168.128.0", key_ipv4->data);
	assert(bpf_map_update_elem(map_fd_ipv4, key_ipv4, &value, 0) == 0);

	value = 5;
	key_ipv4->prefixlen = 24;
	inet_pton(AF_INET, "192.168.1.0", key_ipv4->data);
	assert(bpf_map_update_elem(map_fd_ipv4, key_ipv4, &value, 0) == 0);

	value = 4;
	key_ipv4->prefixlen = 23;
	inet_pton(AF_INET, "192.168.0.0", key_ipv4->data);
	assert(bpf_map_update_elem(map_fd_ipv4, key_ipv4, &value, 0) == 0);

	value = 0xdeadbeef;
	key_ipv6->prefixlen = 64;
	inet_pton(AF_INET6, "2a00:1450:4001:814::200e", key_ipv6->data);
	assert(bpf_map_update_elem(map_fd_ipv6, key_ipv6, &value, 0) == 0);

	/* Set tprefixlen to maximum for lookups */
	key_ipv4->prefixlen = 32;
	key_ipv6->prefixlen = 128;

	/* Test some lookups that should come back with a value */
	inet_pton(AF_INET, "192.168.128.23", key_ipv4->data);
	assert(bpf_map_lookup_elem(map_fd_ipv4, key_ipv4, &value) == 0);
	assert(value == 3);

	inet_pton(AF_INET, "192.168.0.1", key_ipv4->data);
	assert(bpf_map_lookup_elem(map_fd_ipv4, key_ipv4, &value) == 0);
	assert(value == 2);

	inet_pton(AF_INET6, "2a00:1450:4001:814::", key_ipv6->data);
	assert(bpf_map_lookup_elem(map_fd_ipv6, key_ipv6, &value) == 0);
	assert(value == 0xdeadbeef);

	inet_pton(AF_INET6, "2a00:1450:4001:814::1", key_ipv6->data);
	assert(bpf_map_lookup_elem(map_fd_ipv6, key_ipv6, &value) == 0);
	assert(value == 0xdeadbeef);

	/* Test some lookups that should not match any entry */
	inet_pton(AF_INET, "10.0.0.1", key_ipv4->data);
	assert(bpf_map_lookup_elem(map_fd_ipv4, key_ipv4, &value) == -1 &&
	       errno == ENOENT);

	inet_pton(AF_INET, "11.11.11.11", key_ipv4->data);
	assert(bpf_map_lookup_elem(map_fd_ipv4, key_ipv4, &value) == -1 &&
	       errno == ENOENT);

	inet_pton(AF_INET6, "2a00:ffff::", key_ipv6->data);
	assert(bpf_map_lookup_elem(map_fd_ipv6, key_ipv6, &value) == -1 &&
	       errno == ENOENT);

	close(map_fd_ipv4);
	close(map_fd_ipv6);
}

static void test_lpm_delete(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts, .map_flags = BPF_F_NO_PREALLOC);
	struct bpf_lpm_trie_key *key;
	size_t key_size;
	int map_fd;
	__u64 value;

	key_size = sizeof(*key) + sizeof(__u32);
	key = alloca(key_size);

	map_fd = bpf_map_create(BPF_MAP_TYPE_LPM_TRIE, NULL,
				key_size, sizeof(value),
				100, &opts);
	assert(map_fd >= 0);

	/* Add nodes:
	 * 192.168.0.0/16   (1)
	 * 192.168.0.0/24   (2)
	 * 192.168.128.0/24 (3)
	 * 192.168.1.0/24   (4)
	 *
	 *         (1)
	 *        /   \
         *     (IM)    (3)
	 *    /   \
         *   (2)  (4)
	 */
	value = 1;
	key->prefixlen = 16;
	inet_pton(AF_INET, "192.168.0.0", key->data);
	assert(bpf_map_update_elem(map_fd, key, &value, 0) == 0);

	value = 2;
	key->prefixlen = 24;
	inet_pton(AF_INET, "192.168.0.0", key->data);
	assert(bpf_map_update_elem(map_fd, key, &value, 0) == 0);

	value = 3;
	key->prefixlen = 24;
	inet_pton(AF_INET, "192.168.128.0", key->data);
	assert(bpf_map_update_elem(map_fd, key, &value, 0) == 0);

	value = 4;
	key->prefixlen = 24;
	inet_pton(AF_INET, "192.168.1.0", key->data);
	assert(bpf_map_update_elem(map_fd, key, &value, 0) == 0);

	/* remove non-existent node */
	key->prefixlen = 32;
	inet_pton(AF_INET, "10.0.0.1", key->data);
	assert(bpf_map_lookup_elem(map_fd, key, &value) == -1 &&
		errno == ENOENT);

	key->prefixlen = 30; // unused prefix so far
	inet_pton(AF_INET, "192.255.0.0", key->data);
	assert(bpf_map_delete_elem(map_fd, key) == -1 &&
		errno == ENOENT);

	key->prefixlen = 16; // same prefix as the root node
	inet_pton(AF_INET, "192.255.0.0", key->data);
	assert(bpf_map_delete_elem(map_fd, key) == -1 &&
		errno == ENOENT);

	/* assert initial lookup */
	key->prefixlen = 32;
	inet_pton(AF_INET, "192.168.0.1", key->data);
	assert(bpf_map_lookup_elem(map_fd, key, &value) == 0);
	assert(value == 2);

	/* remove leaf node */
	key->prefixlen = 24;
	inet_pton(AF_INET, "192.168.0.0", key->data);
	assert(bpf_map_delete_elem(map_fd, key) == 0);

	key->prefixlen = 32;
	inet_pton(AF_INET, "192.168.0.1", key->data);
	assert(bpf_map_lookup_elem(map_fd, key, &value) == 0);
	assert(value == 1);

	/* remove leaf (and intermediary) node */
	key->prefixlen = 24;
	inet_pton(AF_INET, "192.168.1.0", key->data);
	assert(bpf_map_delete_elem(map_fd, key) == 0);

	key->prefixlen = 32;
	inet_pton(AF_INET, "192.168.1.1", key->data);
	assert(bpf_map_lookup_elem(map_fd, key, &value) == 0);
	assert(value == 1);

	/* remove root node */
	key->prefixlen = 16;
	inet_pton(AF_INET, "192.168.0.0", key->data);
	assert(bpf_map_delete_elem(map_fd, key) == 0);

	key->prefixlen = 32;
	inet_pton(AF_INET, "192.168.128.1", key->data);
	assert(bpf_map_lookup_elem(map_fd, key, &value) == 0);
	assert(value == 3);

	/* remove last node */
	key->prefixlen = 24;
	inet_pton(AF_INET, "192.168.128.0", key->data);
	assert(bpf_map_delete_elem(map_fd, key) == 0);

	key->prefixlen = 32;
	inet_pton(AF_INET, "192.168.128.1", key->data);
	assert(bpf_map_lookup_elem(map_fd, key, &value) == -1 &&
		errno == ENOENT);

	close(map_fd);
}

static void test_lpm_get_next_key(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts, .map_flags = BPF_F_NO_PREALLOC);
	struct bpf_lpm_trie_key *key_p, *next_key_p;
	size_t key_size;
	__u32 value = 0;
	int map_fd;

	key_size = sizeof(*key_p) + sizeof(__u32);
	key_p = alloca(key_size);
	next_key_p = alloca(key_size);

	map_fd = bpf_map_create(BPF_MAP_TYPE_LPM_TRIE, NULL, key_size, sizeof(value), 100, &opts);
	assert(map_fd >= 0);

	/* empty tree. get_next_key should return ENOENT */
	assert(bpf_map_get_next_key(map_fd, NULL, key_p) == -1 &&
	       errno == ENOENT);

	/* get and verify the first key, get the second one should fail. */
	key_p->prefixlen = 16;
	inet_pton(AF_INET, "192.168.0.0", key_p->data);
	assert(bpf_map_update_elem(map_fd, key_p, &value, 0) == 0);

	memset(key_p, 0, key_size);
	assert(bpf_map_get_next_key(map_fd, NULL, key_p) == 0);
	assert(key_p->prefixlen == 16 && key_p->data[0] == 192 &&
	       key_p->data[1] == 168);

	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == -1 &&
	       errno == ENOENT);

	/* no exact matching key should get the first one in post order. */
	key_p->prefixlen = 8;
	assert(bpf_map_get_next_key(map_fd, NULL, key_p) == 0);
	assert(key_p->prefixlen == 16 && key_p->data[0] == 192 &&
	       key_p->data[1] == 168);

	/* add one more element (total two) */
	key_p->prefixlen = 24;
	inet_pton(AF_INET, "192.168.128.0", key_p->data);
	assert(bpf_map_update_elem(map_fd, key_p, &value, 0) == 0);

	memset(key_p, 0, key_size);
	assert(bpf_map_get_next_key(map_fd, NULL, key_p) == 0);
	assert(key_p->prefixlen == 24 && key_p->data[0] == 192 &&
	       key_p->data[1] == 168 && key_p->data[2] == 128);

	memset(next_key_p, 0, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == 0);
	assert(next_key_p->prefixlen == 16 && next_key_p->data[0] == 192 &&
	       next_key_p->data[1] == 168);

	memcpy(key_p, next_key_p, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == -1 &&
	       errno == ENOENT);

	/* Add one more element (total three) */
	key_p->prefixlen = 24;
	inet_pton(AF_INET, "192.168.0.0", key_p->data);
	assert(bpf_map_update_elem(map_fd, key_p, &value, 0) == 0);

	memset(key_p, 0, key_size);
	assert(bpf_map_get_next_key(map_fd, NULL, key_p) == 0);
	assert(key_p->prefixlen == 24 && key_p->data[0] == 192 &&
	       key_p->data[1] == 168 && key_p->data[2] == 0);

	memset(next_key_p, 0, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == 0);
	assert(next_key_p->prefixlen == 24 && next_key_p->data[0] == 192 &&
	       next_key_p->data[1] == 168 && next_key_p->data[2] == 128);

	memcpy(key_p, next_key_p, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == 0);
	assert(next_key_p->prefixlen == 16 && next_key_p->data[0] == 192 &&
	       next_key_p->data[1] == 168);

	memcpy(key_p, next_key_p, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == -1 &&
	       errno == ENOENT);

	/* Add one more element (total four) */
	key_p->prefixlen = 24;
	inet_pton(AF_INET, "192.168.1.0", key_p->data);
	assert(bpf_map_update_elem(map_fd, key_p, &value, 0) == 0);

	memset(key_p, 0, key_size);
	assert(bpf_map_get_next_key(map_fd, NULL, key_p) == 0);
	assert(key_p->prefixlen == 24 && key_p->data[0] == 192 &&
	       key_p->data[1] == 168 && key_p->data[2] == 0);

	memset(next_key_p, 0, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == 0);
	assert(next_key_p->prefixlen == 24 && next_key_p->data[0] == 192 &&
	       next_key_p->data[1] == 168 && next_key_p->data[2] == 1);

	memcpy(key_p, next_key_p, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == 0);
	assert(next_key_p->prefixlen == 24 && next_key_p->data[0] == 192 &&
	       next_key_p->data[1] == 168 && next_key_p->data[2] == 128);

	memcpy(key_p, next_key_p, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == 0);
	assert(next_key_p->prefixlen == 16 && next_key_p->data[0] == 192 &&
	       next_key_p->data[1] == 168);

	memcpy(key_p, next_key_p, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == -1 &&
	       errno == ENOENT);

	/* Add one more element (total five) */
	key_p->prefixlen = 28;
	inet_pton(AF_INET, "192.168.1.128", key_p->data);
	assert(bpf_map_update_elem(map_fd, key_p, &value, 0) == 0);

	memset(key_p, 0, key_size);
	assert(bpf_map_get_next_key(map_fd, NULL, key_p) == 0);
	assert(key_p->prefixlen == 24 && key_p->data[0] == 192 &&
	       key_p->data[1] == 168 && key_p->data[2] == 0);

	memset(next_key_p, 0, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == 0);
	assert(next_key_p->prefixlen == 28 && next_key_p->data[0] == 192 &&
	       next_key_p->data[1] == 168 && next_key_p->data[2] == 1 &&
	       next_key_p->data[3] == 128);

	memcpy(key_p, next_key_p, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == 0);
	assert(next_key_p->prefixlen == 24 && next_key_p->data[0] == 192 &&
	       next_key_p->data[1] == 168 && next_key_p->data[2] == 1);

	memcpy(key_p, next_key_p, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == 0);
	assert(next_key_p->prefixlen == 24 && next_key_p->data[0] == 192 &&
	       next_key_p->data[1] == 168 && next_key_p->data[2] == 128);

	memcpy(key_p, next_key_p, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == 0);
	assert(next_key_p->prefixlen == 16 && next_key_p->data[0] == 192 &&
	       next_key_p->data[1] == 168);

	memcpy(key_p, next_key_p, key_size);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == -1 &&
	       errno == ENOENT);

	/* no exact matching key should return the first one in post order */
	key_p->prefixlen = 22;
	inet_pton(AF_INET, "192.168.1.0", key_p->data);
	assert(bpf_map_get_next_key(map_fd, key_p, next_key_p) == 0);
	assert(next_key_p->prefixlen == 24 && next_key_p->data[0] == 192 &&
	       next_key_p->data[1] == 168 && next_key_p->data[2] == 0);

	close(map_fd);
}

#define MAX_TEST_KEYS	4
struct lpm_mt_test_info {
	int cmd; /* 0: update, 1: delete, 2: lookup, 3: get_next_key */
	int iter;
	int map_fd;
	struct {
		__u32 prefixlen;
		__u32 data;
	} key[MAX_TEST_KEYS];
};

static void *lpm_test_command(void *arg)
{
	int i, j, ret, iter, key_size;
	struct lpm_mt_test_info *info = arg;
	struct bpf_lpm_trie_key *key_p;

	key_size = sizeof(struct bpf_lpm_trie_key) + sizeof(__u32);
	key_p = alloca(key_size);
	for (iter = 0; iter < info->iter; iter++)
		for (i = 0; i < MAX_TEST_KEYS; i++) {
			/* first half of iterations in forward order,
			 * and second half in backward order.
			 */
			j = (iter < (info->iter / 2)) ? i : MAX_TEST_KEYS - i - 1;
			key_p->prefixlen = info->key[j].prefixlen;
			memcpy(key_p->data, &info->key[j].data, sizeof(__u32));
			if (info->cmd == 0) {
				__u32 value = j;
				/* update must succeed */
				assert(bpf_map_update_elem(info->map_fd, key_p, &value, 0) == 0);
			} else if (info->cmd == 1) {
				ret = bpf_map_delete_elem(info->map_fd, key_p);
				assert(ret == 0 || errno == ENOENT);
			} else if (info->cmd == 2) {
				__u32 value;
				ret = bpf_map_lookup_elem(info->map_fd, key_p, &value);
				assert(ret == 0 || errno == ENOENT);
			} else {
				struct bpf_lpm_trie_key *next_key_p = alloca(key_size);
				ret = bpf_map_get_next_key(info->map_fd, key_p, next_key_p);
				assert(ret == 0 || errno == ENOENT || errno == ENOMEM);
			}
		}

	// Pass successful exit info back to the main thread
	pthread_exit((void *)info);
}

static void setup_lpm_mt_test_info(struct lpm_mt_test_info *info, int map_fd)
{
	info->iter = 2000;
	info->map_fd = map_fd;
	info->key[0].prefixlen = 16;
	inet_pton(AF_INET, "192.168.0.0", &info->key[0].data);
	info->key[1].prefixlen = 24;
	inet_pton(AF_INET, "192.168.0.0", &info->key[1].data);
	info->key[2].prefixlen = 24;
	inet_pton(AF_INET, "192.168.128.0", &info->key[2].data);
	info->key[3].prefixlen = 24;
	inet_pton(AF_INET, "192.168.1.0", &info->key[3].data);
}

static void test_lpm_multi_thread(void)
{
	LIBBPF_OPTS(bpf_map_create_opts, opts, .map_flags = BPF_F_NO_PREALLOC);
	struct lpm_mt_test_info info[4];
	size_t key_size, value_size;
	pthread_t thread_id[4];
	int i, map_fd;
	void *ret;

	/* create a trie */
	value_size = sizeof(__u32);
	key_size = sizeof(struct bpf_lpm_trie_key) + value_size;
	map_fd = bpf_map_create(BPF_MAP_TYPE_LPM_TRIE, NULL, key_size, value_size, 100, &opts);

	/* create 4 threads to test update, delete, lookup and get_next_key */
	setup_lpm_mt_test_info(&info[0], map_fd);
	for (i = 0; i < 4; i++) {
		if (i != 0)
			memcpy(&info[i], &info[0], sizeof(info[i]));
		info[i].cmd = i;
		assert(pthread_create(&thread_id[i], NULL, &lpm_test_command, &info[i]) == 0);
	}

	for (i = 0; i < 4; i++)
		assert(pthread_join(thread_id[i], &ret) == 0 && ret == (void *)&info[i]);

	close(map_fd);
}

int main(void)
{
	int i;

	/* we want predictable, pseudo random tests */
	srand(0xf00ba1);

	test_lpm_basic();
	test_lpm_order();

	/* Test with 8, 16, 24, 32, ... 128 bit prefix length */
	for (i = 1; i <= 16; ++i)
		test_lpm_map(i);

	test_lpm_ipaddr();
	test_lpm_delete();
	test_lpm_get_next_key();
	test_lpm_multi_thread();

	printf("test_lpm: OK\n");
	return 0;
}
