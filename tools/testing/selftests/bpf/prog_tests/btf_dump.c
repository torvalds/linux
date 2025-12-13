// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include <bpf/btf.h>

static int duration = 0;

void btf_dump_printf(void *ctx, const char *fmt, va_list args)
{
	vfprintf(ctx, fmt, args);
}

static struct btf_dump_test_case {
	const char *name;
	const char *file;
	bool known_ptr_sz;
} btf_dump_test_cases[] = {
	{"btf_dump: syntax", "btf_dump_test_case_syntax", true},
	{"btf_dump: ordering", "btf_dump_test_case_ordering", false},
	{"btf_dump: padding", "btf_dump_test_case_padding", true},
	{"btf_dump: packing", "btf_dump_test_case_packing", true},
	{"btf_dump: bitfields", "btf_dump_test_case_bitfields", true},
	{"btf_dump: multidim", "btf_dump_test_case_multidim", false},
	{"btf_dump: namespacing", "btf_dump_test_case_namespacing", false},
};

static int btf_dump_all_types(const struct btf *btf, void *ctx)
{
	size_t type_cnt = btf__type_cnt(btf);
	struct btf_dump *d;
	int err = 0, id;

	d = btf_dump__new(btf, btf_dump_printf, ctx, NULL);
	err = libbpf_get_error(d);
	if (err)
		return err;

	for (id = 1; id < type_cnt; id++) {
		err = btf_dump__dump_type(d, id);
		if (err)
			goto done;
	}

done:
	btf_dump__free(d);
	return err;
}

static int test_btf_dump_case(int n, struct btf_dump_test_case *t)
{
	char test_file[256], out_file[256], diff_cmd[1024];
	struct btf *btf = NULL;
	int err = 0, fd = -1;
	FILE *f = NULL;

	snprintf(test_file, sizeof(test_file), "%s.bpf.o", t->file);

	btf = btf__parse_elf(test_file, NULL);
	if (!ASSERT_OK_PTR(btf, "btf_parse_elf")) {
		err = -PTR_ERR(btf);
		btf = NULL;
		goto done;
	}

	/* tests with t->known_ptr_sz have no "long" or "unsigned long" type,
	 * so it's impossible to determine correct pointer size; but if they
	 * do, it should be 8 regardless of host architecture, because BPF
	 * target is always 64-bit
	 */
	if (!t->known_ptr_sz) {
		btf__set_pointer_size(btf, 8);
	} else {
		CHECK(btf__pointer_size(btf) != 8, "ptr_sz", "exp %d, got %zu\n",
		      8, btf__pointer_size(btf));
	}

	snprintf(out_file, sizeof(out_file), "/tmp/%s.output.XXXXXX", t->file);
	fd = mkstemp(out_file);
	if (!ASSERT_GE(fd, 0, "create_tmp")) {
		err = fd;
		goto done;
	}
	f = fdopen(fd, "w");
	if (CHECK(f == NULL, "open_tmp",  "failed to open file: %s(%d)\n",
		  strerror(errno), errno)) {
		close(fd);
		goto done;
	}

	err = btf_dump_all_types(btf, f);
	fclose(f);
	close(fd);
	if (CHECK(err, "btf_dump", "failure during C dumping: %d\n", err)) {
		goto done;
	}

	snprintf(test_file, sizeof(test_file), "progs/%s.c", t->file);
	if (access(test_file, R_OK) == -1)
		/*
		 * When the test is run with O=, kselftest copies TEST_FILES
		 * without preserving the directory structure.
		 */
		snprintf(test_file, sizeof(test_file), "%s.c", t->file);
	/*
	 * Diff test output and expected test output, contained between
	 * START-EXPECTED-OUTPUT and END-EXPECTED-OUTPUT lines in test case.
	 * For expected output lines, everything before '*' is stripped out.
	 * Also lines containing comment start and comment end markers are
	 * ignored. 
	 */
	snprintf(diff_cmd, sizeof(diff_cmd),
		 "awk '/START-EXPECTED-OUTPUT/{out=1;next} "
		 "/END-EXPECTED-OUTPUT/{out=0} "
		 "/\\/\\*|\\*\\//{next} " /* ignore comment start/end lines */
		 "out {sub(/^[ \\t]*\\*/, \"\"); print}' '%s' | diff -u - '%s'",
		 test_file, out_file);
	err = system(diff_cmd);
	if (CHECK(err, "diff",
		  "differing test output, output=%s, err=%d, diff cmd:\n%s\n",
		  out_file, err, diff_cmd))
		goto done;

	remove(out_file);

done:
	btf__free(btf);
	return err;
}

struct test_ctx {
	struct btf *btf;
	struct btf_dump *d;
	char *dump_buf;
	size_t dump_buf_sz;
	FILE *dump_buf_file;
};

static void test_ctx__free(struct test_ctx *t)
{
	fclose(t->dump_buf_file);
	free(t->dump_buf);
	btf_dump__free(t->d);
	btf__free(t->btf);
}

static int test_ctx__init(struct test_ctx *t)
{
	t->dump_buf_file = open_memstream(&t->dump_buf, &t->dump_buf_sz);
	if (!ASSERT_OK_PTR(t->dump_buf_file, "dump_memstream"))
		return -1;
	t->btf = btf__new_empty();
	if (!ASSERT_OK_PTR(t->btf, "new_empty"))
		goto err_out;
	t->d = btf_dump__new(t->btf, btf_dump_printf, t->dump_buf_file, NULL);
	if (!ASSERT_OK(libbpf_get_error(t->d), "btf_dump__new"))
		goto err_out;

	return 0;

err_out:
	test_ctx__free(t);
	return -1;
}

