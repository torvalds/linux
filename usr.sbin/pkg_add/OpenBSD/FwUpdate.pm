#! /usr/bin/perl

# ex:ts=8 sw=4:
# $OpenBSD: FwUpdate.pm,v 1.37 2025/03/28 13:21:59 stsp Exp $
#
# Copyright (c) 2014 Marc Espie <espie@openbsd.org>
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
use OpenBSD::PkgAdd;
use OpenBSD::PackageRepository;
use OpenBSD::PackageLocator;

package OpenBSD::FwUpdate::Locator;
our @ISA = qw(OpenBSD::PackageLocator);

sub add_default($self, $state, $p)
{
	my $path = $state->opt('p');
	if (!$path) {
		my $dir = OpenBSD::Paths->os_directory;
		if (!defined $dir) {
			$state->fatal("Couldn't find/parse OS version");
		}
		$path = "http://firmware.openbsd.org/firmware/$dir/";
	}
	$p->add(OpenBSD::PackageRepository->new($path, $state));
}

package OpenBSD::FwUpdate::State;
our @ISA = qw(OpenBSD::PkgAdd::State);

sub cache_directory($)
{
	return undef;
}

sub locator($)
{
	return "OpenBSD::FwUpdate::Locator";
}

sub handle_options($state)
{
	$state->OpenBSD::State::handle_options('adinp:', 
	    '[-adinv] [-D keyword] [-p path] [driver...]');
	$state->{not} = $state->opt('n');
	if ($state->opt('i')) {
		$state->{not} = 1;
	}
	$main::not = $state->{not};
	$state->progress->setup(0, 0, $state);
	$state->{localbase} = OpenBSD::Paths->localbase;
	$state->{destdir} = '';
	$state->{wantntogo} = 0;
	$state->{interactive} = OpenBSD::InteractiveStub->new($state);
	$state->{subst}->add('repair', 1);
	if ($state->opt('a') && @ARGV != 0) {
		$state->usage;
	}
	$state->{fw_verbose} = $state->{v};
	if ($state->{v}) {
		$state->{v}--;
	}
	if ($state->{fw_verbose}) {
		$state->say("Path to firmware: #1", 
		    $state->locator->printable_default_path($state));
	}
	$state->{subst}->add('NO_SCP', 1);
}

sub finish_init($state)
{
	$state->{subst}->add('FW_UPDATE', 1);
}

sub installed_drivers($self)
{
	return keys %{$self->{installed_drivers}};
}

sub is_installed($self, $driver)
{
	return $self->{installed_drivers}{$driver};
}

sub machine_drivers($self)
{
	return keys %{$self->{machine_drivers}};
}

sub all_drivers($self)
{
	return keys %{$self->{all_drivers}};
}

sub is_needed($self, $driver)
{
	my ($self, $driver) = @_;
	return $self->{machine_drivers}{$driver};
}

sub display_timestamp($state, $pkgname, $timestamp)
{
	return unless $state->verbose;
	$state->SUPER::display_timestamp($pkgname, $timestamp);
}

sub fw_status($state, $msg, $list)
{
	return if @$list == 0;
	$state->say("#1: #2", $msg, join(' ', @$list));
}

package OpenBSD::FwUpdate::Update;
our @ISA = qw(OpenBSD::Update);

package OpenBSD::FwUpdate;
our @ISA = qw(OpenBSD::PkgAdd);

OpenBSD::Auto::cache(updater,
    sub($) {
	    require OpenBSD::Update;
	    return OpenBSD::FwUpdate::Update->new;
    });

my %possible_drivers = map {($_, "$_-firmware")}
    (qw(acx amdgpu athn bwfm bwi ice intel inteldrm ipw iwi
	iwm iwn iwx malo mtw ogx otus pgt radeondrm
	uath upgt uvideo vmm wpi));

my %match = map {($_, qr{^\Q$_\E\d+\s+at\s})} (keys %possible_drivers);
$match{'intel'} = qr{^cpu\d+: Intel};

sub parse_dmesg($self, $f, $search, $found)
{
	while (<$f>) {
		chomp;
		for my $driver (keys %$search) {
			next unless $_ =~ $match{$driver};
			$found->{$driver} = 1;
			delete $search->{$driver};
		}
	}
}

sub find_machine_drivers($self, $state)
{
	$state->{machine_drivers} = {};
	$state->{all_drivers} = \%possible_drivers;
	my %search = %possible_drivers;
	if (open(my $f, '<', '/var/run/dmesg.boot')) {
		$self->parse_dmesg($f, \%search, $state->{machine_drivers});
		close($f);
	} else {
		$state->errsay("Can't open dmesg.boot: #1", $!);
	}
	if (open(my $cmd, '-|', 'dmesg')) {
		$self->parse_dmesg($cmd, \%search, $state->{machine_drivers});
		close($cmd);
	} else {
		$state->errsay("Can't run dmesg: #1", $!);
	}
}

