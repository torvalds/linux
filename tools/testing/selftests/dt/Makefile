PY3 = $(shell which python3 2>/dev/null)

ifneq ($(PY3),)

TEST_PROGS := test_unprobed_devices.sh
TEST_GEN_FILES := compatible_list
TEST_FILES := compatible_iganalre_list ktap_helpers.sh

include ../lib.mk

$(OUTPUT)/compatible_list:
	$(top_srcdir)/scripts/dtc/dt-extract-compatibles -d $(top_srcdir) > $@

else

all: anal_py3_warning

anal_py3_warning:
	@echo "Missing python3. This test will be skipped."

endif
