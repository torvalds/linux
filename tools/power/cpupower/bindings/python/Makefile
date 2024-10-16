# SPDX-License-Identifier: GPL-2.0-only
# Makefile for libcpupower's Python bindings
#
# This Makefile expects you have already run the makefile for cpupower to build
# the .o files in the lib directory for the bindings to be created.

CC := gcc
HAVE_SWIG := $(shell if which swig >/dev/null 2>&1; then echo 1; else echo 0; fi)
HAVE_PYCONFIG := $(shell if which python-config >/dev/null 2>&1; then echo 1; else echo 0; fi)

LIB_DIR := ../../lib
PY_INCLUDE = $(firstword $(shell python-config --includes))
OBJECTS_LIB = $(wildcard $(LIB_DIR)/*.o)

all: _raw_pylibcpupower.so

_raw_pylibcpupower.so: raw_pylibcpupower_wrap.o
	$(CC) -shared $(OBJECTS_LIB) raw_pylibcpupower_wrap.o -o _raw_pylibcpupower.so

raw_pylibcpupower_wrap.o: raw_pylibcpupower_wrap.c
	$(CC) -fPIC -c raw_pylibcpupower_wrap.c $(PY_INCLUDE)

raw_pylibcpupower_wrap.c: raw_pylibcpupower.swg
ifeq ($(HAVE_SWIG),0)
	$(error "swig was not found. Make sure you have it installed and in the PATH to generate the bindings.")
else ifeq ($(HAVE_PYCONFIG),0)
	$(error "python-config was not found. Make sure you have it installed and in the PATH to generate the bindings.")
endif
	swig -python raw_pylibcpupower.swg

# Will only clean the bindings folder; will not clean the actual cpupower folder
clean:
	rm -f raw_pylibcpupower.py raw_pylibcpupower_wrap.c raw_pylibcpupower_wrap.o _raw_pylibcpupower.so
