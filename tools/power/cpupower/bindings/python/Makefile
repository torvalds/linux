# SPDX-License-Identifier: GPL-2.0-only
# Makefile for libcpupower's Python bindings
#
# This Makefile expects you have already run the makefile for cpupower to build
# the .o files in the lib directory for the bindings to be created.

CC=gcc

LIB_DIR = ../../lib
BIND_DIR = .
PY_INCLUDE := $(firstword $(shell python-config --includes))
#PY_INCLUDE = $(shell python-config --includes | awk '{ print $1 }')

OBJECTS_LIB = $(wildcard $(LIB_DIR)/*.o)
OBJECTS_BIND = $(wildcard $(BIND_DIR)/*.o)

all: _raw_pylibcpupower.so

_raw_pylibcpupower.so: raw_pylibcpupower_wrap.o
	$(CC) -shared $(OBJECTS_LIB) raw_pylibcpupower_wrap.o -o _raw_pylibcpupower.so # raw_pylibcpupower_wrap.o
#	$(CC) -shared $(OBJECTS_BIND) $(OBJECTS_LIB) -o _raw_pylibcpupower.so # raw_pylibcpupower_wrap.o

raw_pylibcpupower_wrap.o: raw_pylibcpupower_wrap.c
	$(CC) -fPIC -c raw_pylibcpupower_wrap.c $(PY_INCLUDE)

raw_pylibcpupower_wrap.c: raw_pylibcpupower.i
	swig -python raw_pylibcpupower.i

# Will only clean the bindings folder; will not clean the actual cpupower folder
clean:
	rm -f raw_pylibcpupower.py raw_pylibcpupower_wrap.c raw_pylibcpupower_wrap.o _raw_pylibcpupower.so
