#! /usr/bin/perl
#
# Detect cycles in the header file dependency graph
# Vegard Nossum <vegardno@ifi.uio.no>
#

use strict;
use warnings;

use Getopt::Long;

my $opt_all;
my @opt_include;
my $opt_graph;

&Getopt::Long::Configure(qw(bundling pass_through));
&GetOptions(
	help	=> \&help,
	version	=> \&version,

	all	=> \$opt_all,
	I	=> \@opt_include,
	graph	=> \$opt_graph,
);

push @opt_include, 'include';
my %deps = ();
my %linenos = ();

my @headers = grep { strip($_) } @ARGV;

parse_all(@headers);

if($opt_graph) {
	graph();
} else {
	detect_cycles(@headers);
}


sub help {
	print "Usage: $0 [options] file...\n";
	print "\n";
	print "Options:\n";
	print "  --all\n";
	print "  --graph\n";
	print "\n";
	print "  -I includedir\n";
	print "\n";
	print "To make nice graphs, try:\n";
	print "  $0 --graph include/linux/kernel.h | dot -Tpng -o graph.png\n";
	exit;
}

sub version {
	print "headerdep version 2\n";
	exit;
}

# Get a file name that is relative to our include paths
sub strip {
	my $filename = shift;

	for my $i (@opt_include) {
		my $stripped = $filename;
		$stripped =~ s/^$i\///;

		return $stripped if $stripped ne $filename;
	}

	return $filename;
}

# Search for the file name in the list of include paths
sub search {
	my $filename = shift;
	return $filename if -f $filename;

	for my $i (@opt_include) {
		my $path = "$i/$filename";
		return $path if -f $path;
	}

	return undef;
}

sub parse_all {
	# Parse all the headers.
	my @queue = @_;
	while(@queue) {
		my $header = pop @queue;
		next if exists $deps{$header};

		$deps{$header} = [] unless exists $deps{$header};

		my $path = search($header);
		next unless $path;

		open(my $file, '<', $path) or die($!);
		chomp(my @lines = <$file>);
		close($file);

		for my $i (0 .. $#lines) {
			my $line = $lines[$i];
			if(my($dep) = ($line =~ m/^#\s*include\s*<(.*?)>/)) {
				push @queue, $dep;
				push @{$deps{$header}}, [$i + 1, $dep];
			}
		}
	}
}

sub print_cycle {
	# $cycle[n] includes $cycle[n + 1];
	# $cycle[-1] will be the culprit
	my $cycle = shift;

	# Adjust the line numbers
	for my $i (0 .. $#$cycle - 1) {
		$cycle->[$i]->[0] = $cycle->[$i + 1]->[0];
	}
	$cycle->[-1]->[0] = 0;

	my $first = shift @$cycle;
	my $last = pop @$cycle;

	my $msg = "In file included";
	printf "%s from %s,\n", $msg, $last->[1] if defined $last;

	for my $header (reverse @$cycle) {
		printf "%s from %s:%d%s\n",
			" " x length $msg,
			$header->[1], $header->[0],
			$header->[1] eq $last->[1] ? ' <-- here' : '';
	}

	printf "%s:%d: warning: recursive header inclusion\n",
		$first->[1], $first->[0];
}

# Find and print the smallest cycle starting in the specified node.
sub detect_cycles {
	my @queue = map { [[0, $_]] } @_;
	while(@queue) {
		my $top = pop @queue;
		my $name = $top->[-1]->[1];

		for my $dep (@{$deps{$name}}) {
			my $chain = [@$top, [$dep->[0], $dep->[1]]];

			# If the dep already exists in the chain, we have a
			# cycle...
			if(grep { $_->[1] eq $dep->[1] } @$top) {
				print_cycle($chain);
				next if $opt_all;
				return;
			}

			push @queue, $chain;
		}
	}
}

sub mangle {
	$_ = shift;
	s/\//__/g;
	s/\./_/g;
	s/-/_/g;
	$_;
}

# Output dependency graph in GraphViz language.
sub graph {
	print "digraph {\n";

	print "\t/* vertices */\n";
	for my $header (keys %deps) {
		printf "\t%s [label=\"%s\"];\n",
			mangle($header), $header;
	}

	print "\n";

	print "\t/* edges */\n";
	for my $header (keys %deps) {
		for my $dep (@{$deps{$header}}) {
			printf "\t%s -> %s;\n",
				mangle($header), mangle($dep->[1]);
		}
	}

	print "}\n";
}
