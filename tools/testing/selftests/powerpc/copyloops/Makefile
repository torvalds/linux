# SPDX-License-Identifier: GPL-2.0
# The loops are all 64-bit code
CFLAGS += -m64
CFLAGS += -I$(CURDIR)
CFLAGS += -D SELFTEST
CFLAGS += -maltivec
CFLAGS += -mcpu=power4

# Use our CFLAGS for the implicit .S rule & set the asm machine type
ASFLAGS = $(CFLAGS) -Wa,-mpower4

TEST_GEN_PROGS := copyuser_64_t0 copyuser_64_t1 copyuser_64_t2 \
		copyuser_p7_t0 copyuser_p7_t1 \
		memcpy_64_t0 memcpy_64_t1 memcpy_64_t2 \
		memcpy_p7_t0 memcpy_p7_t1 \
		copyuser_64_exc_t0 copyuser_64_exc_t1 copyuser_64_exc_t2

EXTRA_SOURCES := validate.c ../harness.c stubs.S

top_srcdir = ../../../../..
include ../../lib.mk

$(OUTPUT)/copyuser_64_t%:	copyuser_64.S $(EXTRA_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-D COPY_LOOP=test___copy_tofrom_user_base \
		-D SELFTEST_CASE=$(subst copyuser_64_t,,$(notdir $@)) \
		-o $@ $^

$(OUTPUT)/copyuser_p7_t%:	copyuser_power7.S $(EXTRA_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-D COPY_LOOP=test___copy_tofrom_user_power7 \
		-D SELFTEST_CASE=$(subst copyuser_p7_t,,$(notdir $@)) \
		-o $@ $^

# Strictly speaking, we only need the memcpy_64 test cases for big-endian
$(OUTPUT)/memcpy_64_t%:	memcpy_64.S $(EXTRA_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-D COPY_LOOP=test_memcpy \
		-D SELFTEST_CASE=$(subst memcpy_64_t,,$(notdir $@)) \
		-o $@ $^

$(OUTPUT)/memcpy_p7_t%:	memcpy_power7.S $(EXTRA_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-D COPY_LOOP=test_memcpy_power7 \
		-D SELFTEST_CASE=$(subst memcpy_p7_t,,$(notdir $@)) \
		-o $@ $^

$(OUTPUT)/copyuser_64_exc_t%: copyuser_64.S exc_validate.c ../harness.c \
		copy_tofrom_user_reference.S stubs.S
	$(CC) $(CPPFLAGS) $(CFLAGS) \
		-D COPY_LOOP=test___copy_tofrom_user_base \
		-D SELFTEST_CASE=$(subst copyuser_64_exc_t,,$(notdir $@)) \
		-o $@ $^
