#! /usr/bin/perl
#
# checkversion find uses of LINUX_VERSION_CODE or KERNEL_VERSION
# without including <linux/version.h>, or cases of
# including <linux/version.h> that don't need it.
# Copyright (C) 2003, Randy Dunlap <rdunlap@xenotime.net>

$| = 1;

my $debugging = 0;

foreach $file (@ARGV)
{
    # Open this file.
    open(FILE, $file) || die "Can't open $file: $!\n";

    # Initialize variables.
    my $fInComment   = 0;
    my $fInString    = 0;
    my $fUseVersion   = 0;
    my $iLinuxVersion = 0;

    LINE: while ( <FILE> )
    {
	# Strip comments.
	$fInComment && (s+^.*?\*/+ +o ? ($fInComment = 0) : next);
	m+/\*+o && (s+/\*.*?\*/+ +go, (s+/\*.*$+ +o && ($fInComment = 1)));

	# Pick up definitions.
	if ( m/^\s*#/o ) {
	    $iLinuxVersion      = $. if m/^\s*#\s*include\s*"linux\/version\.h"/o;
	}

	# Strip strings.
	$fInString && (s+^.*?"+ +o ? ($fInString = 0) : next);
	m+"+o && (s+".*?"+ +go, (s+".*$+ +o && ($fInString = 1)));

	# Pick up definitions.
	if ( m/^\s*#/o ) {
	    $iLinuxVersion      = $. if m/^\s*#\s*include\s*<linux\/version\.h>/o;
	}

	# Look for uses: LINUX_VERSION_CODE, KERNEL_VERSION, UTS_RELEASE
	if (($_ =~ /LINUX_VERSION_CODE/) || ($_ =~ /\WKERNEL_VERSION/)) {
	    $fUseVersion = 1;
	    last LINE if $iLinuxVersion;
	}
    }

    # Report used version IDs without include?
    if ($fUseVersion && ! $iLinuxVersion) {
	print "$file: $.: need linux/version.h\n";
    }

    # Report superfluous includes.
    if ($iLinuxVersion && ! $fUseVersion) {
	print "$file: $iLinuxVersion linux/version.h not needed.\n";
    }

    # debug: report OK results:
    if ($debugging) {
        if ($iLinuxVersion && $fUseVersion) {
	    print "$file: version use is OK ($iLinuxVersion)\n";
        }
        if (! $iLinuxVersion && ! $fUseVersion) {
	    print "$file: version use is OK (none)\n";
        }
    }

    close(FILE);
}
