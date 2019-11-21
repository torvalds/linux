/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __BPF_CORE_READ_H__
#define __BPF_CORE_READ_H__

/*
 * enum bpf_field_info_kind is passed as a second argument into
 * __builtin_preserve_field_info() built-in to get a specific aspect of
 * a field, captured as a first argument. __builtin_preserve_field_info(field,
 * info_kind) returns __u32 integer and produces BTF field relocation, which
 * is understood and processed by libbpf during BPF object loading. See
 * selftests/bpf for examples.
 */
enum bpf_field_info_kind {
	BPF_FIELD_BYTE_OFFSET = 0,	/* field byte offset */
	BPF_FIELD_BYTE_SIZE = 1,
	BPF_FIELD_EXISTS = 2,		/* field existence in target kernel */
	BPF_FIELD_SIGNED = 3,
	BPF_FIELD_LSHIFT_U64 = 4,
	BPF_FIELD_RSHIFT_U64 = 5,
};

#define __CORE_RELO(src, field, info)					      \
	__builtin_preserve_field_info((src)->field, BPF_FIELD_##info)

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define __CORE_BITFIELD_PROBE_READ(dst, src, fld)			      \
	bpf_probe_read((void *)dst,					      \
		       __CORE_RELO(src, fld, BYTE_SIZE),		      \
		       (const void *)src + __CORE_RELO(src, fld, BYTE_OFFSET))
#else
/* semantics of LSHIFT_64 assumes loading values into low-ordered bytes, so
 * for big-endian we need to adjust destination pointer accordingly, based on
 * field byte size
 */
#define __CORE_BITFIELD_PROBE_READ(dst, src, fld)			      \
	bpf_probe_read((void *)dst + (8 - __CORE_RELO(src, fld, BYTE_SIZE)),  \
		       __CORE_RELO(src, fld, BYTE_SIZE),		      \
		       (const void *)src + __CORE_RELO(src, fld, BYTE_OFFSET))
#endif

/*
 * Extract bitfield, identified by s->field, and return its value as u64.
 * All this is done in relocatable manner, so bitfield changes such as
 * signedness, bit size, offset changes, this will be handled automatically.
 * This version of macro is using bpf_probe_read() to read underlying integer
 * storage. Macro functions as an expression and its return type is
 * bpf_probe_read()'s return value: 0, on success, <0 on error.
 */
#define BPF_CORE_READ_BITFIELD_PROBED(s, field) ({			      \
	unsigned long long val = 0;					      \
									      \
	__CORE_BITFIELD_PROBE_READ(&val, s, field);			      \
	val <<= __CORE_RELO(s, field, LSHIFT_U64);			      \
	if (__CORE_RELO(s, field, SIGNED))				      \
		val = ((long long)val) >> __CORE_RELO(s, field, RSHIFT_U64);  \
	else								      \
		val = val >> __CORE_RELO(s, field, RSHIFT_U64);		      \
	val;								      \
})

/*
 * Extract bitfield, identified by s->field, and return its value as u64.
 * This version of macro is using direct memory reads and should be used from
 * BPF program types that support such functionality (e.g., typed raw
 * tracepoints).
 */
#define BPF_CORE_READ_BITFIELD(s, field) ({				      \
	const void *p = (const void *)s + __CORE_RELO(s, field, BYTE_OFFSET); \
	unsigned long long val;						      \
									      \
	switch (__CORE_RELO(s, field, BYTE_SIZE)) {			      \
	case 1: val = *(const unsigned char *)p;			      \
	case 2: val = *(const unsigned short *)p;			      \
	case 4: val = *(const unsigned int *)p;				      \
	case 8: val = *(const unsigned long long *)p;			      \
	}								      \
	val <<= __CORE_RELO(s, field, LSHIFT_U64);			      \
	if (__CORE_RELO(s, field, SIGNED))				      \
		val = ((long long)val) >> __CORE_RELO(s, field, RSHIFT_U64);  \
	else								      \
		val = val >> __CORE_RELO(s, field, RSHIFT_U64);		      \
	val;								      \
})

/*
 * Convenience macro to check that field actually exists in target kernel's.
 * Returns:
 *    1, if matching field is present in target kernel;
 *    0, if no matching field found.
 */
#define bpf_core_field_exists(field)					    \
	__builtin_preserve_field_info(field, BPF_FIELD_EXISTS)

/*
 * Convenience macro to get byte size of a field. Works for integers,
 * struct/unions, pointers, arrays, and enums.
 */