static void test_ctx__dump_and_compare(struct test_ctx *t,
				       const char *expected_output,
				       const char *message)
{
	int i, err;

	for (i = 1; i < btf__type_cnt(t->btf); i++) {
		err = btf_dump__dump_type(t->d, i);
		ASSERT_OK(err, "dump_type_ok");
	}

	fflush(t->dump_buf_file);
	t->dump_buf[t->dump_buf_sz] = 0; /* some libc implementations don't do this */

	ASSERT_STREQ(t->dump_buf, expected_output, message);
}

static void test_btf_dump_incremental(void)
{
	struct test_ctx t = {};
	struct btf *btf;
	int id, err;

	if (test_ctx__init(&t))
		return;

	btf = t.btf;

	/* First, generate BTF corresponding to the following C code:
	 *
	 * enum x;
	 *
	 * enum x { X = 1 };
	 *
	 * enum { Y = 1 };
	 *
	 * struct s;
	 *
	 * struct s { int x; };
	 *
	 */
	id = btf__add_enum(btf, "x", 4);
	ASSERT_EQ(id, 1, "enum_declaration_id");
	id = btf__add_enum(btf, "x", 4);
	ASSERT_EQ(id, 2, "named_enum_id");
	err = btf__add_enum_value(btf, "X", 1);
	ASSERT_OK(err, "named_enum_val_ok");

	id = btf__add_enum(btf, NULL, 4);
	ASSERT_EQ(id, 3, "anon_enum_id");
	err = btf__add_enum_value(btf, "Y", 1);
	ASSERT_OK(err, "anon_enum_val_ok");

	id = btf__add_int(btf, "int", 4, BTF_INT_SIGNED);
	ASSERT_EQ(id, 4, "int_id");

	id = btf__add_fwd(btf, "s", BTF_FWD_STRUCT);
	ASSERT_EQ(id, 5, "fwd_id");

	id = btf__add_struct(btf, "s", 4);
	ASSERT_EQ(id, 6, "struct_id");
	err = btf__add_field(btf, "x", 4, 0, 0);
	ASSERT_OK(err, "field_ok");

	test_ctx__dump_and_compare(&t,
"enum x;\n"
"\n"
"enum x {\n"
"	X = 1,\n"
"};\n"
"\n"
"enum {\n"
"	Y = 1,\n"
"};\n"
"\n"
"struct s;\n"
"\n"
"struct s {\n"
"	int x;\n"
"};\n\n", "c_dump1");

	/* Now, after dumping original BTF, append another struct that embeds
	 * anonymous enum. It also has a name conflict with the first struct:
	 *
	 * struct s___2 {
	 *     enum { VAL___2 = 1 } x;
	 *     struct s s;
	 * };
	 *
	 * This will test that btf_dump'er maintains internal state properly.
	 * Note that VAL___2 enum value. It's because we've already emitted
	 * that enum as a global anonymous enum, so btf_dump will ensure that
	 * enum values don't conflict;
	 *
	 */
	fseek(t.dump_buf_file, 0, SEEK_SET);

	id = btf__add_struct(btf, "s", 4);
	ASSERT_EQ(id, 7, "struct_id");
	err = btf__add_field(btf, "x", 2, 0, 0);
	ASSERT_OK(err, "field_ok");
	err = btf__add_field(btf, "y", 3, 32, 0);
	ASSERT_OK(err, "field_ok");
	err = btf__add_field(btf, "s", 6, 64, 0);
	ASSERT_OK(err, "field_ok");

	test_ctx__dump_and_compare(&t,
"struct s___2 {\n"
"	enum x x;\n"
"	enum {\n"
"		Y___2 = 1,\n"
"	} y;\n"
"	struct s s;\n"
"};\n\n" , "c_dump1");

	test_ctx__free(&t);
}

static void test_btf_dump_type_tags(void)
{
	struct test_ctx t = {};
	struct btf *btf;
	int id, err;

	if (test_ctx__init(&t))
		return;

	btf = t.btf;

	/* Generate BTF corresponding to the following C code:
	 *
	 * struct s {
	 *   void __attribute__((btf_type_tag(\"void_tag\"))) *p1;
	 *   void __attribute__((void_attr)) *p2;
	 * };
	 *
	 */

	id = btf__add_type_tag(btf, "void_tag", 0);
	ASSERT_EQ(id, 1, "type_tag_id");
	id = btf__add_ptr(btf, id);
	ASSERT_EQ(id, 2, "void_ptr_id1");

	id = btf__add_type_attr(btf, "void_attr", 0);
	ASSERT_EQ(id, 3, "type_attr_id");
	id = btf__add_ptr(btf, id);
	ASSERT_EQ(id, 4, "void_ptr_id2");

	id = btf__add_struct(btf, "s", 8);
	ASSERT_EQ(id, 5, "struct_id");
	err = btf__add_field(btf, "p1", 2, 0, 0);
	ASSERT_OK(err, "field_ok1");
	err = btf__add_field(btf, "p2", 4, 0, 0);
	ASSERT_OK(err, "field_ok2");

	test_ctx__dump_and_compare(&t,
"struct s {\n"
"	void __attribute__((btf_type_tag(\"void_tag\"))) *p1;\n"
"	void __attribute__((void_attr)) *p2;\n"
"};\n\n", "dump_and_compare");

	test_ctx__free(&t);
}

#define STRSIZE				4096

static void btf_dump_snprintf(void *ctx, const char *fmt, va_list args)
{
	char *s = ctx, new[STRSIZE];

	vsnprintf(new, STRSIZE, fmt, args);
	if (strlen(s) < STRSIZE)
		strncat(s, new, STRSIZE - strlen(s) - 1);
}

