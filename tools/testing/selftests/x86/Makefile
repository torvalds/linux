.PHONY: all all_32 all_64 check_build32 clean run_tests

TARGETS_C_BOTHBITS := sigreturn

BINARIES_32 := $(TARGETS_C_BOTHBITS:%=%_32)
BINARIES_64 := $(TARGETS_C_BOTHBITS:%=%_64)

CFLAGS := -O2 -g -std=gnu99 -pthread -Wall

UNAME_P := $(shell uname -p)

# Always build 32-bit tests
all: all_32

# If we're on a 64-bit host, build 64-bit tests as well
ifeq ($(shell uname -p),x86_64)
all: all_64
endif

all_32: check_build32 $(BINARIES_32)

all_64: $(BINARIES_64)

clean:
	$(RM) $(BINARIES_32) $(BINARIES_64)

run_tests:
	./run_x86_tests.sh

$(TARGETS_C_BOTHBITS:%=%_32): %_32: %.c
	$(CC) -m32 -o $@ $(CFLAGS) $(EXTRA_CFLAGS) $^ -lrt -ldl

$(TARGETS_C_BOTHBITS:%=%_64): %_64: %.c
	$(CC) -m64 -o $@ $(CFLAGS) $(EXTRA_CFLAGS) $^ -lrt -ldl

check_build32:
	@if ! $(CC) -m32 -o /dev/null trivial_32bit_program.c; then	\
	  echo "Warning: you seem to have a broken 32-bit build" 2>&1; 	\
	  echo "environment.  If you are using a Debian-like";		\
	  echo " distribution, try:"; 					\
	  echo "";							\
	  echo "  apt-get install gcc-multilib libc6-i386 libc6-dev-i386"; \
	  echo "";							\
	  echo "If you are using a Fedora-like distribution, try:";	\
	  echo "";							\
	  echo "  yum install glibc-devel.*i686";			\
	  exit 1;							\
	fi