#define bpf_core_field_size(field)					    \
	__builtin_preserve_field_info(field, BPF_FIELD_BYTE_SIZE)

/*
 * bpf_core_read() abstracts away bpf_probe_read() call and captures offset
 * relocation for source address using __builtin_preserve_access_index()
 * built-in, provided by Clang.
 *
 * __builtin_preserve_access_index() takes as an argument an expression of
 * taking an address of a field within struct/union. It makes compiler emit
 * a relocation, which records BTF type ID describing root struct/union and an
 * accessor string which describes exact embedded field that was used to take
 * an address. See detailed description of this relocation format and
 * semantics in comments to struct bpf_field_reloc in libbpf_internal.h.
 *
 * This relocation allows libbpf to adjust BPF instruction to use correct
 * actual field offset, based on target kernel BTF type that matches original
 * (local) BTF, used to record relocation.
 */
#define bpf_core_read(dst, sz, src)					    \
	bpf_probe_read(dst, sz,						    \
		       (const void *)__builtin_preserve_access_index(src))

/*
 * bpf_core_read_str() is a thin wrapper around bpf_probe_read_str()
 * additionally emitting BPF CO-RE field relocation for specified source
 * argument.
 */
#define bpf_core_read_str(dst, sz, src)					    \
	bpf_probe_read_str(dst, sz,					    \
			   (const void *)__builtin_preserve_access_index(src))

#define ___concat(a, b) a ## b
#define ___apply(fn, n) ___concat(fn, n)
#define ___nth(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, __11, N, ...) N

/*
 * return number of provided arguments; used for switch-based variadic macro
 * definitions (see ___last, ___arrow, etc below)
 */
#define ___narg(...) ___nth(_, ##__VA_ARGS__, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
/*
 * return 0 if no arguments are passed, N - otherwise; used for
 * recursively-defined macros to specify termination (0) case, and generic
 * (N) case (e.g., ___read_ptrs, ___core_read)
 */
#define ___empty(...) ___nth(_, ##__VA_ARGS__, N, N, N, N, N, N, N, N, N, N, 0)

#define ___last1(x) x
#define ___last2(a, x) x
#define ___last3(a, b, x) x
#define ___last4(a, b, c, x) x
#define ___last5(a, b, c, d, x) x
#define ___last6(a, b, c, d, e, x) x
#define ___last7(a, b, c, d, e, f, x) x
#define ___last8(a, b, c, d, e, f, g, x) x
#define ___last9(a, b, c, d, e, f, g, h, x) x
#define ___last10(a, b, c, d, e, f, g, h, i, x) x
#define ___last(...) ___apply(___last, ___narg(__VA_ARGS__))(__VA_ARGS__)

#define ___nolast2(a, _) a
#define ___nolast3(a, b, _) a, b
#define ___nolast4(a, b, c, _) a, b, c
#define ___nolast5(a, b, c, d, _) a, b, c, d
#define ___nolast6(a, b, c, d, e, _) a, b, c, d, e
#define ___nolast7(a, b, c, d, e, f, _) a, b, c, d, e, f
#define ___nolast8(a, b, c, d, e, f, g, _) a, b, c, d, e, f, g
#define ___nolast9(a, b, c, d, e, f, g, h, _) a, b, c, d, e, f, g, h
#define ___nolast10(a, b, c, d, e, f, g, h, i, _) a, b, c, d, e, f, g, h, i
#define ___nolast(...) ___apply(___nolast, ___narg(__VA_ARGS__))(__VA_ARGS__)

#define ___arrow1(a) a
#define ___arrow2(a, b) a->b
#define ___arrow3(a, b, c) a->b->c
#define ___arrow4(a, b, c, d) a->b->c->d
#define ___arrow5(a, b, c, d, e) a->b->c->d->e
#define ___arrow6(a, b, c, d, e, f) a->b->c->d->e->f
#define ___arrow7(a, b, c, d, e, f, g) a->b->c->d->e->f->g
#define ___arrow8(a, b, c, d, e, f, g, h) a->b->c->d->e->f->g->h
#define ___arrow9(a, b, c, d, e, f, g, h, i) a->b->c->d->e->f->g->h->i
#define ___arrow10(a, b, c, d, e, f, g, h, i, j) a->b->c->d->e->f->g->h->i->j
#define ___arrow(...) ___apply(___arrow, ___narg(__VA_ARGS__))(__VA_ARGS__)

#define ___type(...) typeof(___arrow(__VA_ARGS__))

#define ___read(read_fn, dst, src_type, src, accessor)			    \
	read_fn((void *)(dst), sizeof(*(dst)), &((src_type)(src))->accessor)

