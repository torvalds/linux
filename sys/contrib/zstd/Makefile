# ################################################################
# Copyright (c) 2015-present, Yann Collet, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under both the BSD-style license (found in the
# LICENSE file in the root directory of this source tree) and the GPLv2 (found
# in the COPYING file in the root directory of this source tree).
# ################################################################

PRGDIR   = programs
ZSTDDIR  = lib
BUILDIR  = build
ZWRAPDIR = zlibWrapper
TESTDIR  = tests
FUZZDIR  = $(TESTDIR)/fuzz

# Define nul output
VOID = /dev/null

ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
else
EXT =
endif

## default: Build lib-release and zstd-release
.PHONY: default
default: lib-release zstd-release

.PHONY: all
all: allmost examples manual contrib

.PHONY: allmost
allmost: allzstd zlibwrapper

# skip zwrapper, can't build that on alternate architectures without the proper zlib installed
.PHONY: allzstd
allzstd: lib
	$(MAKE) -C $(PRGDIR) all
	$(MAKE) -C $(TESTDIR) all

.PHONY: all32
all32:
	$(MAKE) -C $(PRGDIR) zstd32
	$(MAKE) -C $(TESTDIR) all32

.PHONY: lib lib-release libzstd.a
lib lib-release :
	@$(MAKE) -C $(ZSTDDIR) $@

.PHONY: zstd zstd-release
zstd zstd-release:
	@$(MAKE) -C $(PRGDIR) $@
	cp $(PRGDIR)/zstd$(EXT) .

.PHONY: zstdmt
zstdmt:
	@$(MAKE) -C $(PRGDIR) $@
	cp $(PRGDIR)/zstd$(EXT) ./zstdmt$(EXT)

.PHONY: zlibwrapper
zlibwrapper: lib
	$(MAKE) -C $(ZWRAPDIR) all

## test: run long-duration tests
.PHONY: test
DEBUGLEVEL ?= 1
test: MOREFLAGS += -g -DDEBUGLEVEL=$(DEBUGLEVEL) -Werror
test:
	MOREFLAGS="$(MOREFLAGS)" $(MAKE) -j -C $(PRGDIR) allVariants
	$(MAKE) -C $(TESTDIR) $@

## shortest: same as `make check`
.PHONY: shortest
shortest:
	$(MAKE) -C $(TESTDIR) $@

## check: run basic tests for `zstd` cli
.PHONY: check
check: shortest

## examples: build all examples in `/examples` directory
.PHONY: examples
examples: lib
	CPPFLAGS=-I../lib LDFLAGS=-L../lib $(MAKE) -C examples/ all

## manual: generate API documentation in html format
.PHONY: manual
manual:
	$(MAKE) -C contrib/gen_html $@

## man: generate man page
.PHONY: man
man:
	$(MAKE) -C programs $@

## contrib: build all supported projects in `/contrib` directory
.PHONY: contrib
contrib: lib
	$(MAKE) -C contrib/pzstd all
	$(MAKE) -C contrib/seekable_format/examples all
	$(MAKE) -C contrib/adaptive-compression all
	$(MAKE) -C contrib/largeNbDicts all

.PHONY: cleanTabs
cleanTabs:
	cd contrib; ./cleanTabs

.PHONY: clean
clean:
	@$(MAKE) -C $(ZSTDDIR) $@ > $(VOID)
	@$(MAKE) -C $(PRGDIR) $@ > $(VOID)
	@$(MAKE) -C $(TESTDIR) $@ > $(VOID)
	@$(MAKE) -C $(ZWRAPDIR) $@ > $(VOID)
	@$(MAKE) -C examples/ $@ > $(VOID)
	@$(MAKE) -C contrib/gen_html $@ > $(VOID)
	@$(MAKE) -C contrib/pzstd $@ > $(VOID)
	@$(MAKE) -C contrib/seekable_format/examples $@ > $(VOID)
	@$(MAKE) -C contrib/adaptive-compression $@ > $(VOID)
	@$(MAKE) -C contrib/largeNbDicts $@ > $(VOID)
	@$(RM) zstd$(EXT) zstdmt$(EXT) tmp*
	@$(RM) -r lz4
	@echo Cleaning completed

