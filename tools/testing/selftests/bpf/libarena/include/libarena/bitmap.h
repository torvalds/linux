#pragma once

#define BITS_PER_BYTE		8
#define BYTES_TO_BITS(nb)	((nb) * BITS_PER_BYTE)

#define BITS_PER_LONG_LONG	(sizeof(long long) * BITS_PER_BYTE)
#define BITS_TO_LONG_LONGS(nr)	(((nr) + BITS_PER_LONG_LONG - 1) / BITS_PER_LONG_LONG)
#define BIT_MASK(nr)		(1ULL << ((nr) % BITS_PER_LONG_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG_LONG)

struct arena_bitmap {
	u64 bits[0];
};

struct arena_bitmap __arena *bmp_alloc(size_t bits);
void bmp_free(struct arena_bitmap __arena *bmp);

void __bmp_set_bit(u32 bit, struct arena_bitmap __arena *bmp);
void __bmp_clear_bit(u32 bit, struct arena_bitmap __arena *bmp);
void bmp_set_bit(u32 bit, struct arena_bitmap __arena *bmp);
void bmp_clear_bit(u32 bit, struct arena_bitmap __arena *bmp);
bool bmp_test_bit(u32 bit, struct arena_bitmap __arena *bmp);
bool bmp_test_and_clear_bit(u32 bit, struct arena_bitmap __arena *bmp);
bool bmp_test_and_set_bit(u32 bit, struct arena_bitmap __arena *bmp);

void bmp_clear(size_t bits, struct arena_bitmap __arena *bmp);
void bmp_and(size_t bits, struct arena_bitmap __arena *dst, struct arena_bitmap __arena *src1, struct arena_bitmap __arena *src2);
void bmp_or(size_t bits, struct arena_bitmap __arena *dst, struct arena_bitmap __arena *src1, struct arena_bitmap __arena *src2);
bool bmp_empty(size_t bits, struct arena_bitmap __arena *bmp);
void bmp_copy(size_t bits, struct arena_bitmap __arena *dst, struct arena_bitmap __arena *src);

bool bmp_intersects(size_t bits, struct arena_bitmap __arena *arg1, struct arena_bitmap __arena *arg2);
bool bmp_subset(size_t bits, struct arena_bitmap __arena *big, struct arena_bitmap __arena *small);
void bmp_print(size_t bits, struct arena_bitmap __arena *bmp);
