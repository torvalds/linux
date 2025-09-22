# ex:ts=8 sw=4:
# $OpenBSD: ProgressMeter.pm,v 1.54 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2010 Marc Espie <espie@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

use v5.36;

package OpenBSD::PackingElement;
sub compute_size($self, $totsize)
{
	$$totsize += $self->{size} if defined $self->{size};
}

package OpenBSD::ProgressMeter;
sub new($)
{
	bless {}, "OpenBSD::ProgressMeter::Stub";
}

sub compute_size($self, $plist)
{
	my $totsize = 0;
	$plist->compute_size(\$totsize);
	$totsize = 1 if $totsize == 0;
	return $totsize;
}

sub setup($self, $opt_x, $opt_m, $state)
{
	$self->{state} = $state;
	if ($opt_m || (!$opt_x && -t STDOUT)) {
		require OpenBSD::ProgressMeter::Term;
		bless $self, "OpenBSD::ProgressMeter::Term";
		$self->init;
	}
}

sub disable($) {}

sub new_sizer($progress, $plist)
{
	return $progress->sizer_class->new($progress, $plist);
}

sub sizer_class($)
{
	"PureSizer"
}

sub for_list($self, $msg, $l, $code)
{
	if (defined $msg) {
		$self->set_header($msg);
	}
	my $total = scalar @$l;
	my $i = 0;
	for my $e (@$l) {
		$self->show(++$i, $total);
		&$code($e);
	}
	$self->next;
}

sub compute_playfield($)
{
}

sub handle_continue($self)
{
	$self->{continued} = 1;
}

sub can_output($self)
{
	return $self->{state}->can_output;
}

# stub class when no actual progressmeter that still prints out.
# see methods documentation under ProgressMeter::Term
package OpenBSD::ProgressMeter::Stub;
our @ISA = qw(OpenBSD::ProgressMeter);

sub forked($) {}

sub clear($) {}


sub show($, $, $) {}

sub working($, $) {}
sub message($, $) {}

sub next($, $ = undef) {}

sub set_header($, $) {}

sub ntogo($, $, $ = undef)
{
	return "";
}

sub visit_with_size($progress, $plist, $method, @r)
{
	$plist->$method($progress->{state}, @r);
}

sub visit_with_count	# forwarder
{
	&OpenBSD::ProgressMeter::Stub::visit_with_size;
}

package PureSizer;

sub new($class, $progress, $plist)
{
	$plist->{totsize} //= $progress->compute_size($plist);
	bless {
	    progress => $progress, 
	    totsize => $plist->{totsize},
	    donesize => 0,
	    }, $class;
}

sub advance($self, $e)
{
	if (defined $e->{size}) {
		$self->{donesize} += $e->{size};
	}
}

1;
