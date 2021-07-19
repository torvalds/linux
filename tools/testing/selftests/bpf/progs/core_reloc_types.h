#include <stdint.h>
#include <stdbool.h>

void preserce_ptr_sz_fn(long x) {}

#define __bpf_aligned __attribute__((aligned(8)))

/*
 * KERNEL
 */

struct core_reloc_kernel_output {
	int valid[10];
	char comm[sizeof("test_progs")];
	int comm_len;
};

/*
 * MODULE
 */

struct core_reloc_module_output {
	long long len;
	long long off;
	int read_ctx_sz;
	bool read_ctx_exists;
	bool buf_exists;
	bool len_exists;
	bool off_exists;
	/* we have test_progs[-flavor], so cut flavor part */
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
	int f10c;
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
	struct core_reloc_arrays_substruct f[][2];
};

/* bigger array dimensions */
struct core_reloc_arrays___diff_arr_dim {
	int a[7];
	char b[3][4][5];
	struct core_reloc_arrays_substruct c[4];
	struct core_reloc_arrays_substruct d[2][3];
	struct core_reloc_arrays_substruct f[1][3];
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
	struct {
		int __padding1;
		int c;
		int __padding2;
	} f[][2];
};

struct core_reloc_arrays___equiv_zero_sz_arr {
	int a[5];
	char b[2][3][4];
	struct core_reloc_arrays_substruct c[3];
	struct core_reloc_arrays_substruct d[1][2];
	/* equivalent to flexible array */
	struct core_reloc_arrays_substruct f[][2];
};

struct core_reloc_arrays___fixed_arr {
	int a[5];
	char b[2][3][4];
	struct core_reloc_arrays_substruct c[3];
	struct core_reloc_arrays_substruct d[1][2];
	/* not a flexible array anymore, but within access bounds */
	struct core_reloc_arrays_substruct f[1][2];
};

struct core_reloc_arrays___err_too_small {
	int a[2]; /* this one is too small */
	char b[2][3][4];
	struct core_reloc_arrays_substruct c[3];
	struct core_reloc_arrays_substruct d[1][2];
	struct core_reloc_arrays_substruct f[][2];
};

struct core_reloc_arrays___err_too_shallow {
	int a[5];
	char b[2][3]; /* this one lacks one dimension */
	struct core_reloc_arrays_substruct c[3];
	struct core_reloc_arrays_substruct d[1][2];
	struct core_reloc_arrays_substruct f[][2];
};

struct core_reloc_arrays___err_non_array {
	int a; /* not an array */
	char b[2][3][4];
	struct core_reloc_arrays_substruct c[3];
	struct core_reloc_arrays_substruct d[1][2];
	struct core_reloc_arrays_substruct f[][2];
};

struct core_reloc_arrays___err_wrong_val_type {
	int a[5];
	char b[2][3][4];
	int c[3]; /* value is not a struct */
	struct core_reloc_arrays_substruct d[1][2];
	struct core_reloc_arrays_substruct f[][2];
};

struct core_reloc_arrays___err_bad_zero_sz_arr {
	/* zero-sized array, but not at the end */
	struct core_reloc_arrays_substruct f[0][2];
	int a[5];
	char b[2][3][4];
	struct core_reloc_arrays_substruct c[3];
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
	void *d __bpf_aligned;
	int (*f)(const char *) __bpf_aligned;
};

struct core_reloc_primitives___diff_enum_def {
	char a;
	int b;
	void *d __bpf_aligned;
	int (*f)(const char *) __bpf_aligned;
	enum {
		X = 100,
		Y = 200,
	} c __bpf_aligned; /* inline enum def with differing set of values */
};

struct core_reloc_primitives___diff_func_proto {
	void (*f)(int) __bpf_aligned; /* incompatible function prototype */
	void *d __bpf_aligned;
	enum core_reloc_primitives_enum c __bpf_aligned;
	int b;
	char a;
};

struct core_reloc_primitives___diff_ptr_type {
	const char * const d __bpf_aligned; /* different pointee type + modifiers */
	char a __bpf_aligned;
	int b;
	enum core_reloc_primitives_enum c;
	int (*f)(const char *) __bpf_aligned;
};

struct core_reloc_primitives___err_non_enum {
	char a[1];
	int b;
	int c; /* int instead of enum */
	void *d __bpf_aligned;
	int (*f)(const char *) __bpf_aligned;
};

struct core_reloc_primitives___err_non_int {
	char a[1];
	int *b __bpf_aligned; /* ptr instead of int */
	enum core_reloc_primitives_enum c __bpf_aligned;
	void *d __bpf_aligned;
	int (*f)(const char *) __bpf_aligned;
};

