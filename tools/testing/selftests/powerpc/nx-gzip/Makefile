TEST_GEN_FILES := gzfht_test gunz_test
TEST_PROGS := nx-gzip-test.sh

include ../../lib.mk
include ../flags.mk

CFLAGS = -O3 -m64 -I./include -I../include

$(TEST_GEN_FILES): gzip_vas.c ../utils.c
