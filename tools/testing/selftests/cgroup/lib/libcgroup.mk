CGROUP_DIR := $(selfdir)/cgroup

LIBCGROUP_C := lib/cgroup_util.c

LIBCGROUP_O := $(patsubst %.c, $(OUTPUT)/%.o, $(LIBCGROUP_C))

LIBCGROUP_O_DIRS := $(shell dirname $(LIBCGROUP_O) | uniq)

CFLAGS += -I$(CGROUP_DIR)/lib/include

EXTRA_HDRS := $(selfdir)/clone3/clone3_selftests.h

$(LIBCGROUP_O_DIRS):
	mkdir -p $@

$(LIBCGROUP_O): $(OUTPUT)/%.o : $(CGROUP_DIR)/%.c $(EXTRA_HDRS) $(LIBCGROUP_O_DIRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@

EXTRA_CLEAN += $(LIBCGROUP_O)
