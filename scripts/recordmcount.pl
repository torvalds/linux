#!/usr/bin/perl -w
# (c) 2008, Steven Rostedt <srostedt@redhat.com>
# Licensed under the terms of the GNU GPL License version 2
#
# recordmcount.pl - makes a section called __mcount_loc that holds
#                   all the offsets to the calls to mcount.
#
#
# What we want to end up with this is that each object file will have a
# section called __mcount_loc that will hold the list of pointers to mcount
# callers. After final linking, the vmlinux will have within .init.data the
# list of all callers to mcount between __start_mcount_loc and __stop_mcount_loc.
# Later on boot up, the kernel will read this list, save the locations and turn
# them into nops. When tracing or profiling is later enabled, these locations
# will then be converted back to pointers to some function.
#
# This is no easy feat. This script is called just after the original
# object is compiled and before it is linked.
#
# When parse this object file using 'objdump', the references to the call
# sites are offsets from the section that the call site is in. Hence, all
# functions in a section that has a call site to mcount, will have the
# offset from the beginning of the section and not the beginning of the
# function.
#
# But where this section will reside finally in vmlinx is undetermined at
# this point. So we can't use this kind of offsets to record the final
# address of this call site.
#
# The trick is to change the call offset referring the start of a section to
# referring a function symbol in this section. During the link step, 'ld' will
# compute the final address according to the information we record.
#
# e.g.
#
#  .section ".sched.text", "ax"
#        [...]
#  func1:
#        [...]
#        call mcount  (offset: 0x10)
#        [...]
#        ret
#  .globl fun2
#  func2:             (offset: 0x20)
#        [...]
#        [...]
#        ret
#  func3:
#        [...]
#        call mcount (offset: 0x30)
#        [...]
#
# Both relocation offsets for the mcounts in the above example will be
# offset from .sched.text. If we choose global symbol func2 as a reference and
# make another file called tmp.s with the new offsets:
#
#  .section __mcount_loc
#  .quad  func2 - 0x10
#  .quad  func2 + 0x10
#
# We can then compile this tmp.s into tmp.o, and link it back to the original
# object.
#
# In our algorithm, we will choose the first global function we meet in this
# section as the reference. But this gets hard if there is no global functions
# in this section. In such a case we have to select a local one. E.g. func1:
#
#  .section ".sched.text", "ax"
#  func1:
#        [...]
#        call mcount  (offset: 0x10)
#        [...]
#        ret
#  func2:
#        [...]
#        call mcount (offset: 0x20)
#        [...]
#  .section "other.section"
#
# If we make the tmp.s the same as above, when we link together with
# the original object, we will end up with two symbols for func1:
# one local, one global.  After final compile, we will end up with
# an undefined reference to func1 or a wrong reference to another global
# func1 in other files.
#
# Since local objects can reference local variables, we need to find
# a way to make tmp.o reference the local objects of the original object
# file after it is linked together. To do this, we convert func1
# into a global symbol before linking tmp.o. Then after we link tmp.o
# we will only have a single symbol for func1 that is global.
# We can convert func1 back into a local symbol and we are done.
#
# Here are the steps we take:
#
# 1) Record all the local and weak symbols by using 'nm'
# 2) Use objdump to find all the call site offsets and sections for
#    mcount.
# 3) Compile the list into its own object.
# 4) Do we have to deal with local functions? If not, go to step 8.
# 5) Make an object that converts these local functions to global symbols
#    with objcopy.
# 6) Link together this new object with the list object.
# 7) Convert the local functions back to local symbols and rename
#    the result as the original object.
# 8) Link the object with the list object.
# 9) Move the result back to the original object.
#

use strict;

my $P = $0;
$P =~ s@.*/@@g;

my $V = '0.1';

if ($#ARGV != 10) {
	print "usage: $P arch bits objdump objcopy cc ld nm rm mv is_module inputfile\n";
	print "version: $V\n";
	exit(1);
}

my ($arch, $bits, $objdump, $objcopy, $cc,
    $ld, $nm, $rm, $mv, $is_module, $inputfile) = @ARGV;

# This file refers to mcount and shouldn't be ftraced, so lets' ignore it
if ($inputfile =~ m,kernel/trace/ftrace\.o$,) {
    exit(0);
}

