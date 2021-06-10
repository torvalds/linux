#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-2.0
#
# Generates a list of Control-Flow Integrity (CFI) jump table symbols
# for kallsyms.
#
# Copyright (C) 2021 Google LLC

use strict;
use warnings;

## parameters
my $ismodule = 0;
my $file;

foreach (@ARGV) {
	if ($_ eq '--module') {
		$ismodule = 1;
	} elsif (!defined($file)) {
		$file = $_;
	} else {
		die "$0: usage $0 [--module] binary";
	}
}

## environment
my $readelf = $ENV{'READELF'} || die "$0: ERROR: READELF not set?";
my $objdump = $ENV{'OBJDUMP'} || die "$0: ERROR: OBJDUMP not set?";
my $nm = $ENV{'NM'} || die "$0: ERROR: NM not set?";

## jump table addresses
my $cfi_jt = {};
## text symbols
my $text_symbols = {};

## parser state
use constant {
	UNKNOWN => 0,
	SYMBOL	=> 1,
	HINT	=> 2,
	BRANCH	=> 3,
	RELOC	=> 4
};

## trims leading zeros from a string
sub trim_zeros {
	my ($n) = @_;
	$n =~ s/^0+//;
	$n = 0 if ($n eq '');
	return $n;
}

## finds __cfi_jt_* symbols from the binary to locate the start and end of the
## jump table
sub find_cfi_jt {
	open(my $fh, "\"$readelf\" --symbols \"$file\" 2>/dev/null | grep __cfi_jt_ |")
		or die "$0: ERROR: failed to execute \"$readelf\": $!";

	while (<$fh>) {
		chomp;

		my ($addr, $name) = $_ =~ /\:.*([a-f0-9]{16}).*\s__cfi_jt_(.*)/;
		if (defined($addr) && defined($name)) {
			$cfi_jt->{$name} = $addr;
		}
	}

	close($fh);

	die "$0: ERROR: __cfi_jt_start symbol missing" if !exists($cfi_jt->{"start"});
	die "$0: ERROR: __cfi_jt_end symbol missing"   if !exists($cfi_jt->{"end"});
}

my $last = UNKNOWN;
my $last_symbol;
my $last_hint_addr;
my $last_branch_addr;
my $last_branch_target;
my $last_reloc_target;

sub is_symbol {
	my ($line) = @_;
	my ($addr, $symbol) = $_ =~ /^([a-f0-9]{16})\s<([^>]+)>\:/;

	if (defined($addr) && defined($symbol)) {
		$last = SYMBOL;
		$last_symbol = $symbol;
		return 1;
	}

	return 0;
}

sub is_hint {
	my ($line) = @_;
	my ($hint) = $_ =~ /^\s*([a-f0-9]+)\:.*\s+hint\s+#/;

	if (defined($hint)) {
		$last = HINT;
		$last_hint_addr = $hint;
		return 1;
	}

	return 0;
}

sub find_text_symbol {
	my ($target) = @_;

	my ($symbol, $expr, $offset) = $target =~ /^(\S*)([-\+])0x([a-f0-9]+)?$/;

	if (!defined($symbol) || !defined(!$expr) || !defined($offset)) {
		return $target;
	}

	if ($symbol =~ /^\.((init|exit)\.)?text$/ && $expr eq '+') {
		$offset = trim_zeros($offset);
		my $actual = $text_symbols->{"$symbol+$offset"};

		if (!defined($actual)) {
			die "$0: unknown symbol at $symbol+0x$offset";
		}

		$symbol = $actual;
	}

	return $symbol;
}

sub is_branch {
	my ($line) = @_;
	my ($addr, $instr, $branch_target) = $_ =~
		/^\s*([a-f0-9]+)\:.*(b|jmpq?)\s+0x[a-f0-9]+\s+<([^>]+)>/;

	if (defined($addr) && defined($instr) && defined($branch_target)) {
		if ($last eq HINT) {
			$last_branch_addr = $last_hint_addr;
		} else {
			$last_branch_addr = $addr;
		}

		$last = BRANCH;
		$last_branch_target = find_text_symbol($branch_target);
		return 1;
	}

	return 0;
}

