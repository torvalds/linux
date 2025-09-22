#!/usr/bin/perl
# $OpenBSD: format-pem.pl,v 1.8 2025/06/16 10:24:55 sthen Exp $
#
# Copyright (c) 2016 Stuart Henderson <sthen@openbsd.org>
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

# To update cert.pem based on the certificates included in Mozilla NSS,
# pkg_add curl for mk-ca-bundle(1), and:
# - perl format-pem.pl < cert.pem > /dev/null 2> calist.old
# - mk-ca-bundle
# - perl format-pem.pl < ca-bundle.crt > certnew.pem 2> calist.new
# Summarize additions/removals for review:
# - diff calist.old calist.new

use strict;
use warnings;

use File::Temp qw/ :seekable /;
if (! eval {require Date::Parse;1;}) {
	print STDERR "Date::Parse not available - install p5-Time-TimeDate to check cert dates.\n";
} else {
	use Date::Parse;
}

my $tmp = File::Temp->new(TEMPLATE => '/tmp/splitcert.XXXXXXXX');
my $t = $tmp->filename;

my $certs = 0;
my $incert = 0;
my %ca;
my $rcsid = '# $'.'OpenBSD$';

while(<>) {
	$rcsid = $_ if ($_ =~ m/^# \$[O]penBSD/);
	$incert++ if ($_ =~ m/^-----BEGIN CERTIFICATE-----/);
	print $tmp $_ if ($incert);

	if ($_ =~ m/^-----END CERTIFICATE-----/) {
		$certs++;

		my $issuer = `openssl x509 -in $t -noout -issuer`;
		$issuer =~ s/^issuer= (.*)\n/$1/;
		my $subj = `openssl x509 -in $t -noout -subject`;
		$subj =~ s/^subject= (.*)\n/$1/;

		my $o = `openssl x509 -in $t -noout -nameopt sep_multiline,use_quote,esc_msb -subject`;
		if ($o =~ /O=/) {
			$o =~ s/.*O=([^\n]*).*/$1/sm;
		} else {
			$o = $subj;
		}

		if (defined $ca{$o}{$subj}) {
			print STDERR "ERROR: '$subj': duplicate\n";
			$ca{$o}{$subj}{'valid'} = 0;
		}

		$ca{$o}{$subj}{'valid'} = 1;

		if ($issuer ne $subj) {
			print STDERR "ERROR: '$subj' not self-signed";
			$ca{$o}{$subj}{'valid'} = 0;
		}

		if (eval {require Date::Parse;1;}) {
			my $startdate = `openssl x509 -in $t -startdate -noout`;
			my $enddate = `openssl x509 -in $t -enddate -noout`;
			$startdate =~ s/notBefore=(.*)\n/$1/;
			$enddate =~ s/notAfter=(.*)\n/$1/;
			my $starttime = str2time($startdate);
			my $endtime = str2time($enddate);

			if ($starttime > time) {
				print STDERR "ERROR: '$subj' not valid yet\n";
				$ca{$o}{$subj}{'valid'} = 0;
			}
			if ($endtime < time) {
				print STDERR "ERROR: '$subj' expired on $startdate\n";
				$ca{$o}{$subj}{'valid'} = 0;
			} elsif ($endtime < time + 86400 * 365 * 2) {
				print STDERR "WARNING: '$subj' expires on $enddate\n";
			}
		}

		my $info = qx/openssl x509 -in $t -text -fingerprint -sha1 -certopt no_pubkey,no_sigdump,no_issuer -noout/;
		$info .= qx/openssl x509 -in $t -fingerprint -sha256 -noout/;
		my $cert = qx/openssl x509 -in $t/;

		my $verify = qx/openssl verify -CAfile $t $t 2>&1/;
		if (not $verify =~ /^$t: OK$/) {
			$verify =~ s,$t: ,,;
			print STDERR "ERROR: '$subj' cannot be verified with libressl\n---\n$verify---\n";
			$ca{$o}{$subj}{'valid'} = 0;
		}

		$ca{$o}{$subj}{'subj'} = $subj;
		$ca{$o}{$subj}{'info'} = $info;
		$ca{$o}{$subj}{'cert'} = $cert;

		$tmp->seek(0, SEEK_SET);
		$incert = 0;
	}
}

close $tmp;
chomp $rcsid;
print $rcsid;
foreach my $o (sort{$a cmp $b} keys %ca) {
	print "\n### $o\n\n";
	foreach my $subj (sort{$a cmp $b} keys %{ $ca{$o} }) {
		if ($ca{$o}{$subj}{'valid'} == 1) {
			print "=== $subj\n";
			print $ca{$o}{$subj}{'info'};
			print $ca{$o}{$subj}{'cert'};
		}
	}
}

# print a visual summary at the end
foreach my $o (sort{$a cmp $b} keys %ca) {
	print STDERR "\n$o\n";
	foreach my $subj (sort{$a cmp $b} keys %{ $ca{$o} }) {
		print STDERR "  $subj\n";
	}
}