static int btf_dump_data(struct btf *btf, struct btf_dump *d,
			 char *name, char *prefix, __u64 flags, void *ptr,
			 size_t ptr_sz, char *str, const char *expected_val)
{
	DECLARE_LIBBPF_OPTS(btf_dump_type_data_opts, opts);
	size_t type_sz;
	__s32 type_id;
	int ret = 0;

	if (flags & BTF_F_COMPACT)
		opts.compact = true;
	if (flags & BTF_F_NONAME)
		opts.skip_names = true;
	if (flags & BTF_F_ZERO)
		opts.emit_zeroes = true;
	if (prefix) {
		ASSERT_STRNEQ(name, prefix, strlen(prefix),
			      "verify prefix match");
		name += strlen(prefix) + 1;
	}
	type_id = btf__find_by_name(btf, name);
	if (!ASSERT_GE(type_id, 0, "find type id"))
		return -ENOENT;
	type_sz = btf__resolve_size(btf, type_id);
	str[0] = '\0';
	ret = btf_dump__dump_type_data(d, type_id, ptr, ptr_sz, &opts);
	if (type_sz <= ptr_sz) {
		if (!ASSERT_EQ(ret, type_sz, "failed/unexpected type_sz"))
			return -EINVAL;
	} else {
		if (!ASSERT_EQ(ret, -E2BIG, "failed to return -E2BIG"))
			return -EINVAL;
	}
	if (!ASSERT_STREQ(str, expected_val, "ensure expected/actual match"))
		return -EFAULT;
	return 0;
}

#define TEST_BTF_DUMP_DATA(_b, _d, _prefix, _str, _type, _flags,	\
			   _expected, ...)				\
	do {								\
		char __ptrtype[64] = #_type;				\
		char *_ptrtype = (char *)__ptrtype;			\
		_type _ptrdata = __VA_ARGS__;				\
		void *_ptr = &_ptrdata;					\
									\
		(void) btf_dump_data(_b, _d, _ptrtype, _prefix, _flags,	\
				     _ptr, sizeof(_type), _str,		\
				     _expected);			\
	} while (0)

/* Use where expected data string matches its stringified declaration */
#define TEST_BTF_DUMP_DATA_C(_b, _d, _prefix,  _str, _type, _flags,	\
			     ...)					\
	TEST_BTF_DUMP_DATA(_b, _d, _prefix, _str, _type, _flags,	\
			   "(" #_type ")" #__VA_ARGS__,	__VA_ARGS__)

/* overflow test; pass typesize < expected type size, ensure E2BIG returned */
#define TEST_BTF_DUMP_DATA_OVER(_b, _d, _prefix, _str, _type, _type_sz,	\
				_expected, ...)				\
	do {								\
		char __ptrtype[64] = #_type;				\
		char *_ptrtype = (char *)__ptrtype;			\
		_type _ptrdata = __VA_ARGS__;				\
		void *_ptr = &_ptrdata;					\
									\
		(void) btf_dump_data(_b, _d, _ptrtype, _prefix, 0,	\
				     _ptr, _type_sz, _str, _expected);	\
	} while (0)

#define TEST_BTF_DUMP_VAR(_b, _d, _prefix, _str, _var, _type, _flags,	\
			  _expected, ...)				\
	do {								\
		_type _ptrdata = __VA_ARGS__;				\
		void *_ptr = &_ptrdata;					\
									\
		(void) btf_dump_data(_b, _d, _var, _prefix, _flags,	\
				     _ptr, sizeof(_type), _str,		\
				     _expected);			\
	} while (0)

static void test_btf_dump_int_data(struct btf *btf, struct btf_dump *d,
				   char *str)
{
#ifdef __SIZEOF_INT128__
	unsigned __int128 i = 0xffffffffffffffff;

	/* this dance is required because we cannot directly initialize
	 * a 128-bit value to anything larger than a 64-bit value.
	 */
	i = (i << 64) | (i - 1);
#endif
	/* simple int */
	TEST_BTF_DUMP_DATA_C(btf, d, NULL, str, int, BTF_F_COMPACT, 1234);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, int, BTF_F_COMPACT | BTF_F_NONAME,
			   "1234", 1234);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, int, 0, "(int)1234", 1234);

	/* zero value should be printed at toplevel */
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, int, BTF_F_COMPACT, "(int)0", 0);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, int, BTF_F_COMPACT | BTF_F_NONAME,
			   "0", 0);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, int, BTF_F_COMPACT | BTF_F_ZERO,
			   "(int)0", 0);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, int,
			   BTF_F_COMPACT | BTF_F_NONAME | BTF_F_ZERO,
			   "0", 0);
	TEST_BTF_DUMP_DATA_C(btf, d, NULL, str, int, BTF_F_COMPACT, -4567);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, int, BTF_F_COMPACT | BTF_F_NONAME,
			   "-4567", -4567);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, int, 0, "(int)-4567", -4567);

	TEST_BTF_DUMP_DATA_OVER(btf, d, NULL, str, int, sizeof(int)-1, "", 1);

