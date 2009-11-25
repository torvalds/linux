package Perf::Trace::Context;

use 5.010000;
use strict;
use warnings;

require Exporter;

our @ISA = qw(Exporter);

our %EXPORT_TAGS = ( 'all' => [ qw(
) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw(
	common_pc common_flags common_lock_depth
);

our $VERSION = '0.01';

require XSLoader;
XSLoader::load('Perf::Trace::Context', $VERSION);

1;
__END__
=head1 NAME

Perf::Trace::Context - Perl extension for accessing functions in perf.

=head1 SYNOPSIS

  use Perf::Trace::Context;

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
