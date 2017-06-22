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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <bpf/bpf.h>
#include "bpf_util.h"

struct tlpm_node {
	struct tlpm_node *next;
	size_t n_bits;
	uint8_t key[];
};

static struct tlpm_node *tlpm_add(struct tlpm_node *list,
				  const uint8_t *key,
				  size_t n_bits)
{
	struct tlpm_node *node;
	size_t n;

	/* add new entry with @key/@n_bits to @list and return new head */

	n = (n_bits + 7) / 8;
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
	size_t i, j, n_matches, n_nodes, n_lookups;
	struct tlpm_node *t, *list = NULL;
	struct bpf_lpm_trie_key *key;
	uint8_t *data, *value;
	int r, map;

	/* Compare behavior of tlpm vs. bpf-lpm. Create a randomized set of
	 * prefixes and insert it into both tlpm and bpf-lpm. Then run some
	 * randomized lookups and verify both maps return the same result.
	 */

	n_matches = 0;
	n_nodes = 1 << 8;
	n_lookups = 1 << 16;

	data = alloca(keysize);
	memset(data, 0, keysize);

	value = alloca(keysize + 1);
	memset(value, 0, keysize + 1);

	key = alloca(sizeof(*key) + keysize);
	memset(key, 0, sizeof(*key) + keysize);

	map = bpf_create_map(BPF_MAP_TYPE_LPM_TRIE,
			     sizeof(*key) + keysize,
			     keysize + 1,
			     4096,
			     BPF_F_NO_PREALLOC);
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

	close(map);
	tlpm_clear(list);

	/* With 255 random nodes in the map, we are pretty likely to match
	 * something on every lookup. For statistics, use this:
	 *
	 *     printf("  nodes: %zu\n"
	 *            "lookups: %zu\n"
	 *            "matches: %zu\n", n_nodes, n_lookups, n_matches);
	 */
}

/* Test the implementation with some 'real world' examples */

static void test_lpm_ipaddr(void)
{
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

	map_fd_ipv4 = bpf_create_map(BPF_MAP_TYPE_LPM_TRIE,
				     key_size_ipv4, sizeof(value),
				     100, BPF_F_NO_PREALLOC);
	assert(map_fd_ipv4 >= 0);

	map_fd_ipv6 = bpf_create_map(BPF_MAP_TYPE_LPM_TRIE,
				     key_size_ipv6, sizeof(value),
				     100, BPF_F_NO_PREALLOC);
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

int main(void)
{
	struct rlimit limit  = { RLIM_INFINITY, RLIM_INFINITY };
	int i, ret;

	/* we want predictable, pseudo random tests */
	srand(0xf00ba1);

	/* allow unlimited locked memory */
	ret = setrlimit(RLIMIT_MEMLOCK, &limit);
	if (ret < 0)
		perror("Unable to lift memlock rlimit");

	test_lpm_basic();
	test_lpm_order();

	/* Test with 8, 16, 24, 32, ... 128 bit prefix length */
	for (i = 1; i <= 16; ++i)
		test_lpm_map(i);

	test_lpm_ipaddr();

	printf("test_lpm: OK\n");
	return 0;
}
