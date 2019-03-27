# ################################################################
# Copyright (c) 2016-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under both the BSD-style license (found in the
# LICENSE file in the root directory of this source tree) and the GPLv2 (found
# in the COPYING file in the root directory of this source tree).
# ################################################################

# Standard variables for installation
DESTDIR ?=
PREFIX  ?= /usr/local
BINDIR  := $(DESTDIR)$(PREFIX)/bin

ZSTDDIR = ../../lib
PROGDIR = ../../programs

# External program to use to run tests, e.g. qemu or valgrind
TESTPROG  ?=
# Flags to pass to the tests
TESTFLAGS ?=

# We use gcc/clang to generate the header dependencies of files
DEPFLAGS = -MMD -MP -MF $*.Td
POSTCOMPILE = mv -f $*.Td $*.d

# CFLAGS, CXXFLAGS, CPPFLAGS, and LDFLAGS are for the users to override
CFLAGS   ?= -O3 -Wall -Wextra
CXXFLAGS ?= -O3 -Wall -Wextra -pedantic
CPPFLAGS ?=
LDFLAGS  ?=

# Include flags
PZSTD_INC  = -I$(ZSTDDIR) -I$(ZSTDDIR)/common -I$(PROGDIR) -I.
GTEST_INC  = -isystem googletest/googletest/include

PZSTD_CPPFLAGS  = $(PZSTD_INC)
PZSTD_CCXXFLAGS =
PZSTD_CFLAGS    = $(PZSTD_CCXXFLAGS)
PZSTD_CXXFLAGS  = $(PZSTD_CCXXFLAGS) -std=c++11
PZSTD_LDFLAGS   =
EXTRA_FLAGS     =
ALL_CFLAGS      = $(EXTRA_FLAGS) $(CPPFLAGS) $(PZSTD_CPPFLAGS) $(CFLAGS)   $(PZSTD_CFLAGS)
ALL_CXXFLAGS    = $(EXTRA_FLAGS) $(CPPFLAGS) $(PZSTD_CPPFLAGS) $(CXXFLAGS) $(PZSTD_CXXFLAGS)
ALL_LDFLAGS     = $(EXTRA_FLAGS) $(CXXFLAGS) $(LDFLAGS) $(PZSTD_LDFLAGS)


# gtest libraries need to go before "-lpthread" because they depend on it.
GTEST_LIB  = -L googletest/build/googlemock/gtest
LIBS       =

# Compilation commands
LD_COMMAND  = $(CXX) $^          $(ALL_LDFLAGS) $(LIBS) -pthread -o $@
CC_COMMAND  = $(CC)  $(DEPFLAGS) $(ALL_CFLAGS)   -c $<  -o $@
CXX_COMMAND = $(CXX) $(DEPFLAGS) $(ALL_CXXFLAGS) -c $<  -o $@

