/*-
 * Copyright (c) 2014-2015 Sandvine Inc.  All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/nv.h>

#include <atf-c++.hpp>

#include <errno.h>
#include <limits>
#include <set>
#include <sstream>
#include <string>

/*
 * Test that a newly created nvlist has no errors, and is empty.
 */
ATF_TEST_CASE_WITHOUT_HEAD(nvlist_create__is_empty);
ATF_TEST_CASE_BODY(nvlist_create__is_empty)
{
	nvlist_t *nvl;
	int type;
	void *it;

	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);

	ATF_REQUIRE_EQ(nvlist_error(nvl), 0);
	ATF_REQUIRE(nvlist_empty(nvl));

	it = NULL;
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type, &it), static_cast<const char *>(NULL));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_null__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_null__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key;
	int type;

	key = "key";
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_null(nvl, key);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_exists_null(nvl, key));
	ATF_REQUIRE(nvlist_exists_null(nvl, "key"));

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_NULL);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), static_cast<const char *>(NULL));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_bool__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_bool__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key;
	int type;

	key = "name";
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_bool(nvl, key, true);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_exists(nvl, "name"));
	ATF_REQUIRE(nvlist_exists_bool(nvl, key));
	ATF_REQUIRE(nvlist_exists_bool(nvl, "name"));
	ATF_REQUIRE_EQ(nvlist_get_bool(nvl, key), true);

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_BOOL);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), static_cast<const char *>(NULL));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_number__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_number__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key;
	uint64_t value;
	int type;

	key = "foo123";
	value = 71965;
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_number(nvl, key, value);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_exists(nvl, "foo123"));
	ATF_REQUIRE(nvlist_exists_number(nvl, key));
	ATF_REQUIRE_EQ(nvlist_get_number(nvl, key), value);

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_NUMBER);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), static_cast<const char *>(NULL));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_string__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_string__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key;
	const char *value;
	int type;

	key = "test";
	value = "fgjdkgjdk";
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_string(nvl, key, value);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_exists(nvl, "test"));
	ATF_REQUIRE(nvlist_exists_string(nvl, key));
	ATF_REQUIRE(nvlist_exists_string(nvl, "test"));
	ATF_REQUIRE_EQ(strcmp(nvlist_get_string(nvl, key), value), 0);

	/* nvlist_add_* is required to clone the value, so check for that. */
	ATF_REQUIRE(nvlist_get_string(nvl, key) != value);

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_STRING);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), static_cast<const char *>(NULL));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_nvlist__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_nvlist__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key, *subkey;
	nvlist_t *sublist;
	const nvlist_t *value;
	int type;

	key = "test";
	subkey = "subkey";
	sublist = nvlist_create(0);
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_null(sublist, subkey);
	nvlist_add_nvlist(nvl, key, sublist);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_exists(nvl, "test"));
	ATF_REQUIRE(nvlist_exists_nvlist(nvl, key));
	ATF_REQUIRE(nvlist_exists_nvlist(nvl, "test"));

	value = nvlist_get_nvlist(nvl, key);
	ATF_REQUIRE(nvlist_exists_null(value, subkey));

	/* nvlist_add_* is required to clone the value, so check for that. */
	ATF_REQUIRE(sublist != value);

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_NVLIST);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), static_cast<const char *>(NULL));

	nvlist_destroy(sublist);
	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_nvlist__child_with_error);
ATF_TEST_CASE_BODY(nvlist_add_nvlist__child_with_error)
{
	nvlist_t *nvl, *parent;

	nvl = nvlist_create(0);
	parent = nvlist_create(0);

	nvlist_set_error(nvl, EBADF);
	nvlist_add_nvlist(parent, "test", nvl);
	ATF_REQUIRE_EQ(nvlist_error(parent), EBADF);

	nvlist_destroy(nvl);
	nvlist_destroy(parent);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_add_binary__single_insert);