struct core_reloc_primitives___err_non_ptr {
	char a[1];
	int b;
	enum core_reloc_primitives_enum c;
	int d; /* int instead of ptr */
	int (*f)(const char *) __bpf_aligned;
};

/*
 * MODS
 */
struct core_reloc_mods_output {
	int a, b, c, d, e, f, g, h;
};

typedef const int int_t;
typedef const char *char_ptr_t __bpf_aligned;
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
	char *c __bpf_aligned;
	char_ptr_t d;
	int e[3] __bpf_aligned;
	arr_t f;
	struct core_reloc_mods_substruct g;
	core_reloc_mods_substruct_t h;
};

/* a/b, c/d, e/f, and g/h pairs are swapped */
struct core_reloc_mods___mod_swap {
	int b;
	int_t a;
	char *d __bpf_aligned;
	char_ptr_t c;
	int f[3] __bpf_aligned;
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

typedef const char * const volatile fancy_char_ptr_t __bpf_aligned;

typedef core_reloc_mods_substruct_t core_reloc_mods_substruct_tt;

/* we need more typedefs */
struct core_reloc_mods___typedefs {
	core_reloc_mods_substruct_tt g;
	core_reloc_mods_substruct_tt h;
	arr4_t f;
	arr4_t e;
	fancy_char_ptr_t d;
	fancy_char_ptr_t c;
	int3_t b __bpf_aligned;
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
 * FIELD EXISTENCE
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

struct core_reloc_existence___wrong_field_defs {
	void *a;
	int b[1];
	struct{ int x; } c;
	int arr;
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
	uint16_t	u32;			/* 32 -> 16 */
	int64_t		s32 __bpf_aligned;	/* 32 -> 64 */
};

/* turn bitfield into non-bitfield and vice versa */
struct core_reloc_bitfields___bitfield_vs_int {
	uint64_t	ub1;		/*  3 -> 64 non-bitfield */
	uint8_t		ub2;		/* 20 ->  8 non-bitfield */
	int64_t		ub7 __bpf_aligned;	/*  7 -> 64 non-bitfield signed */
	int64_t		sb4 __bpf_aligned;	/*  4 -> 64 non-bitfield signed */
	uint64_t	sb20 __bpf_aligned;	/* 20 -> 16 non-bitfield unsigned */
	int32_t		u32: 20;		/* 32 non-bitfield -> 20 bitfield */
	uint64_t	s32: 60 __bpf_aligned;	/* 32 non-bitfield -> 60 bitfield */
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
	int float_sz;
};

struct core_reloc_size {
	int int_field;
	struct { int x; } struct_field;
	union { int x; } union_field;
	int arr_field[4];
	void *ptr_field;
	enum { VALUE = 123 } enum_field;
	float float_field;
};

struct core_reloc_size___diff_sz {
	uint64_t int_field;
	struct { int x; int y; int z; } struct_field;
	union { int x; char bla[123]; } union_field;
	char arr_field[10];
	void *ptr_field;
	enum { OTHER_VALUE = 0xFFFFFFFFFFFFFFFF } enum_field;
	double float_field;
};

/* Error case of two candidates with the fields (int_field) at the same
 * offset, but with differing final relocation values: size 4 vs size 1
 */
struct core_reloc_size___err_ambiguous1 {
	/* int at offset 0 */
	int int_field;

	struct { int x; } struct_field;
	union { int x; } union_field;
	int arr_field[4];
	void *ptr_field;
	enum { VALUE___1 = 123 } enum_field;
	float float_field;
};

struct core_reloc_size___err_ambiguous2 {
	/* char at offset 0 */
	char int_field;

	struct { int x; } struct_field;
	union { int x; } union_field;
	int arr_field[4];
	void *ptr_field;
	enum { VALUE___2 = 123 } enum_field;
	float float_field;
};

/*
 * TYPE EXISTENCE & SIZE
 */
struct core_reloc_type_based_output {
	bool struct_exists;
	bool union_exists;
	bool enum_exists;
	bool typedef_named_struct_exists;
	bool typedef_anon_struct_exists;
	bool typedef_struct_ptr_exists;
	bool typedef_int_exists;
	bool typedef_enum_exists;
	bool typedef_void_ptr_exists;
	bool typedef_func_proto_exists;
	bool typedef_arr_exists;

