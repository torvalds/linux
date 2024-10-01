// SPDX-License-Identifier: GPL-2.0
#include <kunit/test.h>

#include "protocol.h"

static struct mptcp_subflow_request_sock *build_req_sock(struct kunit *test)
{
	struct mptcp_subflow_request_sock *req;

	req = kunit_kzalloc(test, sizeof(struct mptcp_subflow_request_sock),
			    GFP_USER);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, req);
	mptcp_token_init_request((struct request_sock *)req);
	sock_net_set((struct sock *)req, &init_net);
	return req;
}

static void mptcp_token_test_req_basic(struct kunit *test)
{
	struct mptcp_subflow_request_sock *req = build_req_sock(test);
	struct mptcp_sock *null_msk = NULL;

	KUNIT_ASSERT_EQ(test, 0,
			mptcp_token_new_request((struct request_sock *)req));
	KUNIT_EXPECT_NE(test, 0, (int)req->token);
	KUNIT_EXPECT_PTR_EQ(test, null_msk, mptcp_token_get_sock(&init_net, req->token));

	/* cleanup */
	mptcp_token_destroy_request((struct request_sock *)req);
}

static struct inet_connection_sock *build_icsk(struct kunit *test)
{
	struct inet_connection_sock *icsk;

	icsk = kunit_kzalloc(test, sizeof(struct inet_connection_sock),
			     GFP_USER);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, icsk);
	return icsk;
}

static struct mptcp_subflow_context *build_ctx(struct kunit *test)
{
	struct mptcp_subflow_context *ctx;

	ctx = kunit_kzalloc(test, sizeof(struct mptcp_subflow_context),
			    GFP_USER);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, ctx);
	return ctx;
}

static struct mptcp_sock *build_msk(struct kunit *test)
{
	struct mptcp_sock *msk;
	struct sock *sk;

	msk = kunit_kzalloc(test, sizeof(struct mptcp_sock), GFP_USER);
	KUNIT_EXPECT_NOT_ERR_OR_NULL(test, msk);
	refcount_set(&((struct sock *)msk)->sk_refcnt, 1);
	sock_net_set((struct sock *)msk, &init_net);

	sk = (struct sock *)msk;

	/* be sure the token helpers can dereference sk->sk_prot */
	sk->sk_prot = &tcp_prot;
	sk->sk_protocol = IPPROTO_MPTCP;

	return msk;
}

static void mptcp_token_test_msk_basic(struct kunit *test)
{
	struct inet_connection_sock *icsk = build_icsk(test);
	struct mptcp_subflow_context *ctx = build_ctx(test);
	struct mptcp_sock *msk = build_msk(test);
	struct mptcp_sock *null_msk = NULL;
	struct sock *sk;

	rcu_assign_pointer(icsk->icsk_ulp_data, ctx);
	ctx->conn = (struct sock *)msk;
	sk = (struct sock *)msk;

	KUNIT_ASSERT_EQ(test, 0,
			mptcp_token_new_connect((struct sock *)icsk));
	KUNIT_EXPECT_NE(test, 0, (int)ctx->token);
	KUNIT_EXPECT_EQ(test, ctx->token, msk->token);
	KUNIT_EXPECT_PTR_EQ(test, msk, mptcp_token_get_sock(&init_net, ctx->token));
	KUNIT_EXPECT_EQ(test, 2, (int)refcount_read(&sk->sk_refcnt));

	mptcp_token_destroy(msk);
	KUNIT_EXPECT_PTR_EQ(test, null_msk, mptcp_token_get_sock(&init_net, ctx->token));
}

static void mptcp_token_test_accept(struct kunit *test)
{
	struct mptcp_subflow_request_sock *req = build_req_sock(test);
	struct mptcp_sock *msk = build_msk(test);

	KUNIT_ASSERT_EQ(test, 0,
			mptcp_token_new_request((struct request_sock *)req));
	msk->token = req->token;
	mptcp_token_accept(req, msk);
	KUNIT_EXPECT_PTR_EQ(test, msk, mptcp_token_get_sock(&init_net, msk->token));

	/* this is now a no-op */
	mptcp_token_destroy_request((struct request_sock *)req);
	KUNIT_EXPECT_PTR_EQ(test, msk, mptcp_token_get_sock(&init_net, msk->token));

	/* cleanup */
	mptcp_token_destroy(msk);
}

static void mptcp_token_test_destroyed(struct kunit *test)
{
	struct mptcp_subflow_request_sock *req = build_req_sock(test);
	struct mptcp_sock *msk = build_msk(test);
	struct mptcp_sock *null_msk = NULL;
	struct sock *sk;

	sk = (struct sock *)msk;

	KUNIT_ASSERT_EQ(test, 0,
			mptcp_token_new_request((struct request_sock *)req));
	msk->token = req->token;
	mptcp_token_accept(req, msk);

	/* simulate race on removal */
	refcount_set(&sk->sk_refcnt, 0);
	KUNIT_EXPECT_PTR_EQ(test, null_msk, mptcp_token_get_sock(&init_net, msk->token));

	/* cleanup */
	mptcp_token_destroy(msk);
}

static struct kunit_case mptcp_token_test_cases[] = {
	KUNIT_CASE(mptcp_token_test_req_basic),
	KUNIT_CASE(mptcp_token_test_msk_basic),
	KUNIT_CASE(mptcp_token_test_accept),
	KUNIT_CASE(mptcp_token_test_destroyed),
	{}
};

static struct kunit_suite mptcp_token_suite = {
	.name = "mptcp-token",
	.test_cases = mptcp_token_test_cases,
};

kunit_test_suite(mptcp_token_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for MPTCP Token");
