# test delay before server read, unsplice during client write

use strict;
use warnings;
use POSIX;

our %args = (
    client => {
	func => sub { errignore(@_); write_stream(@_); },
	len => 2**17,
	sndbuf => 2**15,
    },
    relay => {
	func => sub {
	    my $self = shift;
	    defined(my $pid = fork())
		or die "relay func: fork failed: $!";
	    if ($pid == 0) {
		sleep 2;
		if ($self->{forward} =~ /splice/) {
		    setsplice(\*STDIN)
			or die ref($self), " unsplice stdin failed: $!";
		}
		POSIX::_exit(0);
	    }
	    sleep 1;
	    eval { relay($self, @_) };
	    if ($self->{forward} =~ /splice/) {
		$@ =~ /^Relay sysread stdin has data:/
		    or die ref($self), " no data after unsplice: $@";
	    }
	    sleep 2;
	    kill 9, $pid;
	    (my $kid = waitpid($pid, 0)) > 0
		or die ref($self), " wait unsplice child failed: $!";
	    my $status = $?;
	    my $code;
	    $code = "exit: ".   WEXITSTATUS($?) if WIFEXITED($?);
	    $code = "signal: ". WTERMSIG($?)    if WIFSIGNALED($?);
	    $code = "stop: ".   WSTOPSIG($?)    if WIFSTOPPED($?);
	    $status == 0
		or die ref($self), " unsplice child status: $status $code";
	},
	rcvbuf => 2**10,
	sndbuf => 2**10,
    },
    server => {
	func => sub { sleep 3; read_stream(@_); },
	rcvbuf => 2**15,
    },
    noecho => 1,
    nocheck => 1,
    len => 131072,
    md5 => "31e5ad3d0d2aeb1ad8aaa847dfa665c2",
);