ATF_TEST_CASE_BODY(nvlist_add_binary__single_insert)
{
	nvlist_t *nvl;
	void *it;
	const char *key;
	void *value;
	const void *ret_value;
	size_t value_size, ret_size;
	int type;

	key = "binary";
	value_size = 13;
	value = malloc(value_size);
	memset(value, 0xa5, value_size);
	nvl = nvlist_create(0);

	ATF_REQUIRE(nvl != NULL);
	ATF_REQUIRE(!nvlist_exists(nvl, key));

	nvlist_add_binary(nvl, key, value, value_size);

	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists(nvl, key));
	ATF_REQUIRE(nvlist_exists(nvl, "binary"));
	ATF_REQUIRE(nvlist_exists_binary(nvl, key));
	ATF_REQUIRE(nvlist_exists_binary(nvl, "binary"));

	ret_value = nvlist_get_binary(nvl, key, &ret_size);
	ATF_REQUIRE_EQ(value_size, ret_size);
	ATF_REQUIRE_EQ(memcmp(value, ret_value, ret_size), 0);

	/* nvlist_add_* is required to clone the value, so check for that. */
	ATF_REQUIRE(value != ret_value);

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_BINARY);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type,&it), static_cast<const char *>(NULL));

	nvlist_destroy(nvl);
	free(value);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_clone__empty_nvlist);
ATF_TEST_CASE_BODY(nvlist_clone__empty_nvlist)
{
	nvlist_t *nvl, *clone;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	clone = nvlist_clone(nvl);
	ATF_REQUIRE(clone != NULL);
	ATF_REQUIRE(clone != nvl);
	ATF_REQUIRE(nvlist_empty(clone));

	nvlist_destroy(clone);
	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_clone__nonempty_nvlist);
ATF_TEST_CASE_BODY(nvlist_clone__nonempty_nvlist)
{
	nvlist_t *nvl, *clone;
	const char *key;
	void *it;
	uint64_t value;
	int type;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	key = "testkey";
	value = 684874;
	nvlist_add_number(nvl, key, value);

	clone = nvlist_clone(nvl);
	ATF_REQUIRE(clone != NULL);
	ATF_REQUIRE(clone != nvl);
	ATF_REQUIRE(nvlist_exists_number(clone, key));
	ATF_REQUIRE_EQ(nvlist_get_number(clone, key), value);

	/* Iterate over the nvlist; ensure that it has only our one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(clone, &type, &it), key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_NUMBER);
	ATF_REQUIRE_EQ(nvlist_next(clone, &type, &it), static_cast<const char *>(NULL));

	nvlist_destroy(clone);
	nvlist_destroy(nvl);
}

static const char * const test_subnvlist_key = "nvlist";

static const char * const test_string_key = "string";
static const char * const test_string_val = "59525";

static nvlist_t*
create_test_nvlist(void)
{
	nvlist_t *nvl, *sublist;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	sublist = nvlist_create(0);
	ATF_REQUIRE(sublist != NULL);

	nvlist_add_string(sublist, test_string_key, test_string_val);
	nvlist_move_nvlist(nvl, test_subnvlist_key, sublist);

	return (nvl);
}

static void
verify_test_nvlist(const nvlist_t *nvl)
{
	void *it;
	const nvlist_t *value;
	int type;

	ATF_REQUIRE(nvlist_exists_nvlist(nvl, test_subnvlist_key));

	value = nvlist_get_nvlist(nvl, test_subnvlist_key);

	ATF_REQUIRE(nvlist_exists_string(value, test_string_key));
	ATF_REQUIRE_EQ(strcmp(nvlist_get_string(value, test_string_key), test_string_val), 0);
	ATF_REQUIRE(nvlist_get_string(value, test_string_key) != test_string_val);

	/* Iterate over both nvlists; ensure that each has only the one key. */
	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(value, &type, &it),
	    test_string_key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_STRING);
	ATF_REQUIRE_EQ(nvlist_next(value, &type, &it), static_cast<const char *>(NULL));

	it = NULL;
	ATF_REQUIRE_EQ(strcmp(nvlist_next(nvl, &type, &it),
	    test_subnvlist_key), 0);
	ATF_REQUIRE_EQ(type, NV_TYPE_NVLIST);
	ATF_REQUIRE_EQ(nvlist_next(nvl, &type, &it), static_cast<const char *>(NULL));
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_clone__nested_nvlist);
ATF_TEST_CASE_BODY(nvlist_clone__nested_nvlist)
{
	nvlist_t *nvl, *clone;

	nvl = create_test_nvlist();
	clone = nvlist_clone(nvl);

	ATF_REQUIRE(clone != NULL);
	ATF_REQUIRE(clone != nvl);
	verify_test_nvlist(clone);

	nvlist_destroy(clone);
	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_clone__error_nvlist);
