/*	$OpenBSD: subr_kubsan.c,v 1.13 2024/09/06 13:31:59 mbuhl Exp $	*/

/*
 * Copyright (c) 2019 Anton Lindqvist <anton@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/syslimits.h>
#include <sys/systm.h>
#include <sys/timeout.h>

#include <uvm/uvm_extern.h>

#define KUBSAN_INTERVAL	100	/* report interval in msec */
#define KUBSAN_NSLOTS	32

#define NUMBER_BUFSIZ		32
#define LOCATION_BUFSIZ		(PATH_MAX + 32)	/* filename:line:column */
#define LOCATION_REPORTED	(1U << 31)

#define NBITS(typ)	(1 << ((typ)->t_info >> 1))
#define SIGNED(typ)	((typ)->t_info & 1)

struct kubsan_report {
	enum {
		KUBSAN_FLOAT_CAST_OVERFLOW,
		KUBSAN_INVALID_BUILTIN,
		KUBSAN_INVALID_VALUE,
		KUBSAN_NEGATE_OVERFLOW,
		KUBSAN_NONNULL_ARG,
		KUBSAN_OUT_OF_BOUNDS,
		KUBSAN_OVERFLOW,
		KUBSAN_POINTER_OVERFLOW,
		KUBSAN_SHIFT_OUT_OF_BOUNDS,
		KUBSAN_TYPE_MISMATCH,
		KUBSAN_UNREACHABLE,
	} kr_type;

	struct source_location *kr_src;

	union {
		struct {
			const struct float_cast_overflow_data *v_data;
			unsigned long v_val;
		} v_float_cast_overflow;

		struct {
			const struct invalid_builtin_data *v_data;
		} v_invalid_builtin;

		struct {
			const struct invalid_value_data *v_data;
			unsigned long v_val;
		} v_invalid_value;

		struct {
			const struct overflow_data *v_data;
			unsigned int v_val;
		} v_negate_overflow;

		struct {
			const struct nonnull_arg_data *v_data;
		} v_nonnull_arg;

		struct {
			const struct out_of_bounds_data *v_data;
			unsigned int v_idx;
		} v_out_of_bounds;

		struct {
			const struct overflow_data *v_data;
			unsigned long v_lhs;
			unsigned long v_rhs;
			char v_op;
		} v_overflow;

		struct {
			const struct pointer_overflow_data *v_data;
			unsigned long v_base;
			unsigned long v_res;
		} v_pointer_overflow;

		struct {
			const struct shift_out_of_bounds_data *v_data;
			unsigned long v_lhs;
			unsigned long v_rhs;
		} v_shift_out_of_bounds;

		struct {
			const struct type_mismatch_data *v_data;
			unsigned long v_ptr;
		} v_type_mismatch;
	} kr_u;
};
#define kr_float_cast_overflow		kr_u.v_float_cast_overflow
#define kr_invalid_builtin		kr_u.v_invalid_builtin
#define kr_invalid_value		kr_u.v_invalid_value
#define kr_negate_overflow		kr_u.v_negate_overflow
#define kr_nonnull_arg			kr_u.v_nonnull_arg
#define kr_out_of_bounds		kr_u.v_out_of_bounds
#define kr_overflow			kr_u.v_overflow
#define kr_pointer_overflow		kr_u.v_pointer_overflow
#define kr_shift_out_of_bounds		kr_u.v_shift_out_of_bounds
#define kr_type_mismatch		kr_u.v_type_mismatch

struct type_descriptor {
	uint16_t t_kind;
	uint16_t t_info;
	char t_name[1];	/* type name as variable length array */
};

struct source_location {
	const char *sl_filename;
	uint32_t sl_line;
	uint32_t sl_column;
};

struct float_cast_overflow_data {
	struct source_location d_src;
	struct type_descriptor *d_ftype;	/* from type */
	struct type_descriptor *d_ttype;	/* to type */
};

struct invalid_builtin_data {
	struct source_location d_src;
	uint8_t d_kind;
};

struct invalid_value_data {
	struct source_location d_src;
	struct type_descriptor *d_type;
};

struct nonnull_arg_data {
	struct source_location d_src;
	struct source_location d_attr_src;	/* __attribute__ location */
	int d_idx;
};

struct out_of_bounds_data {
	struct source_location d_src;
	struct type_descriptor *d_atype;	/* array type */
	struct type_descriptor *d_itype;	/* index type */
};

