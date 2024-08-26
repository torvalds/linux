# SPDX-License-Identifier: GPL-2.0

ifneq ($(CONFIG_UNICODE),)
obj-y			+= unicode.o
endif
obj-$(CONFIG_UNICODE)	+= utf8data.o
obj-$(CONFIG_UNICODE_NORMALIZATION_SELFTEST) += utf8-selftest.o

unicode-y := utf8-norm.o utf8-core.o

$(obj)/utf8-data.o: $(obj)/utf8data.c

# In the normal build, the checked-in utf8data.c is just shipped.
#
# To generate utf8data.c from UCD, put *.txt files in this directory
# and pass REGENERATE_UTF8DATA=1 from the command line.
ifdef REGENERATE_UTF8DATA

quiet_cmd_utf8data = GEN     $@
      cmd_utf8data = $< \
		-a $(src)/DerivedAge.txt \
		-c $(src)/DerivedCombiningClass.txt \
		-p $(src)/DerivedCoreProperties.txt \
		-d $(src)/UnicodeData.txt \
		-f $(src)/CaseFolding.txt \
		-n $(src)/NormalizationCorrections.txt \
		-t $(src)/NormalizationTest.txt \
		-o $@

$(obj)/utf8data.c: $(obj)/mkutf8data $(filter %.txt, $(cmd_utf8data)) FORCE
	$(call if_changed,utf8data)

else

$(obj)/utf8data.c: $(src)/utf8data.c_shipped FORCE
	$(call if_changed,copy)

endif

targets += utf8data.c
hostprogs += mkutf8data
