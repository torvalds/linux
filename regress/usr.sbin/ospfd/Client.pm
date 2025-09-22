#	$OpenBSD: Client.pm,v 1.7 2020/01/30 13:03:46 bluhm Exp $

# Copyright (c) 2010-2015 Alexander Bluhm <bluhm@openbsd.org>
# Copyright (c) 2014-2015 Florian Riehm <mail@friehm.de>
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

use strict;
use warnings;
use feature "state";

package Client;
use parent 'Proc';
use Carp;

use Fcntl;
use Data::Dumper;
use YAML;

use AnyEvent;
use AnyEvent::Handle;
use AnyEvent::Strict;

use Packet;
use Tap 'opentap';

my $tap_number;
my $area;
my $hello_interval;
# Parameters for interface state machine of the test
my $ism_mac;
my $ism_ip;
my $ism_rtrid;
# Parameters for ospfd under test
my $ospfd_ip;
my $ospfd_rtrid;

my $handle;
my $check;
my $wait;
my $cv;
my @isms;

sub handle_arp {
    my %arp = consume_arp(\$handle->{rbuf});
    my %ether = (
	src_str => $ism_mac,
	dst_str => $arp{sha_str},
	type    => 0x0806,
    );
    $arp{op} = 2;
    @arp{qw(sha_str spa_str tha_str tpa_str)} =
	($ism_mac, @arp{qw(tpa_str sha_str spa_str)});
    $handle->push_write(
	construct_ether(\%ether,
	construct_arp(\%arp))
    );
}

sub handle_ip {
    my %ip = consume_ip(\$handle->{rbuf});
    unless ($ip{p} == 89) {
	warn "ip proto is not ospf";
	return;
    }
    $ip{src_str} eq $ospfd_ip
	or return $cv->croak(
	"ospfd src ip is $ip{src_str}: expected $ospfd_ip");
    my %ospf = consume_ospf(\$handle->{rbuf});
    $ospf{router_id_str} eq $ospfd_rtrid
	or return $cv->croak(
	"ospfd rtrid is $ospf{router_id_str}: expected $ospfd_rtrid");
    $ospf{area_id_str} eq $area
	or return $cv->croak(
	"ospfd area is $ospf{area_id_str}: expected $area");
    if ($ospf{type} == 1) {
	handle_hello();
    } elsif ($ospf{type} == 2) {
	handle_dd();
    } else {
	warn "ospf type is not supported: $ospf{type}";
    }
}

sub handle_hello {
    my %hello = consume_hello(\$handle->{rbuf});

    my $compare = sub {
	my $expect = shift;
	if ($expect->{dr}) {
	    $hello{designated_router_str} eq $expect->{dr}
		or return "dr is $hello{designated_router_str}: ".
		    "expected $expect->{dr}";
	}
	if ($expect->{bdr}) {
	    $hello{backup_designated_router_str} eq $expect->{bdr}
		or return "bdr is $hello{backup_designated_router_str}: ".
		    "expected $expect->{bdr}";
	}
	if ($expect->{nbrs}) {
	    my @neighbors = sort @{$hello{neighbors_str} || []};
	    my @nbrs = @{$expect->{nbrs}};
	    "@neighbors" eq "@nbrs"
		or return "nbrs [@neighbors]: expected [@nbrs]";
	}
	return "";
    };

    my $error = $compare->($check);
    return $cv->croak("check: $error") if $error;
    print "check hello successful\n";

    my $reason;
    if ($wait) {
	$reason = $compare->($wait);
    }
    if ($reason) {
	print "wait for hello because of: $reason\n";
    } elsif (!$wait || $wait->{dr} || $wait->{bdr} || $wait->{nbrs}) {
	$cv->send();
    }
}

