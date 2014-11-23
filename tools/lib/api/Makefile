include ../../scripts/Makefile.include
include ../../perf/config/utilities.mak		# QUIET_CLEAN

CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar

# guard against environment variables
LIB_H=
LIB_OBJS=

LIB_H += fs/debugfs.h
LIB_H += fs/fs.h
# See comment below about piggybacking...
LIB_H += fd/array.h

LIB_OBJS += $(OUTPUT)fs/debugfs.o
LIB_OBJS += $(OUTPUT)fs/fs.o
# XXX piggybacking here, need to introduce libapikfd, or rename this
# to plain libapik.a and make it have it all api goodies
LIB_OBJS += $(OUTPUT)fd/array.o

LIBFILE = libapikfs.a

CFLAGS = -ggdb3 -Wall -Wextra -std=gnu99 -Werror -O6 -D_FORTIFY_SOURCE=2 $(EXTRA_WARNINGS) $(EXTRA_CFLAGS) -fPIC
EXTLIBS = -lelf -lpthread -lrt -lm
ALL_CFLAGS = $(CFLAGS) $(BASIC_CFLAGS) -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64
ALL_LDFLAGS = $(LDFLAGS)

RM = rm -f

$(LIBFILE): $(LIB_OBJS)
	$(QUIET_AR)$(RM) $@ && $(AR) rcs $(OUTPUT)$@ $(LIB_OBJS)

$(LIB_OBJS): $(LIB_H)

libapi_dirs:
	$(QUIET_MKDIR)mkdir -p $(OUTPUT)fd $(OUTPUT)fs

$(OUTPUT)%.o: %.c libapi_dirs
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) $<
$(OUTPUT)%.s: %.c libapi_dirs
	$(QUIET_CC)$(CC) -S $(ALL_CFLAGS) $<
$(OUTPUT)%.o: %.S libapi_dirs
	$(QUIET_CC)$(CC) -o $@ -c $(ALL_CFLAGS) $<

clean:
	$(call QUIET_CLEAN, libapi) $(RM) $(LIB_OBJS) $(LIBFILE)

.PHONY: clean
