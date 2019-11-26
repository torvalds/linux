#include <stdint.h>
#include <stdbool.h>
/*
 * KERNEL
 */

struct core_reloc_kernel_output {
	int valid[10];
	char comm[sizeof("test_progs")];
	int comm_len;
};

/*
 * FLAVORS
 */
struct core_reloc_flavors {
	int a;
	int b;
	int c;
};

/* this is not a flavor, as it doesn't have triple underscore */
struct core_reloc_flavors__err_wrong_name {
	int a;
	int b;
	int c;
};

/*
 * NESTING
 */
/* original set up, used to record relocations in BPF program */
struct core_reloc_nesting_substruct {
	int a;
};

union core_reloc_nesting_subunion {
	int b;
};

struct core_reloc_nesting {
	union {
		struct core_reloc_nesting_substruct a;
	} a;
	struct {
		union core_reloc_nesting_subunion b;
	} b;
};

/* inlined anonymous struct/union instead of named structs in original */
struct core_reloc_nesting___anon_embed {
	int __just_for_padding;
	union {
		struct {
			int a;
		} a;
	} a;
	struct {
		union {
			int b;
		} b;
	} b;
};

/* different mix of nested structs/unions than in original */
struct core_reloc_nesting___struct_union_mixup {
	int __a;
	struct {
		int __a;
		union {
			char __a;
			int a;
		} a;
	} a;
	int __b;
	union {
		int __b;
		union {
			char __b;
			int b;
		} b;
	} b;
};

/* extra anon structs/unions, but still valid a.a.a and b.b.b accessors */
struct core_reloc_nesting___extra_nesting {
	int __padding;
	struct {
		struct {
			struct {
				struct {
					union {
						int a;
					} a;
				};
			};
		} a;
		int __some_more;
		struct {
			union {
				union {
					union {
						struct {
							int b;
						};
					} b;
				};
			} b;
		};
	};
};

/* three flavors of same struct with different structure but same layout for
 * a.a.a and b.b.b, thus successfully resolved and relocatable */
struct core_reloc_nesting___dup_compat_types {
	char __just_for_padding;
	/* 3 more bytes of padding */
	struct {
		struct {
			int a; /* offset 4 */
		} a;
	} a;
	long long __more_padding;
	struct {
		struct {
			int b; /* offset 16 */
		} b;
	} b;
};

struct core_reloc_nesting___dup_compat_types__2 {
	int __aligned_padding;
	struct {
		int __trickier_noop[0];
		struct {
			char __some_more_noops[0];
			int a; /* offset 4 */
		} a;
	} a;
	int __more_padding;
	struct {
		struct {
			struct {
				int __critical_padding;
				int b; /* offset 16 */
			} b;
			int __does_not_matter;
		};
	} b;
	int __more_irrelevant_stuff;
};

struct core_reloc_nesting___dup_compat_types__3 {
	char __correct_padding[4];
	struct {
		struct {
			int a; /* offset 4 */
		} a;
	} a;
	/* 8 byte padding due to next struct's alignment */
	struct {
		struct {
			int b;
		} b;
	} b __attribute__((aligned(16)));
};

/* b.b.b field is missing */
struct core_reloc_nesting___err_missing_field {
	struct {
		struct {
			int a;
		} a;
	} a;
	struct {
		struct {
			int x;
		} b;
	} b;
};

/* b.b.b field is an array of integers instead of plain int */
struct core_reloc_nesting___err_array_field {
	struct {
		struct {
			int a;
		} a;
	} a;
	struct {
		struct {
			int b[1];
		} b;
	} b;
};

/* middle b container is missing */
struct core_reloc_nesting___err_missing_container {
	struct {
		struct {
			int a;
		} a;
	} a;
	struct {
		int x;
	} b;
};

/* middle b container is referenced through pointer instead of being embedded */
struct core_reloc_nesting___err_nonstruct_container {
	struct {
		struct {
			int a;
		} a;
	} a;
	struct {
		struct {
			int b;
		} *b;
	} b;
};

/* middle b container is an array of structs instead of plain struct */
struct core_reloc_nesting___err_array_container {
	struct {
		struct {
			int a;
		} a;
	} a;
	struct {
		struct {
			int b;
		} b[1];
	} b;
};

