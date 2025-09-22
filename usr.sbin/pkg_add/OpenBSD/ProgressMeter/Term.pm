# ex:ts=8 sw=4:
# $OpenBSD: Term.pm,v 1.45 2023/10/18 08:50:13 espie Exp $
#
# Copyright (c) 2004-2007 Marc Espie <espie@openbsd.org>
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

use v5.36;
use warnings;

package OpenBSD::PackingElement;
sub size_and($self, $p, $method, @r)
{
	$p->advance($self);
	$self->$method(@r);
}

sub compute_count($self, $count)
{
	$$count++;
}

sub count_and($self, $progress, $done, $total, $method, @r)
{
	$$done ++;
	$progress->show($$done, $total);
	$self->$method(@r);
}

package OpenBSD::ProgressMeter::Real;
our @ISA = qw(OpenBSD::ProgressMeter);

sub ntogo($self, $state, $offset = 0)
{
	return $state->ntodo($offset);
}

sub compute_count($progress, $plist)
{
	my $total = 0;
	$plist->compute_count(\$total);
	$total = 1 if $total == 0;
	return $total;
}

sub visit_with_size($progress, $plist, $method, @r)
{
	my $p = $progress->new_sizer($plist);
	$plist->size_and($p, $method, $progress->{state}, @r);
}

sub sizer_class($)
{
	"ProgressSizer"
}

sub visit_with_count($progress, $plist, $method, @r)
{
	$plist->{total} //= $progress->compute_count($plist);
	my $count = 0;
	$progress->show($count, $plist->{total});
	$plist->count_and($progress, \$count, $plist->{total},
	    $method, $progress->{state}, @r);
}

package OpenBSD::ProgressMeter::Term;
our @ISA = qw(OpenBSD::ProgressMeter::Real);
use POSIX;
use Term::Cap;

sub width($self)
{
	return $self->{state}->width;
}

sub forked($self)
{
	$self->{lastdisplay} = ' 'x($self->width-1);
}

sub init($self)
{
	my $oldfh = select(STDOUT);
	$| = 1;
	select($oldfh);
	$self->{lastdisplay} = '';
	$self->{continued} = 0;
	$self->{work} = 0;
	$self->{header} = '';
	return unless defined $ENV{TERM} || defined $ENV{TERMCAP};
	my $termios = POSIX::Termios->new;
	$termios->getattr(0);
	my $terminal;
	eval {
		$terminal = 
		    Term::Cap->Tgetent({ OSPEED => $termios->getospeed});
	};
	if ($@) {
		chomp $@;
		$@ =~ s/\s+at\s+\/.*\s+line\s+.*//;
		$self->{state}->errsay("No progress meter: #1", $@);
		bless $self, "OpenBSD::ProgressMeter::Stub";
		return;
	}
	$self->{glitch} = $terminal->{_xn};
	$self->{cleareol} = $terminal->Tputs("ce", 1);
	$self->{hpa} = $terminal->Tputs("ch", 1);
	if (!defined $self->{hpa}) {
		# XXX this works with screen and tmux
		$self->{cuf} = $terminal->Tputs("RI", 1);
		if (defined $self->{cuf}) {
			$self->{hpa} = "\r".$self->{cuf};
		}
	}
}

sub compute_playfield($self)
{
	$self->{playfield} = $self->width - length($self->{header}) - 7;
	if ($self->{playfield} < 5) {
		$self->{playfield} = 0;
	}
}

sub set_header($self, $header)
{
	$self->{header} = $header;
	$self->compute_playfield;
	return 1;
}

sub hmove($self, $v)
{
	my $seq = $self->{hpa};
	$seq =~ s/\%i// and $v++;
	$seq =~ s/\%n// and $v ^= 0140;
	$seq =~ s/\%B// and $v = 16 * ($v/10) + $v%10;
	$seq =~ s/\%D// and $v = $v - 2*($v%16);
	$seq =~ s/\%\./sprintf('%c', $v)/e;
	$seq =~ s/\%d/sprintf('%d', $v)/e;
	$seq =~ s/\%2/sprintf('%2d', $v)/e;
	$seq =~ s/\%3/sprintf('%3d', $v)/e;
	$seq =~ s/\%\+(.)/sprintf('%c', $v+ord($1))/e;
	$seq =~ s/\%\%/\%/g;
	return $seq;
}

