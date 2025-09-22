LEVEL = ../../test/make

CXX_SOURCES := main.cpp

EXE := lldb-functions
USE_LIBCPP := 1

MY_OS = $(shell uname -s)

ifeq "$(MY_OS)" "Darwin"
    LLDB_BUILD_DIR ?= /Applications/Xcode.app/Contents/SharedFrameworks
    LD_EXTRAS ?= -framework LLDB -Wl,-rpath,"$(LLDB_BUILD_DIR)"
	FRAMEWORK_INCLUDES=-F"$(LLDB_BUILD_DIR)"
else
    LD_EXTRAS ?= $(LLDB_BUILD_DIR)/_lldb.so
endif

include $(LEVEL)/Makefile.rules