# Acceptable sections to record.
my %text_sections = (
     ".text" => 1,
     ".sched.text" => 1,
     ".spinlock.text" => 1,
     ".irqentry.text" => 1,
     ".text.unlikely" => 1,
);

$objdump = "objdump" if ((length $objdump) == 0);
$objcopy = "objcopy" if ((length $objcopy) == 0);
$cc = "gcc" if ((length $cc) == 0);
$ld = "ld" if ((length $ld) == 0);
$nm = "nm" if ((length $nm) == 0);
$rm = "rm" if ((length $rm) == 0);
$mv = "mv" if ((length $mv) == 0);

#print STDERR "running: $P '$arch' '$objdump' '$objcopy' '$cc' '$ld' " .
#    "'$nm' '$rm' '$mv' '$inputfile'\n";

my %locals;		# List of local (static) functions
my %weak;		# List of weak functions
my %convert;		# List of local functions used that needs conversion

my $type;
my $local_regex;	# Match a local function (return function)
my $weak_regex; 	# Match a weak function (return function)
my $section_regex;	# Find the start of a section
my $function_regex;	# Find the name of a function
			#    (return offset and func name)
my $mcount_regex;	# Find the call site to mcount (return offset)
my $alignment;		# The .align value to use for $mcount_section
my $section_type;	# Section header plus possible alignment command
my $can_use_local = 0; 	# If we can use local function references

##
# check_objcopy - whether objcopy supports --globalize-symbols
#
#  --globalize-symbols came out in 2.17, we must test the version
#  of objcopy, and if it is less than 2.17, then we can not
#  record local functions.
sub check_objcopy
{
    open (IN, "$objcopy --version |") or die "error running $objcopy";
    while (<IN>) {
	if (/objcopy.*\s(\d+)\.(\d+)/) {
	    $can_use_local = 1 if ($1 > 2 || ($1 == 2 && $2 >= 17));
	    last;
	}
    }
    close (IN);

    if (!$can_use_local) {
	print STDERR "WARNING: could not find objcopy version or version " .
	    "is less than 2.17.\n" .
	    "\tLocal function references is disabled.\n";
    }
}

if ($arch eq "x86") {
    if ($bits == 64) {
	$arch = "x86_64";
    } else {
	$arch = "i386";
    }
}

#
# We base the defaults off of i386, the other archs may
# feel free to change them in the below if statements.
#
$local_regex = "^[0-9a-fA-F]+\\s+t\\s+(\\S+)";
$weak_regex = "^[0-9a-fA-F]+\\s+([wW])\\s+(\\S+)";
$section_regex = "Disassembly of section\\s+(\\S+):";
$function_regex = "^([0-9a-fA-F]+)\\s+<(.*?)>:";
$mcount_regex = "^\\s*([0-9a-fA-F]+):.*\\smcount\$";
$section_type = '@progbits';
$type = ".long";

