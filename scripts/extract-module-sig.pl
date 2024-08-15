#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
#
# extract-mod-sig <part> <module-file>
#
# Reads the module file and writes out some or all of the signature
# section to stdout.  Part is the bit to be written and is one of:
#
#  -0: The unsigned module, no signature data at all
#  -a: All of the signature data, including magic number
#  -d: Just the descriptor values as a sequence of numbers
#  -n: Just the signer's name
#  -k: Just the key ID
#  -s: Just the crypto signature or PKCS#7 message
#
use warnings;
use strict;

die "Format: $0 -[0adnks] module-file >out\n"
    if ($#ARGV != 1);

my $part = $ARGV[0];
my $modfile = $ARGV[1];

my $magic_number = "~Module signature appended~\n";

#
# Read the module contents
#
open FD, "<$modfile" || die $modfile;
binmode(FD);
my @st = stat(FD);
die "$modfile" unless (@st);
my $buf = "";
my $len = sysread(FD, $buf, $st[7]);
die "$modfile" unless (defined($len));
die "Short read on $modfile\n" unless ($len == $st[7]);
close(FD) || die $modfile;

print STDERR "Read ", $len, " bytes from module file\n";

die "The file is too short to have a sig magic number and descriptor\n"
    if ($len < 12 + length($magic_number));

#
# Check for the magic number and extract the information block
#
my $p = $len - length($magic_number);
my $raw_magic = substr($buf, $p);

die "Magic number not found at $len\n"
    if ($raw_magic ne $magic_number);
print STDERR "Found magic number at $len\n";

$p -= 12;
my $raw_info = substr($buf, $p, 12);

my @info = unpack("CCCCCxxxN", $raw_info);
my ($algo, $hash, $id_type, $name_len, $kid_len, $sig_len) = @info;

if ($id_type == 0) {
    print STDERR "Found PGP key identifier\n";
} elsif ($id_type == 1) {
    print STDERR "Found X.509 cert identifier\n";
} elsif ($id_type == 2) {
    print STDERR "Found PKCS#7/CMS encapsulation\n";
} else {
    print STDERR "Found unsupported identifier type $id_type\n";
}

#
# Extract the three pieces of info data
#
die "Insufficient name+kid+sig data in file\n"
    unless ($p >= $name_len + $kid_len + $sig_len);

$p -= $sig_len;
my $raw_sig = substr($buf, $p, $sig_len);
$p -= $kid_len;
my $raw_kid = substr($buf, $p, $kid_len);
$p -= $name_len;
my $raw_name = substr($buf, $p, $name_len);

my $module_len = $p;

if ($sig_len > 0) {
    print STDERR "Found $sig_len bytes of signature [";
    my $n = $sig_len > 16 ? 16 : $sig_len;
    foreach my $i (unpack("C" x $n, substr($raw_sig, 0, $n))) {
	printf STDERR "%02x", $i;
    }
    print STDERR "]\n";
}

if ($kid_len > 0) {
    print STDERR "Found $kid_len bytes of key identifier [";
    my $n = $kid_len > 16 ? 16 : $kid_len;
    foreach my $i (unpack("C" x $n, substr($raw_kid, 0, $n))) {
	printf STDERR "%02x", $i;
    }
    print STDERR "]\n";
}

if ($name_len > 0) {
    print STDERR "Found $name_len bytes of signer's name [$raw_name]\n";
}

#
# Produce the requested output
#
if ($part eq "-0") {
    # The unsigned module, no signature data at all
    binmode(STDOUT);
    print substr($buf, 0, $module_len);
} elsif ($part eq "-a") {
    # All of the signature data, including magic number
    binmode(STDOUT);
    print substr($buf, $module_len);
} elsif ($part eq "-d") {
    # Just the descriptor values as a sequence of numbers
    print join(" ", @info), "\n";
} elsif ($part eq "-n") {
    # Just the signer's name
    print STDERR "No signer's name for PKCS#7 message type sig\n"
	if ($id_type == 2);
    binmode(STDOUT);
    print $raw_name;
} elsif ($part eq "-k") {
    # Just the key identifier
    print STDERR "No key ID for PKCS#7 message type sig\n"
	if ($id_type == 2);
    binmode(STDOUT);
    print $raw_kid;
} elsif ($part eq "-s") {
    # Just the crypto signature or PKCS#7 message
    binmode(STDOUT);
    print $raw_sig;
}