ATF_TEST_CASE_BODY(nvlist_clone__error_nvlist)
{
	nvlist_t *nvl, *clone;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	nvlist_set_error(nvl, ENOMEM);

	clone = nvlist_clone(nvl);
	ATF_REQUIRE(clone == NULL);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_pack__empty_nvlist);
ATF_TEST_CASE_BODY(nvlist_pack__empty_nvlist)
{
	nvlist_t *nvl, *unpacked;
	void *packed;
	size_t packed_size;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	packed = nvlist_pack(nvl, &packed_size);
	ATF_REQUIRE(packed != NULL);

	unpacked = nvlist_unpack(packed, packed_size, 0);
	ATF_REQUIRE(unpacked != NULL);
	ATF_REQUIRE(unpacked != nvl);
	ATF_REQUIRE(nvlist_empty(unpacked));

	nvlist_destroy(unpacked);
	nvlist_destroy(nvl);
	free(packed);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_unpack__flags_nvlist);
ATF_TEST_CASE_BODY(nvlist_unpack__flags_nvlist)
{
	nvlist_t *nvl, *unpacked;
	void *packed;
	size_t packed_size;

	nvl = nvlist_create(NV_FLAG_NO_UNIQUE);
	ATF_REQUIRE(nvl != NULL);

	nvlist_add_bool(nvl, "name", true);
	ATF_REQUIRE(!nvlist_empty(nvl));
	ATF_REQUIRE(nvlist_exists_bool(nvl, "name"));

	packed = nvlist_pack(nvl, &packed_size);
	ATF_REQUIRE(packed != NULL);

	unpacked = nvlist_unpack(packed, packed_size, 0);
	ATF_REQUIRE(unpacked == NULL);

	unpacked = nvlist_unpack(packed, packed_size, NV_FLAG_IGNORE_CASE);
	ATF_REQUIRE(unpacked == NULL);

	unpacked = nvlist_unpack(packed, packed_size, NV_FLAG_NO_UNIQUE);
	ATF_REQUIRE(unpacked != NULL);
	ATF_REQUIRE(unpacked != nvl);
	ATF_REQUIRE(!nvlist_empty(unpacked));
	ATF_REQUIRE(nvlist_exists_bool(unpacked, "name"));

	nvlist_destroy(unpacked);
	nvlist_destroy(nvl);
	free(packed);
}

static void
verify_null(const nvlist_t *nvl, int type)
{

	ATF_REQUIRE_EQ(type, NV_TYPE_NULL);
}

static void
verify_number(const nvlist_t *nvl, const char *name, int type, uint64_t value)
{

	ATF_REQUIRE_EQ(type, NV_TYPE_NUMBER);
	ATF_REQUIRE_EQ(nvlist_get_number(nvl, name), value);
}

static void
verify_string(const nvlist_t *nvl, const char *name, int type,
    const char * value)
{

	ATF_REQUIRE_EQ(type, NV_TYPE_STRING);
	ATF_REQUIRE_EQ(strcmp(nvlist_get_string(nvl, name), value), 0);
}

static void
verify_nvlist(const nvlist_t *nvl, const char *name, int type)
{

	ATF_REQUIRE_EQ(type, NV_TYPE_NVLIST);
	verify_test_nvlist(nvlist_get_nvlist(nvl, name));
}

static void
verify_binary(const nvlist_t *nvl, const char *name, int type,
    const void * value, size_t size)
{
	const void *actual_value;
	size_t actual_size;

	ATF_REQUIRE_EQ(type, NV_TYPE_BINARY);
	actual_value = nvlist_get_binary(nvl, name, &actual_size);
	ATF_REQUIRE_EQ(size, actual_size);
	ATF_REQUIRE_EQ(memcmp(value, actual_value, size), 0);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_pack__multiple_values);
