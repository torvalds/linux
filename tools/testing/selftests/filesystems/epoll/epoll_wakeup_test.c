// SPDX-License-Identifier: GPL-2.0

#define _GNU_SOURCE
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/eventfd.h>
#include "../../kselftest_harness.h"

struct epoll_mtcontext
{
	int efd[3];
	int sfd[4];
	volatile int count;

	pthread_t main;
	pthread_t waiter;
};

static void signal_handler(int signum)
{
}

static void kill_timeout(struct epoll_mtcontext *ctx)
{
	usleep(1000000);
	pthread_kill(ctx->main, SIGUSR1);
	pthread_kill(ctx->waiter, SIGUSR1);
}

static void *waiter_entry1a(void *data)
{
	struct epoll_event e;
	struct epoll_mtcontext *ctx = data;

	if (epoll_wait(ctx->efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx->count, 1);

	return NULL;
}

static void *waiter_entry1ap(void *data)
{
	struct pollfd pfd;
	struct epoll_event e;
	struct epoll_mtcontext *ctx = data;

	pfd.fd = ctx->efd[0];
	pfd.events = POLLIN;
	if (poll(&pfd, 1, -1) > 0) {
		if (epoll_wait(ctx->efd[0], &e, 1, 0) > 0)
			__sync_fetch_and_add(&ctx->count, 1);
	}

	return NULL;
}

static void *waiter_entry1o(void *data)
{
	struct epoll_event e;
	struct epoll_mtcontext *ctx = data;

	if (epoll_wait(ctx->efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_or(&ctx->count, 1);

	return NULL;
}

static void *waiter_entry1op(void *data)
{
	struct pollfd pfd;
	struct epoll_event e;
	struct epoll_mtcontext *ctx = data;

	pfd.fd = ctx->efd[0];
	pfd.events = POLLIN;
	if (poll(&pfd, 1, -1) > 0) {
		if (epoll_wait(ctx->efd[0], &e, 1, 0) > 0)
			__sync_fetch_and_or(&ctx->count, 1);
	}

	return NULL;
}

static void *waiter_entry2a(void *data)
{
	struct epoll_event events[2];
	struct epoll_mtcontext *ctx = data;

	if (epoll_wait(ctx->efd[0], events, 2, -1) > 0)
		__sync_fetch_and_add(&ctx->count, 1);

	return NULL;
}

static void *waiter_entry2ap(void *data)
{
	struct pollfd pfd;
	struct epoll_event events[2];
	struct epoll_mtcontext *ctx = data;

	pfd.fd = ctx->efd[0];
	pfd.events = POLLIN;
	if (poll(&pfd, 1, -1) > 0) {
		if (epoll_wait(ctx->efd[0], events, 2, 0) > 0)
			__sync_fetch_and_add(&ctx->count, 1);
	}

	return NULL;
}

static void *emitter_entry1(void *data)
{
	struct epoll_mtcontext *ctx = data;

	usleep(100000);
	write(ctx->sfd[1], "w", 1);

	kill_timeout(ctx);

	return NULL;
}

static void *emitter_entry2(void *data)
{
	struct epoll_mtcontext *ctx = data;

	usleep(100000);
	write(ctx->sfd[1], "w", 1);
	write(ctx->sfd[3], "w", 1);

	kill_timeout(ctx);

	return NULL;
}

/*
 *          t0
 *           | (ew)
 *          e0
 *           | (lt)
 *          s0
 */
TEST(epoll1)
{
	int efd;
	int sfd[2];
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sfd), 0);

	efd = epoll_create(1);
	ASSERT_GE(efd, 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[0], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	EXPECT_EQ(epoll_wait(efd, &e, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd, &e, 1, 0), 1);

	close(efd);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *          t0
 *           | (ew)
 *          e0
 *           | (et)
 *          s0
 */
TEST(epoll2)
{
	int efd;
	int sfd[2];
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sfd), 0);

	efd = epoll_create(1);
	ASSERT_GE(efd, 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[0], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	EXPECT_EQ(epoll_wait(efd, &e, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd, &e, 1, 0), 0);

	close(efd);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *           t0
 *            | (ew)
 *           e0
 *     (lt) /  \ (lt)
 *        s0    s2
 */
TEST(epoll3)
{
	int efd;
	int sfd[4];
	struct epoll_event events[2];

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[2]), 0);

	efd = epoll_create(1);
	ASSERT_GE(efd, 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[0], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[2], events), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);
	ASSERT_EQ(write(sfd[3], "w", 1), 1);

	EXPECT_EQ(epoll_wait(efd, events, 2, 0), 2);
	EXPECT_EQ(epoll_wait(efd, events, 2, 0), 2);

	close(efd);
	close(sfd[0]);
	close(sfd[1]);
	close(sfd[2]);
	close(sfd[3]);
}

