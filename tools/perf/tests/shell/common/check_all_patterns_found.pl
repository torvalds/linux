#!/usr/bin/perl
# SPDX-License-Identifier: GPL-2.0

@regexps = @ARGV;

$quiet = 1;
$quiet = 0 if (defined $ENV{TESTLOG_VERBOSITY} && $ENV{TESTLOG_VERBOSITY} ge 2);

%found = ();
$passed = 1;

while (<STDIN>)
{
	s/\n//;

	for $r (@regexps)
	{
		if (/$r/)
		{
			$found{$r} = 1;	# FIXME: maybe add counters -- how many times was the regexp matched
		}
	}
}

for $r (@regexps)
{
	unless (exists $found{$r})
	{
		print "Regexp not found: \"$r\"\n" unless $quiet;
		$passed = 0;
	}
}

exit ($passed == 0);
