CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall

test_objs = open-unlink create-read

all: $(test_objs)

run_tests: all
	@/bin/bash ./efivarfs.sh || echo "efivarfs selftests: [FAIL]"

clean:
	rm -f $(test_objs)
