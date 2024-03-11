// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Oracle and/or its affiliates.
 *
 * KUnit test of the handshake upcall mechanism.
 */

#include <kunit/test.h>
#include <kunit/visibility.h>

#include <linux/kernel.h>

#include <net/sock.h>
#include <net/genetlink.h>
#include <net/netns/generic.h>

#include <uapi/linux/handshake.h>
#include "handshake.h"

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);

static int test_accept_func(struct handshake_req *req, struct genl_info *info,
			    int fd)
{
	return 0;
}

static void test_done_func(struct handshake_req *req, unsigned int status,
			   struct genl_info *info)
{
}

struct handshake_req_alloc_test_param {
	const char			*desc;
	struct handshake_proto		*proto;
	gfp_t				gfp;
	bool				expect_success;
};

static struct handshake_proto handshake_req_alloc_proto_2 = {
	.hp_handler_class	= HANDSHAKE_HANDLER_CLASS_NONE,
};

static struct handshake_proto handshake_req_alloc_proto_3 = {
	.hp_handler_class	= HANDSHAKE_HANDLER_CLASS_MAX,
};

static struct handshake_proto handshake_req_alloc_proto_4 = {
	.hp_handler_class	= HANDSHAKE_HANDLER_CLASS_TLSHD,
};

static struct handshake_proto handshake_req_alloc_proto_5 = {
	.hp_handler_class	= HANDSHAKE_HANDLER_CLASS_TLSHD,
	.hp_accept		= test_accept_func,
};

static struct handshake_proto handshake_req_alloc_proto_6 = {
	.hp_handler_class	= HANDSHAKE_HANDLER_CLASS_TLSHD,
	.hp_privsize		= UINT_MAX,
	.hp_accept		= test_accept_func,
	.hp_done		= test_done_func,
};

static struct handshake_proto handshake_req_alloc_proto_good = {
	.hp_handler_class	= HANDSHAKE_HANDLER_CLASS_TLSHD,
	.hp_accept		= test_accept_func,
	.hp_done		= test_done_func,
};

static const
struct handshake_req_alloc_test_param handshake_req_alloc_params[] = {
	{
		.desc			= "handshake_req_alloc NULL proto",
		.proto			= NULL,
		.gfp			= GFP_KERNEL,
		.expect_success		= false,
	},
	{
		.desc			= "handshake_req_alloc CLASS_NONE",
		.proto			= &handshake_req_alloc_proto_2,
		.gfp			= GFP_KERNEL,
		.expect_success		= false,
	},
	{
		.desc			= "handshake_req_alloc CLASS_MAX",
		.proto			= &handshake_req_alloc_proto_3,
		.gfp			= GFP_KERNEL,
		.expect_success		= false,
	},
	{
		.desc			= "handshake_req_alloc no callbacks",
		.proto			= &handshake_req_alloc_proto_4,
		.gfp			= GFP_KERNEL,
		.expect_success		= false,
	},
	{
		.desc			= "handshake_req_alloc no done callback",
		.proto			= &handshake_req_alloc_proto_5,
		.gfp			= GFP_KERNEL,
		.expect_success		= false,
	},
	{
		.desc			= "handshake_req_alloc excessive privsize",
		.proto			= &handshake_req_alloc_proto_6,
		.gfp			= GFP_KERNEL | __GFP_NOWARN,
		.expect_success		= false,
	},
	{
		.desc			= "handshake_req_alloc all good",
		.proto			= &handshake_req_alloc_proto_good,
		.gfp			= GFP_KERNEL,
		.expect_success		= true,
	},
};

static void
handshake_req_alloc_get_desc(const struct handshake_req_alloc_test_param *param,
			     char *desc)
{
	strscpy(desc, param->desc, KUNIT_PARAM_DESC_SIZE);
}

/* Creates the function handshake_req_alloc_gen_params */
KUNIT_ARRAY_PARAM(handshake_req_alloc, handshake_req_alloc_params,
		  handshake_req_alloc_get_desc);

static void handshake_req_alloc_case(struct kunit *test)
{
	const struct handshake_req_alloc_test_param *param = test->param_value;
	struct handshake_req *result;

	/* Arrange */

	/* Act */
	result = handshake_req_alloc(param->proto, param->gfp);

	/* Assert */
	if (param->expect_success)
		KUNIT_EXPECT_NOT_NULL(test, result);
	else
		KUNIT_EXPECT_NULL(test, result);

	kfree(result);
}

static void handshake_req_submit_test1(struct kunit *test)
{
	struct socket *sock;
	int err, result;

	/* Arrange */
	err = __sock_create(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP,
			    &sock, 1);
	KUNIT_ASSERT_EQ(test, err, 0);

	/* Act */
	result = handshake_req_submit(sock, NULL, GFP_KERNEL);

	/* Assert */
	KUNIT_EXPECT_EQ(test, result, -EINVAL);

	sock_release(sock);
}

