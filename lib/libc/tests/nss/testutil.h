/*-
 * Copyright (c) 2006 Michael Bushkov <bushman@freebsd.org>
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
 *
 * $FreeBSD$
 */

#include <sys/queue.h>

#define DECLARE_TEST_DATA(ent)						\
struct ent##_entry {							\
	struct ent data;						\
	STAILQ_ENTRY(ent##_entry) entries;				\
};									\
									\
struct ent##_test_data {						\
	void (*clone_func)(struct ent *, struct ent const *);		\
	void (*free_func)(struct ent *);				\
									\
	STAILQ_HEAD(ent_head, ent##_entry) snapshot_data;		\
};									\
									\
void __##ent##_test_data_init(struct ent##_test_data *, 		\
	void (*)(struct ent *, struct ent const *),			\
	void (*freef)(struct ent *));		 			\
void __##ent##_test_data_destroy(struct ent##_test_data *);		\
									\
void __##ent##_test_data_append(struct ent##_test_data *, struct ent *data);\
int __##ent##_test_data_foreach(struct ent##_test_data *,		\
	int (*)(struct ent *, void *), void *);				\
int __##ent##_test_data_compare(struct ent##_test_data *,		\
	struct ent##_test_data *, int (*)(struct ent *, struct ent *,	\
	void *), void *);						\
struct ent *__##ent##_test_data_find(struct ent##_test_data *, struct ent *,\
	int (*)(struct ent *, struct ent *, void *), void *);		\
void __##ent##_test_data_clear(struct ent##_test_data *);

#define TEST_DATA_INIT(ent, td, clonef, freef)\
	__##ent##_test_data_init(td, clonef, freef)
#define TEST_DATA_DESTROY(ent, td) __##ent##_test_data_destroy(td)
#define TEST_DATA_APPEND(ent, td, d) __##ent##_test_data_append(td, d)
#define TEST_DATA_FOREACH(ent, td, f, mdata)\
	__##ent##_test_data_foreach(td, f, mdata)
#define TEST_DATA_COMPARE(ent, td1, td2, fcmp, mdata)\
	__##ent##_test_data_compare(td1, td2, fcmp, mdata);
#define TEST_DATA_FIND(ent, td, d, fcmp, mdata)\
	__##ent##_test_data_find(td, d, fcmp, mdata)
#define TEST_DATA_CLEAR(ent, td) __##ent##_test_data_clear(td)

#define IMPLEMENT_TEST_DATA(ent)					\
void									\
__##ent##_test_data_init(struct ent##_test_data *td,			\
	void (*clonef)(struct ent *, struct ent const *),		\
	void (*freef)(struct ent *))					\
{									\
	ATF_REQUIRE(td != NULL);					\
	ATF_REQUIRE(clonef != NULL);					\
	ATF_REQUIRE(freef != NULL);					\
									\
	memset(td, 0, sizeof(*td));					\
	td->clone_func = clonef;					\
	td->free_func = freef;						\
	STAILQ_INIT(&td->snapshot_data);				\
}									\
									\
void 									\
__##ent##_test_data_destroy(struct ent##_test_data *td)			\
{									\
	__##ent##_test_data_clear(td);					\
}									\
									\
void 									\
__##ent##_test_data_append(struct ent##_test_data *td, struct ent *app_data)\
{									\
	struct ent##_entry *e;						\
									\
	ATF_REQUIRE(td != NULL);					\
	ATF_REQUIRE(app_data != NULL);					\
									\
	e = (struct ent##_entry *)malloc(sizeof(struct ent##_entry));	\
	ATF_REQUIRE(e != NULL);						\
	memset(e, 0, sizeof(struct ent##_entry));			\
									\
	td->clone_func(&e->data, app_data);				\
	STAILQ_INSERT_TAIL(&td->snapshot_data, e, entries);		\
}									\
									\
int									\
__##ent##_test_data_foreach(struct ent##_test_data *td,			\
	int (*forf)(struct ent *, void *), void *mdata)			\
{									\
	struct ent##_entry *e;						\
	int rv;								\
									\
	ATF_REQUIRE(td != NULL);					\
	ATF_REQUIRE(forf != NULL);					\
									\
	rv = 0;								\
	STAILQ_FOREACH(e, &td->snapshot_data, entries) {		\
		rv = forf(&e->data, mdata);				\
		if (rv != 0)						\
			break;						\
	}								\
									\
	return (rv);							\
}									\
									\
int									\
__##ent##_test_data_compare(struct ent##_test_data *td1, struct ent##_test_data *td2,\
	int (*cmp_func)(struct ent *, struct ent *, void *), void *mdata)\
{									\
	struct ent##_entry *e1, *e2;					\
	int rv;								\
									\
	ATF_REQUIRE(td1 != NULL);					\
	ATF_REQUIRE(td2 != NULL);					\
	ATF_REQUIRE(cmp_func != NULL);					\
									\
	e1 = STAILQ_FIRST(&td1->snapshot_data);				\
	e2 = STAILQ_FIRST(&td2->snapshot_data);				\
									\
	rv = 0;								\
	do {								\
		if ((e1 == NULL) || (e2 == NULL)) {			\
			if (e1 == e2)					\
				return (0);				\
			else						\
				return (-1);				\
		}							\
									\
		rv = cmp_func(&e1->data, &e2->data, mdata);		\
		e1 = STAILQ_NEXT(e1, entries);				\
		e2 = STAILQ_NEXT(e2, entries);				\
	} while (rv == 0);						\
									\
	return (rv);							\
}									\
									\
struct ent *								\
__##ent##_test_data_find(struct ent##_test_data *td, struct ent *data,	\
	int (*cmp)(struct ent *, struct ent *, void *), void *mdata)	\
{									\
	struct ent##_entry *e;						\
	struct ent *result;						\
									\
	ATF_REQUIRE(td != NULL);					\
	ATF_REQUIRE(cmp != NULL);					\
									\
	result = NULL;							\
	STAILQ_FOREACH(e, &td->snapshot_data, entries) {		\
		if (cmp(&e->data, data, mdata) == 0) {			\
			result = &e->data;				\
			break;						\
		}							\
	}								\
									\
	return (result);						\
}									\
									\
									\
