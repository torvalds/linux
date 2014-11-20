# Makefile for firmware loading selftests

# No binaries, but make sure arg-less "make" doesn't trigger "run_tests"
all:

fw_filesystem:
	@if /bin/sh ./fw_filesystem.sh ; then \
                echo "fw_filesystem: ok"; \
        else \
                echo "fw_filesystem: [FAIL]"; \
                exit 1; \
        fi

fw_userhelper:
	@if /bin/sh ./fw_userhelper.sh ; then \
                echo "fw_userhelper: ok"; \
        else \
                echo "fw_userhelper: [FAIL]"; \
                exit 1; \
        fi

run_tests: all fw_filesystem fw_userhelper

# Nothing to clean up.
clean:

.PHONY: all clean run_tests fw_filesystem fw_userhelper