ATF_TEST_CASE_BODY(nvlist_pack__multiple_values)
{
	std::ostringstream msg;
	std::set<std::string> keys_seen;
	nvlist_t *nvl, *unpacked, *nvvalue;
	const char *nullkey, *numkey, *strkey, *nvkey, *binkey, *name;
	int numvalue;
	const char * strvalue;
	void *binvalue, *packed, *it;
	size_t binsize, packed_size;
	int type;

	nvl = nvlist_create(0);

	nullkey = "null";
	nvlist_add_null(nvl, nullkey);

	numkey = "number";
	numvalue = 939853984;
	nvlist_add_number(nvl, numkey, numvalue);

	strkey = "string";
	strvalue = "jfieutijf";
	nvlist_add_string(nvl, strkey, strvalue);

	nvkey = "nvlist";
	nvvalue = create_test_nvlist();
	nvlist_move_nvlist(nvl, nvkey, nvvalue);

	binkey = "binary";
	binsize = 4;
	binvalue = malloc(binsize);
	memset(binvalue, 'b', binsize);
	nvlist_move_binary(nvl, binkey, binvalue, binsize);

	packed = nvlist_pack(nvl, &packed_size);
	ATF_REQUIRE(packed != NULL);

	unpacked = nvlist_unpack(packed, packed_size, 0);
	ATF_REQUIRE(unpacked != 0);

	it = NULL;
	while ((name = nvlist_next(unpacked, &type, &it)) != NULL) {
		/* Ensure that we see every key only once. */
		ATF_REQUIRE_EQ(keys_seen.count(name), 0);

		if (strcmp(name, nullkey) == 0)
			verify_null(unpacked, type);
		else if (strcmp(name, numkey) == 0)
			verify_number(unpacked, name, type, numvalue);
		else if (strcmp(name, strkey) == 0)
			verify_string(unpacked, name, type, strvalue);
		else if (strcmp(name, nvkey) == 0)
			verify_nvlist(unpacked, name, type);
		else if (strcmp(name, binkey) == 0)
			verify_binary(unpacked, name, type, binvalue, binsize);
		else {
			msg << "Unexpected key :'" << name << "'";
			ATF_FAIL(msg.str().c_str());
		}

		keys_seen.insert(name);
	}

	/* Ensure that we saw every key. */
	ATF_REQUIRE_EQ(keys_seen.size(), 5);

	nvlist_destroy(nvl);
	nvlist_destroy(unpacked);
	free(packed);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_pack__error_nvlist);
ATF_TEST_CASE_BODY(nvlist_pack__error_nvlist)
{
	nvlist_t *nvl;
	void *packed;
	size_t size;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	nvlist_set_error(nvl, ENOMEM);

	packed = nvlist_pack(nvl, &size);
	ATF_REQUIRE(packed == NULL);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_unpack__duplicate_key);
ATF_TEST_CASE_BODY(nvlist_unpack__duplicate_key)
{
	nvlist_t *nvl, *unpacked;
	const char *key1, *key2;
	void *packed, *keypos;
	size_t size, keylen;

	nvl = nvlist_create(0);

	key1 = "key1";
	keylen = strlen(key1);
	nvlist_add_number(nvl, key1, 5);

	key2 = "key2";
	ATF_REQUIRE_EQ(keylen, strlen(key2));
	nvlist_add_number(nvl, key2, 10);

	packed = nvlist_pack(nvl, &size);
	ATF_REQUIRE(packed != NULL);

	/*
	 * Mangle the packed nvlist by replacing key1 with key2, creating a
	 * packed nvlist with a duplicate key.
	 */
	keypos = memmem(packed, size, key1, keylen);
	ATF_REQUIRE(keypos != NULL);
	memcpy(keypos, key2, keylen);

	unpacked = nvlist_unpack(packed, size, 0);
	ATF_REQUIRE(nvlist_error(unpacked) != 0);

	free(packed);
	nvlist_destroy(nvl);
	nvlist_destroy(unpacked);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_move_string__single_insert);
