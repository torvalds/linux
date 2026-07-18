#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0

use warnings;
use strict;

my $srctree = shift @ARGV;
unless (defined $srctree) {
	$srctree = `git rev-parse --show-toplevel 2>/dev/null`;
	chomp $srctree;
	my $msg = "Usage: $0 <srctree> [excludes file]\n";
	$msg .= "Please provide <srctree>.";
	$msg .= " Is it '$srctree'?" if $srctree;
	$msg .= "\n";
	die $msg;
}
my $kconfig_sym_check_excludes = defined $ARGV[0] ? $ARGV[0] : undef;

sub indent_depth {
	my ($ws) = @_;
	my $col = 0;
	for my $c (split //, $ws) {
		$col = $c eq "\t" ? int($col / 8) * 8 + 8 : $col + 1;
	}
	return $col;
}

my @files = `git -C \Q$srctree\E ls-files '*Kconfig*' 2>/dev/null`;
if (@files) {
	chomp @files;
	@files = map { "$srctree/$_" } @files;
} else {
	@files = `find \Q$srctree\E -name '*Kconfig*'`;
	chomp @files;
}

@files = grep { !m{/scripts/kconfig/tests/} } @files;

my %configs = ();
my %refs = ();

foreach my $file (@files) {
	open F, $file or die "Cannot open $file: $!";

	my $help = 0;
	my $help_level;
	my $level;

	while (<F>) {
		chomp;

		while (/\\\s*$/) {
			s/\\\s*$/ /;
			my $cont = <F> // last;
			chomp $cont;
			$_ .= $cont;
		}

		next if /^\s*$/;
		next if /^\s*#/;

		/^(\s*)/;
		$level = indent_depth($1);

		if ($help && $level < $help_level) {
			$help = 0;
		}

		next if ($help);

		if (/^\s*(help|\-\-\-help\-\-\-)$/) {
			$help = 1;
			my $next;
			while (defined($next = <F>)) {
				last unless $next =~ /^\s*(?:#.*)?$/;
			}
			last unless defined $next;
			$next =~ /^(\s*)/;
			if (indent_depth($1) >= $level) {
				$help_level = indent_depth($1);
			} else {
				$help = 0;
			}
			$_ = $next;
			redo;
		}

		if (/^\s*(config|menuconfig)\s+([a-zA-Z0-9_]+)\s*(#.*)?$/) {
			$configs{$2}++;
			next;
		}

		if (/^\s*(default|def_bool|def_tristate|select|depends\s+on|imply|visible\s+if|range|if|bool|tristate|int|hex|string|prompt)\s+(.+)\s*$/) {
			my $s = $2;
			$s =~ s/"(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*'//g;
			$s =~ s/#.*//;
			$s =~ s/\$\((?:[^()]*|\((?:[^()]*|\([^()]*\))*\))*\)//g;
			$s =~ s/%%[^%]*%%//g;
			my @syms = split /[^a-zA-Z0-9_]+/, $s;
			map {
				$refs{$_}++ if (/[a-zA-Z]/ && $_ ne "if" && $_ ne "y" && $_ ne "n" && $_ ne "m" && !/^0[xX][0-9a-fA-F]+$/);
			} @syms
		}
	}

	close F;
}

my %known_syms = ();
if (defined $kconfig_sym_check_excludes) {
	my $file = $kconfig_sym_check_excludes;
	open(F, "<", $file) or die "Cannot open $file: $!";
	while (<F>) {
		chomp;
		next if /^\s*$/;
		next if /^\s*#/;
		$known_syms{$1}++ if (/^\s*([a-zA-Z0-9_]+)\s*(#.*)?$/);
	}
}

my $ret = 0;
foreach my $k (sort keys %refs) {
	next if (exists $configs{$k} || exists $known_syms{$k});

	print "$k";
	print " - warning: '$k' is probably not what you want; Kconfig tristate literals are always lowercase ('n', 'y', 'm')" if ($k eq "N" || $k eq "Y" || $k eq "M");
	print "\n";

	$ret = 1;
}

exit $ret;
