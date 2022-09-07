# SPDX-License-Identifier: GPL-2.0
# Makefile for AppArmor Linux Security Module
#
obj-$(CONFIG_SECURITY_APPARMOR) += apparmor.o

apparmor-y := apparmorfs.o audit.o capability.o task.o ipc.o lib.o match.o \
              path.o domain.o policy.o policy_unpack.o procattr.o lsm.o \
              resource.o secid.o file.o policy_ns.o label.o mount.o net.o \
              policy_compat.o af_unix.o
apparmor-$(CONFIG_SECURITY_APPARMOR_HASH) += crypto.o

obj-$(CONFIG_SECURITY_APPARMOR_KUNIT_TEST) += apparmor_policy_unpack_test.o
apparmor_policy_unpack_test-objs += policy_unpack_test.o

clean-files := capability_names.h rlim_names.h net_names.h

# Build a lower case string table of address family names
# Transform lines from
#    #define AF_LOCAL		1	/* POSIX name for AF_UNIX	*/
#    #define AF_INET		2	/* Internet IP Protocol 	*/
# to
#    [1] = "local",
#    [2] = "inet",
#
# and build the securityfs entries for the mapping.
# Transforms lines from
#    #define AF_INET		2	/* Internet IP Protocol 	*/
# to
#    #define AA_SFS_AF_MASK "local inet"
quiet_cmd_make-af = GEN     $@
cmd_make-af = echo "static const char *address_family_names[] = {" > $@ ;\
	sed $< >>$@ -r -n -e "/AF_MAX/d" -e "/AF_LOCAL/d" -e "/AF_ROUTE/d" -e \
	 's/^\#define[ \t]+AF_([A-Z0-9_]+)[ \t]+([0-9]+)(.*)/[\2] = "\L\1",/p';\
	echo "};" >> $@ ;\
	printf '%s' '\#define AA_SFS_AF_MASK "' >> $@ ;\
	sed -r -n -e "/AF_MAX/d" -e "/AF_LOCAL/d" -e "/AF_ROUTE/d" -e \
	 's/^\#define[ \t]+AF_([A-Z0-9_]+)[ \t]+([0-9]+)(.*)/\L\1/p'\
	 $< | tr '\n' ' ' | sed -e 's/ $$/"\n/' >> $@

# Build a lower case string table of sock type names
# Transform lines from
#    SOCK_STREAM	= 1,
# to
#    [1] = "stream",
quiet_cmd_make-sock = GEN     $@
cmd_make-sock = echo "static const char *sock_type_names[] = {" >> $@ ;\
	sed $^ >>$@ -r -n \
	-e 's/^\tSOCK_([A-Z0-9_]+)[\t]+=[ \t]+([0-9]+)(.*)/[\2] = "\L\1",/p';\
	echo "};" >> $@

# Build a lower case string table of capability names
# Transforms lines from
#    #define CAP_DAC_OVERRIDE     1
# to
#    [1] = "dac_override",
quiet_cmd_make-caps = GEN     $@
cmd_make-caps = echo "static const char *const capability_names[] = {" > $@ ;\
	sed $< >>$@ -r -n -e '/CAP_FS_MASK/d' \
	-e 's/^\#define[ \t]+CAP_([A-Z0-9_]+)[ \t]+([0-9]+)/[\2] = "\L\1",/p';\
	echo "};" >> $@ ;\
	printf '%s' '\#define AA_SFS_CAPS_MASK "' >> $@ ;\
	sed $< -r -n -e '/CAP_FS_MASK/d' \
	    -e 's/^\#define[ \t]+CAP_([A-Z0-9_]+)[ \t]+([0-9]+)/\L\1/p' | \
	     tr '\n' ' ' | sed -e 's/ $$/"\n/' >> $@


# Build a lower case string table of rlimit names.
# Transforms lines from
#    #define RLIMIT_STACK		3	/* max stack size */
# to
#    [RLIMIT_STACK] = "stack",
#
# and build a second integer table (with the second sed cmd), that maps
# RLIMIT defines to the order defined in asm-generic/resource.h  This is
# required by policy load to map policy ordering of RLIMITs to internal
# ordering for architectures that redefine an RLIMIT.
# Transforms lines from
#    #define RLIMIT_STACK		3	/* max stack size */
# to
# RLIMIT_STACK, 
#
# and build the securityfs entries for the mapping.
# Transforms lines from
#    #define RLIMIT_FSIZE        1   /* Maximum filesize */
#    #define RLIMIT_STACK		3	/* max stack size */
# to
# #define AA_SFS_RLIMIT_MASK "fsize stack"
quiet_cmd_make-rlim = GEN     $@
cmd_make-rlim = echo "static const char *const rlim_names[RLIM_NLIMITS] = {" \
	> $@ ;\
	sed $< >> $@ -r -n \
	    -e 's/^\# ?define[ \t]+(RLIMIT_([A-Z0-9_]+)).*/[\1] = "\L\2",/p';\
	echo "};" >> $@ ;\
	echo "static const int rlim_map[RLIM_NLIMITS] = {" >> $@ ;\
	sed -r -n "s/^\# ?define[ \t]+(RLIMIT_[A-Z0-9_]+).*/\1,/p" $< >> $@ ;\
	echo "};" >> $@ ; \
	printf '%s' '\#define AA_SFS_RLIMIT_MASK "' >> $@ ;\
	sed -r -n 's/^\# ?define[ \t]+RLIMIT_([A-Z0-9_]+).*/\L\1/p' $< | \
	    tr '\n' ' ' | sed -e 's/ $$/"\n/' >> $@

$(obj)/capability.o : $(obj)/capability_names.h
$(obj)/net.o : $(obj)/net_names.h
$(obj)/resource.o : $(obj)/rlim_names.h
$(obj)/capability_names.h : $(srctree)/include/uapi/linux/capability.h \
			    $(src)/Makefile
	$(call cmd,make-caps)
$(obj)/rlim_names.h : $(srctree)/include/uapi/asm-generic/resource.h \
		      $(src)/Makefile
	$(call cmd,make-rlim)
$(obj)/net_names.h : $(srctree)/include/linux/socket.h \
		     $(srctree)/include/linux/net.h \
		     $(src)/Makefile
	$(call cmd,make-af)
	$(call cmd,make-sock)
