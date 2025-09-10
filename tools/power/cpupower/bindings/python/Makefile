# SPDX-License-Identifier: GPL-2.0-only
# Makefile for libcpupower's Python bindings
#
# This Makefile expects you have already run `make install-lib` in the lib
# directory for the bindings to be created.

CC ?= gcc
# CFLAGS ?=
LDFLAGS ?= -lcpupower
HAVE_SWIG := $(shell if which swig >/dev/null 2>&1; then echo 1; else echo 0; fi)
HAVE_PYCONFIG := $(shell if which python-config >/dev/null 2>&1; then echo 1; else echo 0; fi)

PY_INCLUDE ?= $(firstword $(shell python-config --includes))
INSTALL_DIR ?= $(shell python3 -c "import site; print(site.getsitepackages()[0])")

all: _raw_pylibcpupower.so

_raw_pylibcpupower.so: raw_pylibcpupower_wrap.o
	$(CC) -shared $(LDFLAGS) raw_pylibcpupower_wrap.o -o _raw_pylibcpupower.so

raw_pylibcpupower_wrap.o: raw_pylibcpupower_wrap.c
	$(CC) $(CFLAGS) $(PY_INCLUDE) -fPIC -c raw_pylibcpupower_wrap.c

raw_pylibcpupower_wrap.c: raw_pylibcpupower.swg
ifeq ($(HAVE_SWIG),0)
	$(error "swig was not found. Make sure you have it installed and in the PATH to generate the bindings.")
else ifeq ($(HAVE_PYCONFIG),0)
	$(error "python-config was not found. Make sure you have it installed and in the PATH to generate the bindings.")
endif
	swig -python raw_pylibcpupower.swg

# Only installs the Python bindings
install: _raw_pylibcpupower.so
	install -D _raw_pylibcpupower.so $(INSTALL_DIR)/_raw_pylibcpupower.so
	install -D raw_pylibcpupower.py $(INSTALL_DIR)/raw_pylibcpupower.py

uninstall:
	rm -f $(INSTALL_DIR)/_raw_pylibcpupower.so
	rm -f $(INSTALL_DIR)/raw_pylibcpupower.py

# Will only clean the bindings folder; will not clean the actual cpupower folder
clean:
	rm -f raw_pylibcpupower.py raw_pylibcpupower_wrap.c raw_pylibcpupower_wrap.o _raw_pylibcpupower.so