static void handshake_req_submit_test2(struct kunit *test)
{
	struct handshake_req *req;
	int result;

	/* Arrange */
	req = handshake_req_alloc(&handshake_req_alloc_proto_good, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, req);

	/* Act */
	result = handshake_req_submit(NULL, req, GFP_KERNEL);

	/* Assert */
	KUNIT_EXPECT_EQ(test, result, -EINVAL);

	/* handshake_req_submit() destroys @req on error */
}

static void handshake_req_submit_test3(struct kunit *test)
{
	struct handshake_req *req;
	struct socket *sock;
	int err, result;

	/* Arrange */
	req = handshake_req_alloc(&handshake_req_alloc_proto_good, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, req);

	err = __sock_create(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP,
			    &sock, 1);
	KUNIT_ASSERT_EQ(test, err, 0);
	sock->file = NULL;

	/* Act */
	result = handshake_req_submit(sock, req, GFP_KERNEL);

	/* Assert */
	KUNIT_EXPECT_EQ(test, result, -EINVAL);

	/* handshake_req_submit() destroys @req on error */
	sock_release(sock);
}

static void handshake_req_submit_test4(struct kunit *test)
{
	struct handshake_req *req, *result;
	struct socket *sock;
	struct file *filp;
	int err;

	/* Arrange */
	req = handshake_req_alloc(&handshake_req_alloc_proto_good, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, req);

	err = __sock_create(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP,
			    &sock, 1);
	KUNIT_ASSERT_EQ(test, err, 0);
	filp = sock_alloc_file(sock, O_NONBLOCK, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filp);
	KUNIT_ASSERT_NOT_NULL(test, sock->sk);
	sock->file = filp;

	err = handshake_req_submit(sock, req, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);

	/* Act */
	result = handshake_req_hash_lookup(sock->sk);

	/* Assert */
	KUNIT_EXPECT_NOT_NULL(test, result);
	KUNIT_EXPECT_PTR_EQ(test, req, result);

	handshake_req_cancel(sock->sk);
	fput(filp);
}

static void handshake_req_submit_test5(struct kunit *test)
{
	struct handshake_req *req;
	struct handshake_net *hn;
	struct socket *sock;
	struct file *filp;
	struct net *net;
	int saved, err;

	/* Arrange */
	req = handshake_req_alloc(&handshake_req_alloc_proto_good, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, req);

	err = __sock_create(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP,
			    &sock, 1);
	KUNIT_ASSERT_EQ(test, err, 0);
	filp = sock_alloc_file(sock, O_NONBLOCK, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filp);
	KUNIT_ASSERT_NOT_NULL(test, sock->sk);
	sock->file = filp;

	net = sock_net(sock->sk);
	hn = handshake_pernet(net);
	KUNIT_ASSERT_NOT_NULL(test, hn);

	saved = hn->hn_pending;
	hn->hn_pending = hn->hn_pending_max + 1;

	/* Act */
	err = handshake_req_submit(sock, req, GFP_KERNEL);

	/* Assert */
	KUNIT_EXPECT_EQ(test, err, -EAGAIN);

	fput(filp);
	hn->hn_pending = saved;
}

static void handshake_req_submit_test6(struct kunit *test)
{
	struct handshake_req *req1, *req2;
	struct socket *sock;
	struct file *filp;
	int err;

	/* Arrange */
	req1 = handshake_req_alloc(&handshake_req_alloc_proto_good, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, req1);
	req2 = handshake_req_alloc(&handshake_req_alloc_proto_good, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, req2);

	err = __sock_create(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP,
			    &sock, 1);
	KUNIT_ASSERT_EQ(test, err, 0);
	filp = sock_alloc_file(sock, O_NONBLOCK, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filp);
	KUNIT_ASSERT_NOT_NULL(test, sock->sk);
	sock->file = filp;

	/* Act */
	err = handshake_req_submit(sock, req1, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);
	err = handshake_req_submit(sock, req2, GFP_KERNEL);

	/* Assert */
	KUNIT_EXPECT_EQ(test, err, -EBUSY);

	handshake_req_cancel(sock->sk);
	fput(filp);
}

static void handshake_req_cancel_test1(struct kunit *test)
{
	struct handshake_req *req;
	struct socket *sock;
	struct file *filp;
	bool result;
	int err;

	/* Arrange */
	req = handshake_req_alloc(&handshake_req_alloc_proto_good, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, req);

	err = __sock_create(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP,
			    &sock, 1);
	KUNIT_ASSERT_EQ(test, err, 0);

	filp = sock_alloc_file(sock, O_NONBLOCK, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filp);
	sock->file = filp;

	err = handshake_req_submit(sock, req, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);

	/* NB: handshake_req hasn't been accepted */

	/* Act */
	result = handshake_req_cancel(sock->sk);

	/* Assert */
	KUNIT_EXPECT_TRUE(test, result);

	fput(filp);
}