if ($arch eq "x86_64") {
    $mcount_regex = "^\\s*([0-9a-fA-F]+):.*\\smcount([+-]0x[0-9a-zA-Z]+)?\$";
    $type = ".quad";
    $alignment = 8;

    # force flags for this arch
    $ld .= " -m elf_x86_64";
    $objdump .= " -M x86-64";
    $objcopy .= " -O elf64-x86-64";
    $cc .= " -m64";

} elsif ($arch eq "i386") {
    $alignment = 4;

    # force flags for this arch
    $ld .= " -m elf_i386";
    $objdump .= " -M i386";
    $objcopy .= " -O elf32-i386";
    $cc .= " -m32";

} elsif ($arch eq "s390" && $bits == 32) {
    $mcount_regex = "^\\s*([0-9a-fA-F]+):\\s*R_390_32\\s+_mcount\$";
    $alignment = 4;
    $ld .= " -m elf_s390";
    $cc .= " -m31";

} elsif ($arch eq "s390" && $bits == 64) {
    $mcount_regex = "^\\s*([0-9a-fA-F]+):\\s*R_390_(PC|PLT)32DBL\\s+_mcount\\+0x2\$";
    $alignment = 8;
    $type = ".quad";
    $ld .= " -m elf64_s390";
    $cc .= " -m64";

} elsif ($arch eq "sh") {
    $alignment = 2;

    # force flags for this arch
    $ld .= " -m shlelf_linux";
    $objcopy .= " -O elf32-sh-linux";
    $cc .= " -m32";

} elsif ($arch eq "powerpc") {
    $local_regex = "^[0-9a-fA-F]+\\s+t\\s+(\\.?\\S+)";
    $function_regex = "^([0-9a-fA-F]+)\\s+<(\\.?.*?)>:";
    $mcount_regex = "^\\s*([0-9a-fA-F]+):.*\\s\\.?_mcount\$";

    if ($bits == 64) {
	$type = ".quad";
    }

} elsif ($arch eq "arm") {
    $alignment = 2;
    $section_type = '%progbits';

} elsif ($arch eq "ia64") {
    $mcount_regex = "^\\s*([0-9a-fA-F]+):.*\\s_mcount\$";
    $type = "data8";

    if ($is_module eq "0") {
        $cc .= " -mconstant-gp";
    }
} elsif ($arch eq "sparc64") {
    # In the objdump output there are giblets like:
    # 0000000000000000 <igmp_net_exit-0x18>:
    # As there's some data blobs that get emitted into the
    # text section before the first instructions and the first
    # real symbols.  We don't want to match that, so to combat
    # this we use '\w' so we'll match just plain symbol names,
    # and not those that also include hex offsets inside of the
    # '<>' brackets.  Actually the generic function_regex setting
    # could safely use this too.
    $function_regex = "^([0-9a-fA-F]+)\\s+<(\\w*?)>:";

    # Sparc64 calls '_mcount' instead of plain 'mcount'.
    $mcount_regex = "^\\s*([0-9a-fA-F]+):.*\\s_mcount\$";

    $alignment = 8;
    $type = ".xword";
    $ld .= " -m elf64_sparc";
    $cc .= " -m64";
    $objcopy .= " -O elf64-sparc";
} else {
    die "Arch $arch is not supported with CONFIG_FTRACE_MCOUNT_RECORD";
}

my $text_found = 0;
my $read_function = 0;
my $opened = 0;
my $mcount_section = "__mcount_loc";

my $dirname;
my $filename;
my $prefix;
my $ext;

if ($inputfile =~ m,^(.*)/([^/]*)$,) {
    $dirname = $1;
    $filename = $2;
} else {
    $dirname = ".";
    $filename = $inputfile;
}

if ($filename =~ m,^(.*)(\.\S),) {
    $prefix = $1;
    $ext = $2;
} else {
    $prefix = $filename;
    $ext = "";
}

my $mcount_s = $dirname . "/.tmp_mc_" . $prefix . ".s";
my $mcount_o = $dirname . "/.tmp_mc_" . $prefix . ".o";

check_objcopy();

#
# Step 1: find all the local (static functions) and weak symbols.
#         't' is local, 'w/W' is weak
#
open (IN, "$nm $inputfile|") || die "error running $nm";
while (<IN>) {
    if (/$local_regex/) {
	$locals{$1} = 1;
    } elsif (/$weak_regex/) {
	$weak{$2} = $1;
    }
}
close(IN);

my @offsets;		# Array of offsets of mcount callers
my $ref_func;		# reference function to use for offsets
my $offset = 0;		# offset of ref_func to section beginning