/*
 *           t0
 *            | (ew)
 *           e0
 *     (et) /  \ (et)
 *        s0    s2
 */
TEST(epoll4)
{
	int efd;
	int sfd[4];
	struct epoll_event events[2];

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[2]), 0);

	efd = epoll_create(1);
	ASSERT_GE(efd, 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[0], events), 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[2], events), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);
	ASSERT_EQ(write(sfd[3], "w", 1), 1);

	EXPECT_EQ(epoll_wait(efd, events, 2, 0), 2);
	EXPECT_EQ(epoll_wait(efd, events, 2, 0), 0);

	close(efd);
	close(sfd[0]);
	close(sfd[1]);
	close(sfd[2]);
	close(sfd[3]);
}

/*
 *          t0
 *           | (p)
 *          e0
 *           | (lt)
 *          s0
 */
TEST(epoll5)
{
	int efd;
	int sfd[2];
	struct pollfd pfd;
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[0]), 0);

	efd = epoll_create(1);
	ASSERT_GE(efd, 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[0], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	pfd.fd = efd;
	pfd.events = POLLIN;
	ASSERT_EQ(poll(&pfd, 1, 0), 1);
	ASSERT_EQ(epoll_wait(efd, &e, 1, 0), 1);

	pfd.fd = efd;
	pfd.events = POLLIN;
	ASSERT_EQ(poll(&pfd, 1, 0), 1);
	ASSERT_EQ(epoll_wait(efd, &e, 1, 0), 1);

	close(efd);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *          t0
 *           | (p)
 *          e0
 *           | (et)
 *          s0
 */
TEST(epoll6)
{
	int efd;
	int sfd[2];
	struct pollfd pfd;
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[0]), 0);

	efd = epoll_create(1);
	ASSERT_GE(efd, 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[0], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	pfd.fd = efd;
	pfd.events = POLLIN;
	ASSERT_EQ(poll(&pfd, 1, 0), 1);
	ASSERT_EQ(epoll_wait(efd, &e, 1, 0), 1);

	pfd.fd = efd;
	pfd.events = POLLIN;
	ASSERT_EQ(poll(&pfd, 1, 0), 0);
	ASSERT_EQ(epoll_wait(efd, &e, 1, 0), 0);

	close(efd);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *           t0
 *            | (p)
 *           e0
 *     (lt) /  \ (lt)
 *        s0    s2
 */

TEST(epoll7)
{
	int efd;
	int sfd[4];
	struct pollfd pfd;
	struct epoll_event events[2];

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[2]), 0);

	efd = epoll_create(1);
	ASSERT_GE(efd, 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[0], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[2], events), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);
	ASSERT_EQ(write(sfd[3], "w", 1), 1);

	pfd.fd = efd;
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd, events, 2, 0), 2);

	pfd.fd = efd;
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd, events, 2, 0), 2);

	close(efd);
	close(sfd[0]);
	close(sfd[1]);
	close(sfd[2]);
	close(sfd[3]);
}

/*
 *           t0
 *            | (p)
 *           e0
 *     (et) /  \ (et)
 *        s0    s2
 */
TEST(epoll8)
{
	int efd;
	int sfd[4];
	struct pollfd pfd;
	struct epoll_event events[2];

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[2]), 0);

	efd = epoll_create(1);
	ASSERT_GE(efd, 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[0], events), 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd, EPOLL_CTL_ADD, sfd[2], events), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);
	ASSERT_EQ(write(sfd[3], "w", 1), 1);

	pfd.fd = efd;
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd, events, 2, 0), 2);

	pfd.fd = efd;
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 0);
	EXPECT_EQ(epoll_wait(efd, events, 2, 0), 0);

	close(efd);
	close(sfd[0]);
	close(sfd[1]);
	close(sfd[2]);
	close(sfd[3]);
}

/*
 *        t0    t1
 *     (ew) \  / (ew)
 *           e0
 *            | (lt)
 *           s0
 */
