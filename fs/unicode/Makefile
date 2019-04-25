# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_UNICODE) += utf8-norm.o

# This rule is not invoked during the kernel compilation.  It is used to
# regenerate the utf8data.h header file.
utf8data.h.new: *.txt $(objdir)/scripts/mkutf8data
	$(objdir)/scripts/mkutf8data \
		-a DerivedAge.txt \
		-c DerivedCombiningClass.txt \
		-p DerivedCoreProperties.txt \
		-d UnicodeData.txt \
		-f CaseFolding.txt \
		-n NormalizationCorrections.txt \
		-t NormalizationTest.txt \
		-o $@