struct overflow_data {
	struct source_location d_src;
	struct type_descriptor *d_type;
};

struct pointer_overflow_data {
	struct source_location d_src;
};

struct shift_out_of_bounds_data {
	struct source_location d_src;
	struct type_descriptor *d_ltype;
	struct type_descriptor *d_rtype;
};

struct type_mismatch_data {
	struct source_location d_src;
	struct type_descriptor *d_type;
	uint8_t d_align;	/* log2 alignment */
	uint8_t d_kind;
};

struct unreachable_data {
	struct source_location d_src;
};

int64_t		 kubsan_deserialize_int(struct type_descriptor *,
		    unsigned long);
uint64_t	 kubsan_deserialize_uint(struct type_descriptor *,
		    unsigned long);
void		 kubsan_defer_report(struct kubsan_report *);
void		 kubsan_format_int(struct type_descriptor *, unsigned long,
		    char *, size_t);
int		 kubsan_format_location(const struct source_location *, char *,
		    size_t);
int		 kubsan_is_reported(struct source_location *);
const char	*kubsan_kind(uint8_t);
void		 kubsan_report(void *);
void		 kubsan_unreport(struct source_location *);

static int	is_negative(struct type_descriptor *, unsigned long);
static int	is_shift_exponent_too_large(struct type_descriptor *,
		    unsigned long);

static const char	*pathstrip(const char *);

#ifdef KUBSAN_WATCH
int kubsan_watch = 2;
#else
int kubsan_watch = 1;
#endif

struct kubsan_report	*kubsan_reports = NULL;
struct timeout		 kubsan_timo = TIMEOUT_INITIALIZER(kubsan_report, NULL);
unsigned int		 kubsan_slot = 0;
int			 kubsan_cold = 1;

/*
 * Compiling the kernel with `-fsanitize=undefined' will cause the following
 * functions to be called when a sanitizer detects undefined behavior.
 * Some sanitizers are omitted since they are only applicable to C++.
 *
 * Every __ubsan_*() sanitizer function also has a corresponding
 * __ubsan_*_abort() function as part of the ABI provided by Clang.
 * But, since the kernel never is compiled with `fno-sanitize-recover' for
 * obvious reasons, they are also omitted.
 */

void
__ubsan_handle_add_overflow(struct overflow_data *data,
    unsigned long lhs, unsigned long rhs)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_OVERFLOW,
		.kr_src			= &data->d_src,
		.kr_overflow		= { data, lhs, rhs, '+' },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_builtin_unreachable(struct unreachable_data *data)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_UNREACHABLE,
		.kr_src			= &data->d_src,
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_divrem_overflow(struct overflow_data *data,
    unsigned long lhs, unsigned long rhs)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_OVERFLOW,
		.kr_src			= &data->d_src,
		.kr_overflow		= { data, lhs, rhs, '/' },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_float_cast_overflow(struct float_cast_overflow_data *data,
    unsigned long val)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_FLOAT_CAST_OVERFLOW,
		.kr_src			= &data->d_src,
		.kr_float_cast_overflow	= { data, val },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_invalid_builtin(struct invalid_builtin_data *data)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_INVALID_VALUE,
		.kr_src			= &data->d_src,
		.kr_invalid_builtin	= { data },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_load_invalid_value(struct invalid_value_data *data,
    unsigned long val)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_INVALID_VALUE,
		.kr_src			= &data->d_src,
		.kr_invalid_value	= { data, val },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_nonnull_arg(struct nonnull_arg_data *data)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_NONNULL_ARG,
		.kr_src			= &data->d_src,
		.kr_nonnull_arg		= { data },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_mul_overflow(struct overflow_data *data,
    unsigned long lhs, unsigned long rhs)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_OVERFLOW,
		.kr_src			= &data->d_src,
		.kr_overflow		= { data, lhs, rhs, '*' },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_negate_overflow(struct overflow_data *data, unsigned long val)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_NEGATE_OVERFLOW,
		.kr_src			= &data->d_src,
		.kr_negate_overflow	= { data, val },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_out_of_bounds(struct out_of_bounds_data *data,
    unsigned long idx)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_OUT_OF_BOUNDS,
		.kr_src			= &data->d_src,
		.kr_out_of_bounds	= { data, idx },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_pointer_overflow(struct pointer_overflow_data *data,
    unsigned long base, unsigned long res)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_POINTER_OVERFLOW,
		.kr_src			= &data->d_src,
		.kr_pointer_overflow	= { data, base, res },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_shift_out_of_bounds(struct shift_out_of_bounds_data *data,
    unsigned long lhs, unsigned long rhs)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_SHIFT_OUT_OF_BOUNDS,
		.kr_src			= &data->d_src,
		.kr_shift_out_of_bounds	= { data, lhs, rhs },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_sub_overflow(struct overflow_data *data,
    unsigned long lhs, unsigned long rhs)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_OVERFLOW,
		.kr_src			= &data->d_src,
		.kr_overflow		= { data, lhs, rhs, '-' },
	};

	kubsan_defer_report(&kr);
}

