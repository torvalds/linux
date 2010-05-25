package Perf::Trace::Util;

use 5.010000;
use strict;
use warnings;

require Exporter;

our @ISA = qw(Exporter);

our %EXPORT_TAGS = ( 'all' => [ qw(
) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw(
avg nsecs nsecs_secs nsecs_nsecs nsecs_usecs print_nsecs
clear_term
);

our $VERSION = '0.01';

sub avg
{
    my ($total, $n) = @_;

    return $total / $n;
}

my $NSECS_PER_SEC    = 1000000000;

sub nsecs
{
    my ($secs, $nsecs) = @_;

    return $secs * $NSECS_PER_SEC + $nsecs;
}

sub nsecs_secs {
    my ($nsecs) = @_;

    return $nsecs / $NSECS_PER_SEC;
}

sub nsecs_nsecs {
    my ($nsecs) = @_;

    return $nsecs % $NSECS_PER_SEC;
}

sub nsecs_str {
    my ($nsecs) = @_;

    my $str = sprintf("%5u.%09u", nsecs_secs($nsecs), nsecs_nsecs($nsecs));

    return $str;
}

sub clear_term
{
    print "\x1b[H\x1b[2J";
}

1;
__END__
=head1 NAME

Perf::Trace::Util - Perl extension for perf trace

=head1 SYNOPSIS

  use Perf::Trace::Util;

=head1 SEE ALSO

Perf (trace) documentation

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