ATF_TEST_CASE_BODY(nvlist_move_string__single_insert)
{
	nvlist_t *nvl;
	const char *key;
	char *value;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	key = "testkey";
	value = strdup("testval");
	ATF_REQUIRE(value != NULL);

	nvlist_move_string(nvl, key, value);
	ATF_REQUIRE_EQ(nvlist_get_string(nvl, key), value);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_move_nvlist__null_child);
ATF_TEST_CASE_BODY(nvlist_move_nvlist__null_child)
{
	nvlist_t *parent;

	parent = nvlist_create(0);

	nvlist_move_nvlist(parent, "test", NULL);

	ATF_REQUIRE(nvlist_error(parent) != 0);

	nvlist_destroy(parent);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_move_nvlist__child_with_error);
ATF_TEST_CASE_BODY(nvlist_move_nvlist__child_with_error)
{
	nvlist_t *nvl, *parent;

	nvl = nvlist_create(0);
	parent = nvlist_create(0);

	nvlist_set_error(nvl, EBADF);
	nvlist_move_nvlist(parent, "test", nvl);
	ATF_REQUIRE_EQ(nvlist_error(parent), EBADF);

	nvlist_destroy(parent);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_move_nvlist__single_insert);
ATF_TEST_CASE_BODY(nvlist_move_nvlist__single_insert)
{
	nvlist_t *nvl;
	const char *key;
	nvlist_t *value;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	key = "testkey";
	value = nvlist_create(0);
	ATF_REQUIRE(value != NULL);

	nvlist_move_nvlist(nvl, key, value);
	ATF_REQUIRE_EQ(nvlist_get_nvlist(nvl, key), value);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_move_binary__single_insert);
ATF_TEST_CASE_BODY(nvlist_move_binary__single_insert)
{
	nvlist_t *nvl;
	const char *key;
	void *value;
	size_t size, actual_size;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	key = "testkey";
	size = 73;
	value = malloc(size);
	ATF_REQUIRE(value != NULL);

	nvlist_move_binary(nvl, key, value, size);
	ATF_REQUIRE_EQ(nvlist_get_binary(nvl, key, &actual_size), value);
	ATF_REQUIRE_EQ(size, actual_size);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_take_bool__single_remove);
ATF_TEST_CASE_BODY(nvlist_take_bool__single_remove)
{
	nvlist_t *nvl;
	const char *testkey;
	bool testval;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	testkey = "boolkey";
	testval = false;
	nvlist_add_bool(nvl, testkey, testval);

	ATF_REQUIRE_EQ(nvlist_take_bool(nvl, testkey), testval);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_take_bool__other_keys_unchanged);
ATF_TEST_CASE_BODY(nvlist_take_bool__other_keys_unchanged)
{
	nvlist_t *nvl;
	const char *testkey, *otherkey1, *otherkey2;
	bool testval, otherval1;
	nvlist_t *otherval2;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	testkey = "boolkey";
	testval = true;
	nvlist_add_bool(nvl, testkey, testval);

	otherkey1 = "key1";
	otherval1 = false;
	nvlist_add_bool(nvl, otherkey1, otherval1);

	otherkey2 = "key2";
	otherval2 = create_test_nvlist();
	nvlist_move_nvlist(nvl, otherkey2, otherval2);

	ATF_REQUIRE_EQ(nvlist_take_bool(nvl, testkey), testval);

	ATF_REQUIRE(nvlist_exists_bool(nvl, otherkey1));
	ATF_REQUIRE_EQ(nvlist_get_bool(nvl, otherkey1), otherval1);

	ATF_REQUIRE(nvlist_exists_nvlist(nvl, otherkey2));
	verify_test_nvlist(nvlist_get_nvlist(nvl, otherkey2));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_take_number__single_remove);
ATF_TEST_CASE_BODY(nvlist_take_number__single_remove)
{
	nvlist_t *nvl;
	const char *testkey;
	uint64_t testval;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	testkey = "numkey";
	testval = std::numeric_limits<uint64_t>::max();
	nvlist_add_number(nvl, testkey, testval);

	ATF_REQUIRE_EQ(nvlist_take_number(nvl, testkey), testval);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_take_number__other_keys_unchanged);