/* two flavors of same struct with incompatible layout for b.b.b */
struct core_reloc_nesting___err_dup_incompat_types__1 {
	struct {
		struct {
			int a; /* offset 0 */
		} a;
	} a;
	struct {
		struct {
			int b; /* offset 4 */
		} b;
	} b;
};

struct core_reloc_nesting___err_dup_incompat_types__2 {
	struct {
		struct {
			int a; /* offset 0 */
		} a;
	} a;
	int __extra_padding;
	struct {
		struct {
			int b; /* offset 8 (!) */
		} b;
	} b;
};

/* two flavors of same struct having one of a.a.a and b.b.b, but not both */
struct core_reloc_nesting___err_partial_match_dups__a {
	struct {
		struct {
			int a;
		} a;
	} a;
};

struct core_reloc_nesting___err_partial_match_dups__b {
	struct {
		struct {
			int b;
		} b;
	} b;
};

struct core_reloc_nesting___err_too_deep {
	struct {
		struct {
			int a;
		} a;
	} a;
	/* 65 levels of nestedness for b.b.b */
	struct {
		struct {
			struct { struct { struct { struct { struct {
			struct { struct { struct { struct { struct {
			struct { struct { struct { struct { struct {
			struct { struct { struct { struct { struct {
			struct { struct { struct { struct { struct {
			struct { struct { struct { struct { struct {
			struct { struct { struct { struct { struct {
			struct { struct { struct { struct { struct {
			struct { struct { struct { struct { struct {
			struct { struct { struct { struct { struct {
			struct { struct { struct { struct { struct {
			struct { struct { struct { struct { struct {
				/* this one is one too much */
				struct {
					int b;
				};
			}; }; }; }; };
			}; }; }; }; };
			}; }; }; }; };
			}; }; }; }; };
			}; }; }; }; };
			}; }; }; }; };
			}; }; }; }; };
			}; }; }; }; };
			}; }; }; }; };
			}; }; }; }; };
			}; }; }; }; };
			}; }; }; }; };
		} b;
	} b;
};

/*
 * ARRAYS
 */
struct core_reloc_arrays_output {
	int a2;
	char b123;
	int c1c;
	int d00d;
};

struct core_reloc_arrays_substruct {
	int c;
	int d;
};

struct core_reloc_arrays {
	int a[5];
	char b[2][3][4];
	struct core_reloc_arrays_substruct c[3];
	struct core_reloc_arrays_substruct d[1][2];
};

/* bigger array dimensions */
struct core_reloc_arrays___diff_arr_dim {
	int a[7];
	char b[3][4][5];
	struct core_reloc_arrays_substruct c[4];
	struct core_reloc_arrays_substruct d[2][3];
};

/* different size of array's value (struct) */
struct core_reloc_arrays___diff_arr_val_sz {
	int a[5];
	char b[2][3][4];
	struct {
		int __padding1;
		int c;
		int __padding2;
	} c[3];
	struct {
		int __padding1;
		int d;
		int __padding2;
	} d[1][2];
};

struct core_reloc_arrays___err_too_small {
	int a[2]; /* this one is too small */
	char b[2][3][4];
	struct core_reloc_arrays_substruct c[3];
	struct core_reloc_arrays_substruct d[1][2];
};

struct core_reloc_arrays___err_too_shallow {
	int a[5];
	char b[2][3]; /* this one lacks one dimension */
	struct core_reloc_arrays_substruct c[3];
	struct core_reloc_arrays_substruct d[1][2];
};

struct core_reloc_arrays___err_non_array {
	int a; /* not an array */
	char b[2][3][4];
	struct core_reloc_arrays_substruct c[3];
	struct core_reloc_arrays_substruct d[1][2];
};

struct core_reloc_arrays___err_wrong_val_type {
	int a[5];
	char b[2][3][4];
	int c[3]; /* value is not a struct */
	struct core_reloc_arrays_substruct d[1][2];
};

/*
 * PRIMITIVES
 */
enum core_reloc_primitives_enum {
	A = 0,
	B = 1,
};

struct core_reloc_primitives {
	char a;
	int b;
	enum core_reloc_primitives_enum c;
	void *d;
	int (*f)(const char *);
};

struct core_reloc_primitives___diff_enum_def {
	char a;
	int b;
	void *d;
	int (*f)(const char *);
	enum {
		X = 100,
		Y = 200,
	} c; /* inline enum def with differing set of values */
};