	int struct_sz;
	int union_sz;
	int enum_sz;
	int typedef_named_struct_sz;
	int typedef_anon_struct_sz;
	int typedef_struct_ptr_sz;
	int typedef_int_sz;
	int typedef_enum_sz;
	int typedef_void_ptr_sz;
	int typedef_func_proto_sz;
	int typedef_arr_sz;
};

struct a_struct {
	int x;
};

union a_union {
	int y;
	int z;
};

typedef struct a_struct named_struct_typedef;

typedef struct { int x, y, z; } anon_struct_typedef;

typedef struct {
	int a, b, c;
} *struct_ptr_typedef;

enum an_enum {
	AN_ENUM_VAL1 = 1,
	AN_ENUM_VAL2 = 2,
	AN_ENUM_VAL3 = 3,
};

typedef int int_typedef;

typedef enum { TYPEDEF_ENUM_VAL1, TYPEDEF_ENUM_VAL2 } enum_typedef;

typedef void *void_ptr_typedef;

typedef int (*func_proto_typedef)(long);

typedef char arr_typedef[20];

struct core_reloc_type_based {
	struct a_struct f1;
	union a_union f2;
	enum an_enum f3;
	named_struct_typedef f4;
	anon_struct_typedef f5;
	struct_ptr_typedef f6;
	int_typedef f7;
	enum_typedef f8;
	void_ptr_typedef f9;
	func_proto_typedef f10;
	arr_typedef f11;
};

/* no types in target */
struct core_reloc_type_based___all_missing {
};

/* different type sizes, extra modifiers, anon vs named enums, etc */
struct a_struct___diff_sz {
	long x;
	int y;
	char z;
};

union a_union___diff_sz {
	char yy;
	char zz;
};

typedef struct a_struct___diff_sz named_struct_typedef___diff_sz;

typedef struct { long xx, yy, zzz; } anon_struct_typedef___diff_sz;

typedef struct {
	char aa[1], bb[2], cc[3];
} *struct_ptr_typedef___diff_sz;

enum an_enum___diff_sz {
	AN_ENUM_VAL1___diff_sz = 0x123412341234,
	AN_ENUM_VAL2___diff_sz = 2,
};

typedef unsigned long int_typedef___diff_sz;

typedef enum an_enum___diff_sz enum_typedef___diff_sz;

typedef const void * const void_ptr_typedef___diff_sz;

typedef int_typedef___diff_sz (*func_proto_typedef___diff_sz)(char);

typedef int arr_typedef___diff_sz[2];

struct core_reloc_type_based___diff_sz {
	struct a_struct___diff_sz f1;
	union a_union___diff_sz f2;
	enum an_enum___diff_sz f3;
	named_struct_typedef___diff_sz f4;
	anon_struct_typedef___diff_sz f5;
	struct_ptr_typedef___diff_sz f6;
	int_typedef___diff_sz f7;
	enum_typedef___diff_sz f8;
	void_ptr_typedef___diff_sz f9;
	func_proto_typedef___diff_sz f10;
	arr_typedef___diff_sz f11;
};

/* incompatibilities between target and local types */
union a_struct___incompat { /* union instead of struct */
	int x;
};

struct a_union___incompat { /* struct instead of union */
	int y;
	int z;
};

/* typedef to union, not to struct */
typedef union a_struct___incompat named_struct_typedef___incompat;

/* typedef to void pointer, instead of struct */
typedef void *anon_struct_typedef___incompat;

/* extra pointer indirection */
typedef struct {
	int a, b, c;
} **struct_ptr_typedef___incompat;

/* typedef of a struct with int, instead of int */
typedef struct { int x; } int_typedef___incompat;

/* typedef to func_proto, instead of enum */
typedef int (*enum_typedef___incompat)(void);

/* pointer to char instead of void */
typedef char *void_ptr_typedef___incompat;

/* void return type instead of int */
typedef void (*func_proto_typedef___incompat)(long);

/* multi-dimensional array instead of a single-dimensional */
typedef int arr_typedef___incompat[20][2];

struct core_reloc_type_based___incompat {
	union a_struct___incompat f1;
	struct a_union___incompat f2;
	/* the only valid one is enum, to check that something still succeeds */
	enum an_enum f3;
	named_struct_typedef___incompat f4;
	anon_struct_typedef___incompat f5;
	struct_ptr_typedef___incompat f6;
	int_typedef___incompat f7;
	enum_typedef___incompat f8;
	void_ptr_typedef___incompat f9;
	func_proto_typedef___incompat f10;
	arr_typedef___incompat f11;
};

/* func_proto with incompatible signature */
typedef void (*func_proto_typedef___fn_wrong_ret1)(long);
typedef int * (*func_proto_typedef___fn_wrong_ret2)(long);
typedef struct { int x; } int_struct_typedef;
typedef int_struct_typedef (*func_proto_typedef___fn_wrong_ret3)(long);
typedef int (*func_proto_typedef___fn_wrong_arg)(void *);
typedef int (*func_proto_typedef___fn_wrong_arg_cnt1)(long, long);
typedef int (*func_proto_typedef___fn_wrong_arg_cnt2)(void);

struct core_reloc_type_based___fn_wrong_args {
	/* one valid type to make sure relos still work */
	struct a_struct f1;
	func_proto_typedef___fn_wrong_ret1 f2;
	func_proto_typedef___fn_wrong_ret2 f3;
	func_proto_typedef___fn_wrong_ret3 f4;
	func_proto_typedef___fn_wrong_arg f5;
	func_proto_typedef___fn_wrong_arg_cnt1 f6;
	func_proto_typedef___fn_wrong_arg_cnt2 f7;
};

/*
 * TYPE ID MAPPING (LOCAL AND TARGET)
 */
struct core_reloc_type_id_output {
	int local_anon_struct;
	int local_anon_union;
	int local_anon_enum;
	int local_anon_func_proto_ptr;
	int local_anon_void_ptr;
	int local_anon_arr;