ATF_TEST_CASE_BODY(nvlist_take_number__other_keys_unchanged)
{
	nvlist_t *nvl;
	const char *testkey, *otherkey1, *otherkey2;
	uint64_t testval, otherval1;
	const char *otherval2;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	otherkey1 = "key1";
	otherval1 = 5;
	nvlist_add_number(nvl, otherkey1, otherval1);

	testkey = "numkey";
	testval = 1654;
	nvlist_add_number(nvl, testkey, testval);

	otherkey2 = "key2";
	otherval2 = "string";
	nvlist_add_string(nvl, otherkey2, otherval2);

	ATF_REQUIRE_EQ(nvlist_take_number(nvl, testkey), testval);

	ATF_REQUIRE(nvlist_exists_number(nvl, otherkey1));
	ATF_REQUIRE_EQ(nvlist_get_number(nvl, otherkey1), otherval1);

	ATF_REQUIRE(nvlist_exists_string(nvl, otherkey2));
	ATF_REQUIRE_EQ(strcmp(nvlist_get_string(nvl, otherkey2), otherval2), 0);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_take_string__single_remove);
ATF_TEST_CASE_BODY(nvlist_take_string__single_remove)
{
	nvlist_t *nvl;
	const char *testkey;
	const char *testval;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	testkey = "numkey";
	testval = "nvlist";
	nvlist_add_string(nvl, testkey, testval);

	ATF_REQUIRE_EQ(strcmp(nvlist_take_string(nvl, testkey), testval), 0);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_take_string__other_keys_unchanged);
ATF_TEST_CASE_BODY(nvlist_take_string__other_keys_unchanged)
{
	nvlist_t *nvl;
	const char *testkey, *otherkey1, *otherkey2;
	const char *testval, *otherval1;
	bool otherval2;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	otherkey1 = "key1";
	otherval1 = "fjdifjdk";
	nvlist_add_string(nvl, otherkey1, otherval1);

	otherkey2 = "key2";
	otherval2 = true;
	nvlist_add_bool(nvl, otherkey2, otherval2);

	testkey = "strkey";
	testval = "1654";
	nvlist_add_string(nvl, testkey, testval);

	ATF_REQUIRE_EQ(strcmp(nvlist_take_string(nvl, testkey), testval), 0);

	ATF_REQUIRE(nvlist_exists_string(nvl, otherkey1));
	ATF_REQUIRE_EQ(strcmp(nvlist_get_string(nvl, otherkey1), otherval1), 0);

	ATF_REQUIRE(nvlist_exists_bool(nvl, otherkey2));
	ATF_REQUIRE_EQ(nvlist_get_bool(nvl, otherkey2), otherval2);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_take_nvlist__single_remove);
ATF_TEST_CASE_BODY(nvlist_take_nvlist__single_remove)
{
	nvlist_t *nvl;
	const char *testkey;
	nvlist_t *testval;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	testkey = "numkey";
	testval = create_test_nvlist();
	nvlist_move_nvlist(nvl, testkey, testval);

	verify_test_nvlist(nvlist_take_nvlist(nvl, testkey));
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_take_nvlist__other_keys_unchanged);
ATF_TEST_CASE_BODY(nvlist_take_nvlist__other_keys_unchanged)
{
	nvlist_t *nvl;
	const char *testkey, *otherkey1, *otherkey2;
	nvlist_t *testval, *otherval1;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	testkey = "strkey";
	testval = create_test_nvlist();
	nvlist_move_nvlist(nvl, testkey, testval);

	otherkey1 = "key1";
	otherval1 = nvlist_create(0);
	nvlist_move_nvlist(nvl, otherkey1, otherval1);

	otherkey2 = "key2";
	nvlist_add_null(nvl, otherkey2);

	verify_test_nvlist(nvlist_take_nvlist(nvl, testkey));

	ATF_REQUIRE(nvlist_exists_nvlist(nvl, otherkey1));
	ATF_REQUIRE(nvlist_empty(nvlist_get_nvlist(nvl, otherkey1)));

	ATF_REQUIRE(nvlist_exists_null(nvl, otherkey2));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_take_binary__single_remove);