TEST(epoll9)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (ew)
 *           e0
 *            | (et)
 *           s0
 */
TEST(epoll10)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 1);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (ew)
 *           e0
 *     (lt) /  \ (lt)
 *        s0    s2
 */
TEST(epoll11)
{
	pthread_t emitter;
	struct epoll_event events[2];
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[2]), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[0], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[2], events), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry2a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry2, &ctx), 0);

	if (epoll_wait(ctx.efd[0], events, 2, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
	close(ctx.sfd[2]);
	close(ctx.sfd[3]);
}

/*
 *        t0    t1
 *     (ew) \  / (ew)
 *           e0
 *     (et) /  \ (et)
 *        s0    s2
 */
TEST(epoll12)
{
	pthread_t emitter;
	struct epoll_event events[2];
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[2]), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[0], events), 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[2], events), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry2, &ctx), 0);

	if (epoll_wait(ctx.efd[0], events, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
	close(ctx.sfd[2]);
	close(ctx.sfd[3]);
}

/*
 *        t0    t1
 *     (ew) \  / (p)
 *           e0
 *            | (lt)
 *           s0
 */
TEST(epoll13)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (p)
 *           e0
 *            | (et)
 *           s0
 */
TEST(epoll14)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 1);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (p)
 *           e0
 *     (lt) /  \ (lt)
 *        s0    s2
 */
TEST(epoll15)
{
	pthread_t emitter;
	struct epoll_event events[2];
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[2]), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[0], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[2], events), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry2ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry2, &ctx), 0);

	if (epoll_wait(ctx.efd[0], events, 2, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
	close(ctx.sfd[2]);
	close(ctx.sfd[3]);
}

/*
 *        t0    t1
 *     (ew) \  / (p)
 *           e0
 *     (et) /  \ (et)
 *        s0    s2
 */
TEST(epoll16)
{
	pthread_t emitter;
	struct epoll_event events[2];
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[2]), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[0], events), 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[2], events), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry2, &ctx), 0);

	if (epoll_wait(ctx.efd[0], events, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
	close(ctx.sfd[2]);
	close(ctx.sfd[3]);
}

/*
 *          t0
 *           | (ew)
 *          e0
 *           | (lt)
 *          e1
 *           | (lt)
 *          s0
 */
TEST(epoll17)
{
	int efd[2];
	int sfd[2];
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sfd), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);

	close(efd[0]);
	close(efd[1]);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *          t0
 *           | (ew)
 *          e0
 *           | (lt)
 *          e1
 *           | (et)
 *          s0
 */
TEST(epoll18)
{
	int efd[2];
	int sfd[2];
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sfd), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);

	close(efd[0]);
	close(efd[1]);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *           t0
 *            | (ew)
 *           e0
 *            | (et)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll19)
{
	int efd[2];
	int sfd[2];
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sfd), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 0);

	close(efd[0]);
	close(efd[1]);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *           t0
 *            | (ew)
 *           e0
 *            | (et)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll20)
{
	int efd[2];
	int sfd[2];
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sfd), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 0);

	close(efd[0]);
	close(efd[1]);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *          t0
 *           | (p)
 *          e0
 *           | (lt)
 *          e1
 *           | (lt)
 *          s0
 */
TEST(epoll21)
{
	int efd[2];
	int sfd[2];
	struct pollfd pfd;
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sfd), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);

	close(efd[0]);
	close(efd[1]);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *          t0
 *           | (p)
 *          e0
 *           | (lt)
 *          e1
 *           | (et)
 *          s0
 */
TEST(epoll22)
{
	int efd[2];
	int sfd[2];
	struct pollfd pfd;
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sfd), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);

	close(efd[0]);
	close(efd[1]);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *          t0
 *           | (p)
 *          e0
 *           | (et)
 *          e1
 *           | (lt)
 *          s0
 */
TEST(epoll23)
{
	int efd[2];
	int sfd[2];
	struct pollfd pfd;
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sfd), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 0);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 0);

	close(efd[0]);
	close(efd[1]);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *          t0
 *           | (p)
 *          e0
 *           | (et)
 *          e1
 *           | (et)
 *          s0
 */