void
__ubsan_handle_type_mismatch_v1(struct type_mismatch_data *data,
    unsigned long ptr)
{
	struct kubsan_report kr = {
		.kr_type		= KUBSAN_TYPE_MISMATCH,
		.kr_src			= &data->d_src,
		.kr_type_mismatch	= { data, ptr },
	};

	kubsan_defer_report(&kr);
}

/*
 * Allocate storage for reports and schedule the reporter.
 * Must be called as early on as possible in order to catch undefined behavior
 * during boot.
 */
void
kubsan_init(void)
{
	kubsan_reports = (void *)uvm_pageboot_alloc(
	    sizeof(struct kubsan_report) * KUBSAN_NSLOTS);
	kubsan_cold = 0;

	timeout_add_msec(&kubsan_timo, KUBSAN_INTERVAL);
}

int64_t
kubsan_deserialize_int(struct type_descriptor *typ, unsigned long val)
{
	switch (NBITS(typ)) {
	case 8:
		return ((int8_t)val);
	case 16:
		return ((int16_t)val);
	case 32:
		return ((int32_t)val);
	case 64:
	default:
		return ((int64_t)val);
	}
}

uint64_t
kubsan_deserialize_uint(struct type_descriptor *typ, unsigned long val)
{
	switch (NBITS(typ)) {
	case 8:
		return ((uint8_t)val);
	case 16:
		return ((uint16_t)val);
	case 32:
		return ((uint32_t)val);
	case 64:
	default:
		return ((uint64_t)val);
	}
}

void
kubsan_defer_report(struct kubsan_report *kr)
{
	unsigned int slot;

	if (__predict_false(kubsan_cold == 1) ||
	    kubsan_is_reported(kr->kr_src))
		return;

	slot = atomic_inc_int_nv(&kubsan_slot) - 1;
	if (slot >= KUBSAN_NSLOTS) {
		/*
		 * No slots left, flag source location as not reported and
		 * hope a slot will be available next time.
		 */
		kubsan_unreport(kr->kr_src);
		return;
	}

	memcpy(&kubsan_reports[slot], kr, sizeof(*kr));
}

void
kubsan_format_int(struct type_descriptor *typ, unsigned long val,
    char *buf, size_t bufsiz)
{
	switch (typ->t_kind) {
	case 0:	/* integer */
		if (SIGNED(typ)) {
			int64_t i = kubsan_deserialize_int(typ, val);
			snprintf(buf, bufsiz, "%lld", i);
		} else {
			uint64_t u = kubsan_deserialize_uint(typ, val);
			snprintf(buf, bufsiz, "%llu", u);
		}
		break;
	default:
		snprintf(buf, bufsiz, "%#x<NaN>", typ->t_kind);
	}
}

int
kubsan_format_location(const struct source_location *src, char *buf,
    size_t bufsiz)
{
	const char *path;

	path = pathstrip(src->sl_filename);

	return snprintf(buf, bufsiz, "%s:%u:%u",
	    path, src->sl_line & ~LOCATION_REPORTED, src->sl_column);
}

int
kubsan_is_reported(struct source_location *src)
{
	uint32_t *line = &src->sl_line;
	uint32_t prev;

	/*
	 * Treat everything as reported when disabled.
	 * Otherwise, new violations would go by unnoticed.
	 */
	if (__predict_false(kubsan_watch == 0))
		return (1);

	do {
		prev = *line;
		/* If already reported, avoid redundant atomic operation. */
		if (prev & LOCATION_REPORTED)
			break;
	} while (atomic_cas_uint(line, prev, prev | LOCATION_REPORTED) != prev);

	return (prev & LOCATION_REPORTED);
}

