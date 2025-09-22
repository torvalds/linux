# The client sends messages with different facility and severity.
# The syslogd writes into multiple files depending on priority.
# The syslogd writes it into a file and through a pipe.
# The syslogd passes it via UDP to the loghost.
# The server receives the message on its UDP socket.
# Find the message in client, file, pipe, syslogd, server log.
# Check that the messages appear in the correct log files.

use strict;
use warnings;
use Sys::Syslog;

my (@messages, @priorities);
foreach my $fac (qw(local5 local6 local7)) {
    foreach my $sev (qw(notice warning err)) {
	my $msg = "$fac.$sev";
	push @messages, $msg;
	no strict 'refs';
	my $prio = ("Sys::Syslog::LOG_".uc($fac))->() |
	    ("Sys::Syslog::LOG_".uc($sev))->();
	push @priorities, $prio;
    }
}

my %selector2messages = (
    "*.*" => [@messages],
    "*.info" => [@messages],
    "*.notice" => [@messages],
    "*.warning" => [ grep { /\.(warning|err)$/ } @messages],
    "*.err" => [ grep { /\.err$/ } @messages],
    "*.crit" => [],
    "*.none" => [],
    "local5.*" => [qw(local5.notice local5.warning local5.err)],
    "local5.info" => [qw(local5.notice local5.warning local5.err)],
    "local5.notice" => [qw(local5.notice local5.warning local5.err)],
    "local5.warning" => [qw(local5.warning local5.err)],
    "local5.err" => [qw(local5.err)],
    "local5.crit" => [],
    "local5.none" => [],
    "local5.warning;local5.err" => [qw(local5.err)],
    "local5.err;local5.warning" => [qw(local5.warning local5.err)],
    "local6.warning;local7.err" => [qw(local6.warning local6.err local7.err)],
    "local6.err;local7.err" => [qw(local6.err local7.err)],
    "local6,local7.err" => [qw(local6.err local7.err)],
    "local6,local7.warning;local6.err" => [qw(local6.err local7.warning
	local7.err)],
    "*.*;local6,local7.none" => [qw(local5.notice local5.warning local5.err)],
);

our %args = (
    client => {
	func => sub {
	    my $self = shift;
	    for (my $i = 0; $i < @messages; $i++) {
		syslog($priorities[$i], $messages[$i]);
	    }
	    write_log($self);
	},
    },
    syslogd => {
	conf => selector2config(%selector2messages),
    },
    multifile => [
	(map { { loggrep => $_ } } (selector2loggrep(%selector2messages))),
    ],
    server => {
	loggrep => { map { qr/ <$_>/ => 1 } @priorities },
    },
    file => {
	loggrep => { map { qr/: $_$/ => 1 } @messages },
    },
);

1;