ATF_TEST_CASE_BODY(nvlist_take_binary__single_remove)
{
	nvlist_t *nvl;
	const char *testkey;
	void *testval;
	const void *actual_val;
	size_t testsize, actual_size;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	testkey = "numkey";
	testsize = 457;
	testval = malloc(testsize);
	memset(testval, '5', testsize);
	nvlist_move_binary(nvl, testkey, testval, testsize);

	actual_val = nvlist_take_binary(nvl, testkey, &actual_size);
	ATF_REQUIRE_EQ(testsize, actual_size);
	ATF_REQUIRE_EQ(memcmp(actual_val, testval, testsize), 0);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_take_binary__other_keys_unchanged);
ATF_TEST_CASE_BODY(nvlist_take_binary__other_keys_unchanged)
{
	nvlist_t *nvl;
	const char *testkey, *otherkey1, *otherkey2;
	const void *actual_value;
	char testval[] = "gjiertj";
	char otherval1[] = "fdreg";
	size_t testsize, othersize, actual_size;
	bool otherval2;

	nvl = nvlist_create(0);
	ATF_REQUIRE(nvl != NULL);

	otherkey1 = "key1";
	othersize = sizeof(otherval1);
	nvlist_add_binary(nvl, otherkey1, otherval1, othersize);

	otherkey2 = "key2";
	otherval2 = true;
	nvlist_add_bool(nvl, otherkey2, otherval2);

	testkey = "strkey";
	testsize = sizeof(testval);
	nvlist_add_binary(nvl, testkey, testval, testsize);

	actual_value = nvlist_take_binary(nvl, testkey, &actual_size);
	ATF_REQUIRE_EQ(testsize, actual_size);
	ATF_REQUIRE_EQ(memcmp(actual_value, testval, testsize), 0);

	ATF_REQUIRE(nvlist_exists_binary(nvl, otherkey1));
	actual_value = nvlist_get_binary(nvl, otherkey1, &actual_size);
	ATF_REQUIRE_EQ(othersize, actual_size);
	ATF_REQUIRE_EQ(memcmp(actual_value, otherval1, othersize), 0);

	ATF_REQUIRE(nvlist_exists_bool(nvl, otherkey2));
	ATF_REQUIRE_EQ(nvlist_get_bool(nvl, otherkey2), otherval2);

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free__single_null);
ATF_TEST_CASE_BODY(nvlist_free__single_null)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_null(nvl, key);

	nvlist_free(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free__single_bool);
ATF_TEST_CASE_BODY(nvlist_free__single_bool)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_bool(nvl, key, true);

	nvlist_free(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free__single_number);
ATF_TEST_CASE_BODY(nvlist_free__single_number)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_number(nvl, key, 584);

	nvlist_free(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free__single_string);
ATF_TEST_CASE_BODY(nvlist_free__single_string)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_string(nvl, key, "gjkfkjd");

	nvlist_free(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free__single_nvlist);
ATF_TEST_CASE_BODY(nvlist_free__single_nvlist)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_nvlist(nvl, key, nvlist_create(0));

	nvlist_free(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free__single_binary);
ATF_TEST_CASE_BODY(nvlist_free__single_binary)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_binary(nvl, key, "jgjgfd", 6);

	nvlist_free(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free_null__single_null);
ATF_TEST_CASE_BODY(nvlist_free_null__single_null)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_null(nvl, key);

	nvlist_free_null(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free_bool__single_bool);
ATF_TEST_CASE_BODY(nvlist_free_bool__single_bool)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_bool(nvl, key, true);

	nvlist_free_bool(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free_number__single_number);
ATF_TEST_CASE_BODY(nvlist_free_number__single_number)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_number(nvl, key, 584);

	nvlist_free_number(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free_string__single_string);
ATF_TEST_CASE_BODY(nvlist_free_string__single_string)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_string(nvl, key, "gjkfkjd");

	nvlist_free_string(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free_nvlist__single_nvlist);