#------------------------------------------------------------------------------
# make install is validated only for Linux, macOS, Hurd and some BSD targets
#------------------------------------------------------------------------------
ifneq (,$(filter $(shell uname),Linux Darwin GNU/kFreeBSD GNU OpenBSD FreeBSD DragonFly NetBSD MSYS_NT Haiku))

HOST_OS = POSIX
CMAKE_PARAMS = -DZSTD_BUILD_CONTRIB:BOOL=ON -DZSTD_BUILD_STATIC:BOOL=ON -DZSTD_BUILD_TESTS:BOOL=ON -DZSTD_ZLIB_SUPPORT:BOOL=ON -DZSTD_LZMA_SUPPORT:BOOL=ON -DCMAKE_BUILD_TYPE=Release

HAVE_COLORNEVER = $(shell echo a | egrep --color=never a > /dev/null 2> /dev/null && echo 1 || echo 0)
EGREP_OPTIONS ?=
ifeq ($HAVE_COLORNEVER, 1)
EGREP_OPTIONS += --color=never
endif
EGREP = egrep $(EGREP_OPTIONS)

# Print a two column output of targets and their description. To add a target description, put a
# comment in the Makefile with the format "## <TARGET>: <DESCRIPTION>".  For example:
#
## list: Print all targets and their descriptions (if provided)
.PHONY: list
list:
	@TARGETS=$$($(MAKE) -pRrq -f $(lastword $(MAKEFILE_LIST)) : 2>/dev/null \
		| awk -v RS= -F: '/^# File/,/^# Finished Make data base/ {if ($$1 !~ "^[#.]") {print $$1}}' \
		| $(EGREP) -v  -e '^[^[:alnum:]]' | sort); \
	{ \
	    printf "Target Name\tDescription\n"; \
	    printf "%0.s-" {1..16}; printf "\t"; printf "%0.s-" {1..40}; printf "\n"; \
	    for target in $$TARGETS; do \
	        line=$$($(EGREP) "^##[[:space:]]+$$target:" $(lastword $(MAKEFILE_LIST))); \
	        description=$$(echo $$line | awk '{i=index($$0,":"); print substr($$0,i+1)}' | xargs); \
	        printf "$$target\t$$description\n"; \
	    done \
	} | column -t -s $$'\t'

.PHONY: install clangtest armtest usan asan uasan
install:
	@$(MAKE) -C $(ZSTDDIR) $@
	@$(MAKE) -C $(PRGDIR) $@

.PHONY: uninstall
uninstall:
	@$(MAKE) -C $(ZSTDDIR) $@
	@$(MAKE) -C $(PRGDIR) $@

.PHONY: travis-install
travis-install:
	$(MAKE) install PREFIX=~/install_test_dir

.PHONY: gcc5build
gcc5build: clean
	gcc-5 -v
	CC=gcc-5 $(MAKE) all MOREFLAGS="-Werror"

.PHONY: gcc6build
gcc6build: clean
	gcc-6 -v
	CC=gcc-6 $(MAKE) all MOREFLAGS="-Werror"

.PHONY: gcc7build
gcc7build: clean
	gcc-7 -v
	CC=gcc-7 $(MAKE) all MOREFLAGS="-Werror"

.PHONY: clangbuild
clangbuild: clean
	clang -v
	CXX=clang++ CC=clang $(MAKE) all MOREFLAGS="-Werror -Wconversion -Wno-sign-conversion -Wdocumentation"

m32build: clean
	gcc -v
	$(MAKE) all32

armbuild: clean
	CC=arm-linux-gnueabi-gcc CFLAGS="-Werror" $(MAKE) allzstd