	int local_struct;
	int local_union;
	int local_enum;
	int local_int;
	int local_struct_typedef;
	int local_func_proto_typedef;
	int local_arr_typedef;

	int targ_struct;
	int targ_union;
	int targ_enum;
	int targ_int;
	int targ_struct_typedef;
	int targ_func_proto_typedef;
	int targ_arr_typedef;
};

struct core_reloc_type_id {
	struct a_struct f1;
	union a_union f2;
	enum an_enum f3;
	named_struct_typedef f4;
	func_proto_typedef f5;
	arr_typedef f6;
};

struct core_reloc_type_id___missing_targets {
	/* nothing */
};

/*
 * ENUMERATOR VALUE EXISTENCE AND VALUE RELOCATION
 */
struct core_reloc_enumval_output {
	bool named_val1_exists;
	bool named_val2_exists;
	bool named_val3_exists;
	bool anon_val1_exists;
	bool anon_val2_exists;
	bool anon_val3_exists;

	int named_val1;
	int named_val2;
	int anon_val1;
	int anon_val2;
};

enum named_enum {
	NAMED_ENUM_VAL1 = 1,
	NAMED_ENUM_VAL2 = 2,
	NAMED_ENUM_VAL3 = 3,
};

typedef enum {
	ANON_ENUM_VAL1 = 0x10,
	ANON_ENUM_VAL2 = 0x20,
	ANON_ENUM_VAL3 = 0x30,
} anon_enum;

struct core_reloc_enumval {
	enum named_enum f1;
	anon_enum f2;
};

/* differing enumerator values */
enum named_enum___diff {
	NAMED_ENUM_VAL1___diff = 101,
	NAMED_ENUM_VAL2___diff = 202,
	NAMED_ENUM_VAL3___diff = 303,
};

typedef enum {
	ANON_ENUM_VAL1___diff = 0x11,
	ANON_ENUM_VAL2___diff = 0x22,
	ANON_ENUM_VAL3___diff = 0x33,
} anon_enum___diff;

struct core_reloc_enumval___diff {
	enum named_enum___diff f1;
	anon_enum___diff f2;
};

/* missing (optional) third enum value */
enum named_enum___val3_missing {
	NAMED_ENUM_VAL1___val3_missing = 111,
	NAMED_ENUM_VAL2___val3_missing = 222,
};

typedef enum {
	ANON_ENUM_VAL1___val3_missing = 0x111,
	ANON_ENUM_VAL2___val3_missing = 0x222,
} anon_enum___val3_missing;

struct core_reloc_enumval___val3_missing {
	enum named_enum___val3_missing f1;
	anon_enum___val3_missing f2;
};

/* missing (mandatory) second enum value, should fail */
enum named_enum___err_missing {
	NAMED_ENUM_VAL1___err_missing = 1,
	NAMED_ENUM_VAL3___err_missing = 3,
};

typedef enum {
	ANON_ENUM_VAL1___err_missing = 0x111,
	ANON_ENUM_VAL3___err_missing = 0x222,
} anon_enum___err_missing;

struct core_reloc_enumval___err_missing {
	enum named_enum___err_missing f1;
	anon_enum___err_missing f2;
};