#ifdef __SIZEOF_INT128__
	/* gcc encode unsigned __int128 type with name "__int128 unsigned" in dwarf,
	 * and clang encode it with name "unsigned __int128" in dwarf.
	 * Do an availability test for either variant before doing actual test.
	 */
	if (btf__find_by_name(btf, "unsigned __int128") > 0) {
		TEST_BTF_DUMP_DATA(btf, d, NULL, str, unsigned __int128, BTF_F_COMPACT,
				   "(unsigned __int128)0xffffffffffffffff",
				   0xffffffffffffffff);
		ASSERT_OK(btf_dump_data(btf, d, "unsigned __int128", NULL, 0, &i, 16, str,
					"(unsigned __int128)0xfffffffffffffffffffffffffffffffe"),
			  "dump unsigned __int128");
	} else if (btf__find_by_name(btf, "__int128 unsigned") > 0) {
		TEST_BTF_DUMP_DATA(btf, d, NULL, str, __int128 unsigned, BTF_F_COMPACT,
				   "(__int128 unsigned)0xffffffffffffffff",
				   0xffffffffffffffff);
		ASSERT_OK(btf_dump_data(btf, d, "__int128 unsigned", NULL, 0, &i, 16, str,
					"(__int128 unsigned)0xfffffffffffffffffffffffffffffffe"),
			  "dump unsigned __int128");
	} else {
		ASSERT_TRUE(false, "unsigned_int128_not_found");
	}
#endif
}

static void test_btf_dump_float_data(struct btf *btf, struct btf_dump *d,
				     char *str)
{
	float t1 = 1.234567;
	float t2 = -1.234567;
	float t3 = 0.0;
	double t4 = 5.678912;
	double t5 = -5.678912;
	double t6 = 0.0;
	long double t7 = 9.876543;
	long double t8 = -9.876543;
	long double t9 = 0.0;

	/* since the kernel does not likely have any float types in its BTF, we
	 * will need to add some of various sizes.
	 */

	ASSERT_GT(btf__add_float(btf, "test_float", 4), 0, "add float");
	ASSERT_OK(btf_dump_data(btf, d, "test_float", NULL, 0, &t1, 4, str,
				"(test_float)1.234567"), "dump float");
	ASSERT_OK(btf_dump_data(btf, d, "test_float", NULL, 0, &t2, 4, str,
				"(test_float)-1.234567"), "dump float");
	ASSERT_OK(btf_dump_data(btf, d, "test_float", NULL, 0, &t3, 4, str,
				"(test_float)0.000000"), "dump float");

	ASSERT_GT(btf__add_float(btf, "test_double", 8), 0, "add_double");
	ASSERT_OK(btf_dump_data(btf, d, "test_double", NULL, 0, &t4, 8, str,
		  "(test_double)5.678912"), "dump double");
	ASSERT_OK(btf_dump_data(btf, d, "test_double", NULL, 0, &t5, 8, str,
		  "(test_double)-5.678912"), "dump double");
	ASSERT_OK(btf_dump_data(btf, d, "test_double", NULL, 0, &t6, 8, str,
				"(test_double)0.000000"), "dump double");

	ASSERT_GT(btf__add_float(btf, "test_long_double", 16), 0, "add long double");
	ASSERT_OK(btf_dump_data(btf, d, "test_long_double", NULL, 0, &t7, 16,
				str, "(test_long_double)9.876543"),
				"dump long_double");
	ASSERT_OK(btf_dump_data(btf, d, "test_long_double", NULL, 0, &t8, 16,
				str, "(test_long_double)-9.876543"),
				"dump long_double");
	ASSERT_OK(btf_dump_data(btf, d, "test_long_double", NULL, 0, &t9, 16,
				str, "(test_long_double)0.000000"),
				"dump long_double");
}

static void test_btf_dump_char_data(struct btf *btf, struct btf_dump *d,
				    char *str)
{
	/* simple char */
	TEST_BTF_DUMP_DATA_C(btf, d, NULL, str, char, BTF_F_COMPACT, 100);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, char, BTF_F_COMPACT | BTF_F_NONAME,
			   "100", 100);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, char, 0, "(char)100", 100);
	/* zero value should be printed at toplevel */
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, char, BTF_F_COMPACT,
			   "(char)0", 0);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, char, BTF_F_COMPACT | BTF_F_NONAME,
			   "0", 0);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, char, BTF_F_COMPACT | BTF_F_ZERO,
			   "(char)0", 0);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, char, BTF_F_COMPACT | BTF_F_NONAME | BTF_F_ZERO,
			   "0", 0);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, char, 0, "(char)0", 0);

	TEST_BTF_DUMP_DATA_OVER(btf, d, NULL, str, char, sizeof(char)-1, "", 100);
}

static void test_btf_dump_typedef_data(struct btf *btf, struct btf_dump *d,
				       char *str)
{
	/* simple typedef */
	TEST_BTF_DUMP_DATA_C(btf, d, NULL, str, uint64_t, BTF_F_COMPACT, 100);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, u64, BTF_F_COMPACT | BTF_F_NONAME,
			   "1", 1);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, u64, 0, "(u64)1", 1);
	/* zero value should be printed at toplevel */
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, u64, BTF_F_COMPACT, "(u64)0", 0);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, u64, BTF_F_COMPACT | BTF_F_NONAME,
			   "0", 0);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, u64, BTF_F_COMPACT | BTF_F_ZERO,
			   "(u64)0", 0);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, u64,
			   BTF_F_COMPACT | BTF_F_NONAME | BTF_F_ZERO,
			   "0", 0);
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, u64, 0, "(u64)0", 0);

	/* typedef struct */
	TEST_BTF_DUMP_DATA_C(btf, d, NULL, str, atomic_t, BTF_F_COMPACT,
			     {.counter = (int)1,});
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, atomic_t, BTF_F_COMPACT | BTF_F_NONAME,
			   "{1,}", { .counter = 1 });
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, atomic_t, 0,
"(atomic_t){\n"
"	.counter = (int)1,\n"
"}",
			   {.counter = 1,});
	/* typedef with 0 value should be printed at toplevel */
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, atomic_t, BTF_F_COMPACT, "(atomic_t){}",
			   {.counter = 0,});
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, atomic_t, BTF_F_COMPACT | BTF_F_NONAME,
			   "{}", {.counter = 0,});
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, atomic_t, 0,
"(atomic_t){\n"
"}",
			   {.counter = 0,});
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, atomic_t, BTF_F_COMPACT | BTF_F_ZERO,
			   "(atomic_t){.counter = (int)0,}",
			   {.counter = 0,});
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, atomic_t,
			   BTF_F_COMPACT | BTF_F_NONAME | BTF_F_ZERO,
			   "{0,}", {.counter = 0,});
	TEST_BTF_DUMP_DATA(btf, d, NULL, str, atomic_t, BTF_F_ZERO,
"(atomic_t){\n"
"	.counter = (int)0,\n"
"}",
			   { .counter = 0,});

	/* overflow should show type but not value since it overflows */
	TEST_BTF_DUMP_DATA_OVER(btf, d, NULL, str, atomic_t, sizeof(atomic_t)-1,
				"(atomic_t){\n", { .counter = 1});
}

