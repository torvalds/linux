# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (c) 1999-2004 Silicon Graphics, Inc.  All Rights Reserved.
# Copyright (c) 2009 Wind River Systems, Inc. All Rights Reserved.
#

CCVERSION	:= $(shell $(CC) -v 2>&1 | sed -ne '$$p')
obj-y := kdb_io.o kdb_main.o kdb_support.o kdb_bt.o gen-kdb_cmds.o kdb_bp.o kdb_debugger.o
obj-$(CONFIG_KDB_KEYBOARD)    += kdb_keyboard.o

clean-files := gen-kdb_cmds.c

quiet_cmd_gen-kdb = GENKDB  $@
      cmd_gen-kdb = $(AWK) 'BEGIN {print "\#include <linux/stddef.h>"; print "\#include <linux/init.h>"} \
		/^\#/{next} \
		/^[ \t]*$$/{next} \
		{gsub(/"/, "\\\"", $$0); \
		  print "static __initdata char kdb_cmd" cmds++ "[] = \"" $$0 "\\n\";"} \
		END {print "extern char *kdb_cmds[]; char __initdata *kdb_cmds[] = {"; for (i = 0; i < cmds; ++i) {print "  kdb_cmd" i ","}; print("  NULL\n};");}' \
		$(filter-out %/Makefile,$^) > $@#

$(obj)/gen-kdb_cmds.c:	$(src)/kdb_cmds $(src)/Makefile
	$(call cmd,gen-kdb)
