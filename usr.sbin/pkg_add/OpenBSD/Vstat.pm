# ex:ts=8 sw=4:
# $OpenBSD: Vstat.pm,v 1.72 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2003-2007 Marc Espie <espie@openbsd.org>
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

# Provides stat and statfs-like functions for package handling.

# allows user to add/remove files.

# uses mount and df directly for now.

use v5.36;

package OpenBSD::Vstat::Object;
my $cache = {};
my $dummy;
$dummy = bless \$dummy, __PACKAGE__;

sub new($class, $value = undef)
{
	if (!defined $value) {
		return $dummy;
	}
	if (!defined $cache->{$value}) {
		$cache->{$value} = bless \$value, $class;
	}
	return $cache->{$value};
}

sub exists($)
{
	return 1;
}

sub value($self)
{
	return $$self;
}

sub none($)
{
	return OpenBSD::Vstat::Object::None->new;
}

package OpenBSD::Vstat::Object::None;
our @ISA = qw(OpenBSD::Vstat::Object);
my $none;
$none = bless \$none, __PACKAGE__;

sub exists($)
{
	return 0;
}

sub new($)
{
	return $none;
}

package OpenBSD::Vstat::Object::Directory;
our @ISA = qw(OpenBSD::Vstat::Object);

sub new($class, $fname, $set, $o)
{
	bless { name => $fname, set => $set, o => $o }, $class;
}

# XXX directories don't do anything until you test for their presence.
# which only happens if you want to replace a directory with a file.
sub exists($self)
{
	require OpenBSD::SharedItems;

	return OpenBSD::SharedItems::check_shared($self->{set}, $self->{o});
}

package OpenBSD::Vstat;
use File::Basename;
use OpenBSD::Paths;

sub stat($self, $fname)
{
	my $dev = (stat $fname)[0];

	if (!defined $dev && $fname ne '/') {
		return $self->stat(dirname($fname));
	}
	return OpenBSD::Mounts->find($dev, $fname, $self->{state});
}

sub account_for($self, $name, $size)
{
	my $e = $self->stat($name);
	$e->{used} += $size;
	return $e;
}

sub account_later($self, $name, $size)
{
	my $e = $self->stat($name);
	$e->{delayed} += $size;
	return $e;
}

sub new($class, $state)
{
	bless {v => [{}], state => $state}, $class;
}

sub exists($self, $name)
{
	for my $v (@{$self->{v}}) {
		if (defined $v->{$name}) {
			return $v->{$name}->exists;
		}
	}
	return -e $name;
}

sub value($self, $name)
{
	for my $v (@{$self->{v}}) {
		if (defined $v->{$name}) {
			return $v->{$name}->value;
		}
	}
	return undef;
}

sub synchronize($self)
{
	OpenBSD::Mounts->synchronize;
	if ($self->{state}->{not}) {
		# this is the actual stacking case: in pretend mode,
		# I have to put a second vfs on top
		if (@{$self->{v}} == 2) {
			my $top = shift @{$self->{v}};
			while (my ($k, $v) = each %$top) {
				$self->{v}[0]{$k} = $v;
			}
		}
		unshift(@{$self->{v}}, {});
	} else {
		$self->{v} = [{}];
	}
}

sub drop_changes($self)
{
	OpenBSD::Mounts->drop_changes;
	# drop the top layer
	$self->{v}[0] = {};
}

sub add($self, $name, $size, $value)
{
	$self->{v}[0]->{$name} = OpenBSD::Vstat::Object->new($value);
	return defined($size) ? $self->account_for($name, $size) : undef;
}

sub remove($self, $name, $size)
{
	$self->{v}[0]->{$name} = OpenBSD::Vstat::Object->none;
	return defined($size) ? $self->account_later($name, -$size) : undef;
}

sub remove_first($self, $name, $size)
{
	$self->{v}[0]->{$name} = OpenBSD::Vstat::Object->none;
	return defined($size) ? $self->account_for($name, -$size) : undef;
}

# since directories may become files during updates, we may have to remove
# them early, so we need to record them: store exactly as much info as needed
# for SharedItems.
sub remove_directory($self, $name, $o)
{
	$self->{v}[0]->{$name} = OpenBSD::Vstat::Object::Directory->new($name,
	    $self->{state}{current_set}, $o);
}


sub tally($self)
{
	OpenBSD::Mounts->tally($self->{state});
}

package OpenBSD::Mounts;

my $devinfo;
my $devinfo2;
my $giveup;

sub giveup($)
{
	if (!defined $giveup) {
		$giveup = OpenBSD::MountPoint::Fail->new;
	}
	return $giveup;
}

sub new($class, $dev, $mp, $opts)
{
	if (!defined $devinfo->{$dev}) {
		$devinfo->{$dev} = OpenBSD::MountPoint->new($dev, $mp, $opts);
	}
	return $devinfo->{$dev};
}

sub run($class, $state, @args)
{
	my $code = pop @args;
	open(my $cmd, "-|", @args) or
		$state->errsay("Can't run #1", join(' ', @args))
		and return;
	while (<$cmd>) {
		&$code($_);
	}
	if (!close($cmd)) {
		if ($!) {
			$state->errsay("Error running #1: #2", $!, 
			    join(' ', @args));
		} else {
			$state->errsay("Exit status #1 from #2", $?, 
			    join(' ', @args));
		}
	}
}

