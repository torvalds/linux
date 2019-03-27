#! /usr/local/bin/perl
#
# $FreeBSD$
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy is of the CDDL is also available via the Internet
# at http://www.illumos.org/license/CDDL.
#

#
# Copyright 2010 Nexenta Systems, Inc.  All rights reserved.
# Copyright 2015 John Marino <draco@marino.st>
#

# This converts MAPPING files to localedef character maps
# suitable for use with the UTF-8 derived localedef data.

sub ucs_to_utf8
{
    my $ucs = shift;
    my $utf8;

    if ($ucs <= 0x7f) {
	$utf8 = sprintf("\\x%02X", $ucs).$utf8;
    } elsif ($ucs <= 0x7ff) {
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", $ucs | 0xc0).$utf8;

    } elsif ($ucs <= 0xffff) {
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", $ucs | 0xe0).$utf8;

    } elsif ($ucs <= 0x1fffff) {
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", $ucs | 0xf0).$utf8;

    } elsif ($ucs <= 0x03ffffff) {
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", $ucs | 0xf8).$utf8;

    } else {
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", ($ucs & 0x3f) | 0x80).$utf8;
	$ucs >>= 6;
	$utf8 = sprintf("\\x%02X", $ucs | 0xf8).$utf8;
    }

    return ($utf8);
}

my %unames;
my %uvalues;

#
# This is not a general purpose Character Map parser, but its good enough
# for the stock one supplied with CLDR.
#
sub load_utf8_cm
{
    my $file = shift;

    open(UTF8, "$file") || die "open";

    while (<UTF8>) {
	next if (/^#/);
	next if (/^\s*$/);
	next if (/^\s*CHARMAP\s*$/);
	next if (/^\s*END\s*CHARMAP\s*$/);
	chomp;
	@words = split /\s+/;
	$name = $words[0];
	$utf8val = $words[1];

	if (defined($unames{$utf8val})) {
	    $unames{$utf8val} .= "\n" .$name;
	} else {
	    $unames{$utf8val} = $name;
	}
	$uvalues{$name} = $utf8val;
    }
    close(UTF8);
}

my %map;

sub load_map
{
    my $file = shift;

    open(MAP, "$file") || die "open";

    while (<MAP>) {
	next if (/^#/);
	next if (/^\s*$/);
	next if (/^0x..\+0x../);
	next if (/^0x[0-9A-F]{4}\t0x[0-9A-F]{4} 0x[0-9A-F]{4}/);
	next if (/^0x[0-9A-F]{2}\s+#/);
	next if (/# ... NO MAPPING .../);
	chomp;
	@words = split /\s+/;
	$utf8 = $words[1];
	$utf8 =~ s/^\\x[0]*//;
	$utf8 = ucs_to_utf8(hex($utf8));
	$val = $words[0];
	if (defined ($map{$val})) {
	    $map{$val} .= " ".$utf8;
	} else {
	    $map{$val} = $utf8;
	}
    }
}

sub mb_str
{
    my $val = shift;
    my $str = "";
    $val = hex($val);

    if ($val == 0) {
	return ("\\x00");
    }
    while ($val) {
	$str = sprintf("\\x%02x", $val & 0xff).$str;
	$val >>= 8;
    }
    return ($str);
}

$mf = shift(@ARGV);
$codeset = shift(@ARGV);
my $max_mb;

load_utf8_cm("etc/final-maps/map.UTF-8");
load_map($mf);


   if ($codeset eq "SJIS")      { $max_mb = 2 }
elsif ($codeset eq "eucCN")     { $max_mb = 2 }
elsif ($codeset eq "eucJP")     { $max_mb = 3 }
elsif ($codeset eq "eucKR")     { $max_mb = 2 }
elsif ($codeset eq "GBK")       { $max_mb = 2 }
elsif ($codeset eq "GB2312")    { $max_mb = 2 }
elsif ($codeset eq "Big5")      { $max_mb = 2 }
else { $max_mb = 1 };
print("<code_set_name> \"$codeset\"\n");
print("<mb_cur_min> 1\n");
print("<mb_cur_max> $max_mb\n");

print("CHARMAP\n");
foreach $val (sort (keys (%map))) {
    #$utf8 = $map{$val};
    foreach $utf8 (split / /, $map{$val}) {
	$ref = $unames{$utf8};
	foreach $name (sort (split /\n/, $ref)) {
	    print "$name";
	    my $nt = int((64 - length($name) + 7) / 8);
	    while ($nt) {
		print "\t";
		$nt--;
	    }
	    print mb_str($val)."\n";
	}
    }
}
print "END CHARMAP\n";
