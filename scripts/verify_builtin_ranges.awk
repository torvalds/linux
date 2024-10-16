#!/usr/bin/gawk -f
# SPDX-License-Identifier: GPL-2.0
# verify_builtin_ranges.awk: Verify address range data for builtin modules
# Written by Kris Van Hees <kris.van.hees@oracle.com>
#
# Usage: verify_builtin_ranges.awk modules.builtin.ranges System.map \
#				   modules.builtin vmlinux.map vmlinux.o.map
#

# Return the module name(s) (if any) associated with the given object.
#
# If we have seen this object before, return information from the cache.
# Otherwise, retrieve it from the corresponding .cmd file.
#
function get_module_info(fn, mod, obj, s) {
	if (fn in omod)
		return omod[fn];

	if (match(fn, /\/[^/]+$/) == 0)
		return "";

	obj = fn;
	mod = "";
	fn = substr(fn, 1, RSTART) "." substr(fn, RSTART + 1) ".cmd";
	if (getline s <fn == 1) {
		if (match(s, /DKBUILD_MODFILE=['"]+[^'"]+/) > 0) {
			mod = substr(s, RSTART + 16, RLENGTH - 16);
			gsub(/['"]/, "", mod);
		} else if (match(s, /RUST_MODFILE=[^ ]+/) > 0)
			mod = substr(s, RSTART + 13, RLENGTH - 13);
	} else {
		print "ERROR: Failed to read: " fn "\n\n" \
		      "  For kernels built with O=<objdir>, cd to <objdir>\n" \
		      "  and execute this script as ./source/scripts/..." \
		      >"/dev/stderr";
		close(fn);
		total = 0;
		exit(1);
	}
	close(fn);

	# A single module (common case) also reflects objects that are not part
	# of a module.  Some of those objects have names that are also a module
	# name (e.g. core).  We check the associated module file name, and if
	# they do not match, the object is not part of a module.
	if (mod !~ / /) {
		if (!(mod in mods))
			mod = "";
	}

	gsub(/([^/ ]*\/)+/, "", mod);
	gsub(/-/, "_", mod);

	# At this point, mod is a single (valid) module name, or a list of
	# module names (that do not need validation).
	omod[obj] = mod;

	return mod;
}

# Return a representative integer value for a given hexadecimal address.
#
# Since all kernel addresses fall within the same memory region, we can safely
# strip off the first 6 hex digits before performing the hex-to-dec conversion,
# thereby avoiding integer overflows.
#
function addr2val(val) {
	sub(/^0x/, "", val);
	if (length(val) == 16)
		val = substr(val, 5);
	return strtonum("0x" val);
}

# Determine the kernel build directory to use (default is .).
#
BEGIN {
	if (ARGC < 6) {
		print "Syntax: verify_builtin_ranges.awk <ranges-file> <system-map>\n" \
		      "          <builtin-file> <vmlinux-map> <vmlinux-o-map>\n" \
		      >"/dev/stderr";
		total = 0;
		exit(1);
	}
}

# (1) Load the built-in module address range data.
#
ARGIND == 1 {
	ranges[FNR] = $0;
	rcnt++;
	next;
}

# (2) Annotate System.map symbols with module names.
#
ARGIND == 2 {
	addr = addr2val($1);
	name = $3;

	while (addr >= mod_eaddr) {
		if (sect_symb) {
			if (sect_symb != name)
				next;

			sect_base = addr - sect_off;
			if (dbg)
				printf "[%s] BASE (%s) %016x - %016x = %016x\n", sect_name, sect_symb, addr, sect_off, sect_base >"/dev/stderr";
			sect_symb = 0;
		}

		if (++ridx > rcnt)
			break;

		$0 = ranges[ridx];
		sub(/-/, " ");
		if ($4 != "=") {
			sub(/-/, " ");
			mod_saddr = strtonum("0x" $2) + sect_base;
			mod_eaddr = strtonum("0x" $3) + sect_base;
			$1 = $2 = $3 = "";
			sub(/^ +/, "");
			mod_name = $0;

			if (dbg)
				printf "[%s] %s from %016x to %016x\n", sect_name, mod_name, mod_saddr, mod_eaddr >"/dev/stderr";
		} else {
			sect_name = $1;
			sect_off = strtonum("0x" $2);
			sect_symb = $5;
		}
	}

	idx = addr"-"name;
	if (addr >= mod_saddr && addr < mod_eaddr)
		sym2mod[idx] = mod_name;

	next;
}

# Once we are done annotating the System.map, we no longer need the ranges data.
#
FNR == 1 && ARGIND == 3 {
	delete ranges;
}

# (3) Build a lookup map of built-in module names.
#
# Lines from modules.builtin will be like:
#	kernel/crypto/lzo-rle.ko
# and we record the object name "crypto/lzo-rle".
#
ARGIND == 3 {
	sub(/kernel\//, "");			# strip off "kernel/" prefix
	sub(/\.ko$/, "");			# strip off .ko suffix

	mods[$1] = 1;
	next;
}

# (4) Get a list of symbols (per object).
#
# Symbols by object are read from vmlinux.map, with fallback to vmlinux.o.map
# if vmlinux is found to have inked in vmlinux.o.
#

# If we were able to get the data we need from vmlinux.map, there is no need to
# process vmlinux.o.map.
#
FNR == 1 && ARGIND == 5 && total > 0 {
	if (dbg)
		printf "Note: %s is not needed.\n", FILENAME >"/dev/stderr";
	exit;
}

# First determine whether we are dealing with a GNU ld or LLVM lld linker map.
#
ARGIND >= 4 && FNR == 1 && NF == 7 && $1 == "VMA" && $7 == "Symbol" {
	map_is_lld = 1;
	next;
}

# (LLD) Convert a section record fronm lld format to ld format.
#
ARGIND >= 4 && map_is_lld && NF == 5 && /[0-9] [^ ]+$/ {
	$0 = $5 " 0x"$1 " 0x"$3 " load address 0x"$2;
}

# (LLD) Convert an object record from lld format to ld format.
#
ARGIND >= 4 && map_is_lld && NF == 5 && $5 ~ /:\(/ {
	if (/\.a\(/ && !/ vmlinux\.a\(/)
		next;

	gsub(/\)/, "");
	sub(/:\(/, " ");
	sub(/ vmlinux\.a\(/, " ");
	$0 = " "$6 " 0x"$1 " 0x"$3 " " $5;
}

# (LLD) Convert a symbol record from lld format to ld format.
#
ARGIND >= 4 && map_is_lld && NF == 5 && $5 ~ /^[A-Za-z_][A-Za-z0-9_]*$/ {
	$0 = "  0x" $1 " " $5;
}

# (LLD) We do not need any other ldd linker map records.
#
ARGIND >= 4 && map_is_lld && /^[0-9a-f]{16} / {
	next;
}

# Handle section records with long section names (spilling onto a 2nd line).
#
ARGIND >= 4 && !map_is_lld && NF == 1 && /^[^ ]/ {
	s = $0;
	getline;
	$0 = s " " $0;
}

# Next section - previous one is done.
#
ARGIND >= 4 && /^[^ ]/ {
	sect = 0;
}

# Get the (top level) section name.
#
ARGIND >= 4 && /^\./ {
	# Explicitly ignore a few sections that are not relevant here.
	if ($1 ~ /^\.orc_/ || $1 ~ /_sites$/ || $1 ~ /\.percpu/)
		next;

	# Sections with a 0-address can be ignored as well (in vmlinux.map).
	if (ARGIND == 4 && $2 ~ /^0x0+$/)
		next;

	sect = $1;

	next;
}

# If we are not currently in a section we care about, ignore records.
#
!sect {
	next;
}

# Handle object records with long section names (spilling onto a 2nd line).
#
ARGIND >= 4 && /^ [^ \*]/ && NF == 1 {
	# If the section name is long, the remainder of the entry is found on
	# the next line.
	s = $0;
	getline;
	$0 = s " " $0;
}

# Objects linked in from static libraries are ignored.
# If the object is vmlinux.o, we need to consult vmlinux.o.map for per-object
# symbol information
#
ARGIND == 4 && /^ [^ ]/ && NF == 4 {
	if ($4 ~ /\.a\(/)
		next;

	idx = sect":"$1;
	if (!(idx in sect_addend)) {
		sect_addend[idx] = addr2val($2);
		if (dbg)
			printf "ADDEND %s = %016x\n", idx, sect_addend[idx] >"/dev/stderr";
	}
	if ($4 == "vmlinux.o") {
		need_o_map = 1;
		next;
	}
}

# If data from vmlinux.o.map is needed, we only process section and object
# records from vmlinux.map to determine which section we need to pay attention
# to in vmlinux.o.map.  So skip everything else from vmlinux.map.
#
ARGIND == 4 && need_o_map {
	next;
}

# Get module information for the current object.
#
ARGIND >= 4 && /^ [^ ]/ && NF == 4 {
	msect = $1;
	mod_name = get_module_info($4);
	mod_eaddr = addr2val($2) + addr2val($3);

	next;
}

# Process a symbol record.
#
# Evaluate the module information obtained from vmlinux.map (or vmlinux.o.map)
# as follows:
#  - For all symbols in a given object:
#     - If the symbol is annotated with the same module name(s) that the object
#       belongs to, count it as a match.
#     - Otherwise:
#        - If the symbol is known to have duplicates of which at least one is
#          in a built-in module, disregard it.
#        - If the symbol us not annotated with any module name(s) AND the
#          object belongs to built-in modules, count it as missing.
#        - Otherwise, count it as a mismatch.
#
ARGIND >= 4 && /^ / && NF == 2 && $1 ~ /^0x/ {
	idx = sect":"msect;
	if (!(idx in sect_addend))
		next;

	addr = addr2val($1);

	# Handle the rare but annoying case where a 0-size symbol is placed at
	# the byte *after* the module range.  Based on vmlinux.map it will be
	# considered part of the current object, but it falls just beyond the
	# module address range.  Unfortunately, its address could be at the
	# start of another built-in module, so the only safe thing to do is to
	# ignore it.
	if (mod_name && addr == mod_eaddr)
		next;

	# If we are processing vmlinux.o.map, we need to apply the base address
	# of the section to the relative address on the record.
	#
	if (ARGIND == 5)
		addr += sect_addend[idx];

	idx = addr"-"$2;
	mod = "";
	if (idx in sym2mod) {
		mod = sym2mod[idx];
		if (sym2mod[idx] == mod_name) {
			mod_matches++;
			matches++;
		} else if (mod_name == "") {
			print $2 " in " mod " (should NOT be)";
			mismatches++;
		} else {
			print $2 " in " mod " (should be " mod_name ")";
			mismatches++;
		}
	} else if (mod_name != "") {
		print $2 " should be in " mod_name;
		missing++;
	} else
		matches++;

	total++;

	next;
}

# Issue the comparison report.
#
END {
	if (total) {
		printf "Verification of %s:\n", ARGV[1];
		printf "  Correct matches:  %6d (%d%% of total)\n", matches, 100 * matches / total;
		printf "    Module matches: %6d (%d%% of matches)\n", mod_matches, 100 * mod_matches / matches;
		printf "  Mismatches:       %6d (%d%% of total)\n", mismatches, 100 * mismatches / total;
		printf "  Missing:          %6d (%d%% of total)\n", missing, 100 * missing / total;

		if (mismatches || missing)
			exit(1);
	}
}
