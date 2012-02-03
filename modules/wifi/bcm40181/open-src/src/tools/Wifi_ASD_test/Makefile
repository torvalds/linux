ifndef  SRCBASE
        SRCBASE = ../..
endif

include ../../Makerules
include Makefile.inc



ifneq ($(findstring x86,$(TARGETARCH)), x86)
		DIRS= ${LIB}
	else
		DIRS= ${LIB} ${CA} ${CLI}
endif

all:
	for i in ${DIRS}; do \
		$(MAKE) -C $$i || exit 1; \
	done

clean:
	for i in ${DIRS}; do \
		$(MAKE) -C $$i clean || exit 1; \
	done
	rm -rf ./dut/x86_mmx ./dut/arm_le