static void test_btf_dump_enum_data(struct btf *btf, struct btf_dump *d,
				    char *str)
{
	/* enum where enum value does (and does not) exist */
	TEST_BTF_DUMP_DATA_C(btf, d, "enum", str, enum bpf_cmd, BTF_F_COMPACT,
			     BPF_MAP_CREATE);
	TEST_BTF_DUMP_DATA(btf, d, "enum", str, enum bpf_cmd, BTF_F_COMPACT,
			   "(enum bpf_cmd)BPF_MAP_CREATE", 0);
	TEST_BTF_DUMP_DATA(btf, d, "enum", str, enum bpf_cmd,
			   BTF_F_COMPACT | BTF_F_NONAME,
			   "BPF_MAP_CREATE",
			   BPF_MAP_CREATE);
	TEST_BTF_DUMP_DATA(btf, d, "enum", str, enum bpf_cmd, 0,
			   "(enum bpf_cmd)BPF_MAP_CREATE",
			   BPF_MAP_CREATE);
	TEST_BTF_DUMP_DATA(btf, d, "enum", str, enum bpf_cmd,
			   BTF_F_COMPACT | BTF_F_NONAME | BTF_F_ZERO,
			   "BPF_MAP_CREATE", 0);
	TEST_BTF_DUMP_DATA(btf, d, "enum", str, enum bpf_cmd,
			   BTF_F_COMPACT | BTF_F_ZERO,
			   "(enum bpf_cmd)BPF_MAP_CREATE",
			   BPF_MAP_CREATE);
	TEST_BTF_DUMP_DATA(btf, d, "enum", str, enum bpf_cmd,
			   BTF_F_COMPACT | BTF_F_NONAME | BTF_F_ZERO,
			   "BPF_MAP_CREATE", BPF_MAP_CREATE);
	TEST_BTF_DUMP_DATA_C(btf, d, "enum", str, enum bpf_cmd, BTF_F_COMPACT, 2000);
	TEST_BTF_DUMP_DATA(btf, d, "enum", str, enum bpf_cmd,
			   BTF_F_COMPACT | BTF_F_NONAME,
			   "2000", 2000);
	TEST_BTF_DUMP_DATA(btf, d, "enum", str, enum bpf_cmd, 0,
			   "(enum bpf_cmd)2000", 2000);

	TEST_BTF_DUMP_DATA_OVER(btf, d, "enum", str, enum bpf_cmd,
				sizeof(enum bpf_cmd) - 1, "", BPF_MAP_CREATE);
}

static void test_btf_dump_struct_data(struct btf *btf, struct btf_dump *d,
				      char *str)
{
	DECLARE_LIBBPF_OPTS(btf_dump_type_data_opts, opts);
	char zero_data[512] = { };
	char type_data[512];
	void *fops = type_data;
	void *skb = type_data;
	size_t type_sz;
	__s32 type_id;
	char *cmpstr;
	int ret;

	memset(type_data, 255, sizeof(type_data));

	/* simple struct */
	TEST_BTF_DUMP_DATA_C(btf, d, "struct", str, struct btf_enum, BTF_F_COMPACT,
			     {.name_off = (__u32)3,.val = (__s32)-1,});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct btf_enum,
			   BTF_F_COMPACT | BTF_F_NONAME,
			   "{3,-1,}",
			   { .name_off = 3, .val = -1,});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct btf_enum, 0,
"(struct btf_enum){\n"
"	.name_off = (__u32)3,\n"
"	.val = (__s32)-1,\n"
"}",
			   { .name_off = 3, .val = -1,});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct btf_enum,
			   BTF_F_COMPACT | BTF_F_NONAME,
			   "{-1,}",
			   { .name_off = 0, .val = -1,});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct btf_enum,
			   BTF_F_COMPACT | BTF_F_NONAME | BTF_F_ZERO,
			   "{0,-1,}",
			   { .name_off = 0, .val = -1,});
	/* empty struct should be printed */
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct btf_enum, BTF_F_COMPACT,
			   "(struct btf_enum){}",
			   { .name_off = 0, .val = 0,});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct btf_enum,
			   BTF_F_COMPACT | BTF_F_NONAME,
			   "{}",
			   { .name_off = 0, .val = 0,});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct btf_enum, 0,
"(struct btf_enum){\n"
"}",
			   { .name_off = 0, .val = 0,});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct btf_enum,
			   BTF_F_COMPACT | BTF_F_ZERO,
			   "(struct btf_enum){.name_off = (__u32)0,.val = (__s32)0,}",
			   { .name_off = 0, .val = 0,});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct btf_enum,
			   BTF_F_ZERO,
