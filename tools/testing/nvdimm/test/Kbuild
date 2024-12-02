# SPDX-License-Identifier: GPL-2.0
ccflags-y := -I$(srctree)/drivers/nvdimm/
ccflags-y += -I$(srctree)/drivers/acpi/nfit/

obj-m += nfit_test.o
obj-m += nfit_test_iomap.o

ifeq  ($(CONFIG_ACPI_NFIT),m)
	nfit_test-y := nfit.o
else
	nfit_test-y := ndtest.o
endif
nfit_test_iomap-y := iomap.o
