# SPDX-License-Identifier: GPL-2.0
#
# Optional per-suite build report, see "Recording suite build results" in
# Documentation/dev-tools/kselftest.rst. Callers set KSFT_BUILD_REPORT to an
# absolute filename and use ksft_record_build_status from a shell loop that
# has stored the exit status of the recorded step in $status.

ifneq (x$(KSFT_BUILD_REPORT)x,xx)
ifeq ($(filter x/%,$(firstword x$(KSFT_BUILD_REPORT))),)
$(error KSFT_BUILD_REPORT must be an absolute path: '$(KSFT_BUILD_REPORT)')
endif
endif
export KSFT_BUILD_REPORT

ksft_build_report_dry_run := $(KSFT_BUILD_REPORT_DRY_RUN) \
	$(foreach f,n t q,$(findstring $(f),$(firstword -$(MAKEFLAGS))))

ifneq ($(strip $(ksft_build_report_dry_run)),)
ksft_record_build_status = :
else
define ksft_record_build_status
if [ -n "$$KSFT_BUILD_REPORT" ]; then \
	printf '%s\t%s\t%d\n' '$(1)' "$(2)" "$$status" >> "$$KSFT_BUILD_REPORT" || exit 1; \
fi
endef
endif