aarch64build: clean
	CC=aarch64-linux-gnu-gcc CFLAGS="-Werror" $(MAKE) allzstd

ppcbuild: clean
	CC=powerpc-linux-gnu-gcc CFLAGS="-m32 -Wno-attributes -Werror" $(MAKE) allzstd

ppc64build: clean
	CC=powerpc-linux-gnu-gcc CFLAGS="-m64 -Werror" $(MAKE) allzstd

armfuzz: clean
	CC=arm-linux-gnueabi-gcc QEMU_SYS=qemu-arm-static MOREFLAGS="-static" FUZZER_FLAGS=--no-big-tests $(MAKE) -C $(TESTDIR) fuzztest

aarch64fuzz: clean
	ld -v
	CC=aarch64-linux-gnu-gcc QEMU_SYS=qemu-aarch64-static MOREFLAGS="-static" FUZZER_FLAGS=--no-big-tests $(MAKE) -C $(TESTDIR) fuzztest

ppcfuzz: clean
	CC=powerpc-linux-gnu-gcc QEMU_SYS=qemu-ppc-static MOREFLAGS="-static" FUZZER_FLAGS=--no-big-tests $(MAKE) -C $(TESTDIR) fuzztest

ppc64fuzz: clean
	CC=powerpc-linux-gnu-gcc QEMU_SYS=qemu-ppc64-static MOREFLAGS="-m64 -static" FUZZER_FLAGS=--no-big-tests $(MAKE) -C $(TESTDIR) fuzztest

.PHONY: cxxtest
cxxtest: CXXFLAGS += -Wall -Wextra -Wundef -Wshadow -Wcast-align -Werror
cxxtest: clean
	$(MAKE) -C $(PRGDIR) all CC="$(CXX) -Wno-deprecated" CFLAGS="$(CXXFLAGS)"   # adding -Wno-deprecated to avoid clang++ warning on dealing with C files directly

gcc5test: clean
	gcc-5 -v
	$(MAKE) all CC=gcc-5 MOREFLAGS="-Werror"

gcc6test: clean
	gcc-6 -v
	$(MAKE) all CC=gcc-6 MOREFLAGS="-Werror"

clangtest: clean
	clang -v
	$(MAKE) all CXX=clang++ CC=clang MOREFLAGS="-Werror -Wconversion -Wno-sign-conversion -Wdocumentation"

armtest: clean
	$(MAKE) -C $(TESTDIR) datagen   # use native, faster
	$(MAKE) -C $(TESTDIR) test CC=arm-linux-gnueabi-gcc QEMU_SYS=qemu-arm-static ZSTDRTTEST= MOREFLAGS="-Werror -static" FUZZER_FLAGS=--no-big-tests

aarch64test:
	$(MAKE) -C $(TESTDIR) datagen   # use native, faster
	$(MAKE) -C $(TESTDIR) test CC=aarch64-linux-gnu-gcc QEMU_SYS=qemu-aarch64-static ZSTDRTTEST= MOREFLAGS="-Werror -static" FUZZER_FLAGS=--no-big-tests

ppctest: clean
	$(MAKE) -C $(TESTDIR) datagen   # use native, faster
	$(MAKE) -C $(TESTDIR) test CC=powerpc-linux-gnu-gcc QEMU_SYS=qemu-ppc-static ZSTDRTTEST= MOREFLAGS="-Werror -Wno-attributes -static" FUZZER_FLAGS=--no-big-tests

ppc64test: clean
	$(MAKE) -C $(TESTDIR) datagen   # use native, faster
	$(MAKE) -C $(TESTDIR) test CC=powerpc-linux-gnu-gcc QEMU_SYS=qemu-ppc64-static ZSTDRTTEST= MOREFLAGS="-m64 -static" FUZZER_FLAGS=--no-big-tests