/* "recursively" read a sequence of inner pointers using local __t var */
#define ___rd_first(src, a) ___read(bpf_core_read, &__t, ___type(src), src, a);
#define ___rd_last(...)							    \
	___read(bpf_core_read, &__t,					    \
		___type(___nolast(__VA_ARGS__)), __t, ___last(__VA_ARGS__));
#define ___rd_p1(...) const void *__t; ___rd_first(__VA_ARGS__)
#define ___rd_p2(...) ___rd_p1(___nolast(__VA_ARGS__)) ___rd_last(__VA_ARGS__)
#define ___rd_p3(...) ___rd_p2(___nolast(__VA_ARGS__)) ___rd_last(__VA_ARGS__)
#define ___rd_p4(...) ___rd_p3(___nolast(__VA_ARGS__)) ___rd_last(__VA_ARGS__)
#define ___rd_p5(...) ___rd_p4(___nolast(__VA_ARGS__)) ___rd_last(__VA_ARGS__)
#define ___rd_p6(...) ___rd_p5(___nolast(__VA_ARGS__)) ___rd_last(__VA_ARGS__)
#define ___rd_p7(...) ___rd_p6(___nolast(__VA_ARGS__)) ___rd_last(__VA_ARGS__)
#define ___rd_p8(...) ___rd_p7(___nolast(__VA_ARGS__)) ___rd_last(__VA_ARGS__)
#define ___rd_p9(...) ___rd_p8(___nolast(__VA_ARGS__)) ___rd_last(__VA_ARGS__)
#define ___read_ptrs(src, ...)						    \
	___apply(___rd_p, ___narg(__VA_ARGS__))(src, __VA_ARGS__)

#define ___core_read0(fn, dst, src, a)					    \
	___read(fn, dst, ___type(src), src, a);
#define ___core_readN(fn, dst, src, ...)				    \
	___read_ptrs(src, ___nolast(__VA_ARGS__))			    \
	___read(fn, dst, ___type(src, ___nolast(__VA_ARGS__)), __t,	    \
		___last(__VA_ARGS__));
#define ___core_read(fn, dst, src, a, ...)				    \
	___apply(___core_read, ___empty(__VA_ARGS__))(fn, dst,		    \
						      src, a, ##__VA_ARGS__)

/*
 * BPF_CORE_READ_INTO() is a more performance-conscious variant of
 * BPF_CORE_READ(), in which final field is read into user-provided storage.
 * See BPF_CORE_READ() below for more details on general usage.
 */
#define BPF_CORE_READ_INTO(dst, src, a, ...)				    \
	({								    \
		___core_read(bpf_core_read, dst, src, a, ##__VA_ARGS__)	    \
	})

/*
 * BPF_CORE_READ_STR_INTO() does same "pointer chasing" as
 * BPF_CORE_READ() for intermediate pointers, but then executes (and returns
 * corresponding error code) bpf_core_read_str() for final string read.
 */
#define BPF_CORE_READ_STR_INTO(dst, src, a, ...)			    \
	({								    \
		___core_read(bpf_core_read_str, dst, src, a, ##__VA_ARGS__) \
	})

/*
 * BPF_CORE_READ() is used to simplify BPF CO-RE relocatable read, especially
 * when there are few pointer chasing steps.
 * E.g., what in non-BPF world (or in BPF w/ BCC) would be something like:
 *	int x = s->a.b.c->d.e->f->g;
 * can be succinctly achieved using BPF_CORE_READ as:
 *	int x = BPF_CORE_READ(s, a.b.c, d.e, f, g);
 *
 * BPF_CORE_READ will decompose above statement into 4 bpf_core_read (BPF
 * CO-RE relocatable bpf_probe_read() wrapper) calls, logically equivalent to:
 * 1. const void *__t = s->a.b.c;
 * 2. __t = __t->d.e;
 * 3. __t = __t->f;
 * 4. return __t->g;
 *
 * Equivalence is logical, because there is a heavy type casting/preservation
 * involved, as well as all the reads are happening through bpf_probe_read()
 * calls using __builtin_preserve_access_index() to emit CO-RE relocations.
 *
 * N.B. Only up to 9 "field accessors" are supported, which should be more
 * than enough for any practical purpose.
 */
#define BPF_CORE_READ(src, a, ...)					    \
	({								    \
		___type(src, a, ##__VA_ARGS__) __r;			    \
		BPF_CORE_READ_INTO(&__r, src, a, ##__VA_ARGS__);	    \
		__r;							    \
	})

#endif