sub ask_mount($class, $state)
{
	delete $ENV{'BLOCKSIZE'};
	$class->run($state, OpenBSD::Paths->mount, sub($l) {
		chomp $l;
		if ($l =~ m/^(.*?)\s+on\s+(\/.*?)\s+type\s+.*?(?:\s+\((.*?)\))?$/o) {
			my ($dev, $mp, $opts) = ($1, $2, $3);
			$class->new($dev, $mp, $opts);
		} else {
			$state->errsay("Can't parse mount line: #1", $l);
		}
	});
}

sub ask_df($class, $fname, $state)
{
	my $info = $class->giveup;
	my $blocksize = 512;

	$class->ask_mount($state) if !defined $devinfo;
	$class->run($state, OpenBSD::Paths->df, "--", $fname, 
	    sub($l) {
		chomp $l;
		if ($l =~ m/^Filesystem\s+(\d+)\-blocks/o) {
			$blocksize = $1;
		} elsif ($l =~ m/^(.*?)\s+\d+\s+\d+\s+(\-?\d+)\s+\d+\%\s+\/.*?$/o) {
			my ($dev, $avail) = ($1, $2);
			$info = $devinfo->{$dev};
			if (!defined $info) {
				$info = $class->new($dev);
			}
			$info->{avail} = $avail;
			$info->{blocksize} = $blocksize;
		}
	    });

	return $info;
}

sub find($class, $dev, $fname, $state)
{
	if (!defined $dev) {
		return $class->giveup;
	}
	if (!defined $devinfo2->{$dev}) {
		$devinfo2->{$dev} = $class->ask_df($fname, $state);
	}
	return $devinfo2->{$dev};
}

sub synchronize($class)
{
	for my $v (values %$devinfo2) {
		$v->synchronize;
	}
}

sub drop_changes($class)
{
	for my $v (values %$devinfo2) {
		$v->drop_changes;
	}
}

sub tally($self, $state)
{
	for my $v ((sort {$a->name cmp $b->name } values %$devinfo2), $self->giveup) {
		$v->tally($state);
	}
}

package OpenBSD::MountPoint;

sub parse_opts($self, $opts)
{
	for my $o (split /\,\s*/o, $opts) {
		if ($o eq 'read-only') {
			$self->{ro} = 1;
		} elsif ($o eq 'nodev') {
			$self->{nodev} = 1;
		} elsif ($o eq 'nosuid') {
			$self->{nosuid} = 1;
		} elsif ($o eq 'noexec') {
			$self->{noexec} = 1;
		}
	}
}

sub ro($self)
{
	return $self->{ro};
}

sub nodev($self)
{
	return $self->{nodev};
}

sub nosuid($self)
{
	return $self->{nosuid};
}

sub noexec($self)
{
	return $self->{noexec};
}

sub new($class, $dev, $mp, $opts)
{
	my $n = bless { commited_use => 0, used => 0, delayed => 0,
	    hw => 0, dev => $dev, mp => $mp }, $class;
	if (defined $opts) {
		$n->parse_opts($opts);
	}
	return $n;
}


sub avail($self, $used = 0)
{
	return $self->{avail} - $self->{used}/$self->{blocksize};
}

sub name($self)
{
	return "$self->{dev} on $self->{mp}";
}

sub report_ro($s, $state, $fname)
{
	if ($state->verbose >= 3 or ++($s->{problems}) < 4) {
		$state->errsay("Error: #1 is read-only (#2)",
		    $s->name, $fname);
	} elsif ($s->{problems} == 4) {
		$state->errsay("Error: ... more files for #1", $s->name);
	}
	$state->{problems}++;
}

sub report_overflow($s, $state, $fname)
{
	if ($state->verbose >= 3 or ++($s->{problems}) < 4) {
		$state->errsay("Error: #1 is not large enough (#2)",
		    $s->name, $fname);
	} elsif ($s->{problems} == 4) {
		$state->errsay("Error: ... more files do not fit on #1",
		    $s->name);
	}
	$state->{problems}++;
	$state->{overflow} = 1;
}

sub report_noexec($s, $state, $fname)
{
	$state->errsay("Error: #1 is noexec (#2)", $s->name, $fname);
	$state->{problems}++;
}

sub synchronize($v)
{
	if ($v->{used} > $v->{hw}) {
		$v->{hw} = $v->{used};
	}
	$v->{used} += $v->{delayed};
	$v->{delayed} = 0;
	$v->{commited_use} = $v->{used};
}

sub drop_changes($v)
{
	$v->{used} = $v->{commited_use};
	$v->{delayed} = 0;
}

sub tally($data, $state)
{
	return  if $data->{used} == 0;
	$state->print("#1: #2 bytes", $data->name, $data->{used});
	my $avail = $data->avail;
	if ($avail < 0) {
		$state->print(" (missing #1 blocks)", int(-$avail+1));
	} elsif ($data->{hw} >0 && $data->{hw} > $data->{used}) {
		$state->print(" (highwater #1 bytes)", $data->{hw});
	}
	$state->print("\n");
}

package OpenBSD::MountPoint::Fail;
our @ISA=qw(OpenBSD::MountPoint);

sub avail($, $)
{
	return 1;
}

sub new($class)
{
	my $n = $class->SUPER::new('???', '???', '');
	$n->{avail} = 0;
	return $n;
}

1;