sub driver2firmware($k)
{
	return $possible_drivers{$k};
}

sub find_installed_drivers($self, $state)
{
	my $inst = $state->repo->installed;
	for my $driver (keys %possible_drivers) {	
		my $search = OpenBSD::Search::Stem->new(driver2firmware($driver));
		my $l = $inst->match_locations($search);
		if (@$l > 0) {
			$state->{installed_drivers}{$driver} = 
			    OpenBSD::Handle->from_location($l->[0]);
		}
	}
}


sub new_state($self, $cmd)
{
	return OpenBSD::FwUpdate::State->new($cmd);
}

sub find_handle($self, $state, $driver)
{
	my $pkgname = driver2firmware($driver);
	my $set;
	my $h = $state->is_installed($driver);
	if ($h) {
		$set = $state->updateset->add_older($h);
	} else {
		$set = $state->updateset->add_hints($pkgname);
	}
	return $set;
}

sub mark_set_for_deletion($self, $set, $state)
{
	# XXX to be simplified. Basically, we pre-do the work of the updater...
	for my $h ($set->older) {
		$h->{update_found} = 1;
	}
	$set->{updates}++;
}

# no quirks for firmware, bypass entirely
sub do_quirks($self, $state)
{
	$state->finish_init;
}

sub to_remove($self, $state, $driver)
{
	$self->mark_set_for_deletion($self->to_add_or_update($state, $driver));
}

sub to_add_or_update($self, $state, $driver)
{
	my $set = $self->find_handle($state, $driver);
	push(@{$state->{setlist}}, $set);
	return $set;
}

sub show_info($self, $state)
{
	my (@installed, @unneeded, @needed);
	for my $d ($state->installed_drivers) {
		my $h = $state->is_installed($d)->pkgname;
		if ($state->is_needed($d)) {
			push(@installed, $h);
		} else {
			push(@unneeded, $h);
		}
	}
	for my $d ($state->machine_drivers) {
		if (!$state->is_installed($d)) {
			push(@needed, driver2firmware($d));
		} 
	}
	$state->fw_status("Installed", \@installed);
	$state->fw_status("Installed, extra", \@unneeded);
	$state->fw_status("Missing", \@needed);
}

sub silence_children($, $)
{
	0
}

sub process_parameters($self, $state)
{
	$self->find_machine_drivers($state);
	$self->find_installed_drivers($state);

	if ($state->opt('i')) {
		$self->show_info($state);
		exit(0);
	}
	if (@ARGV == 0) {
		if ($state->opt('d')) {
			for my $driver ($state->installed_drivers) {
				if ($state->opt('a') || 
				    !$state->is_needed($driver)) {
					$self->to_remove($state, $driver);
				}
			}
		} else {
			if ($state->opt('a')) {
				for my $driver ($state->all_drivers) {
					$self->to_add_or_update($state, 
					    $driver);
				}
			} else {
				for my $driver ($state->machine_drivers) {
					$self->to_add_or_update($state, 
					    $driver);
				}
				for my $driver ($state->installed_drivers) {
					# XXX skip already done up there ^
					next if $state->is_needed($driver);
					$self->to_add_or_update($state, 
					    $driver);
				}
			}
		}
		if (!(defined $state->{setlist}) && $state->{fw_verbose}) {
			$state->say($state->opt('d') ?
			    "No firmware to delete." :
			    "No devices found which need firmware files to be downloaded.");
			exit(0);
		}
	} else {
		for my $driver (@ARGV) {
			$driver =~ s/\-firmware(\-\d.*)?$//;
			if (!$possible_drivers{$driver}) {
				$state->errsay("#1: unknown driver", $driver);
				exit(1);
			}
			if ($state->opt('d') && 
			    !$state->is_installed($driver)) {
				$state->errsay("Can't delete uninstalled driver: #1", $driver);
				next;
			}

			my $set = $self->to_add_or_update($state, $driver);
			if ($state->opt('d')) {
				$self->mark_set_for_deletion($set);
			} 
		}
	}
	if ($state->{fw_verbose}) {
		my (@deleting, @updating, @installing);
		for my $set (@{$state->{setlist}}) {
			for my $h ($set->older) {
				if ($h->{update_found}) {
					push(@deleting, $h->pkgname);
				} else {
					push(@updating, $h->pkgname);
				}
			}
			for my $h ($set->hints) {
				push(@installing, $h->pkgname);
			}
		}
		$state->fw_status("Installing", \@installing);
		$state->fw_status("Deleting", \@deleting);
		$state->fw_status("Updating", \@updating);
	}
}

1;
