# tools/power/acpi/Makefile - ACPI tool Makefile
#
# Copyright (c) 2013, Intel Corporation
#   Author: Lv Zheng <lv.zheng@intel.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2
# of the License.

include ../../scripts/Makefile.include

all: acpidbg acpidump ec
clean: acpidbg_clean acpidump_clean ec_clean
install: acpidbg_install acpidump_install ec_install
uninstall: acpidbg_uninstall acpidump_uninstall ec_uninstall

acpidbg acpidump ec: FORCE
	$(call descend,tools/$@,all)
acpidbg_clean acpidump_clean ec_clean:
	$(call descend,tools/$(@:_clean=),clean)
acpidbg_install acpidump_install ec_install:
	$(call descend,tools/$(@:_install=),install)
acpidbg_uninstall acpidump_uninstall ec_uninstall:
	$(call descend,tools/$(@:_uninstall=),uninstall)

.PHONY: FORCE
