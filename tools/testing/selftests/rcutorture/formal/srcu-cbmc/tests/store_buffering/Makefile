# SPDX-License-Identifier: GPL-2.0
CBMC_FLAGS = -I../.. -I../../src -I../../include -I../../empty_includes -32 -pointer-check -mm pso

all:
	for i in ./*.pass; do \
		echo $$i ; \
		CBMC_FLAGS="$(CBMC_FLAGS)" sh ../test_script.sh --should-pass $$i > $$i.out 2>&1 ; \
	done
	for i in ./*.fail; do \
		echo $$i ; \
		CBMC_FLAGS="$(CBMC_FLAGS)" sh ../test_script.sh --should-fail $$i > $$i.out 2>&1 ; \
	done