void									\
__##ent##_test_data_clear(struct ent##_test_data *td)			\
{									\
	struct ent##_entry *e;						\
	ATF_REQUIRE(td != NULL);					\
									\
	while (!STAILQ_EMPTY(&td->snapshot_data)) {			\
		e = STAILQ_FIRST(&td->snapshot_data);			\
		STAILQ_REMOVE_HEAD(&td->snapshot_data, entries);	\
									\
		td->free_func(&e->data);				\
		free(e);						\
		e = NULL;						\
	}								\
}

#define DECLARE_TEST_FILE_SNAPSHOT(ent)					\
struct ent##_snp_param {						\
	FILE *fp;							\
	void (*sdump_func)(struct ent *, char *, size_t);		\
};									\
									\
int __##ent##_snapshot_write_func(struct ent *, void *);		\
int __##ent##_snapshot_write(char const *, struct ent##_test_data *,	\
	void (*)(struct ent *, char *, size_t));			\
int __##ent##_snapshot_read(char const *, struct ent##_test_data *,	\
	int (*)(struct ent *, char *));

#define TEST_SNAPSHOT_FILE_WRITE(ent, fname, td, f)			\
	__##ent##_snapshot_write(fname, td, f)
#define TEST_SNAPSHOT_FILE_READ(ent, fname, td, f)			\
	__##ent##_snapshot_read(fname, td, f)

#define IMPLEMENT_TEST_FILE_SNAPSHOT(ent)				\
int									\
__##ent##_snapshot_write_func(struct ent *data, void *mdata)		\
{									\
	char buffer[1024];						\
	struct ent##_snp_param *param;					\
									\
	ATF_REQUIRE(data != NULL);					\
									\
	param = (struct ent##_snp_param *)mdata;			\
	param->sdump_func(data, buffer, sizeof(buffer));		\
	fputs(buffer, param->fp);					\
	fputc('\n', param->fp);						\
									\
	return (0);							\
}									\
									\
int									\
__##ent##_snapshot_write(char const *fname, struct ent##_test_data *td,	\
	void (*sdump_func)(struct ent *, char *, size_t))		\
{									\
	struct ent##_snp_param	param;					\
									\
	ATF_REQUIRE(fname != NULL);					\
	ATF_REQUIRE(td != NULL);					\
									\
	param.fp = fopen(fname, "w");					\
	if (param.fp == NULL)						\
		return (-1);						\
									\
	param.sdump_func = sdump_func;					\
	__##ent##_test_data_foreach(td, __##ent##_snapshot_write_func, &param);\
	fclose(param.fp);						\
									\
	return (0);							\
}									\
									\
int									\
__##ent##_snapshot_read(char const *fname, struct ent##_test_data *td,	\
	int (*read_func)(struct ent *, char *))				\
{									\
	struct ent data;						\
	FILE *fi;							\
	size_t len;							\
	int rv;								\
									\
	ATF_REQUIRE(fname != NULL);					\
	ATF_REQUIRE(td != NULL);					\
									\
	fi = fopen(fname, "r");						\
	if (fi == NULL)							\
		return (-1);						\
									\
	rv = 0;								\
	while (!feof(fi)) {						\
		char *buf = fgetln(fi, &len);				\
		if (buf == NULL || len <= 1)				\
			continue;					\
		if (buf[len - 1] == '\n')				\
			buf[len - 1] = '\0';				\
		else							\
			buf[len] = '\0';				\
		if (buf[0] == '#')					\
			continue;					\
		rv = read_func(&data, buf);				\
		if (rv == 0) {						\
			__##ent##_test_data_append(td, &data);		\
			td->free_func(&data);				\
		} else 							\
			goto fin;					\
	}								\
									\
fin:									\
	fclose(fi);							\
	return (rv);							\
}

#define DECLARE_1PASS_TEST(ent)						\
int __##ent##_1pass_test(struct ent##_test_data *, 			\
	int (*)(struct ent *, void *),					\
	void *);

#define DO_1PASS_TEST(ent, td, f, mdata)				\
	__##ent##_1pass_test(td, f, mdata)

#define IMPLEMENT_1PASS_TEST(ent)					\
int									\
__##ent##_1pass_test(struct ent##_test_data *td, 			\
	int (*tf)(struct ent *, void *),				\
	void *mdata)							\
{									\
	int rv;								\
	rv = __##ent##_test_data_foreach(td, tf, mdata);		\
									\
	return (rv);							\
}

#define DECLARE_2PASS_TEST(ent)						\
int __##ent##_2pass_test(struct ent##_test_data *, 			\
	struct ent##_test_data *, 					\
	int (*)(struct ent *, struct ent *, void *), void *);

#define DO_2PASS_TEST(ent, td1, td2, f, mdata)				\
	__##ent##_2pass_test(td1, td2, f, mdata)

#define IMPLEMENT_2PASS_TEST(ent)					\
int									\
__##ent##_2pass_test(struct ent##_test_data *td1,			\
	struct ent##_test_data *td2,					\
	int (*cmp_func)(struct ent *, struct ent *, void *),		\
	void *cmp_mdata)						\
{									\
	int rv;								\
									\
	rv = __##ent##_test_data_compare(td1, td2, cmp_func, cmp_mdata);	\
	return (rv);							\
}
