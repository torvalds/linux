# SPDX-License-Identifier: GPL-2.0
# Standalone Makefile for developer tooling (not part of kbuild).

SHELLCHECK := $(shell which shellcheck 2> /dev/null)

SRCS := \
  klp-build

.DEFAULT_GOAL := help
.PHONY: help
help:
	@echo "  check      - Run shellcheck on $(SRCS)"
	@echo "  help       - Show this help message"

.PHONY: check
check:
ifndef SHELLCHECK
	$(error shellcheck is not installed. Please install it to run checks)
endif
	@$(SHELLCHECK) $(SHELLCHECK_OPTIONS) $(SRCS)