sub handle_dd {
    my %dd = consume_dd(\$handle->{rbuf});

    my $compare = sub {
	my $expect = shift;
	foreach my $key (qw(options bits)) {
	    if ($expect->{"dd_$key"}) {
		$dd{$key} == $expect->{"dd_$key"} or
		    return sprintf("dd key '%s' is 0x%x: expected 0x%x\n",
			$key, $dd{$key}, $expect->{"dd_$key"});
	    }
	}
	if ($expect->{dd_seq}) {
	    $dd{dd_sequence_number} == $expect->{dd_seq} or
		return sprintf("dd_sequence_number is 0x%x: expected 0x%x\n",
		    $dd{dd_sequence_number}, $expect->{dd_seq});
	}
	return "";
    };

    my $error = $compare->($check);
    return $cv->croak("check: $error") if $error;
    print "check dd successful\n";

    my $reason;
    if ($wait) {
	$reason = $compare->($wait);
    }
    if ($reason) {
	print "wait for dd because of: $reason\n";
    } elsif (!$wait || $wait->{dd_bits} || $wait->{dd_options} ||
	$wait->{dd_seq}) {
	$cv->send();
    }
}

my $ism_count = 0;
sub interface_state_machine {
    my %state = (
	dr  => "0.0.0.0",
	bdr => "0.0.0.0",
	pri => 1,
    );

    # increment the ip address and router id for each instance of ism
    my $ip_number = unpack("N", pack("C4", split(/\./, $ism_ip)));
    my $ip = join(".", unpack("C4", pack("N", $ip_number + $ism_count)));
    my $rtrid_number = unpack("N", pack("C4", split(/\./, $ism_rtrid)));
    my $rtrid = join(".", unpack("C4", pack("N", $rtrid_number + $ism_count)));
    $ism_count++;

    my $hello_count = 0;
    $state{timer} = AnyEvent->timer(
	after => 3,
	interval => $hello_interval,
	cb => sub {
	    my %ether = (
		src_str => $ism_mac,
		dst_str => "01:00:5e:00:00:05",  # multicast ospf
		type    => 0x0800,               # ipv4
	    );
	    my %ip = (
		v       => 4,               # ipv4
		hlen    => 20,
		tos     => 0xc0,
		id      => $hello_count++,  # increment for debugging
		off     => 0,               # no fragment
		ttl     => 1,               # only for direct connected
		p       => 89,              # protocol ospf
		src_str => $ip,
		dst_str => "224.0.0.5",     # all ospf router multicast
	    );
	    my %ospf = (
		version       => 2,           # ospf v2
		type	      => 1,           # hello
		router_id_str => $rtrid,
		area_id_str   => $area,
		autype        => 0,           # no authentication
	    );
	    my %hello = (
		network_mask_str             => "255.255.255.0",
		hellointerval                => $hello_interval,
		options                      => 0x02,
		rtr_pri		             => $state{pri},
		routerdeadinterval           => 4 * $hello_interval,
		designated_router_str        => $state{dr},
		backup_designated_router_str => $state{bdr},
		neighbors_str                => $state{nbrs},
	    );
	    $handle->push_write(
		construct_ether(\%ether,
		construct_ip(\%ip,
		construct_ospf(\%ospf,
		construct_hello(\%hello))))
	    );
	},
    );

    return \%state;
}

sub send_dd {
    my $state = shift;
    my $ip_number = unpack("N", pack("C4", split(/\./, $ism_ip)));
    my $ip = join(".", unpack("C4", pack("N", $ip_number)));
    my $rtrid_number = unpack("N", pack("C4", split(/\./, $ism_rtrid)));
    my $rtrid = join(".", unpack("C4", pack("N", $rtrid_number)));
    state $dd_count = 0;

    my %ether = (
	src_str => $ism_mac,
	dst_str => "01:00:5e:00:00:05",  # don't know the real dst mac
	type    => 0x0800,               # ipv4
    );
    my %ip = (
	v       => 4,               # ipv4
	hlen    => 20,
	tos     => 0xc0,
	id      => $dd_count++,	    # increment for debugging
	off     => 0,               # no fragment
	ttl     => 1,               # only for direct connected
	p       => 89,              # protocol ospf
	src_str => $ip,
	dst_str => $ospfd_ip,
    );
    my %ospf = (
	version       => 2,           # ospf v2
	type	      => 2,           # dd
	router_id_str => $rtrid,
	area_id_str   => $area,
	autype        => 0,           # no authentication
    );
    my %dd = (
	interface_mtu => 1500,
	options => 0x02,
	bits => $state->{dd_bits},
	dd_sequence_number => 999,	# some value
    );
    $handle->push_write(
	construct_ether(\%ether,
	construct_ip(\%ip,
	construct_ospf(\%ospf,
	construct_dd(\%dd))))
    );
}

