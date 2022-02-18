# SPDX-License-Identifier: GPL-2.0-only
# tools/power/acpi/Makefile - ACPI tool Makefile
#
# Copyright (c) 2013, Intel Corporation
#   Author: Lv Zheng <lv.zheng@intel.com>
#

include ../../scripts/Makefile.include

.NOTPARALLEL:

all: acpidbg acpidump ec pfrut
clean: acpidbg_clean acpidump_clean ec_clean pfrut_clean
install: acpidbg_install acpidump_install ec_install pfrut_install
uninstall: acpidbg_uninstall acpidump_uninstall ec_uninstall pfrut_uninstall

acpidbg acpidump ec pfrut: FORCE
	$(call descend,tools/$@,all)
acpidbg_clean acpidump_clean ec_clean pfrut_clean:
	$(call descend,tools/$(@:_clean=),clean)
acpidbg_install acpidump_install ec_install pfrut_install:
	$(call descend,tools/$(@:_install=),install)
acpidbg_uninstall acpidump_uninstall ec_uninstall pfrut_uninstall:
	$(call descend,tools/$(@:_uninstall=),uninstall)

.PHONY: FORCE
