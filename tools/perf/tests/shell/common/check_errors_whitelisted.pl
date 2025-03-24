#!/usr/bin/perl
# SPDX-License-Identifier: GPL-2.0

$whitelist_file = shift;

if (defined $whitelist_file)
{
	open (INFILE, $whitelist_file) or die "Checker error: Unable to open the whitelist file: $whitelist_file\n";
	@regexps = <INFILE>;
	close INFILE or die "Checker error: Unable to close the whitelist file: $whitelist_file\n";
}
else
{
	@regexps = ();
}

$max_printed_lines = 20;
$max_printed_lines = $ENV{TESTLOG_ERR_MSG_MAX_LINES} if (defined $ENV{TESTLOG_ERR_MSG_MAX_LINES});

$quiet = 1;
$quiet = 0 if (defined $ENV{TESTLOG_VERBOSITY} && $ENV{TESTLOG_VERBOSITY} ge 2);

$passed = 1;
$lines_printed = 0;

while (<STDIN>)
{
	s/\n//;

	$line_matched = 0;
	for $r (@regexps)
	{
		chomp $r;
		if (/$r/)
		{
			$line_matched = 1;
			last;
		}
	}

	unless ($line_matched)
	{
		if ($lines_printed++ < $max_printed_lines)
		{
			print "Line did not match any pattern: \"$_\"\n" unless $quiet;
		}
		$passed = 0;
	}
}

exit ($passed == 0);
