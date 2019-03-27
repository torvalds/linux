/*
 * Copyright (c) 2017-2018, Juniper Networks, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "../libsecureboot-priv.h"

#include <unistd.h>
#include <err.h>
#include <verify_file.h>

char *Skip;

int
main(int argc, char *argv[])
{
	int n;
	int fd;
	int c;
	int Vflag;
	char *cp;
	char *prefix;

	prefix = NULL;
	Skip = NULL;

	n = ve_trust_init();
	printf("Trust %d\n", n);
	Vflag = 0;

	while ((c = getopt(argc, argv, "dp:s:T:V")) != -1) {
		switch (c) {
		case 'd':
			DebugVe++;
			break;
		case 'p':
			prefix = optarg;
			break;
		case 's':
			Skip = optarg;
			break;
		case 'T':
			n = ve_trust_add(optarg);
			printf("Local trust %s: %d\n", optarg, n);
			break;
		case 'V':
			Vflag = 1;
			break;
		default:
			errx(1, "unknown option: -%c", c);
			break;
		}
	}

	ve_self_tests();

	for ( ; optind < argc; optind++) {
		if (Vflag) {
			/*
			 * Simulate what loader does.
			 * verify_file should "just work"
			 */
			fd = open(argv[optind], O_RDONLY);
			if (fd > 0) {
				/*
				 * See if verify_file is happy
				 */
				int x;

				x = verify_file(fd, argv[optind], 0, VE_GUESS);
				printf("verify_file(%s) = %d\n", argv[optind], x);
				close(fd);
			}
			continue;
		}
#ifdef VE_OPENPGP_SUPPORT
		if (strstr(argv[optind], "asc")) {
			cp = (char *)verify_asc(argv[optind], 1);
			if (cp) {
				printf("Verified: %s: %.28s...\n",
				    argv[optind], cp);
				fingerprint_info_add(argv[optind],
				    prefix, Skip, cp, NULL);
			} else {
				fprintf(stderr, "%s: %s\n",
				    argv[optind], ve_error_get());
			}
		} else
#endif
		if (strstr(argv[optind], "sig")) {
			cp = (char *)verify_sig(argv[optind], 1);
			if (cp) {
				printf("Verified: %s: %.28s...\n",
				    argv[optind], cp);
				fingerprint_info_add(argv[optind],
				    prefix, Skip, cp, NULL);
			} else {
				fprintf(stderr, "%s: %s\n",
				    argv[optind], ve_error_get());
			}
		} else if (strstr(argv[optind], "manifest")) {
			cp = (char *)read_file(argv[optind], NULL);
			if (cp) {
				fingerprint_info_add(argv[optind],
				    prefix, Skip, cp, NULL);
			}
		} else {
			fd = verify_open(argv[optind], O_RDONLY);
			printf("verify_open(%s) = %d %s\n", argv[optind], fd,
			    (fd < 0) ? ve_error_get() : "");
			if (fd > 0) {
				/*
				 * Check that vectx_* can also verify the file.
				 */
				void *vp;
				char buf[BUFSIZ];
				struct stat st;
				int error;
				size_t off, n;

				fstat(fd, &st);
				lseek(fd, 0, SEEK_SET);
				off = st.st_size % 512;
				vp = vectx_open(fd, argv[optind], off,
				    &st, &error);
				if (!vp) {
					printf("vectx_open(%s) failed: %d %s\n",
					    argv[optind], error,
					    ve_error_get());
				} else {
					off = vectx_lseek(vp,
					    (st.st_size % 1024), SEEK_SET);

					if (off < st.st_size) {
						n = vectx_read(vp, buf,
						    sizeof(buf));
						if (n > 0)
							off += n;
					}
					off = vectx_lseek(vp, 0, SEEK_END);
					/* repeating that should be harmless */
					off = vectx_lseek(vp, 0, SEEK_END);
					error = vectx_close(vp);
					if (error) {
						printf("vectx_close(%s) == %d %s\n",
						    argv[optind], error,
						    ve_error_get());
					} else {
						printf("vectx_close: Verified: %s\n",
						    argv[optind]);
					}
				}
				close(fd);
			}
		}
	}
	return (0);
}

