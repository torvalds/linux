ifndef  SRCBASE
        SRCBASE = ../../..
endif

include ../../../Makerules
include ../Makefile.inc


all:    ${LIB_OBJS}

		$(AR) crv ${LIBWFA_NAME} $?
		$(AR) crv ${LIBWFA_NAME_CA} $?

#add dut lib to this ranlib for normal ASD (without RWL)
		${RANLIB} ${LIBWFA_NAME} ${LIBWFA_NAME_CA}

vpath %.c $(SRCBASE)/tools/Wifi_ASD_test/dut


../dut/$(TARGETARCH)/%.o: %.c
	@mkdir -p ../dut/$(TARGETARCH)
	$(CC) -c $(CFLAGS) -o $@ $^

$(TARGETARCH)/%.o: %.c
	@mkdir -p $(TARGETARCH)
	$(CC) -c $(CFLAGS) -o $@ $^

clean:
		rm -f ${PROGS} ${CLEANFILES}
		rm -rf x86_mmx arm_le