sub _show($self, $extra = undef, $stars = undef)
{
	my $d = $self->{header};
	my $prefix = length($d);
	if (defined $extra) {
		$d.="|$extra";
		$prefix++;
	}
	if ($self->width > length($d)) {
		if ($self->{cleareol}) {
			$d .= $self->{cleareol};
		} else {
			$d .= ' 'x($self->width - length($d) - 1);
		}
	}

	if ($self->{continued}) {
		print "\r$d";
		$self->{continued} = 0;
		$self->{lastdisplay} = $d;
		return;
	}

	return if $d eq $self->{lastdisplay};


	if (defined $self->{hpa}) {
		if (defined $stars && defined $self->{stars}) {
			$prefix += $self->{stars};
		}
	}
	if (defined $self->{hpa} && substr($self->{lastdisplay}, 0, $prefix) eq
	    substr($d, 0, $prefix)) {
		print $self->hmove($prefix), substr($d, $prefix);
	} else {
		print "\r$d";
	}
	$self->{lastdisplay} = $d;
}

sub message($self, $message)
{
	return unless $self->can_output;
	if ($self->{cleareol}) {
		$message .= $self->{cleareol};
	} elsif ($self->{playfield} > length($message)) {
		$message .= ' 'x($self->{playfield} - length($message));
	}
	if ($self->{playfield}) {
		$self->_show(substr($message, 0, $self->{playfield}));
	} else {
		$self->_show;
	}
}

sub show($self, $current, $total)
{
	return unless $self->can_output;

	if ($self->{playfield}) {
		my $stars = int (($current * $self->{playfield}) / $total + 0.5);
		my $percent = int (($current * 100)/$total + 0.5);
		if ($percent < 100) {
			$self->_show('*'x$stars.' 'x($self->{playfield}-$stars)."| ".$percent."\%", $stars);
		} else {
			$self->_show('*'x$self->{playfield}."|100\%", $stars);
		}
		$self->{stars} = $stars;
	} else {
	    	$self->_show;
	}
}

sub working($self, $slowdown)
{
	$self->{work}++;
	return if $self->{work} < $slowdown;
	$self->message(substr("/-\\|", ($self->{work}/$slowdown) % 4, 1));
}

sub clear($self)
{
	return unless length($self->{lastdisplay}) > 0;
	if ($self->can_output) {
		if ($self->{cleareol}) {
			print "\r", $self->{cleareol};
		} else {
			print "\r", ' 'x length($self->{lastdisplay}), "\r";
		}
	}
	$self->{lastdisplay} = '';
	delete $self->{stars};
}

sub disable($self)
{
	print "\n" if length($self->{lastdisplay}) > 0 and $self->can_output;

	bless $self, "OpenBSD::ProgressMeter::Stub";
}

sub next($self, $todo = 'ok')
{
	$self->clear;

	$todo //= 'ok';
	print "\r$self->{header}: $todo\n" if $self->can_output;
}

package ProgressSizer;
our @ISA = qw(PureSizer);

sub new($class, $progress, $plist)
{
	my $p = $class->SUPER::new($progress, $plist);
	$progress->show(0, $p->{totsize});
	if (defined $progress->{state}{archive}) {
		$progress->{state}{archive}->set_callback(
		    sub($done) {
			$progress->show($p->{donesize} + $done, $p->{totsize});
		});
	}
	return $p;
}

sub advance($self, $e)
{
	if (defined $e->{size}) {
		$self->{donesize} += $e->{size};
		$self->{progress}->show($self->{donesize}, $self->{totsize});
	}
}

1;