sub is_branch_reloc {
	my ($line) = @_;

	if ($last ne BRANCH) {
		return 0;
	}

	my ($addr, $type, $reloc_target) = /\s*([a-f0-9]{16})\:\s+R_(\S+)\s+(\S+)$/;

	if (defined($addr) && defined($type) && defined($reloc_target)) {
		$last = RELOC;
		$last_reloc_target = find_text_symbol($reloc_target);
		return 1;
	}

	return 0;
}

## walks through the jump table looking for branches and prints out a jump
## table symbol for each branch if one is missing
sub print_missing_symbols {
	my @symbols;

	open(my $fh, "\"$objdump\" -d -r " .
		"--start-address=0x" . $cfi_jt->{"start"} .
		" --stop-address=0x" . $cfi_jt->{"end"} .
		" \"$file\" 2>/dev/null |")
		or die "$0: ERROR: failed to execute \"$objdump\": $!";

	while (<$fh>) {
		chomp;

		if (is_symbol($_) || is_hint($_)) {
			next;
		}

		my $cfi_jt_symbol;

		if (is_branch($_)) {
			if ($ismodule) {
				next; # wait for the relocation
			}

			$cfi_jt_symbol = $last_branch_target;
		} elsif (is_branch_reloc($_)) {
			$cfi_jt_symbol = $last_reloc_target;
		} else {
			next;
		}

		# ignore functions with a canonical jump table
		if ($cfi_jt_symbol =~ /\.cfi$/) {
			next;
		}

		$cfi_jt_symbol .= ".cfi_jt";
		$cfi_jt->{$last_branch_addr} = $cfi_jt_symbol;

		if (defined($last_symbol) && $last_symbol eq $cfi_jt_symbol) {
			next; # already exists
		}

		# print out the symbol
		if ($ismodule) {
			push(@symbols, "\t\t$cfi_jt_symbol = . + 0x$last_branch_addr;");
		} else {
			push(@symbols, "$last_branch_addr t $cfi_jt_symbol");
		}
	}

	close($fh);

	if (!scalar(@symbols)) {
		return;
	}

	if ($ismodule) {
		print "SECTIONS {\n";
		# With -fpatchable-function-entry, LLD isn't happy without this
		print "\t__patchable_function_entries : { *(__patchable_function_entries) }\n";
		print "\t.text : {\n";
	}

	foreach (@symbols) {
		print "$_\n";
	}

	if ($ismodule) {
		print "\t}\n}\n";
	}
}

## reads defined text symbols from the file
sub read_symbols {
	open(my $fh, "\"$objdump\" --syms \"$file\" 2>/dev/null |")
		or die "$0: ERROR: failed to execute \"$nm\": $!";

	while (<$fh>) {
		chomp;

		# llvm/tools/llvm-objdump/objdump.cpp:objdump::printSymbol
		my ($addr, $debug, $section, $ref, $symbol) = $_ =~
			/^([a-f0-9]{16})\s.{5}(.).{2}(\S+)\s[a-f0-9]{16}(\s\.\S+)?\s(.*)$/;

		if (defined($addr) && defined($section) && defined($symbol)) {
			if (!($section =~ /^\.((init|exit)\.)?text$/)) {
				next;
			}
			# skip arm mapping symbols
			if ($symbol =~ /^\$[xd]\.\d+$/) {
				next;
			}
			if (defined($debug) && $debug eq "d") {
				next;
			}

			$addr = trim_zeros($addr);
			$text_symbols->{"$section+$addr"} = $symbol;
		}
	}

	close($fh);
}

## prints out the remaining symbols from nm -n, filtering out the unnecessary
## __typeid__ symbols aliasing the jump table symbols we added
sub print_kallsyms {
	open(my $fh, "\"$nm\" -n \"$file\" 2>/dev/null |")
		or die "$0: ERROR: failed to execute \"$nm\": $!";

	while (<$fh>) {
		chomp;

		my ($addr, $symbol) = $_ =~ /^([a-f0-9]{16})\s.\s(.*)$/;

		if (defined($addr) && defined($symbol)) {
			# drop duplicate __typeid__ symbols
			if ($symbol =~ /^__typeid__.*_global_addr$/ &&
				exists($cfi_jt->{$addr})) {
				next;
			}
		}

		print "$_\n";
	}

	close($fh);
}

## main
find_cfi_jt();

if ($ismodule) {
	read_symbols();
	print_missing_symbols();
} else {
	print_missing_symbols();
	print_kallsyms();
}