TEST(epoll24)
{
	int efd[2];
	int sfd[2];
	struct pollfd pfd;
	struct epoll_event e;

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sfd), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], &e), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 1);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 0);
	EXPECT_EQ(epoll_wait(efd[0], &e, 1, 0), 0);

	close(efd[0]);
	close(efd[1]);
	close(sfd[0]);
	close(sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (ew)
 *           e0
 *            | (lt)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll25)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (ew)
 *           e0
 *            | (lt)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll26)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (ew)
 *           e0
 *            | (et)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll27)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 1);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (ew)
 *           e0
 *            | (et)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll28)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 1);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (p)
 *           e0
 *            | (lt)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll29)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (p)
 *           e0
 *            | (lt)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll30)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (p)
 *           e0
 *            | (et)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll31)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 1);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0    t1
 *     (ew) \  / (p)
 *           e0
 *            | (et)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll32)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 1);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *    (ew) |    | (ew)
 *         |   e0
 *          \  / (lt)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll33)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[1], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *    (ew) |    | (ew)
 *         |   e0
 *          \  / (lt)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll34)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1o, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[1], &e, 1, -1) > 0)
		__sync_fetch_and_or(&ctx.count, 2);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_TRUE((ctx.count == 2) || (ctx.count == 3));

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *    (ew) |    | (ew)
 *         |   e0
 *          \  / (et)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll35)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[1], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *    (ew) |    | (ew)
 *         |   e0
 *          \  / (et)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll36)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1o, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[1], &e, 1, -1) > 0)
		__sync_fetch_and_or(&ctx.count, 2);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_TRUE((ctx.count == 2) || (ctx.count == 3));

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *     (p) |    | (ew)
 *         |   e0
 *          \  / (lt)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll37)
{
	pthread_t emitter;
	struct pollfd pfd;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	pfd.fd = ctx.efd[1];
	pfd.events = POLLIN;
	if (poll(&pfd, 1, -1) > 0) {
		if (epoll_wait(ctx.efd[1], &e, 1, 0) > 0)
			__sync_fetch_and_add(&ctx.count, 1);
	}

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *     (p) |    | (ew)
 *         |   e0
 *          \  / (lt)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll38)
{
	pthread_t emitter;
	struct pollfd pfd;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1o, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	pfd.fd = ctx.efd[1];
	pfd.events = POLLIN;
	if (poll(&pfd, 1, -1) > 0) {
		if (epoll_wait(ctx.efd[1], &e, 1, 0) > 0)
			__sync_fetch_and_or(&ctx.count, 2);
	}

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_TRUE((ctx.count == 2) || (ctx.count == 3));

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *     (p) |    | (ew)
 *         |   e0
 *          \  / (et)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll39)
{
	pthread_t emitter;
	struct pollfd pfd;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	pfd.fd = ctx.efd[1];
	pfd.events = POLLIN;
	if (poll(&pfd, 1, -1) > 0) {
		if (epoll_wait(ctx.efd[1], &e, 1, 0) > 0)
			__sync_fetch_and_add(&ctx.count, 1);
	}

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *     (p) |    | (ew)
 *         |   e0
 *          \  / (et)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll40)
{
	pthread_t emitter;
	struct pollfd pfd;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1o, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	pfd.fd = ctx.efd[1];
	pfd.events = POLLIN;
	if (poll(&pfd, 1, -1) > 0) {
		if (epoll_wait(ctx.efd[1], &e, 1, 0) > 0)
			__sync_fetch_and_or(&ctx.count, 2);
	}

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_TRUE((ctx.count == 2) || (ctx.count == 3));

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *    (ew) |    | (p)
 *         |   e0
 *          \  / (lt)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll41)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[1], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *    (ew) |    | (p)
 *         |   e0
 *          \  / (lt)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll42)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1op, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[1], &e, 1, -1) > 0)
		__sync_fetch_and_or(&ctx.count, 2);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_TRUE((ctx.count == 2) || (ctx.count == 3));

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *    (ew) |    | (p)
 *         |   e0
 *          \  / (et)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll43)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[1], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *    (ew) |    | (p)
 *         |   e0
 *          \  / (et)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll44)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1op, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[1], &e, 1, -1) > 0)
		__sync_fetch_and_or(&ctx.count, 2);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_TRUE((ctx.count == 2) || (ctx.count == 3));

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *     (p) |    | (p)
 *         |   e0
 *          \  / (lt)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll45)
{
	pthread_t emitter;
	struct pollfd pfd;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	pfd.fd = ctx.efd[1];
	pfd.events = POLLIN;
	if (poll(&pfd, 1, -1) > 0) {
		if (epoll_wait(ctx.efd[1], &e, 1, 0) > 0)
			__sync_fetch_and_add(&ctx.count, 1);
	}

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *     (p) |    | (p)
 *         |   e0
 *          \  / (lt)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll46)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1op, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[1], &e, 1, -1) > 0)
		__sync_fetch_and_or(&ctx.count, 2);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_TRUE((ctx.count == 2) || (ctx.count == 3));

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *     (p) |    | (p)
 *         |   e0
 *          \  / (et)
 *           e1
 *            | (lt)
 *           s0
 */
