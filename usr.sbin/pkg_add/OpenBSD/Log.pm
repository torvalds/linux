# ex:ts=8 sw=4:
# $OpenBSD: Log.pm,v 1.11 2024/10/01 18:48:29 tb Exp $
#
# Copyright (c) 2007-2010 Marc Espie <espie@openbsd.org>
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
#

use v5.36;

package OpenBSD::Log;

sub new($class, $printer)
{
	bless { p => $printer }, $class;
}

sub set_context($self, $context)
{
	$self->{context} = $context;
}

sub messages($self)
{
	$self->{context} //= "???";
	return $self->{messages}{$self->{context}} //= [];
}

sub errmessages($self)
{
	$self->{context} //= "???";
	return $self->{errmessages}{$self->{context}} //= [];
}

sub f($self, @p)
{
	$self->{p}->f(@p);
}

sub print($self, @p)
{
	push(@{$self->messages}, $self->f(@p));
}

sub say($self, @p)
{
	push(@{$self->messages}, $self->f(@p)."\n");
}

sub errprint($self, @p)
{
	push(@{$self->errmessages}, $self->f(@p));
}

sub errsay($self, @p)
{
	push(@{$self->errmessages}, $self->f(@p)."\n");
}

sub specialsort(@p)
{
	return ((sort grep { /^\-/ } @p), (sort grep { /^\+/} @p),
	    (sort grep { !/^[\-+]/ } @p));
}

sub dump($self)
{
	for my $ctxt (specialsort keys %{$self->{errmessages}}) {
		my $msgs = $self->{errmessages}{$ctxt};
		if (@$msgs > 0) {
			$self->{p}->errsay("--- #1 -------------------", $ctxt);
			$self->{p}->_errprint(@$msgs);
		}
	}
	$self->{errmessages} = {};
	for my $ctxt (specialsort keys %{$self->{messages}}) {
		my $msgs = $self->{messages}{$ctxt};
		if (@$msgs > 0) {
			$self->{p}->say("--- #1 -------------------", $ctxt);
			$self->{p}->_print(@$msgs);
		}
	}
	$self->{messages} = {};
}

sub fatal($self, @p)
{
	if (defined $self->{context}) {
		$self->{p}->_fatal($self->{context}, ":", $self->f(@p));
	}

	$self->{p}->_fatal($self->f(@p));
}

sub system($self, @p)
{
	my ($todo, $todo2);
	if (ref $p[0] eq 'CODE') {
		$todo = shift @p;
	} else {
		$todo = sub() {};
	}
	if (ref $p[0] eq 'CODE') {
		$todo2 = shift @p;
	} else {
		$todo2 = sub() {};
	}
	my $child_pid = open(my $grab, "-|");
	if (!defined $child_pid) {
		$self->{p}->say("system(#1) was not run: #2 #3",
		    join(", ", @p), $!, $self->{p}->child_error);
	}
	if ($child_pid) {
		&$todo2();
		while (<$grab>) {
			$self->{p}->_print($_);
		}
		if (!close $grab) {
			$self->{p}->say("system(#1) failed: #2 #3",
			    join(", ", @p), $!,
			    $self->{p}->child_error);
		}
		return $?;
	} else {
		$DB::inhibit_exit = 0;
		&$todo();
		exec {$p[0]} (@p) or exit 1;
	}
}

1;