sub ism_set_state {
    my $state = shift;

    my @states = ref($state) eq 'ARRAY' ? @$state : ( $state || () );
    for (my $i = 0; $i < @states; $i++) {
	$isms[$i] ||= interface_state_machine();
	%{$isms[$i]} = (%{$isms[$i]}, %{$states[$i]});
	if ($states[$i]{dd_bits}) {
	    send_dd($states[$i]);
	    delete $states[$i]{dd_bits};
	}
    }
}

sub runtest {
    my $self = shift;

    $| = 1;

    ism_set_state($self->{state} || {});

    foreach my $task (@{$self->{tasks}}) {
	print "Task: $task->{name}\n";
	$check = $task->{check};
	$wait = $task->{wait};
	my $timeout = $task->{timeout};
	my $t;
	if ($timeout) {
	    $t = AnyEvent->timer(
		after => $timeout,
		cb => sub { $cv->croak("timeout after $timeout seconds"); },
	    );
	}
	$cv = AnyEvent->condvar;
	$cv->recv;
	ism_set_state($task->{state});
    }

    print "Terminating\n";
    sleep 1;
}

sub new {
    my ($class, %args) = @_;
    $args{logfile} ||= "client.log";
    $args{up} = "Starting test client";
    $args{down} = "Terminating";
    $args{func} = \&runtest;
    my $self = Proc::new($class, %args);
    return $self;
}

sub child {
    my $self = shift;

    $area = $self->{area}
	or croak ref($self), " area id missing";
    $hello_interval = $self->{hello_intervall}
	or croak ref($self), " hello_interval missing";
    $ism_mac = $self->{mac_address}
	or croak ref($self), " client mac address missing";
    $ism_ip = $self->{ospf_address}
	or croak ref($self), " client ospf address missing";
    $ism_rtrid = $self->{router_id}
	or croak ref($self), " client router id missing";
    $tap_number =  $self->{tap_number}
	or croak ref($self), " tap device number missing";
    $ospfd_ip = $self->{ospfd_ip}
	or croak ref($self), " ospfd ip missing";
    $ospfd_rtrid = $self->{ospfd_rtrid}
	or croak ref($self), " ospfd router id missing";

    if ($ENV{KTRACE}) {
	my @ktrace = $ENV{KTRACE};
	push @ktrace, "-i", "-f", "client.ktrace", "-p", $$;
	system(@ktrace)
	    and die "Command '@ktrace' failed: $?";
    }

    my $tap = opentap($tap_number);

    $handle = AnyEvent::Handle->new(
	fh => $tap,
	read_size => 70000,  # little more than max ip size
	on_error => sub {
	    $cv->croak("error on tap device $tap_number: $!");
	    $handle->destroy();
	    undef $handle;
	},
	on_eof => sub {
	    $cv->croak("end-of-file on tap device $tap_number: $!");
	    $handle->destroy();
	    undef $handle;
	},
	on_read => sub {
	    my %ether = consume_ether(\$handle->{rbuf});
	    if ($ether{type} == 0x0800) {
		handle_ip();
	    } elsif ($ether{type} == 0x0806) {
		handle_arp();
	    } else {
		warn "ether type is not supported: $ether{type_hex}";
	    }
	    $handle->{rbuf} = "";  # packets must not cumulate
	},
    );
}

1;
