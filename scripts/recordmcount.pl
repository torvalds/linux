#!/usr/bin/perl -w
# (c) 2008, Steven Rostedt <srostedt@redhat.com>
# Licensed under the terms of the GNU GPL License version 2
#
# recordmcount.pl - makes a section called __mcount_loc that holds
#                   all the offsets to the calls to mcount.
#
#
# What we want to end up with is a section in vmlinux called
# __mcount_loc that contains a list of pointers to all the
# call sites in the kernel that call mcount. Later on boot up, the kernel
# will read this list, save the locations and turn them into nops.
# When tracing or profiling is later enabled, these locations will then
# be converted back to pointers to some function.
#
# This is no easy feat. This script is called just after the original
# object is compiled and before it is linked.
#
# The references to the call sites are offsets from the section of text
# that the call site is in. Hence, all functions in a section that
# has a call site to mcount, will have the offset from the beginning of
# the section and not the beginning of the function.
#
# The trick is to find a way to record the beginning of the section.
# The way we do this is to look at the first function in the section
# which will also be the location of that section after final link.
# e.g.
#
#  .section ".text.sched"
#  .globl my_func
#  my_func:
#        [...]
#        call mcount  (offset: 0x5)
#        [...]
#        ret
#  other_func:
#        [...]
#        call mcount (offset: 0x1b)
#        [...]
#
# Both relocation offsets for the mcounts in the above example will be
# offset from .text.sched. If we make another file called tmp.s with:
#
#  .section __mcount_loc
#  .quad  my_func + 0x5
#  .quad  my_func + 0x1b
#
# We can then compile this tmp.s into tmp.o, and link it to the original
# object.
#
# But this gets hard if my_func is not globl (a static function).
# In such a case we have:
#
#  .section ".text.sched"
#  my_func:
#        [...]
#        call mcount  (offset: 0x5)
#        [...]
#        ret
#  .globl my_func
#  other_func:
#        [...]
#        call mcount (offset: 0x1b)
#        [...]
#
# If we make the tmp.s the same as above, when we link together with
# the original object, we will end up with two symbols for my_func:
# one local, one global.  After final compile, we will end up with
# an undefined reference to my_func.
#
# Since local objects can reference local variables, we need to find
# a way to make tmp.o reference the local objects of the original object
# file after it is linked together. To do this, we convert the my_func
# into a global symbol before linking tmp.o. Then after we link tmp.o
# we will only have a single symbol for my_func that is global.
# We can convert my_func back into a local symbol and we are done.
#
# Here are the steps we take:
#
# 1) Record all the local symbols by using 'nm'
# 2) Use objdump to find all the call site offsets and sections for
#    mcount.
# 3) Compile the list into its own object.
# 4) Do we have to deal with local functions? If not, go to step 8.
# 5) Make an object that converts these local functions to global symbols
#    with objcopy.
# 6) Link together this new object with the list object.
# 7) Convert the local functions back to local symbols and rename
#    the result as the original object.
#    End.
# 8) Link the object with the list object.
# 9) Move the result back to the original object.
#    End.
#

use strict;

my $P = $0;
$P =~ s@.*/@@g;

my $V = '0.1';

if ($#ARGV < 6) {
	print "usage: $P arch objdump objcopy cc ld nm rm mv inputfile\n";
	print "version: $V\n";
	exit(1);
}

my ($arch, $objdump, $objcopy, $cc, $ld, $nm, $rm, $mv, $inputfile) = @ARGV;

if ($arch eq "i386") {
  $ld = "ld -m elf_i386";
  $objdump = "objdump -M i386";
  $objcopy = "objcopy -O elf32-i386";
  $cc = "gcc -m32";
}

if ($arch eq "x86_64") {
  $ld = "ld -m elf_x86_64";
  $objdump = "objdump -M x86-64";
  $objcopy = "objcopy -O elf64-x86-64";
  $cc = "gcc -m64";
}

$objdump = "objdump" if ((length $objdump) == 0);
$objcopy = "objcopy" if ((length $objcopy) == 0);
$cc = "gcc" if ((length $cc) == 0);
$ld = "ld" if ((length $ld) == 0);
$nm = "nm" if ((length $nm) == 0);
$rm = "rm" if ((length $rm) == 0);
$mv = "mv" if ((length $mv) == 0);

#print STDERR "running: $P '$arch' '$objdump' '$objcopy' '$cc' '$ld' " .
#    "'$nm' '$rm' '$mv' '$inputfile'\n";

my %locals;
my %convert;

my $type;
my $section_regex;	# Find the start of a section
my $function_regex;	# Find the name of a function (return func name)
my $mcount_regex;	# Find the call site to mcount (return offset)

if ($arch eq "x86_64") {
    $section_regex = "Disassembly of section";
    $function_regex = "<(.*?)>:";
    $mcount_regex = "^\\s*([0-9a-fA-F]+):.*\\smcount([+-]0x[0-9a-zA-Z]+)?\$";
    $type = ".quad";
} elsif ($arch eq "i386") {
    $section_regex = "Disassembly of section";
    $function_regex = "<(.*?)>:";
    $mcount_regex = "^\\s*([0-9a-fA-F]+):.*\\smcount\$";
    $type = ".long";
} else {
    die "Arch $arch is not supported with CONFIG_FTRACE_MCOUNT_RECORD";
}

my $text_found = 0;
my $read_function = 0;
my $opened = 0;
my $text = "";
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

#
# Step 1: find all the local symbols (static functions).
#
open (IN, "$nm $inputfile|") || die "error running $nm";
while (<IN>) {
    if (/^[0-9a-fA-F]+\s+t\s+(\S+)/) {
	$locals{$1} = 1;
    }
}
close(IN);

#
# Step 2: find the sections and mcount call sites
#
open(IN, "$objdump -dr $inputfile|") || die "error running $objdump";

while (<IN>) {
    # is it a section?
    if (/$section_regex/) {
	$read_function = 1;
	$text_found = 0;
    # section found, now is this a start of a function?
    } elsif ($read_function && /$function_regex/) {
	$read_function = 0;
	$text_found = 1;
	$text = $1;
	# is this function static? If so, note this fact.
	if (defined $locals{$text}) {
	    $convert{$text} = 1;
	}
    # is this a call site to mcount? If so, print the offset from the section
    } elsif ($text_found && /$mcount_regex/) {
	if (!$opened) {
	    open(FILE, ">$mcount_s") || die "can't create $mcount_s\n";
	    $opened = 1;
	    print FILE "\t.section $mcount_section,\"a\",\@progbits\n";
	}
	print FILE "\t$type $text + 0x$1\n";
    }
}

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
