include ../../scripts/Makefile.include

CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar

# Makefiles suck: This macro sets a default value of $(2) for the
# variable named by $(1), unless the variable has been set by
# environment or command line. This is necessary for CC and AR
# because make sets default values, so the simpler ?= approach
# won't work as expected.
define allow-override
  $(if $(or $(findstring environment,$(origin $(1))),\
            $(findstring command line,$(origin $(1)))),,\
    $(eval $(1) = $(2)))
endef

# Allow setting CC and AR, or setting CROSS_COMPILE as a prefix.
$(call allow-override,CC,$(CROSS_COMPILE)gcc)
$(call allow-override,AR,$(CROSS_COMPILE)ar)

# guard against environment variables
LIB_H=
LIB_OBJS=

LIB_H += debugfs.h

LIB_OBJS += $(OUTPUT)debugfs.o

LIBFILE = liblk.a

CFLAGS = -ggdb3 -Wall -Wextra -std=gnu99 -Werror -O6 -D_FORTIFY_SOURCE=2 $(EXTRA_WARNINGS) $(EXTRA_CFLAGS) -fPIC
EXTLIBS = -lelf -lpthread -lrt -lm
ALL_CFLAGS = $(CFLAGS) $(BASIC_CFLAGS) -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
ALL_LDFLAGS = $(LDFLAGS)

RM = rm -f

$(LIBFILE): $(LIB_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $(OUTPUT)$@ $(LIB_OBJS)

$(LIB_OBJS): $(LIB_H)

$(OUTPUT)%.o: %.c
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) $<
$(OUTPUT)%.s: %.c
	$(QUIET_CC)$(CC) -S $(ALL_CFLAGS) $<
$(OUTPUT)%.o: %.S
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) $<

clean:
	$(RM) $(LIB_OBJS) $(LIBFILE)

.PHONY: clean