##
# update_funcs - print out the current mcount callers
#
#  Go through the list of offsets to callers and write them to
#  the output file in a format that can be read by an assembler.
#
sub update_funcs
{
    return if ($#offsets < 0);

    defined($ref_func) || die "No function to reference";

    # A section only had a weak function, to represent it.
    # Unfortunately, a weak function may be overwritten by another
    # function of the same name, making all these offsets incorrect.
    # To be safe, we simply print a warning and bail.
    if (defined $weak{$ref_func}) {
	print STDERR
	    "$inputfile: WARNING: referencing weak function" .
	    " $ref_func for mcount\n";
	return;
    }

    # is this function static? If so, note this fact.
    if (defined $locals{$ref_func}) {

	# only use locals if objcopy supports globalize-symbols
	if (!$can_use_local) {
	    return;
	}
	$convert{$ref_func} = 1;
    }

    # Loop through all the mcount caller offsets and print a reference
    # to the caller based from the ref_func.
    for (my $i=0; $i <= $#offsets; $i++) {
	if (!$opened) {
	    open(FILE, ">$mcount_s") || die "can't create $mcount_s\n";
	    $opened = 1;
	    print FILE "\t.section $mcount_section,\"a\",$section_type\n";
	    print FILE "\t.align $alignment\n" if (defined($alignment));
	}
	printf FILE "\t%s %s + %d\n", $type, $ref_func, $offsets[$i] - $offset;
    }
}

#
# Step 2: find the sections and mcount call sites
#
open(IN, "$objdump -hdr $inputfile|") || die "error running $objdump";

my $text;


# read headers first
my $read_headers = 1;

while (<IN>) {

    if ($read_headers && /$mcount_section/) {
	#
	# Somehow the make process can execute this script on an
	# object twice. If it does, we would duplicate the mcount
	# section and it will cause the function tracer self test
	# to fail. Check if the mcount section exists, and if it does,
	# warn and exit.
	#
	print STDERR "ERROR: $mcount_section already in $inputfile\n" .
	    "\tThis may be an indication that your build is corrupted.\n" .
	    "\tDelete $inputfile and try again. If the same object file\n" .
	    "\tstill causes an issue, then disable CONFIG_DYNAMIC_FTRACE.\n";
	exit(-1);
    }

    # is it a section?
    if (/$section_regex/) {
	$read_headers = 0;

	# Only record text sections that we know are safe
	if (defined($text_sections{$1})) {
	    $read_function = 1;
	} else {
	    $read_function = 0;
	}
	# print out any recorded offsets
	update_funcs() if (defined($ref_func));

	# reset all markers and arrays
	$text_found = 0;
	undef($ref_func);
	undef(@offsets);

    # section found, now is this a start of a function?
    } elsif ($read_function && /$function_regex/) {
	$text_found = 1;
	$text = $2;

	# if this is either a local function or a weak function
	# keep looking for functions that are global that
	# we can use safely.
	if (!defined($locals{$text}) && !defined($weak{$text})) {
	    $ref_func = $text;
	    $read_function = 0;
	    $offset = hex $1;
	} else {
	    # if we already have a function, and this is weak, skip it
	    if (!defined($ref_func) && !defined($weak{$text}) &&
		 # PPC64 can have symbols that start with .L and
		 # gcc considers these special. Don't use them!
		 $text !~ /^\.L/) {
		$ref_func = $text;
		$offset = hex $1;
	    }
	}
    }
    # is this a call site to mcount? If so, record it to print later
    if ($text_found && /$mcount_regex/) {
	$offsets[$#offsets + 1] = hex $1;
    }
}

# dump out anymore offsets that may have been found
update_funcs() if (defined($ref_func));

# If we did not find any mcount callers, we are done (do nothing).
if (!$opened) {
    exit(0);
}

close(FILE);

#
# Step 3: Compile the file that holds the list of call sites to mcount.
#
`$cc -o $mcount_o -c $mcount_s`;

my @converts = keys %convert;

#
# Step 4: Do we have sections that started with local functions?
#
if ($#converts >= 0) {
    my $globallist = "";
    my $locallist = "";

    foreach my $con (@converts) {
	$globallist .= " --globalize-symbol $con";
	$locallist .= " --localize-symbol $con";
    }

    my $globalobj = $dirname . "/.tmp_gl_" . $filename;
    my $globalmix = $dirname . "/.tmp_mx_" . $filename;

    #
    # Step 5: set up each local function as a global
    #
    `$objcopy $globallist $inputfile $globalobj`;

    #
    # Step 6: Link the global version to our list.
    #
    `$ld -r $globalobj $mcount_o -o $globalmix`;

    #
    # Step 7: Convert the local functions back into local symbols
    #
    `$objcopy $locallist $globalmix $inputfile`;

    # Remove the temp files
    `$rm $globalobj $globalmix`;

} else {

    my $mix = $dirname . "/.tmp_mx_" . $filename;

    #
    # Step 8: Link the object with our list of call sites object.
    #
    `$ld -r $inputfile $mcount_o -o $mix`;

    #
    # Step 9: Move the result back to the original object.
    #
    `$mv $mix $inputfile`;
}

# Clean up the temp files
`$rm $mcount_o $mcount_s`;

exit(0);
