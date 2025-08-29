// SPDX-License-Identifier: GPL-2.0
/*
 * KFuzzTest tool for sending inputs into a KFuzzTest harness
 *
 * Copyright 2025 Google LLC
 */

#include <asm-generic/errno-base.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "byte_buffer.h"
#include "encoder.h"
#include "input_lexer.h"
#include "input_parser.h"
#include "rand_stream.h"

static int invoke_kfuzztest_target(const char *target_name, const char *data, size_t data_size)
{
	ssize_t bytes_written;
	char buf[256];
	int ret;
	int fd;

	ret = snprintf(buf, sizeof(buf), "/sys/kernel/debug/kfuzztest/%s/input", target_name);
	if (ret < 0)
		return ret;

	fd = openat(AT_FDCWD, buf, O_WRONLY, 0);
	if (fd < 0)
		return fd;

	bytes_written = write(fd, (void *)data, data_size);
	if (bytes_written < 0) {
		close(fd);
		return bytes_written;
	}

	if (close(fd) != 0)
		return 1;
	return 0;
}

static int invoke_one(const char *input_fmt, const char *fuzz_target, const char *input_filepath)
{
	struct ast_node *ast_prog;
	struct byte_buffer *bb;
	struct rand_stream *rs;
	struct token **tokens;
	size_t num_tokens;
	size_t num_bytes;
	int err;

	err = tokenize(input_fmt, &tokens, &num_tokens);
	if (err) {
		printf("tokenization failed: %s\n", strerror(-err));
		return err;
	}

	err = parse(tokens, num_tokens, &ast_prog);
	if (err) {
		printf("parsing failed: %s\n", strerror(-err));
		return err;
	}

	rs = new_rand_stream(input_filepath, 1024);
	err = encode(ast_prog, rs, &num_bytes, &bb);
	if (err) {
		printf("encoding failed: %s\n", strerror(-err));
		return err;
	}

	err = invoke_kfuzztest_target(fuzz_target, bb->buffer, num_bytes);
	if (err) {
		printf("invocation failed: %s\n", strerror(-err));
		return err;
	}
	destroy_byte_buffer(bb);
	return err;
}

int main(int argc, char *argv[])
{
	if (argc != 4) {
		printf("Usage: %s <input-description> <fuzz-target-name> <input-file>\n", argv[0]);
		printf("For more detailed information see /Documentation/dev-tools/kfuzztest.rst\n");
		return 1;
	}

	return invoke_one(argv[1], argv[2], argv[3]);
}