"(struct btf_enum){\n"
"	.name_off = (__u32)0,\n"
"	.val = (__s32)0,\n"
"}",
			   { .name_off = 0, .val = 0,});

	/* struct with pointers */
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct list_head, BTF_F_COMPACT,
			   "(struct list_head){.next = (struct list_head *)0x1,}",
			   { .next = (struct list_head *)1 });
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct list_head, 0,
"(struct list_head){\n"
"	.next = (struct list_head *)0x1,\n"
"}",
			   { .next = (struct list_head *)1 });
	/* NULL pointer should not be displayed */
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct list_head, BTF_F_COMPACT,
			   "(struct list_head){}",
			   { .next = (struct list_head *)0 });
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct list_head, 0,
"(struct list_head){\n"
"}",
			   { .next = (struct list_head *)0 });

	/* struct with function pointers */
	type_id = btf__find_by_name(btf, "file_operations");
	if (ASSERT_GT(type_id, 0, "find type id")) {
		type_sz = btf__resolve_size(btf, type_id);
		str[0] = '\0';

		ret = btf_dump__dump_type_data(d, type_id, fops, type_sz, &opts);
		ASSERT_EQ(ret, type_sz,
			  "unexpected return value dumping file_operations");
		cmpstr =
"(struct file_operations){\n"
"	.owner = (struct module *)0xffffffffffffffff,\n"
"	.fop_flags = (fop_flags_t)4294967295,";

		ASSERT_STRNEQ(str, cmpstr, strlen(cmpstr), "file_operations");
	}

	/* struct with char array */
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct bpf_prog_info, BTF_F_COMPACT,
			   "(struct bpf_prog_info){.name = (char[16])['f','o','o',],}",
			   { .name = "foo",});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct bpf_prog_info,
			   BTF_F_COMPACT | BTF_F_NONAME,
			   "{['f','o','o',],}",
			   {.name = "foo",});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct bpf_prog_info, 0,
"(struct bpf_prog_info){\n"
"	.name = (char[16])[\n"
"		'f',\n"
"		'o',\n"
"		'o',\n"
"	],\n"
"}",
			   {.name = "foo",});
	/* leading null char means do not display string */
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct bpf_prog_info, BTF_F_COMPACT,
			   "(struct bpf_prog_info){}",
			   {.name = {'\0', 'f', 'o', 'o'}});
	/* handle non-printable characters */
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct bpf_prog_info, BTF_F_COMPACT,
			   "(struct bpf_prog_info){.name = (char[16])[1,2,3,],}",
			   { .name = {1, 2, 3, 0}});

	/* struct with non-char array */
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct __sk_buff, BTF_F_COMPACT,
			   "(struct __sk_buff){.cb = (__u32[5])[1,2,3,4,5,],}",
			   { .cb = {1, 2, 3, 4, 5,},});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct __sk_buff,
			   BTF_F_COMPACT | BTF_F_NONAME,
			   "{[1,2,3,4,5,],}",
			   { .cb = { 1, 2, 3, 4, 5},});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct __sk_buff, 0,
"(struct __sk_buff){\n"
"	.cb = (__u32[5])[\n"
"		1,\n"
"		2,\n"
"		3,\n"
"		4,\n"
"		5,\n"
"	],\n"
"}",
			   { .cb = { 1, 2, 3, 4, 5},});
	/* For non-char, arrays, show non-zero values only */
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct __sk_buff, BTF_F_COMPACT,
			   "(struct __sk_buff){.cb = (__u32[5])[0,0,1,0,0,],}",
			   { .cb = { 0, 0, 1, 0, 0},});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct __sk_buff, 0,