static void handshake_req_cancel_test2(struct kunit *test)
{
	struct handshake_req *req, *next;
	struct handshake_net *hn;
	struct socket *sock;
	struct file *filp;
	struct net *net;
	bool result;
	int err;

	/* Arrange */
	req = handshake_req_alloc(&handshake_req_alloc_proto_good, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, req);

	err = __sock_create(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP,
			    &sock, 1);
	KUNIT_ASSERT_EQ(test, err, 0);

	filp = sock_alloc_file(sock, O_NONBLOCK, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filp);
	sock->file = filp;

	err = handshake_req_submit(sock, req, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);

	net = sock_net(sock->sk);
	hn = handshake_pernet(net);
	KUNIT_ASSERT_NOT_NULL(test, hn);

	/* Pretend to accept this request */
	next = handshake_req_next(hn, HANDSHAKE_HANDLER_CLASS_TLSHD);
	KUNIT_ASSERT_PTR_EQ(test, req, next);

	/* Act */
	result = handshake_req_cancel(sock->sk);

	/* Assert */
	KUNIT_EXPECT_TRUE(test, result);

	fput(filp);
}

static void handshake_req_cancel_test3(struct kunit *test)
{
	struct handshake_req *req, *next;
	struct handshake_net *hn;
	struct socket *sock;
	struct file *filp;
	struct net *net;
	bool result;
	int err;

	/* Arrange */
	req = handshake_req_alloc(&handshake_req_alloc_proto_good, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, req);

	err = __sock_create(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP,
			    &sock, 1);
	KUNIT_ASSERT_EQ(test, err, 0);

	filp = sock_alloc_file(sock, O_NONBLOCK, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filp);
	sock->file = filp;

	err = handshake_req_submit(sock, req, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);

	net = sock_net(sock->sk);
	hn = handshake_pernet(net);
	KUNIT_ASSERT_NOT_NULL(test, hn);

	/* Pretend to accept this request */
	next = handshake_req_next(hn, HANDSHAKE_HANDLER_CLASS_TLSHD);
	KUNIT_ASSERT_PTR_EQ(test, req, next);

	/* Pretend to complete this request */
	handshake_complete(next, -ETIMEDOUT, NULL);

	/* Act */
	result = handshake_req_cancel(sock->sk);

	/* Assert */
	KUNIT_EXPECT_FALSE(test, result);

	fput(filp);
}

static struct handshake_req *handshake_req_destroy_test;

static void test_destroy_func(struct handshake_req *req)
{
	handshake_req_destroy_test = req;
}

static struct handshake_proto handshake_req_alloc_proto_destroy = {
	.hp_handler_class	= HANDSHAKE_HANDLER_CLASS_TLSHD,
	.hp_accept		= test_accept_func,
	.hp_done		= test_done_func,
	.hp_destroy		= test_destroy_func,
};

static void handshake_req_destroy_test1(struct kunit *test)
{
	struct handshake_req *req;
	struct socket *sock;
	struct file *filp;
	int err;

	/* Arrange */
	handshake_req_destroy_test = NULL;

	req = handshake_req_alloc(&handshake_req_alloc_proto_destroy, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, req);

	err = __sock_create(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP,
			    &sock, 1);
	KUNIT_ASSERT_EQ(test, err, 0);

	filp = sock_alloc_file(sock, O_NONBLOCK, NULL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, filp);
	sock->file = filp;

	err = handshake_req_submit(sock, req, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, err, 0);

	handshake_req_cancel(sock->sk);

	/* Act */
	/* Ensure the close/release/put process has run to
	 * completion before checking the result.
	 */
	__fput_sync(filp);

	/* Assert */
	KUNIT_EXPECT_PTR_EQ(test, handshake_req_destroy_test, req);
}

static struct kunit_case handshake_api_test_cases[] = {
	{
		.name			= "req_alloc API fuzzing",
		.run_case		= handshake_req_alloc_case,
		.generate_params	= handshake_req_alloc_gen_params,
	},
	{
		.name			= "req_submit NULL req arg",
		.run_case		= handshake_req_submit_test1,
	},
	{
		.name			= "req_submit NULL sock arg",
		.run_case		= handshake_req_submit_test2,
	},
	{
		.name			= "req_submit NULL sock->file",
		.run_case		= handshake_req_submit_test3,
	},
	{
		.name			= "req_lookup works",
		.run_case		= handshake_req_submit_test4,
	},
	{
		.name			= "req_submit max pending",
		.run_case		= handshake_req_submit_test5,
	},
	{
		.name			= "req_submit multiple",
		.run_case		= handshake_req_submit_test6,
	},
	{
		.name			= "req_cancel before accept",
		.run_case		= handshake_req_cancel_test1,
	},
	{
		.name			= "req_cancel after accept",
		.run_case		= handshake_req_cancel_test2,
	},
	{
		.name			= "req_cancel after done",
		.run_case		= handshake_req_cancel_test3,
	},
	{
		.name			= "req_destroy works",
		.run_case		= handshake_req_destroy_test1,
	},
	{}
};

static struct kunit_suite handshake_api_suite = {
       .name                   = "Handshake API tests",
       .test_cases             = handshake_api_test_cases,
};

kunit_test_suites(&handshake_api_suite);

MODULE_DESCRIPTION("Test handshake upcall API functions");
MODULE_LICENSE("GPL");
