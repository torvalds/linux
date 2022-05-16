# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_UNICODE) += unicode.o
obj-$(CONFIG_UNICODE_NORMALIZATION_SELFTEST) += utf8-selftest.o

unicode-y := utf8-norm.o utf8-core.o

$(obj)/utf8-norm.o: $(obj)/utf8data.h

# In the normal build, the checked-in utf8data.h is just shipped.
#
# To generate utf8data.h from UCD, put *.txt files in this directory
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

$(obj)/utf8data.h: $(obj)/mkutf8data $(filter %.txt, $(cmd_utf8data)) FORCE
	$(call if_changed,utf8data)

else

$(obj)/utf8data.h: $(src)/utf8data.h_shipped FORCE
	$(call if_changed,shipped)

endif

targets += utf8data.h
hostprogs += mkutf8data
