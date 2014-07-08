#
# linux/pie/Makefile
#
# Copyright 2013 Texas Instruments, Inc.
#      Russ Dill <russ.dill@ti.com>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#

obj-y		:= pie.bin.o

# Report unresolved symbol references
ldflags-y	+= --no-undefined
# Delete all temporary local symbols
ldflags-y	+= -X

# Reset objcopy flags, ARM puts "-O binary" here
OBJCOPYFLAGS	=

# Reference gcc builtins for use in PIE with __pie_
$(obj)/pie_rename.syms: $(KBUILD_LIBPIE)
	@$(NM) $^ | awk '{if ($$3) print $$3,"__pie_"$$3}' > $@

# For weakening the links to the original gcc builtins
$(obj)/pie_weaken.syms: $(KBUILD_LIBPIE)
	@$(NM) $^ | awk '{if ($$3) print "__pie_"$$3}' > $@

# For embedding address of the symbols copied from the PIE into the kernel
$(obj)/pie.syms: $(obj)/pie.elf
	@$(NM) $^ | awk '{if ($$3 && $$2 == toupper($$2)) print $$3,"=","0x"$$1" + _binary_pie_pie_bin_start;"}' > $@

# Collect together the libpie objects
LDFLAGS_libpie_stage1.o += -r

$(obj)/libpie_stage1.o: $(KBUILD_LIBPIE)
	$(call if_changed,ld)

# Rename the libpie gcc builtins with a __pie_ prefix
OBJCOPYFLAGS_libpie_stage2.o += --redefine-syms=$(obj)/pie_rename.syms
OBJCOPYFLAGS_libpie_stage2.o += --rename-section .text=.pie.text

$(obj)/libpie_stage2.o: $(obj)/libpie_stage1.o $(obj)/pie_rename.syms
	$(call if_changed,objcopy)

# Generate a version of vmlinux.o with weakened and rename references to gcc
# builtins.
OBJCOPYFLAGS_pie_stage1.o += --weaken-symbols=$(obj)/pie_weaken.syms
OBJCOPYFLAGS_pie_stage1.o += --redefine-syms=$(obj)/pie_rename.syms

$(obj)/pie_stage1.o: $(obj)/../vmlinux.o $(obj)/pie_rename.syms $(obj)/pie_weaken.syms
	$(call if_changed,objcopy)

# Drop in the PIE versions instead
LDFLAGS_pie_stage2.o += -r
# Allow the _GLOBAL_OFFSET_TABLE to redefine
LDFLAGS_pie_stage2.o += --defsym=_GLOBAL_OFFSET_TABLE_=_GLOBAL_OFFSET_TABLE_

$(obj)/pie_stage2.o: $(obj)/pie_stage1.o $(obj)/libpie_stage2.o
	$(call if_changed,ld)

# Drop everything but the pie sections
OBJCOPYFLAGS_pie_stage3.o += -j ".pie.*"
OBJCOPYFLAGS_pie_stage3.o += -j ".pie.text"
OBJCOPYFLAGS_pie_stage3.o += -j ".pie.rk3036.text" -j ".pie.rk3036.data"
OBJCOPYFLAGS_pie_stage3.o += -j ".pie.rk3188.text" -j ".pie.rk3188.data"
OBJCOPYFLAGS_pie_stage3.o += -j ".pie.rk3288.text" -j ".pie.rk3288.data"

$(obj)/pie_stage3.o: $(obj)/pie_stage2.o
	$(call if_changed,objcopy)

# Create the position independant executable
LDFLAGS_pie.elf += -T $(KBUILD_PIE_LDS) --pie --gc-sections

$(obj)/pie.elf: $(obj)/pie_stage3.o $(KBUILD_PIE_LDS)
	$(call if_changed,ld)

# Create binary data for the kernel
OBJCOPYFLAGS_pie.bin += -O binary

$(obj)/pie.bin: $(obj)/pie.elf $(obj)/pie.syms
	$(call if_changed,objcopy)

# Import the data into the kernel
OBJCOPYFLAGS_pie.bin.o += -B $(ARCH) -I binary -O $(OBJCOPY_OUTPUT_FORMAT)

$(obj)/pie.bin.o: $(obj)/pie.bin
	$(call if_changed,objcopy)

extra-y += pie_rename.syms pie_weaken.syms pie.syms pie.elf pie.bin
