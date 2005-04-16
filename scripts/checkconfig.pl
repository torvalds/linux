#! /usr/bin/perl
#
# checkconfig: find uses of CONFIG_* names without matching definitions.
# Copyright abandoned, 1998, Michael Elizabeth Chastain <mailto:mec@shout.net>.

use integer;

$| = 1;

foreach $file (@ARGV)
{
    # Open this file.
    open(FILE, $file) || die "Can't open $file: $!\n";

    # Initialize variables.
    my $fInComment   = 0;
    my $fInString    = 0;
    my $fUseConfig   = 0;
    my $iLinuxConfig = 0;
    my %configList   = ();

    LINE: while ( <FILE> )
    {
	# Strip comments.
	$fInComment && (s+^.*?\*/+ +o ? ($fInComment = 0) : next);
	m+/\*+o && (s+/\*.*?\*/+ +go, (s+/\*.*$+ +o && ($fInComment = 1)));

	# Pick up definitions.
	if ( m/^\s*#/o )
	{
	    $iLinuxConfig      = $. if m/^\s*#\s*include\s*"linux\/config\.h"/o;
	    $configList{uc $1} = 1  if m/^\s*#\s*include\s*"config\/(\S*)\.h"/o;
	}

	# Strip strings.
	$fInString && (s+^.*?"+ +o ? ($fInString = 0) : next);
	m+"+o && (s+".*?"+ +go, (s+".*$+ +o && ($fInString = 1)));

	# Pick up definitions.
	if ( m/^\s*#/o )
	{
	    $iLinuxConfig      = $. if m/^\s*#\s*include\s*<linux\/config\.h>/o;
	    $configList{uc $1} = 1  if m/^\s*#\s*include\s*<config\/(\S*)\.h>/o;
	    $configList{$1}    = 1  if m/^\s*#\s*define\s+CONFIG_(\w*)/o;
	    $configList{$1}    = 1  if m/^\s*#\s*undef\s+CONFIG_(\w*)/o;
	}

	# Look for usages.
	next unless m/CONFIG_/o;
	WORD: while ( m/\bCONFIG_(\w+)/og )
	{
	    $fUseConfig = 1;
	    last LINE if $iLinuxConfig;
	    next WORD if exists $configList{$1};
	    print "$file: $.: need CONFIG_$1.\n";
	    $configList{$1} = 0;
	}
    }

    # Report superfluous includes.
    if ( $iLinuxConfig && ! $fUseConfig )
	{ print "$file: $iLinuxConfig: linux/config.h not needed.\n"; }

    close(FILE);
}