arm-ppc-compilation:
	$(MAKE) -C $(PRGDIR) clean zstd CC=arm-linux-gnueabi-gcc QEMU_SYS=qemu-arm-static ZSTDRTTEST= MOREFLAGS="-Werror -static"
	$(MAKE) -C $(PRGDIR) clean zstd CC=aarch64-linux-gnu-gcc QEMU_SYS=qemu-aarch64-static ZSTDRTTEST= MOREFLAGS="-Werror -static"
	$(MAKE) -C $(PRGDIR) clean zstd CC=powerpc-linux-gnu-gcc QEMU_SYS=qemu-ppc-static ZSTDRTTEST= MOREFLAGS="-Werror -Wno-attributes -static"
	$(MAKE) -C $(PRGDIR) clean zstd CC=powerpc-linux-gnu-gcc QEMU_SYS=qemu-ppc64-static ZSTDRTTEST= MOREFLAGS="-m64 -static"

regressiontest:
	$(MAKE) -C $(FUZZDIR) regressiontest

uasanregressiontest:
	$(MAKE) -C $(FUZZDIR) regressiontest CC=clang CXX=clang++ CFLAGS="-O3 -fsanitize=address,undefined" CXXFLAGS="-O3 -fsanitize=address,undefined"

msanregressiontest:
	$(MAKE) -C $(FUZZDIR) regressiontest CC=clang CXX=clang++ CFLAGS="-O3 -fsanitize=memory" CXXFLAGS="-O3 -fsanitize=memory"

# run UBsan with -fsanitize-recover=signed-integer-overflow
# due to a bug in UBsan when doing pointer subtraction
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=63303

usan: clean
	$(MAKE) test CC=clang MOREFLAGS="-g -fno-sanitize-recover=all -fsanitize-recover=signed-integer-overflow -fsanitize=undefined -Werror"

asan: clean
	$(MAKE) test CC=clang MOREFLAGS="-g -fsanitize=address -Werror"

asan-%: clean
	LDFLAGS=-fuse-ld=gold MOREFLAGS="-g -fno-sanitize-recover=all -fsanitize=address -Werror" $(MAKE) -C $(TESTDIR) $*

msan: clean
	$(MAKE) test CC=clang MOREFLAGS="-g -fsanitize=memory -fno-omit-frame-pointer -Werror" HAVE_LZMA=0   # datagen.c fails this test for no obvious reason

msan-%: clean
	LDFLAGS=-fuse-ld=gold MOREFLAGS="-g -fno-sanitize-recover=all -fsanitize=memory -fno-omit-frame-pointer -Werror" FUZZER_FLAGS=--no-big-tests $(MAKE) -C $(TESTDIR) HAVE_LZMA=0 $*

asan32: clean
	$(MAKE) -C $(TESTDIR) test32 CC=clang MOREFLAGS="-g -fsanitize=address"

uasan: clean
	$(MAKE) test CC=clang MOREFLAGS="-g -fno-sanitize-recover=all -fsanitize-recover=signed-integer-overflow -fsanitize=address,undefined -Werror"

uasan-%: clean
	LDFLAGS=-fuse-ld=gold MOREFLAGS="-g -fno-sanitize-recover=all -fsanitize-recover=signed-integer-overflow -fsanitize=address,undefined -Werror" $(MAKE) -C $(TESTDIR) $*

tsan-%: clean
	LDFLAGS=-fuse-ld=gold MOREFLAGS="-g -fno-sanitize-recover=all -fsanitize=thread -Werror" $(MAKE) -C $(TESTDIR) $* FUZZER_FLAGS=--no-big-tests

apt-install:
	sudo apt-get -yq --no-install-suggests --no-install-recommends --force-yes install $(APT_PACKAGES)

apt-add-repo:
	sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
	sudo apt-get update -y -qq

ppcinstall:
	APT_PACKAGES="qemu-system-ppc qemu-user-static gcc-powerpc-linux-gnu" $(MAKE) apt-install