"(struct __sk_buff){\n"
"	.cb = (__u32[5])[\n"
"		0,\n"
"		0,\n"
"		1,\n"
"		0,\n"
"		0,\n"
"	],\n"
"}",
			   { .cb = { 0, 0, 1, 0, 0},});

	/* struct with bitfields */
	TEST_BTF_DUMP_DATA_C(btf, d, "struct", str, struct bpf_insn, BTF_F_COMPACT,
		{.code = (__u8)1,.dst_reg = (__u8)0x2,.src_reg = (__u8)0x3,.off = (__s16)4,.imm = (__s32)5,});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct bpf_insn,
			   BTF_F_COMPACT | BTF_F_NONAME,
			   "{1,0x2,0x3,4,5,}",
			   { .code = 1, .dst_reg = 0x2, .src_reg = 0x3, .off = 4,
			     .imm = 5,});
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct bpf_insn, 0,
"(struct bpf_insn){\n"
"	.code = (__u8)1,\n"
"	.dst_reg = (__u8)0x2,\n"
"	.src_reg = (__u8)0x3,\n"
"	.off = (__s16)4,\n"
"	.imm = (__s32)5,\n"
"}",
			   {.code = 1, .dst_reg = 2, .src_reg = 3, .off = 4, .imm = 5});

	/* zeroed bitfields should not be displayed */
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct bpf_insn, BTF_F_COMPACT,
			   "(struct bpf_insn){.dst_reg = (__u8)0x1,}",
			   { .code = 0, .dst_reg = 1});

	/* struct with enum bitfield */
	type_id = btf__find_by_name(btf, "fs_context");
	if (ASSERT_GT(type_id,  0, "find fs_context")) {
		type_sz = btf__resolve_size(btf, type_id);
		str[0] = '\0';

		opts.emit_zeroes = true;
		ret = btf_dump__dump_type_data(d, type_id, zero_data, type_sz, &opts);
		ASSERT_EQ(ret, type_sz,
			  "unexpected return value dumping fs_context");

		ASSERT_NEQ(strstr(str, "FS_CONTEXT_FOR_MOUNT"), NULL,
				  "bitfield value not present");
	}

	/* struct with nested anon union */
	TEST_BTF_DUMP_DATA(btf, d, "struct", str, struct bpf_sock_ops, BTF_F_COMPACT,
			   "(struct bpf_sock_ops){.op = (__u32)1,(union){.args = (__u32[4])[1,2,3,4,],.reply = (__u32)1,.replylong = (__u32[4])[1,2,3,4,],},}",
			   { .op = 1, .args = { 1, 2, 3, 4}});

	/* union with nested struct */
	TEST_BTF_DUMP_DATA(btf, d, "union", str, union bpf_iter_link_info, BTF_F_COMPACT,
			   "(union bpf_iter_link_info){.map = (struct){.map_fd = (__u32)1,},.cgroup = (struct){.order = (enum bpf_cgroup_iter_order)BPF_CGROUP_ITER_SELF_ONLY,.cgroup_fd = (__u32)1,},.task = (struct){.tid = (__u32)1,.pid = (__u32)1,},}",
			   { .cgroup = { .order = 1, .cgroup_fd = 1, }});

	/* struct skb with nested structs/unions; because type output is so
	 * complex, we don't do a string comparison, just verify we return
	 * the type size as the amount of data displayed.
	 */
	type_id = btf__find_by_name(btf, "sk_buff");
	if (ASSERT_GT(type_id, 0, "find struct sk_buff")) {
		type_sz = btf__resolve_size(btf, type_id);
		str[0] = '\0';

		ret = btf_dump__dump_type_data(d, type_id, skb, type_sz, &opts);
		ASSERT_EQ(ret, type_sz,
			  "unexpected return value dumping sk_buff");
	}

	/* overflow bpf_sock_ops struct with final element nonzero/zero.
	 * Regardless of the value of the final field, we don't have all the
	 * data we need to display it, so we should trigger an overflow.
	 * In other words overflow checking should trump "is field zero?"
	 * checks because if we've overflowed, it shouldn't matter what the
	 * field is - we can't trust its value so shouldn't display it.
	 */
	TEST_BTF_DUMP_DATA_OVER(btf, d, "struct", str, struct bpf_sock_ops,
				sizeof(struct bpf_sock_ops) - 1,
				"(struct bpf_sock_ops){\n\t.op = (__u32)1,\n",
				{ .op = 1, .skb_hwtstamp = 2});
	TEST_BTF_DUMP_DATA_OVER(btf, d, "struct", str, struct bpf_sock_ops,
				sizeof(struct bpf_sock_ops) - 1,
				"(struct bpf_sock_ops){\n\t.op = (__u32)1,\n",
				{ .op = 1, .skb_hwtstamp = 0});
}

static void test_btf_dump_var_data(struct btf *btf, struct btf_dump *d,
				   char *str)
{
#if 0
	TEST_BTF_DUMP_VAR(btf, d, NULL, str, "cpu_number", int, BTF_F_COMPACT,
			  "int cpu_number = (int)100", 100);
#endif
	TEST_BTF_DUMP_VAR(btf, d, NULL, str, "bpf_cgrp_storage_busy", int, BTF_F_COMPACT,
			  "static int bpf_cgrp_storage_busy = (int)2", 2);
}

struct btf_dump_string_ctx {
	struct btf *btf;
	struct btf_dump *d;
	char *str;
	struct btf_dump_type_data_opts *opts;
	int array_id;
};

static int btf_dump_one_string(struct btf_dump_string_ctx *ctx,
			       char *ptr, size_t ptr_sz,
			       const char *expected_val)
{
	size_t type_sz;
	int ret;

	ctx->str[0] = '\0';
	type_sz = btf__resolve_size(ctx->btf, ctx->array_id);
	ret = btf_dump__dump_type_data(ctx->d, ctx->array_id, ptr, ptr_sz, ctx->opts);
	if (type_sz <= ptr_sz) {
		if (!ASSERT_EQ(ret, type_sz, "failed/unexpected type_sz"))
			return -EINVAL;
	}
	if (!ASSERT_STREQ(ctx->str, expected_val, "ensure expected/actual match"))
		return -EFAULT;
	return 0;
}

static void btf_dump_strings(struct btf_dump_string_ctx *ctx)
{
	struct btf_dump_type_data_opts *opts = ctx->opts;

	opts->emit_strings = true;

	opts->compact = true;
	opts->emit_zeroes = false;

	opts->skip_names = false;
	btf_dump_one_string(ctx, "foo", 4, "(char[4])\"foo\"");

	opts->skip_names = true;
	btf_dump_one_string(ctx, "foo", 4, "\"foo\"");

	/* This should have no effect. */
	opts->emit_zeroes = false;
	btf_dump_one_string(ctx, "foo", 4, "\"foo\"");

	/* This should have no effect. */
	opts->compact = false;
	btf_dump_one_string(ctx, "foo", 4, "\"foo\"");

	/* Non-printable characters come out as hex. */
	btf_dump_one_string(ctx, "fo\xff", 4, "\"fo\\xff\"");
	btf_dump_one_string(ctx, "fo\x7", 4, "\"fo\\x07\"");

	/*
	 * Strings that are too long for the specified type ("char[4]")
	 * should fall back to the current behavior.
	 */
	opts->compact = true;
	btf_dump_one_string(ctx, "abcde", 6, "['a','b','c','d',]");

	/*
	 * Strings that are too short for the specified type ("char[4]")
	 * should work normally.
	 */
	btf_dump_one_string(ctx, "ab", 3, "\"ab\"");

	/* Non-NUL-terminated arrays don't get printed as strings. */
	char food[4] = { 'f', 'o', 'o', 'd' };
	char bye[3] = { 'b', 'y', 'e' };

	btf_dump_one_string(ctx, food, 4, "['f','o','o','d',]");
	btf_dump_one_string(ctx, bye, 3, "['b','y','e',]");

	/* The embedded NUL should terminate the string. */
	char embed[4] = { 'f', 'o', '\0', 'd' };

	btf_dump_one_string(ctx, embed, 4, "\"fo\"");
}