struct core_reloc_primitives___diff_func_proto {
	void (*f)(int); /* incompatible function prototype */
	void *d;
	enum core_reloc_primitives_enum c;
	int b;
	char a;
};

struct core_reloc_primitives___diff_ptr_type {
	const char * const d; /* different pointee type + modifiers */
	char a;
	int b;
	enum core_reloc_primitives_enum c;
	int (*f)(const char *);
};

struct core_reloc_primitives___err_non_enum {
	char a[1];
	int b;
	int c; /* int instead of enum */
	void *d;
	int (*f)(const char *);
};

struct core_reloc_primitives___err_non_int {
	char a[1];
	int *b; /* ptr instead of int */
	enum core_reloc_primitives_enum c;
	void *d;
	int (*f)(const char *);
};

struct core_reloc_primitives___err_non_ptr {
	char a[1];
	int b;
	enum core_reloc_primitives_enum c;
	int d; /* int instead of ptr */
	int (*f)(const char *);
};

/*
 * MODS
 */
struct core_reloc_mods_output {
	int a, b, c, d, e, f, g, h;
};

typedef const int int_t;
typedef const char *char_ptr_t;
typedef const int arr_t[7];

struct core_reloc_mods_substruct {
	int x;
	int y;
};

typedef struct {
	int x;
	int y;
} core_reloc_mods_substruct_t;

struct core_reloc_mods {
	int a;
	int_t b;
	char *c;
	char_ptr_t d;
	int e[3];
	arr_t f;
	struct core_reloc_mods_substruct g;
	core_reloc_mods_substruct_t h;
};

/* a/b, c/d, e/f, and g/h pairs are swapped */
struct core_reloc_mods___mod_swap {
	int b;
	int_t a;
	char *d;
	char_ptr_t c;
	int f[3];
	arr_t e;
	struct {
		int y;
		int x;
	} h;
	core_reloc_mods_substruct_t g;
};

typedef int int1_t;
typedef int1_t int2_t;
typedef int2_t int3_t;

typedef int arr1_t[5];
typedef arr1_t arr2_t;
typedef arr2_t arr3_t;
typedef arr3_t arr4_t;

typedef const char * const volatile fancy_char_ptr_t;

typedef core_reloc_mods_substruct_t core_reloc_mods_substruct_tt;

/* we need more typedefs */
struct core_reloc_mods___typedefs {
	core_reloc_mods_substruct_tt g;
	core_reloc_mods_substruct_tt h;
	arr4_t f;
	arr4_t e;
	fancy_char_ptr_t d;
	fancy_char_ptr_t c;
	int3_t b;
	int3_t a;
};

/*
 * PTR_AS_ARR
 */
struct core_reloc_ptr_as_arr {
	int a;
};

struct core_reloc_ptr_as_arr___diff_sz {
	int :32; /* padding */
	char __some_more_padding;
	int a;
};

/*
 * INTS
 */
struct core_reloc_ints {
	uint8_t		u8_field;
	int8_t		s8_field;
	uint16_t	u16_field;
	int16_t		s16_field;
	uint32_t	u32_field;
	int32_t		s32_field;
	uint64_t	u64_field;
	int64_t		s64_field;
};

/* signed/unsigned types swap */
struct core_reloc_ints___reverse_sign {
	int8_t		u8_field;
	uint8_t		s8_field;
	int16_t		u16_field;
	uint16_t	s16_field;
	int32_t		u32_field;
	uint32_t	s32_field;
	int64_t		u64_field;
	uint64_t	s64_field;
};

struct core_reloc_ints___bool {
	bool		u8_field; /* bool instead of uint8 */
	int8_t		s8_field;
	uint16_t	u16_field;
	int16_t		s16_field;
	uint32_t	u32_field;
	int32_t		s32_field;
	uint64_t	u64_field;
	int64_t		s64_field;
};

/*
 * MISC
 */
struct core_reloc_misc_output {
	int a, b, c;
};

struct core_reloc_misc___a {
	int a1;
	int a2;
};

struct core_reloc_misc___b {
	int b1;
	int b2;
};

/* this one extends core_reloc_misc_extensible struct from BPF prog */
struct core_reloc_misc_extensible {
	int a;
	int b;
	int c;
	int d;
};

/*
 * EXISTENCE
 */