# Get a list of all zstd files so we rebuild the static library when we need to
ZSTDCOMMON_FILES := $(wildcard $(ZSTDDIR)/common/*.c) \
                    $(wildcard $(ZSTDDIR)/common/*.h)
ZSTDCOMP_FILES   := $(wildcard $(ZSTDDIR)/compress/*.c) \
                    $(wildcard $(ZSTDDIR)/compress/*.h)
ZSTDDECOMP_FILES := $(wildcard $(ZSTDDIR)/decompress/*.c) \
                    $(wildcard $(ZSTDDIR)/decompress/*.h)
ZSTDPROG_FILES   := $(wildcard $(PROGDIR)/*.c) \
                    $(wildcard $(PROGDIR)/*.h)
ZSTD_FILES       := $(wildcard $(ZSTDDIR)/*.h) \
                    $(ZSTDDECOMP_FILES) $(ZSTDCOMMON_FILES) $(ZSTDCOMP_FILES) \
                    $(ZSTDPROG_FILES)

# List all the pzstd source files so we can determine their dependencies
PZSTD_SRCS  := $(wildcard *.cpp)
PZSTD_TESTS := $(wildcard test/*.cpp)
UTILS_TESTS := $(wildcard utils/test/*.cpp)
ALL_SRCS    := $(PZSTD_SRCS) $(PZSTD_TESTS) $(UTILS_TESTS)


# Define *.exe as extension for Windows systems
ifneq (,$(filter Windows%,$(OS)))
EXT =.exe
else
EXT =
endif

# Standard targets
.PHONY: default
default: all

.PHONY: test-pzstd
test-pzstd: TESTFLAGS=--gtest_filter=-*ExtremelyLarge*
test-pzstd: clean googletest pzstd tests check

.PHONY: test-pzstd32
test-pzstd32: clean googletest32 all32 check

.PHONY: test-pzstd-tsan
test-pzstd-tsan: LDFLAGS=-fuse-ld=gold
test-pzstd-tsan: TESTFLAGS=--gtest_filter=-*ExtremelyLarge*
test-pzstd-tsan: clean googletest tsan check

.PHONY: test-pzstd-asan
test-pzstd-asan: LDFLAGS=-fuse-ld=gold
test-pzstd-asan: TESTFLAGS=--gtest_filter=-*ExtremelyLarge*
test-pzstd-asan: clean asan check

.PHONY: check
check:
	$(TESTPROG) ./utils/test/BufferTest$(EXT) $(TESTFLAGS)
	$(TESTPROG) ./utils/test/RangeTest$(EXT) $(TESTFLAGS)
	$(TESTPROG) ./utils/test/ResourcePoolTest$(EXT) $(TESTFLAGS)
	$(TESTPROG) ./utils/test/ScopeGuardTest$(EXT) $(TESTFLAGS)
	$(TESTPROG) ./utils/test/ThreadPoolTest$(EXT) $(TESTFLAGS)
	$(TESTPROG) ./utils/test/WorkQueueTest$(EXT) $(TESTFLAGS)
	$(TESTPROG) ./test/OptionsTest$(EXT) $(TESTFLAGS)
	$(TESTPROG) ./test/PzstdTest$(EXT) $(TESTFLAGS)

.PHONY: install
install: PZSTD_CPPFLAGS += -DNDEBUG
install: pzstd$(EXT)
	install -d -m 755 $(BINDIR)/
	install -m 755 pzstd$(EXT) $(BINDIR)/pzstd$(EXT)

.PHONY: uninstall
uninstall:
	$(RM) $(BINDIR)/pzstd$(EXT)

# Targets for many different builds
.PHONY: all
all: PZSTD_CPPFLAGS += -DNDEBUG
all: pzstd$(EXT)

.PHONY: debug
debug: EXTRA_FLAGS += -g
debug: pzstd$(EXT) tests roundtrip

.PHONY: tsan
tsan: PZSTD_CCXXFLAGS += -fsanitize=thread -fPIC
tsan: PZSTD_LDFLAGS   += -fsanitize=thread
tsan: debug

.PHONY: asan
asan: EXTRA_FLAGS += -fsanitize=address
asan: debug

.PHONY: ubsan
ubsan: EXTRA_FLAGS += -fsanitize=undefined
ubsan: debug

.PHONY: all32
all32: EXTRA_FLAGS += -m32
all32: all tests roundtrip

.PHONY: debug32
debug32: EXTRA_FLAGS += -m32
debug32: debug

.PHONY: asan32
asan32: EXTRA_FLAGS += -m32
asan32: asan

.PHONY: tsan32
tsan32: EXTRA_FLAGS += -m32
tsan32: tsan

.PHONY: ubsan32
ubsan32: EXTRA_FLAGS += -m32
ubsan32: ubsan

# Run long round trip tests
.PHONY: roundtripcheck
roundtripcheck: roundtrip check
	$(TESTPROG) ./test/RoundTripTest$(EXT) $(TESTFLAGS)

# Build the main binary
pzstd$(EXT): main.o $(PROGDIR)/util.o Options.o Pzstd.o SkippableFrame.o $(ZSTDDIR)/libzstd.a
	$(LD_COMMAND)

# Target that depends on all the tests
.PHONY: tests
tests: EXTRA_FLAGS += -Wno-deprecated-declarations
tests: $(patsubst %,%$(EXT),$(basename $(PZSTD_TESTS) $(UTILS_TESTS)))

# Build the round trip tests
.PHONY: roundtrip
roundtrip: EXTRA_FLAGS += -Wno-deprecated-declarations
roundtrip: test/RoundTripTest$(EXT)

# Use the static library that zstd builds for simplicity and
# so we get the compiler options correct
$(ZSTDDIR)/libzstd.a: $(ZSTD_FILES)
	CFLAGS="$(ALL_CFLAGS)" LDFLAGS="$(ALL_LDFLAGS)" $(MAKE) -C $(ZSTDDIR) libzstd.a

# Rules to build the tests
test/RoundTripTest$(EXT): test/RoundTripTest.o $(PROGDIR)/datagen.o Options.o \
                          Pzstd.o SkippableFrame.o $(ZSTDDIR)/libzstd.a
	$(LD_COMMAND)

test/%Test$(EXT): PZSTD_LDFLAGS += $(GTEST_LIB)
test/%Test$(EXT): LIBS += -lgtest -lgtest_main
test/%Test$(EXT): test/%Test.o $(PROGDIR)/datagen.o Options.o Pzstd.o  \
                  SkippableFrame.o $(ZSTDDIR)/libzstd.a
	$(LD_COMMAND)

utils/test/%Test$(EXT): PZSTD_LDFLAGS += $(GTEST_LIB)
utils/test/%Test$(EXT): LIBS += -lgtest -lgtest_main
utils/test/%Test$(EXT): utils/test/%Test.o
	$(LD_COMMAND)


GTEST_CMAKEFLAGS =

# Install googletest
.PHONY: googletest
googletest: PZSTD_CCXXFLAGS += -fPIC
googletest:
	@$(RM) -rf googletest
	@git clone https://github.com/google/googletest
	@mkdir -p googletest/build
	@cd googletest/build && cmake $(GTEST_CMAKEFLAGS) -DCMAKE_CXX_FLAGS="$(ALL_CXXFLAGS)" .. && $(MAKE)

.PHONY: googletest32
googletest32: PZSTD_CCXXFLAGS  += -m32
googletest32: googletest

.PHONY: googletest-mingw64
googletest-mingw64: GTEST_CMAKEFLAGS += -G "MSYS Makefiles"
googletest-mingw64: googletest

.PHONY: clean
clean:
	$(RM) -f *.o pzstd$(EXT) *.Td *.d
	$(RM) -f test/*.o test/*Test$(EXT) test/*.Td test/*.d
	$(RM) -f utils/test/*.o utils/test/*Test$(EXT) utils/test/*.Td utils/test/*.d
	$(RM) -f $(PROGDIR)/*.o $(PROGDIR)/*.Td $(PROGDIR)/*.d
	$(MAKE) -C $(ZSTDDIR) clean
	@echo Cleaning completed


# Cancel implicit rules
%.o: %.c
%.o: %.cpp

# Object file rules
%.o: %.c
	$(CC_COMMAND)
	$(POSTCOMPILE)

$(PROGDIR)/%.o: $(PROGDIR)/%.c
	$(CC_COMMAND)
	$(POSTCOMPILE)

%.o: %.cpp
	$(CXX_COMMAND)
	$(POSTCOMPILE)

test/%.o: PZSTD_CPPFLAGS += $(GTEST_INC)
test/%.o: test/%.cpp
	$(CXX_COMMAND)
	$(POSTCOMPILE)

utils/test/%.o: PZSTD_CPPFLAGS += $(GTEST_INC)
utils/test/%.o: utils/test/%.cpp
	$(CXX_COMMAND)
	$(POSTCOMPILE)

# Dependency file stuff
.PRECIOUS: %.d test/%.d utils/test/%.d

# Include rules that specify header file dependencies
-include $(patsubst %,%.d,$(basename $(ALL_SRCS)))
