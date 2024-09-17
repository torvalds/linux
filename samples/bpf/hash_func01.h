/* SPDX-License-Identifier: LGPL-2.1
 *
 * Based on Paul Hsieh's (LGPG 2.1) hash function
 * From: http://www.azillionmonkeys.com/qed/hash.html
 */

#define get16bits(d) (*((const __u16 *) (d)))

static __always_inline
__u32 SuperFastHash (const char *data, int len, __u32 initval) {
	__u32 hash = initval;
	__u32 tmp;
	int rem;

	if (len <= 0 || data == NULL) return 0;

	rem = len & 3;
	len >>= 2;

	/* Main loop */
#pragma clang loop unroll(full)
	for (;len > 0; len--) {
		hash  += get16bits (data);
		tmp    = (get16bits (data+2) << 11) ^ hash;
		hash   = (hash << 16) ^ tmp;
		data  += 2*sizeof (__u16);
		hash  += hash >> 11;
	}

	/* Handle end cases */
	switch (rem) {
        case 3: hash += get16bits (data);
                hash ^= hash << 16;
                hash ^= ((signed char)data[sizeof (__u16)]) << 18;
                hash += hash >> 11;
                break;
        case 2: hash += get16bits (data);
                hash ^= hash << 11;
                hash += hash >> 17;
                break;
        case 1: hash += (signed char)*data;
                hash ^= hash << 10;
                hash += hash >> 1;
	}

	/* Force "avalanching" of final 127 bits */
	hash ^= hash << 3;
	hash += hash >> 5;
	hash ^= hash << 4;
	hash += hash >> 17;
	hash ^= hash << 25;
	hash += hash >> 6;

	return hash;
}
