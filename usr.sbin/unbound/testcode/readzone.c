/*
 * testcode/readzone.c - readzone tool reads zonefiles
 *
 * Copyright (c) 2021, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/**
 * \file
 * Command to read and echo a zonefile.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "sldns/str2wire.h"
#include "sldns/wire2str.h"

int print_usage(FILE *out, const char *progname)
{
	fprintf(out, "usage: %s [ -u ] <zonefile> [<origin>]\n", progname);
	fprintf(out, "\t-u\tprint in unknown type (RFC3597) format\n");
	return out == stdout ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char *const *argv)
{
	char *progname = argv[0];
	uint8_t rr[LDNS_RR_BUF_SIZE];
	char *str = malloc(1024 * 1024);
	size_t str_len = sizeof(str);
	struct sldns_file_parse_state state;
	FILE *in = NULL;
	int s = -1;
	int opt;
	int print_in_unknown_type_format = 0;

	while ((opt = getopt(argc, argv, "hu")) != -1) {
		switch (opt) {
		case 'h':
			free(str);
			return print_usage(stdout, progname);
		case 'u':
			print_in_unknown_type_format = 1;
			break;
		default:
			free(str);
			return print_usage(stderr, progname);
		}
	}
	argc -= optind;
	argv += optind;

	memset(&state, 0, sizeof(state));
	state.default_ttl = 3600;
	state.lineno = 1;
	if (argc == 2) {
		state.origin_len = sizeof(state.origin);
		s = sldns_str2wire_dname_buf(argv[1], state.origin
		                                    , &state.origin_len);
		if (s) {
			fprintf(stderr, "Error parsing origin: %s\n"
			              , sldns_get_errorstr_parse(s));
			free(str);
			return EXIT_FAILURE;
		}
		s = -1;
	}
	if (!str)
		fprintf(stderr, "Memory allocation error: %s\n"
		              , strerror(errno));

	else if (argc != 1 && argc != 2) {
		free(str);
		return print_usage(stderr, progname);
	}

	else if (!(in = fopen(argv[0], "r")))
		fprintf(stderr, "Error opening \"%s\": %s\n"
		              , argv[0], strerror(errno));
	else while (!feof(in)) {
		size_t rr_len = sizeof(rr), dname_len = 0;
		size_t written;

		s = sldns_fp2wire_rr_buf(in, rr, &rr_len, &dname_len, &state);
		if (s) {
			fprintf( stderr, "parse error %d:%d: %s\n"
			               , state.lineno, LDNS_WIREPARSE_OFFSET(s)
			               , sldns_get_errorstr_parse(s));
			break;
		}
		if (rr_len == 0)
			continue;

		if (print_in_unknown_type_format)
			written = sldns_wire2str_rr_unknown_buf(
				rr, rr_len, str, str_len);
		else
			written = sldns_wire2str_rr_buf(
				rr, rr_len, str, str_len);

		if (written > str_len) {
			while (written > str_len)
				str_len *= 2;
			free(str);
			if (!(str = malloc(str_len))) {
				fprintf(stderr, "Memory allocation error: %s\n"
				              , strerror(errno));
				s = -1;
				break;
			}
			if (print_in_unknown_type_format)
				(void) sldns_wire2str_rr_unknown_buf(
					rr, rr_len, str, str_len);
			else
				(void) sldns_wire2str_rr_buf(
					rr, rr_len, str, str_len);
		}
		fprintf(stdout, "%s", str);
	}
	if (in)
		fclose(in);
	free(str);
	return !in || s ? EXIT_FAILURE : EXIT_SUCCESS;
}