static void test_btf_dump_string_data(void)
{
	struct test_ctx t = {};
	char str[STRSIZE];
	struct btf_dump *d;
	DECLARE_LIBBPF_OPTS(btf_dump_type_data_opts, opts);
	struct btf_dump_string_ctx ctx;
	int char_id, int_id, array_id;

	if (test_ctx__init(&t))
		return;

	d = btf_dump__new(t.btf, btf_dump_snprintf, str, NULL);
	if (!ASSERT_OK_PTR(d, "could not create BTF dump"))
		return;

	/* Generate BTF for a four-element char array. */
	char_id = btf__add_int(t.btf, "char", 1, BTF_INT_CHAR);
	ASSERT_EQ(char_id, 1, "char_id");
	int_id = btf__add_int(t.btf, "int", 4, BTF_INT_SIGNED);
	ASSERT_EQ(int_id, 2, "int_id");
	array_id = btf__add_array(t.btf, int_id, char_id, 4);
	ASSERT_EQ(array_id, 3, "array_id");

	ctx.btf = t.btf;
	ctx.d = d;
	ctx.str = str;
	ctx.opts = &opts;
	ctx.array_id = array_id;

	btf_dump_strings(&ctx);

	btf_dump__free(d);
	test_ctx__free(&t);
}

static void test_btf_datasec(struct btf *btf, struct btf_dump *d, char *str,
			     const char *name, const char *expected_val,
			     void *data, size_t data_sz)
{
	DECLARE_LIBBPF_OPTS(btf_dump_type_data_opts, opts);
	int ret = 0, cmp;
	size_t secsize;
	__s32 type_id;

	opts.compact = true;

	type_id = btf__find_by_name(btf, name);
	if (!ASSERT_GT(type_id, 0, "find type id"))
		return;

	secsize = btf__resolve_size(btf, type_id);
	ASSERT_EQ(secsize,  0, "verify section size");

	str[0] = '\0';
	ret = btf_dump__dump_type_data(d, type_id, data, data_sz, &opts);
	ASSERT_EQ(ret, 0, "unexpected return value");

	cmp = strcmp(str, expected_val);
	ASSERT_EQ(cmp, 0, "ensure expected/actual match");
}

static void test_btf_dump_datasec_data(char *str)
{
	struct btf *btf;
	char license[4] = "GPL";
	struct btf_dump *d;

	btf = btf__parse("xdping_kern.bpf.o", NULL);
	if (!ASSERT_OK_PTR(btf, "xdping_kern.bpf.o BTF not found"))
		return;

	d = btf_dump__new(btf, btf_dump_snprintf, str, NULL);
	if (!ASSERT_OK_PTR(d, "could not create BTF dump"))
		goto out;

	test_btf_datasec(btf, d, str, "license",
			 "SEC(\"license\") char[4] _license = (char[4])['G','P','L',];",
			 license, sizeof(license));
out:
	btf_dump__free(d);
	btf__free(btf);
}

void test_btf_dump() {
	char str[STRSIZE];
	struct btf_dump *d;
	struct btf *btf;
	int i;

	for (i = 0; i < ARRAY_SIZE(btf_dump_test_cases); i++) {
		struct btf_dump_test_case *t = &btf_dump_test_cases[i];

		if (!test__start_subtest(t->name))
			continue;

		test_btf_dump_case(i, &btf_dump_test_cases[i]);
	}
	if (test__start_subtest("btf_dump: incremental"))
		test_btf_dump_incremental();

	if (test__start_subtest("btf_dump: type_tags"))
		test_btf_dump_type_tags();

	btf = libbpf_find_kernel_btf();
	if (!ASSERT_OK_PTR(btf, "no kernel BTF found"))
		return;

	d = btf_dump__new(btf, btf_dump_snprintf, str, NULL);
	if (!ASSERT_OK_PTR(d, "could not create BTF dump"))
		return;

	/* Verify type display for various types. */
	if (test__start_subtest("btf_dump: int_data"))
		test_btf_dump_int_data(btf, d, str);
	if (test__start_subtest("btf_dump: float_data"))
		test_btf_dump_float_data(btf, d, str);
	if (test__start_subtest("btf_dump: char_data"))
		test_btf_dump_char_data(btf, d, str);
	if (test__start_subtest("btf_dump: typedef_data"))
		test_btf_dump_typedef_data(btf, d, str);
	if (test__start_subtest("btf_dump: enum_data"))
		test_btf_dump_enum_data(btf, d, str);
	if (test__start_subtest("btf_dump: struct_data"))
		test_btf_dump_struct_data(btf, d, str);
	if (test__start_subtest("btf_dump: var_data"))
		test_btf_dump_var_data(btf, d, str);
	if (test__start_subtest("btf_dump: string_data"))
		test_btf_dump_string_data();
	btf_dump__free(d);
	btf__free(btf);

	if (test__start_subtest("btf_dump: datasec_data"))
		test_btf_dump_datasec_data(str);
}