const char *
kubsan_kind(uint8_t kind)
{
	static const char *kinds[] = {
		"load of",
		"store to",
		"reference binding to",
		"member access within",
		"member call on",
		"constructor call on",
		"downcast of",
		"downcast of",
		"upcast of",
		"cast to virtual base of",
		"_Nonnull binding to",
		"dynamic operation on"
	};

	if (kind >= nitems(kinds))
		return ("?");

	return (kinds[kind]);
}

void
kubsan_report(void *arg)
{
	char bloc[LOCATION_BUFSIZ];
	char blhs[NUMBER_BUFSIZ];
	char brhs[NUMBER_BUFSIZ];
	struct kubsan_report *kr;
	unsigned int nslots;
	unsigned int i = 0;

again:
	nslots = kubsan_slot;
	if (nslots == 0)
		goto done;
	if (nslots > KUBSAN_NSLOTS)
		nslots = KUBSAN_NSLOTS;

	for (; i < nslots; i++) {
		kr = &kubsan_reports[i];

		kubsan_format_location(kr->kr_src, bloc, sizeof(bloc));
		switch (kr->kr_type) {
		case KUBSAN_FLOAT_CAST_OVERFLOW: {
			const struct float_cast_overflow_data *data =
			    kr->kr_float_cast_overflow.v_data;

			kubsan_format_int(data->d_ftype,
			    kr->kr_float_cast_overflow.v_val,
			    blhs, sizeof(blhs));
			printf("kubsan: %s: %s of type %s is outside the range "
			    "of representable values of type %s\n",
			    bloc, blhs, data->d_ftype->t_name,
			    data->d_ttype->t_name);
			break;
		}

		case KUBSAN_INVALID_BUILTIN: {
			const struct invalid_builtin_data *data =
			    kr->kr_invalid_builtin.v_data;

			printf("kubsan: %s: invalid builtin: passing zero to "
			    "%s, which is not a valid argument\n",
			    bloc, kubsan_kind(data->d_kind));
			break;
		}

		case KUBSAN_INVALID_VALUE: {
			const struct invalid_value_data *data =
			    kr->kr_invalid_value.v_data;

			kubsan_format_int(data->d_type,
			    kr->kr_invalid_value.v_val, blhs, sizeof(blhs));
			printf("kubsan: %s: load invalid value: load of value " 
			    "%s is not a valid value for type %s\n",
			    bloc, blhs, data->d_type->t_name);
			break;
		}

		case KUBSAN_NEGATE_OVERFLOW: {
			const struct overflow_data *data =
			    kr->kr_negate_overflow.v_data;

			kubsan_format_int(data->d_type,
			    kr->kr_negate_overflow.v_val, blhs, sizeof(blhs));
			printf("kubsan: %s: negate overflow: negation of %s "
			    "cannot be represented in type %s\n",
			    bloc, blhs, data->d_type->t_name);
			break;
		}

		case KUBSAN_NONNULL_ARG: {
			const struct nonnull_arg_data *data =
			    kr->kr_nonnull_arg.v_data;

			if (data->d_attr_src.sl_filename)
				kubsan_format_location(&data->d_attr_src,
				    blhs, sizeof(blhs));
			else
				blhs[0] = '\0';

			printf("kubsan: %s: null pointer passed as argument "
			    "%d, which is declared to never be null%s%s\n",
			    bloc, data->d_idx,
			    blhs[0] ? "nonnull specified in " : "", blhs);
			break;
		}

		case KUBSAN_OUT_OF_BOUNDS: {
			const struct out_of_bounds_data *data =
			    kr->kr_out_of_bounds.v_data;

			kubsan_format_int(data->d_itype,
			    kr->kr_out_of_bounds.v_idx, blhs, sizeof(blhs));
			printf("kubsan: %s: out of bounds: index %s is out of " 
			    "range for type %s\n",
			    bloc, blhs, data->d_atype->t_name);
			break;
		}

		case KUBSAN_OVERFLOW: {
			const struct overflow_data *data =
			    kr->kr_overflow.v_data;

			kubsan_format_int(data->d_type,
			    kr->kr_overflow.v_lhs, blhs, sizeof(blhs));
			kubsan_format_int(data->d_type,
			    kr->kr_overflow.v_rhs, brhs, sizeof(brhs));
                        printf("kubsan: %s: %s integer overflow: %s %c %s "
                            "cannot be represented in type %s\n",
			    bloc, SIGNED(data->d_type) ? "signed" : "unsigned",
			    blhs, kr->kr_overflow.v_op, brhs,
			    data->d_type->t_name);
			break;
		}

		case KUBSAN_POINTER_OVERFLOW:
			printf("kubsan: %s: pointer overflow: pointer "
			    "expression with base %#lx overflowed to %#lx\n",
			    bloc, kr->kr_pointer_overflow.v_base,
			    kr->kr_pointer_overflow.v_res);
			break;

		case KUBSAN_SHIFT_OUT_OF_BOUNDS: {
			const struct shift_out_of_bounds_data *data =
				kr->kr_shift_out_of_bounds.v_data;
			unsigned long lhs = kr->kr_shift_out_of_bounds.v_lhs;
			unsigned long rhs = kr->kr_shift_out_of_bounds.v_rhs;

			kubsan_format_int(data->d_ltype, lhs, blhs,
			    sizeof(blhs));
			kubsan_format_int(data->d_rtype, rhs, brhs,
			    sizeof(brhs));
			if (is_negative(data->d_rtype, rhs))
				printf("kubsan: %s: shift: shift exponent %s "
				    "is negative\n",
				    bloc, brhs);
			else if (is_shift_exponent_too_large(data->d_rtype, rhs))
				printf("kubsan: %s: shift: shift exponent %s "
				    "is too large for %u-bit type\n",
				    bloc, brhs, NBITS(data->d_rtype));
			else if (is_negative(data->d_ltype, lhs))
				printf("kubsan: %s: shift: left shift of "
				    "negative value %s\n",
				    bloc, blhs);
			else
				printf("kubsan: %s: shift: left shift of %s by "
				    "%s places cannot be represented in type "
				    "%s\n",
				    bloc, blhs, brhs, data->d_ltype->t_name);
			break;
		}

		case KUBSAN_TYPE_MISMATCH: {
			const struct type_mismatch_data *data =
				kr->kr_type_mismatch.v_data;
			unsigned long ptr = kr->kr_type_mismatch.v_ptr;
			unsigned long align = 1UL << data->d_align;

			if (ptr == 0UL)
				printf("kubsan: %s: type mismatch: %s null "
				    "pointer of type %s\n",
				    bloc, kubsan_kind(data->d_kind),
				    data->d_type->t_name);
			else if (ptr & (align - 1))
				printf("kubsan: %s: type mismatch: %s "
				    "misaligned address %p for type %s which "
				    "requires %lu byte alignment\n",
				    bloc, kubsan_kind(data->d_kind),
				    (void *)ptr, data->d_type->t_name, align);
			else
				printf("kubsan: %s: type mismatch: %s address "
				    "%p with insufficient space for an object "
				    "of type %s\n",
				    bloc, kubsan_kind(data->d_kind),
				    (void *)ptr, data->d_type->t_name);
			break;
		}

		case KUBSAN_UNREACHABLE:
			printf("kubsan: %s: unreachable: calling "
			    "__builtin_unreachable()\n",
			    bloc);
			break;
		}

#ifdef DDB
		if (kubsan_watch == 2)
			db_enter();
#endif
	}

	/* New reports can arrive at any time. */
	if (atomic_cas_uint(&kubsan_slot, nslots, 0) != nslots) {
		if (nslots < KUBSAN_NSLOTS)
			goto again;
		atomic_swap_uint(&kubsan_slot, 0);
	}

done:
	timeout_add_msec(&kubsan_timo, KUBSAN_INTERVAL);
}

void
kubsan_unreport(struct source_location *src)
{
	uint32_t *line = &src->sl_line;

	atomic_clearbits_int(line, LOCATION_REPORTED);
}

static int
is_negative(struct type_descriptor *typ, unsigned long val)
{
	return (SIGNED(typ) && kubsan_deserialize_int(typ, val) < 0);
}

static int
is_shift_exponent_too_large(struct type_descriptor *typ, unsigned long val)
{
	return (kubsan_deserialize_int(typ, val) >= NBITS(typ));
}

/*
 * A source location is an absolute path making reports quite long.
 * Instead, use everything after the first /sys/ segment as the path.
 */
static const char *
pathstrip(const char *path)
{
	const char *needle = "/sys/";
	size_t i, j;

	for (i = j = 0; path[i] != '\0'; i++) {
		if (path[i] != needle[j]) {
			j = 0;
			continue;
		}

		if (needle[++j] == '\0')
			return path + i + 1;
	}

	return path;
}
