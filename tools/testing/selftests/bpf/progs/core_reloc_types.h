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