TEST(epoll47)
{
	pthread_t emitter;
	struct pollfd pfd;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	pfd.fd = ctx.efd[1];
	pfd.events = POLLIN;
	if (poll(&pfd, 1, -1) > 0) {
		if (epoll_wait(ctx.efd[1], &e, 1, 0) > 0)
			__sync_fetch_and_add(&ctx.count, 1);
	}

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *        t0   t1
 *     (p) |    | (p)
 *         |   e0
 *          \  / (et)
 *           e1
 *            | (et)
 *           s0
 */
TEST(epoll48)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, ctx.sfd), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1op, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry1, &ctx), 0);

	if (epoll_wait(ctx.efd[1], &e, 1, -1) > 0)
		__sync_fetch_and_or(&ctx.count, 2);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_TRUE((ctx.count == 2) || (ctx.count == 3));

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
}

/*
 *           t0
 *            | (ew)
 *           e0
 *     (lt) /  \ (lt)
 *        e1    e2
 *    (lt) |     | (lt)
 *        s0    s2
 */
TEST(epoll49)
{
	int efd[3];
	int sfd[4];
	struct epoll_event events[2];

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[2]), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	efd[2] = epoll_create(1);
	ASSERT_GE(efd[2], 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[2], EPOLL_CTL_ADD, sfd[2], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[2], events), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);
	ASSERT_EQ(write(sfd[3], "w", 1), 1);

	EXPECT_EQ(epoll_wait(efd[0], events, 2, 0), 2);
	EXPECT_EQ(epoll_wait(efd[0], events, 2, 0), 2);

	close(efd[0]);
	close(efd[1]);
	close(efd[2]);
	close(sfd[0]);
	close(sfd[1]);
	close(sfd[2]);
	close(sfd[3]);
}

/*
 *           t0
 *            | (ew)
 *           e0
 *     (et) /  \ (et)
 *        e1    e2
 *    (lt) |     | (lt)
 *        s0    s2
 */
TEST(epoll50)
{
	int efd[3];
	int sfd[4];
	struct epoll_event events[2];

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[2]), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	efd[2] = epoll_create(1);
	ASSERT_GE(efd[2], 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[2], EPOLL_CTL_ADD, sfd[2], events), 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], events), 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[2], events), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);
	ASSERT_EQ(write(sfd[3], "w", 1), 1);

	EXPECT_EQ(epoll_wait(efd[0], events, 2, 0), 2);
	EXPECT_EQ(epoll_wait(efd[0], events, 2, 0), 0);

	close(efd[0]);
	close(efd[1]);
	close(efd[2]);
	close(sfd[0]);
	close(sfd[1]);
	close(sfd[2]);
	close(sfd[3]);
}

/*
 *           t0
 *            | (p)
 *           e0
 *     (lt) /  \ (lt)
 *        e1    e2
 *    (lt) |     | (lt)
 *        s0    s2
 */
TEST(epoll51)
{
	int efd[3];
	int sfd[4];
	struct pollfd pfd;
	struct epoll_event events[2];

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[2]), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	efd[2] = epoll_create(1);
	ASSERT_GE(efd[2], 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[2], EPOLL_CTL_ADD, sfd[2], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[2], events), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);
	ASSERT_EQ(write(sfd[3], "w", 1), 1);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], events, 2, 0), 2);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], events, 2, 0), 2);

	close(efd[0]);
	close(efd[1]);
	close(efd[2]);
	close(sfd[0]);
	close(sfd[1]);
	close(sfd[2]);
	close(sfd[3]);
}

