package Perf::Trace::Core;

use 5.010000;
use strict;
use warnings;

require Exporter;

our @ISA = qw(Exporter);

our %EXPORT_TAGS = ( 'all' => [ qw(
) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw(
define_flag_field define_flag_value flag_str dump_flag_fields
define_symbolic_field define_symbolic_value symbol_str dump_symbolic_fields
trace_flag_str
);

our $VERSION = '0.01';

my %trace_flags = (0x00 => "NONE",
		   0x01 => "IRQS_OFF",
		   0x02 => "IRQS_NOSUPPORT",
		   0x04 => "NEED_RESCHED",
		   0x08 => "HARDIRQ",
		   0x10 => "SOFTIRQ");

sub trace_flag_str
{
    my ($value) = @_;

    my $string;

    my $print_delim = 0;

    foreach my $idx (sort {$a <=> $b} keys %trace_flags) {
	if (!$value && !$idx) {
	    $string .= "NONE";
	    last;
	}

	if ($idx && ($value & $idx) == $idx) {
	    if ($print_delim) {
		$string .= " | ";
	    }
	    $string .= "$trace_flags{$idx}";
	    $print_delim = 1;
	    $value &= ~$idx;
	}
    }

    return $string;
}

my %flag_fields;
my %symbolic_fields;

sub flag_str
{
    my ($event_name, $field_name, $value) = @_;

    my $string;

    if ($flag_fields{$event_name}{$field_name}) {
	my $print_delim = 0;
	foreach my $idx (sort {$a <=> $b} keys %{$flag_fields{$event_name}{$field_name}{"values"}}) {
	    if (!$value && !$idx) {
		$string .= "$flag_fields{$event_name}{$field_name}{'values'}{$idx}";
		last;
	    }
	    if ($idx && ($value & $idx) == $idx) {
		if ($print_delim && $flag_fields{$event_name}{$field_name}{'delim'}) {
		    $string .= " $flag_fields{$event_name}{$field_name}{'delim'} ";
		}
		$string .= "$flag_fields{$event_name}{$field_name}{'values'}{$idx}";
		$print_delim = 1;
		$value &= ~$idx;
	    }
	}
    }

    return $string;
}

sub define_flag_field
{
    my ($event_name, $field_name, $delim) = @_;

    $flag_fields{$event_name}{$field_name}{"delim"} = $delim;
}

sub define_flag_value
{
    my ($event_name, $field_name, $value, $field_str) = @_;

    $flag_fields{$event_name}{$field_name}{"values"}{$value} = $field_str;
}

sub dump_flag_fields
{
    for my $event (keys %flag_fields) {
	print "event $event:\n";
	for my $field (keys %{$flag_fields{$event}}) {
	    print "    field: $field:\n";
	    print "        delim: $flag_fields{$event}{$field}{'delim'}\n";
	    foreach my $idx (sort {$a <=> $b} keys %{$flag_fields{$event}{$field}{"values"}}) {
		print "        value $idx: $flag_fields{$event}{$field}{'values'}{$idx}\n";
	    }
	}
    }
}

sub symbol_str
{
    my ($event_name, $field_name, $value) = @_;

    if ($symbolic_fields{$event_name}{$field_name}) {
	foreach my $idx (sort {$a <=> $b} keys %{$symbolic_fields{$event_name}{$field_name}{"values"}}) {
	    if (!$value && !$idx) {
		return "$symbolic_fields{$event_name}{$field_name}{'values'}{$idx}";
		last;
	    }
	    if ($value == $idx) {
		return "$symbolic_fields{$event_name}{$field_name}{'values'}{$idx}";
	    }
	}
    }

    return undef;
}

sub define_symbolic_field
{
    my ($event_name, $field_name) = @_;

    # nothing to do, really
}

sub define_symbolic_value
{
    my ($event_name, $field_name, $value, $field_str) = @_;

    $symbolic_fields{$event_name}{$field_name}{"values"}{$value} = $field_str;
}

sub dump_symbolic_fields
{
    for my $event (keys %symbolic_fields) {
	print "event $event:\n";
	for my $field (keys %{$symbolic_fields{$event}}) {
	    print "    field: $field:\n";
	    foreach my $idx (sort {$a <=> $b} keys %{$symbolic_fields{$event}{$field}{"values"}}) {
		print "        value $idx: $symbolic_fields{$event}{$field}{'values'}{$idx}\n";
	    }
	}
    }
}

1;
__END__
=head1 NAME

Perf::Trace::Core - Perl extension for perf script

=head1 SYNOPSIS

  use Perf::Trace::Core

=head1 SEE ALSO

Perf (script) documentation

=head1 AUTHOR

Tom Zanussi, E<lt>tzanussi@gmail.com<gt>

=head1 COPYRIGHT AND LICENSE

Copyright (C) 2009 by Tom Zanussi

This library is free software; you can redistribute it and/or modify
it under the same terms as Perl itself, either Perl version 5.10.0 or,
at your option, any later version of Perl 5 you may have available.

Alternatively, this software may be distributed under the terms of the
GNU General Public License ("GPL") version 2 as published by the Free
Software Foundation.

=cut