arminstall:
	APT_PACKAGES="qemu-system-arm qemu-user-static gcc-arm-linux-gnueabi libc6-dev-armel-cross gcc-aarch64-linux-gnu libc6-dev-arm64-cross" $(MAKE) apt-install

valgrindinstall:
	APT_PACKAGES="valgrind" $(MAKE) apt-install

libc6install:
	APT_PACKAGES="libc6-dev-i386 gcc-multilib" $(MAKE) apt-install

gcc6install: apt-add-repo
	APT_PACKAGES="libc6-dev-i386 gcc-multilib gcc-6 gcc-6-multilib" $(MAKE) apt-install

gcc7install: apt-add-repo
	APT_PACKAGES="libc6-dev-i386 gcc-multilib gcc-7 gcc-7-multilib" $(MAKE) apt-install

gcc8install: apt-add-repo
	APT_PACKAGES="libc6-dev-i386 gcc-multilib gcc-8 gcc-8-multilib" $(MAKE) apt-install

gpp6install: apt-add-repo
	APT_PACKAGES="libc6-dev-i386 g++-multilib gcc-6 g++-6 g++-6-multilib" $(MAKE) apt-install

clang38install:
	APT_PACKAGES="clang-3.8" $(MAKE) apt-install

# Ubuntu 14.04 ships a too-old lz4
lz4install:
	[ -e lz4 ] || git clone https://github.com/lz4/lz4 && sudo $(MAKE) -C lz4 install

endif


ifneq (,$(filter MSYS%,$(shell uname)))
HOST_OS = MSYS
CMAKE_PARAMS = -G"MSYS Makefiles" -DZSTD_MULTITHREAD_SUPPORT:BOOL=OFF -DZSTD_BUILD_STATIC:BOOL=ON -DZSTD_BUILD_TESTS:BOOL=ON
endif


#------------------------------------------------------------------------
# target specific tests
#------------------------------------------------------------------------
ifneq (,$(filter $(HOST_OS),MSYS POSIX))
cmakebuild:
	cmake --version
	$(RM) -r $(BUILDIR)/cmake/build
	mkdir $(BUILDIR)/cmake/build
	cd $(BUILDIR)/cmake/build ; cmake -DCMAKE_INSTALL_PREFIX:PATH=~/install_test_dir $(CMAKE_PARAMS) .. ; $(MAKE) install ; $(MAKE) uninstall

c90build: clean
	$(CC) -v
	CFLAGS="-std=c90 -Werror" $(MAKE) allmost  # will fail, due to missing support for `long long`

gnu90build: clean
	$(CC) -v
	CFLAGS="-std=gnu90 -Werror" $(MAKE) allmost

c99build: clean
	$(CC) -v
	CFLAGS="-std=c99 -Werror" $(MAKE) allmost

gnu99build: clean
	$(CC) -v
	CFLAGS="-std=gnu99 -Werror" $(MAKE) allmost

c11build: clean
	$(CC) -v
	CFLAGS="-std=c11 -Werror" $(MAKE) allmost

bmix64build: clean
	$(CC) -v
	CFLAGS="-O3 -mbmi -Werror" $(MAKE) -C $(TESTDIR) test

bmix32build: clean
	$(CC) -v
	CFLAGS="-O3 -mbmi -mx32 -Werror" $(MAKE) -C $(TESTDIR) test

bmi32build: clean
	$(CC) -v
	CFLAGS="-O3 -mbmi -m32 -Werror" $(MAKE) -C $(TESTDIR) test

# static analyzer test uses clang's scan-build
# does not analyze zlibWrapper, due to detected issues in zlib source code
staticAnalyze: SCANBUILD ?= scan-build
staticAnalyze:
	$(CC) -v
	CC=$(CC) CPPFLAGS=-g $(SCANBUILD) --status-bugs -v $(MAKE) allzstd examples contrib
endif
