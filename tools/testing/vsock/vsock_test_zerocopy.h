/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef VSOCK_TEST_ZEROCOPY_H
#define VSOCK_TEST_ZEROCOPY_H
#include "util.h"

void test_stream_msgzcopy_client(const struct test_opts *opts);
void test_stream_msgzcopy_server(const struct test_opts *opts);

void test_seqpacket_msgzcopy_client(const struct test_opts *opts);
void test_seqpacket_msgzcopy_server(const struct test_opts *opts);

void test_stream_msgzcopy_empty_errq_client(const struct test_opts *opts);
void test_stream_msgzcopy_empty_errq_server(const struct test_opts *opts);

#endif /* VSOCK_TEST_ZEROCOPY_H */