struct core_reloc_existence_output {
	int a_exists;
	int a_value;
	int b_exists;
	int b_value;
	int c_exists;
	int c_value;
	int arr_exists;
	int arr_value;
	int s_exists;
	int s_value;
};

struct core_reloc_existence {
	int a;
	struct {
		int b;
	};
	int c;
	int arr[1];
	struct {
		int x;
	} s;
};

struct core_reloc_existence___minimal {
	int a;
};

struct core_reloc_existence___err_wrong_int_sz {
	short a;
};

struct core_reloc_existence___err_wrong_int_type {
	int b[1];
};

struct core_reloc_existence___err_wrong_int_kind {
	struct{ int x; } c;
};

struct core_reloc_existence___err_wrong_arr_kind {
	int arr;
};

struct core_reloc_existence___err_wrong_arr_value_type {
	short arr[1];
};

struct core_reloc_existence___err_wrong_struct_type {
	int s;
};

/*
 * BITFIELDS
 */
/* bitfield read results, all as plain integers */
struct core_reloc_bitfields_output {
	int64_t		ub1;
	int64_t		ub2;
	int64_t		ub7;
	int64_t		sb4;
	int64_t		sb20;
	int64_t		u32;
	int64_t		s32;
};

struct core_reloc_bitfields {
	/* unsigned bitfields */
	uint8_t		ub1: 1;
	uint8_t		ub2: 2;
	uint32_t	ub7: 7;
	/* signed bitfields */
	int8_t		sb4: 4;
	int32_t		sb20: 20;
	/* non-bitfields */
	uint32_t	u32;
	int32_t		s32;
};

/* different bit sizes (both up and down) */
struct core_reloc_bitfields___bit_sz_change {
	/* unsigned bitfields */
	uint16_t	ub1: 3;		/*  1 ->  3 */
	uint32_t	ub2: 20;	/*  2 -> 20 */
	uint8_t		ub7: 1;		/*  7 ->  1 */
	/* signed bitfields */
	int8_t		sb4: 1;		/*  4 ->  1 */
	int32_t		sb20: 30;	/* 20 -> 30 */
	/* non-bitfields */
	uint16_t	u32;		/* 32 -> 16 */
	int64_t		s32;		/* 32 -> 64 */
};

/* turn bitfield into non-bitfield and vice versa */
struct core_reloc_bitfields___bitfield_vs_int {
	uint64_t	ub1;		/*  3 -> 64 non-bitfield */
	uint8_t		ub2;		/* 20 ->  8 non-bitfield */
	int64_t		ub7;		/*  7 -> 64 non-bitfield signed */
	int64_t		sb4;		/*  4 -> 64 non-bitfield signed */
	uint64_t	sb20;		/* 20 -> 16 non-bitfield unsigned */
	int32_t		u32: 20;	/* 32 non-bitfield -> 20 bitfield */
	uint64_t	s32: 60;	/* 32 non-bitfield -> 60 bitfield */
};

struct core_reloc_bitfields___just_big_enough {
	uint64_t	ub1: 4;
	uint64_t	ub2: 60; /* packed tightly */
	uint32_t	ub7;
	uint32_t	sb4;
	uint32_t	sb20;
	uint32_t	u32;
	uint32_t	s32;
} __attribute__((packed)) ;

struct core_reloc_bitfields___err_too_big_bitfield {
	uint64_t	ub1: 4;
	uint64_t	ub2: 61; /* packed tightly */
	uint32_t	ub7;
	uint32_t	sb4;
	uint32_t	sb20;
	uint32_t	u32;
	uint32_t	s32;
} __attribute__((packed)) ;

/*
 * SIZE
 */
struct core_reloc_size_output {
	int int_sz;
	int struct_sz;
	int union_sz;
	int arr_sz;
	int arr_elem_sz;
	int ptr_sz;
	int enum_sz;
};

struct core_reloc_size {
	int int_field;
	struct { int x; } struct_field;
	union { int x; } union_field;
	int arr_field[4];
	void *ptr_field;
	enum { VALUE = 123 } enum_field;
};

struct core_reloc_size___diff_sz {
	uint64_t int_field;
	struct { int x; int y; int z; } struct_field;
	union { int x; char bla[123]; } union_field;
	char arr_field[10];
	void *ptr_field;
	enum { OTHER_VALUE = 0xFFFFFFFFFFFFFFFF } enum_field;
};