/*
 *           t0
 *            | (p)
 *           e0
 *     (et) /  \ (et)
 *        e1    e2
 *    (lt) |     | (lt)
 *        s0    s2
 */
TEST(epoll52)
{
	int efd[3];
	int sfd[4];
	struct pollfd pfd;
	struct epoll_event events[2];

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &sfd[2]), 0);

	efd[0] = epoll_create(1);
	ASSERT_GE(efd[0], 0);

	efd[1] = epoll_create(1);
	ASSERT_GE(efd[1], 0);

	efd[2] = epoll_create(1);
	ASSERT_GE(efd[2], 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[1], EPOLL_CTL_ADD, sfd[0], events), 0);

	events[0].events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(efd[2], EPOLL_CTL_ADD, sfd[2], events), 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[1], events), 0);

	events[0].events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(efd[0], EPOLL_CTL_ADD, efd[2], events), 0);

	ASSERT_EQ(write(sfd[1], "w", 1), 1);
	ASSERT_EQ(write(sfd[3], "w", 1), 1);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 1);
	EXPECT_EQ(epoll_wait(efd[0], events, 2, 0), 2);

	pfd.fd = efd[0];
	pfd.events = POLLIN;
	EXPECT_EQ(poll(&pfd, 1, 0), 0);
	EXPECT_EQ(epoll_wait(efd[0], events, 2, 0), 0);

	close(efd[0]);
	close(efd[1]);
	close(efd[2]);
	close(sfd[0]);
	close(sfd[1]);
	close(sfd[2]);
	close(sfd[3]);
}

/*
 *        t0    t1
 *     (ew) \  / (ew)
 *           e0
 *     (lt) /  \ (lt)
 *        e1    e2
 *    (lt) |     | (lt)
 *        s0    s2
 */
TEST(epoll53)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[2]), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	ctx.efd[2] = epoll_create(1);
	ASSERT_GE(ctx.efd[2], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[2], EPOLL_CTL_ADD, ctx.sfd[2], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[2], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry2, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.efd[2]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
	close(ctx.sfd[2]);
	close(ctx.sfd[3]);
}

/*
 *        t0    t1
 *     (ew) \  / (ew)
 *           e0
 *     (et) /  \ (et)
 *        e1    e2
 *    (lt) |     | (lt)
 *        s0    s2
 */
TEST(epoll54)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[2]), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	ctx.efd[2] = epoll_create(1);
	ASSERT_GE(ctx.efd[2], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[2], EPOLL_CTL_ADD, ctx.sfd[2], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[2], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1a, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry2, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.efd[2]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
	close(ctx.sfd[2]);
	close(ctx.sfd[3]);
}

/*
 *        t0    t1
 *     (ew) \  / (p)
 *           e0
 *     (lt) /  \ (lt)
 *        e1    e2
 *    (lt) |     | (lt)
 *        s0    s2
 */
TEST(epoll55)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[2]), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	ctx.efd[2] = epoll_create(1);
	ASSERT_GE(ctx.efd[2], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[2], EPOLL_CTL_ADD, ctx.sfd[2], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[2], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry2, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.efd[2]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
	close(ctx.sfd[2]);
	close(ctx.sfd[3]);
}

/*
 *        t0    t1
 *     (ew) \  / (p)
 *           e0
 *     (et) /  \ (et)
 *        e1    e2
 *    (lt) |     | (lt)
 *        s0    s2
 */
TEST(epoll56)
{
	pthread_t emitter;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[2]), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	ctx.efd[2] = epoll_create(1);
	ASSERT_GE(ctx.efd[2], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[2], EPOLL_CTL_ADD, ctx.sfd[2], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[2], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry2, &ctx), 0);

	if (epoll_wait(ctx.efd[0], &e, 1, -1) > 0)
		__sync_fetch_and_add(&ctx.count, 1);

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.efd[2]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
	close(ctx.sfd[2]);
	close(ctx.sfd[3]);
}

/*
 *        t0    t1
 *      (p) \  / (p)
 *           e0
 *     (lt) /  \ (lt)
 *        e1    e2
 *    (lt) |     | (lt)
 *        s0    s2
 */
