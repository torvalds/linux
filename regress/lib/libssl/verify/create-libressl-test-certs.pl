#!/usr/bin/perl

# Copyright (c) 2021 Steffen Ullrich <sullr@cpan.org>
# Public Domain

use strict;
use warnings;
use IO::Socket::SSL::Utils;

# primitive CA - ROOT
my @ca = cert(
    CA => 1,
    subject => { CN => 'ROOT' }
);
out('caR.pem', pem(crt => $ca[0]));
out('caR.key', pem(key => $ca[1]));

# server certificate where SAN contains in-label wildcards, which a
# client MAY choose to accept as per RFC 6125 section 6.4.3.
my @leafcert = cert(
    issuer => \@ca,
    purpose => 'server',
    subject => { CN => 'server.local' },
    subjectAltNames => [
	[ DNS => 'bar.server.local' ],
	[ DNS => 'www*.server.local'],
	[ DNS => '*.www.server.local'],
	[ DNS => 'foo.server.local' ],
	[ DNS => 'server.local' ],
    ]
);
out('server-unusual-wildcard.pem', pem(@leafcert));

@leafcert = cert(
    issuer => \@ca,
    purpose => 'server',
    subject => { CN => 'server.local' },
    subjectAltNames => [
	[ DNS => 'bar.server.local' ],
	[ DNS => '*.www.server.local'],
	[ DNS => 'foo.server.local' ],
	[ DNS => 'server.local' ],
    ]
);
out('server-common-wildcard.pem', pem(@leafcert));

# alternative CA - OLD_ROOT
my @caO = cert(
    CA => 1,
    subject => { CN => 'OLD_ROOT' }
);
out('caO.pem', pem(crt => $caO[0]));
out('caO.key', pem(key => $caO[1]));

# alternative ROOT CA, signed by OLD_ROOT, same key as other ROOT CA
my @caX = cert(
    issuer => \@caO,
    CA => 1,
    subject => { CN => 'ROOT' },
    key => $ca[1],
);
out('caX.pem', pem(crt => $caX[0]));
out('caX.key', pem(key => $caX[1]));

# subCA below ROOT
my @subcaR = cert(
    issuer => \@ca,
    CA => 1,
    subject => { CN => 'SubCA.of.ROOT' }
);
out('subcaR.pem', pem(crt => $subcaR[0]));
out('subcaR.key', pem(key => $subcaR[1]));
out('chainSX.pem', pem($subcaR[0]), pem($caX[0]));

@leafcert = cert(
    issuer => \@subcaR,
    purpose => 'server',
    subject => { CN => 'server.subca.local' },
    subjectAltNames => [
	[ DNS => 'server.subca.local' ],
    ]
);
out('server-subca.pem', pem(@leafcert));
out('server-subca-chainSX.pem', pem(@leafcert, $subcaR[0], $caX[0]));
out('server-subca-chainS.pem', pem(@leafcert, $subcaR[0]));


sub cert { CERT_create(not_after => 10*365*86400+time(), @_) }
sub pem {
    my @default = qw(crt key);
    my %m = (key => \&PEM_key2string, crt => \&PEM_cert2string);
    my $result = '';
    while (my $f = shift(@_)) {
	my $v;
	if ($f =~m{^(key|crt)$}) {
	    $v = shift(@_);
	} else {
	    $v = $f;
	    $f = shift(@default) || 'crt';
	}
	$f = $m{$f} || die "wrong key $f";
	$result .= $f->($v);
    }
    return $result;
}

sub out {
    my $file = shift;
    open(my $fh,'>',"$file") or die "failed to create $file: $!";
    print $fh @_
}
