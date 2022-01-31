# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_UNICODE) += unicode.o
obj-$(CONFIG_UNICODE_NORMALIZATION_SELFTEST) += utf8-selftest.o
obj-$(CONFIG_UNICODE_UTF8_DATA) += utf8data.o

unicode-y := utf8-norm.o utf8-core.o

$(obj)/utf8-data.o: $(obj)/utf8data.c

# In the normal build, the checked-in utf8data.c is just shipped.
#
# To generate utf8data.c from UCD, put *.txt files in this directory
# and pass REGENERATE_UTF8DATA=1 from the command line.
ifdef REGENERATE_UTF8DATA

quiet_cmd_utf8data = GEN     $@
      cmd_utf8data = $< \
		-a $(srctree)/$(src)/DerivedAge.txt \
		-c $(srctree)/$(src)/DerivedCombiningClass.txt \
		-p $(srctree)/$(src)/DerivedCoreProperties.txt \
		-d $(srctree)/$(src)/UnicodeData.txt \
		-f $(srctree)/$(src)/CaseFolding.txt \
		-n $(srctree)/$(src)/NormalizationCorrections.txt \
		-t $(srctree)/$(src)/NormalizationTest.txt \
		-o $@

$(obj)/utf8data.c: $(obj)/mkutf8data $(filter %.txt, $(cmd_utf8data)) FORCE
	$(call if_changed,utf8data)

else

$(obj)/utf8data.c: $(src)/utf8data.c_shipped FORCE
	$(call if_changed,shipped)

endif

targets += utf8data.c
hostprogs += mkutf8data