TEST(epoll57)
{
	pthread_t emitter;
	struct pollfd pfd;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[2]), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	ctx.efd[2] = epoll_create(1);
	ASSERT_GE(ctx.efd[2], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[2], EPOLL_CTL_ADD, ctx.sfd[2], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[2], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry2, &ctx), 0);

	pfd.fd = ctx.efd[0];
	pfd.events = POLLIN;
	if (poll(&pfd, 1, -1) > 0) {
		if (epoll_wait(ctx.efd[0], &e, 1, 0) > 0)
			__sync_fetch_and_add(&ctx.count, 1);
	}

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.efd[2]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
	close(ctx.sfd[2]);
	close(ctx.sfd[3]);
}

/*
 *        t0    t1
 *      (p) \  / (p)
 *           e0
 *     (et) /  \ (et)
 *        e1    e2
 *    (lt) |     | (lt)
 *        s0    s2
 */
TEST(epoll58)
{
	pthread_t emitter;
	struct pollfd pfd;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };

	signal(SIGUSR1, signal_handler);

	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[0]), 0);
	ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, &ctx.sfd[2]), 0);

	ctx.efd[0] = epoll_create(1);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.efd[1] = epoll_create(1);
	ASSERT_GE(ctx.efd[1], 0);

	ctx.efd[2] = epoll_create(1);
	ASSERT_GE(ctx.efd[2], 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[1], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	e.events = EPOLLIN;
	ASSERT_EQ(epoll_ctl(ctx.efd[2], EPOLL_CTL_ADD, ctx.sfd[2], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[1], &e), 0);

	e.events = EPOLLIN | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.efd[2], &e), 0);

	ctx.main = pthread_self();
	ASSERT_EQ(pthread_create(&ctx.waiter, NULL, waiter_entry1ap, &ctx), 0);
	ASSERT_EQ(pthread_create(&emitter, NULL, emitter_entry2, &ctx), 0);

	pfd.fd = ctx.efd[0];
	pfd.events = POLLIN;
	if (poll(&pfd, 1, -1) > 0) {
		if (epoll_wait(ctx.efd[0], &e, 1, 0) > 0)
			__sync_fetch_and_add(&ctx.count, 1);
	}

	ASSERT_EQ(pthread_join(ctx.waiter, NULL), 0);
	EXPECT_EQ(ctx.count, 2);

	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}

	close(ctx.efd[0]);
	close(ctx.efd[1]);
	close(ctx.efd[2]);
	close(ctx.sfd[0]);
	close(ctx.sfd[1]);
	close(ctx.sfd[2]);
	close(ctx.sfd[3]);
}

static void *epoll59_thread(void *ctx_)
{
	struct epoll_mtcontext *ctx = ctx_;
	struct epoll_event e;
	int i;

	for (i = 0; i < 100000; i++) {
		while (ctx->count == 0)
			;

		e.events = EPOLLIN | EPOLLERR | EPOLLET;
		epoll_ctl(ctx->efd[0], EPOLL_CTL_MOD, ctx->sfd[0], &e);
		ctx->count = 0;
	}

	return NULL;
}

/*
 *        t0
 *      (p) \
 *           e0
 *     (et) /
 *        e0
 *
 * Based on https://bugzilla.kernel.org/show_bug.cgi?id=205933
 */
TEST(epoll59)
{
	pthread_t emitter;
	struct pollfd pfd;
	struct epoll_event e;
	struct epoll_mtcontext ctx = { 0 };
	int i, ret;

	signal(SIGUSR1, signal_handler);

	ctx.efd[0] = epoll_create1(0);
	ASSERT_GE(ctx.efd[0], 0);

	ctx.sfd[0] = eventfd(1, 0);
	ASSERT_GE(ctx.sfd[0], 0);

	e.events = EPOLLIN | EPOLLERR | EPOLLET;
	ASSERT_EQ(epoll_ctl(ctx.efd[0], EPOLL_CTL_ADD, ctx.sfd[0], &e), 0);

	ASSERT_EQ(pthread_create(&emitter, NULL, epoll59_thread, &ctx), 0);

	for (i = 0; i < 100000; i++) {
		ret = epoll_wait(ctx.efd[0], &e, 1, 1000);
		ASSERT_GT(ret, 0);

		while (ctx.count != 0)
			;
		ctx.count = 1;
	}
	if (pthread_tryjoin_np(emitter, NULL) < 0) {
		pthread_kill(emitter, SIGUSR1);
		pthread_join(emitter, NULL);
	}
	close(ctx.efd[0]);
	close(ctx.sfd[0]);
}

TEST_HARNESS_MAIN