ATF_TEST_CASE_BODY(nvlist_free_nvlist__single_nvlist)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_nvlist(nvl, key, nvlist_create(0));

	nvlist_free_nvlist(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_TEST_CASE_WITHOUT_HEAD(nvlist_free_binary__single_binary);
ATF_TEST_CASE_BODY(nvlist_free_binary__single_binary)
{
	nvlist_t *nvl;
	const char *key;

	nvl = nvlist_create(0);
	key = "test";
	nvlist_add_binary(nvl, key, "jgjgfd", 6);

	nvlist_free_binary(nvl, key);
	ATF_REQUIRE(nvlist_empty(nvl));

	nvlist_destroy(nvl);
}

ATF_INIT_TEST_CASES(tp)
{
	ATF_ADD_TEST_CASE(tp, nvlist_create__is_empty);
	ATF_ADD_TEST_CASE(tp, nvlist_add_null__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_add_bool__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_add_number__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_add_string__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_add_nvlist__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_add_nvlist__child_with_error);
	ATF_ADD_TEST_CASE(tp, nvlist_add_binary__single_insert);

	ATF_ADD_TEST_CASE(tp, nvlist_clone__empty_nvlist);
	ATF_ADD_TEST_CASE(tp, nvlist_clone__nonempty_nvlist);
	ATF_ADD_TEST_CASE(tp, nvlist_clone__nested_nvlist);
	ATF_ADD_TEST_CASE(tp, nvlist_clone__error_nvlist);

	ATF_ADD_TEST_CASE(tp, nvlist_pack__empty_nvlist);
	ATF_ADD_TEST_CASE(tp, nvlist_pack__multiple_values);
	ATF_ADD_TEST_CASE(tp, nvlist_pack__error_nvlist);
	ATF_ADD_TEST_CASE(tp, nvlist_unpack__duplicate_key);
	ATF_ADD_TEST_CASE(tp, nvlist_unpack__flags_nvlist);

	ATF_ADD_TEST_CASE(tp, nvlist_move_string__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_move_nvlist__single_insert);
	ATF_ADD_TEST_CASE(tp, nvlist_move_nvlist__null_child);
	ATF_ADD_TEST_CASE(tp, nvlist_move_nvlist__child_with_error);
	ATF_ADD_TEST_CASE(tp, nvlist_move_binary__single_insert);

	ATF_ADD_TEST_CASE(tp, nvlist_take_bool__single_remove);
	ATF_ADD_TEST_CASE(tp, nvlist_take_bool__other_keys_unchanged);
	ATF_ADD_TEST_CASE(tp, nvlist_take_number__single_remove);
	ATF_ADD_TEST_CASE(tp, nvlist_take_number__other_keys_unchanged);
	ATF_ADD_TEST_CASE(tp, nvlist_take_string__single_remove);
	ATF_ADD_TEST_CASE(tp, nvlist_take_string__other_keys_unchanged);
	ATF_ADD_TEST_CASE(tp, nvlist_take_nvlist__single_remove);
	ATF_ADD_TEST_CASE(tp, nvlist_take_nvlist__other_keys_unchanged);
	ATF_ADD_TEST_CASE(tp, nvlist_take_binary__single_remove);
	ATF_ADD_TEST_CASE(tp, nvlist_take_binary__other_keys_unchanged);

	ATF_ADD_TEST_CASE(tp, nvlist_free__single_null);
	ATF_ADD_TEST_CASE(tp, nvlist_free__single_bool);
	ATF_ADD_TEST_CASE(tp, nvlist_free__single_number);
	ATF_ADD_TEST_CASE(tp, nvlist_free__single_string);
	ATF_ADD_TEST_CASE(tp, nvlist_free__single_nvlist);
	ATF_ADD_TEST_CASE(tp, nvlist_free__single_binary);

	ATF_ADD_TEST_CASE(tp, nvlist_free_null__single_null);
	ATF_ADD_TEST_CASE(tp, nvlist_free_bool__single_bool);
	ATF_ADD_TEST_CASE(tp, nvlist_free_number__single_number);
	ATF_ADD_TEST_CASE(tp, nvlist_free_string__single_string);
	ATF_ADD_TEST_CASE(tp, nvlist_free_nvlist__single_nvlist);
	ATF_ADD_TEST_CASE(tp, nvlist_free_binary__single_binary);
}
