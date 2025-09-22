#!/usr/bin/perl

#
# Copyright (c) 2021 Joel Sing <jsing@openbsd.org>
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

sub root_pem_to_dir() {
	$certs = 0;
	$in_cert = 0;

	($roots_file, $certs_dir) = @_;

	open ROOTS, "<$roots_file" or
		die "failed to open roots file '$roots_file'";
	while (<ROOTS>) {
		if ($_ eq "-----BEGIN CERTIFICATE-----\n") {
			$in_cert = 1;
			$cert_path = "$certs_dir/ca-$certs.pem";
			open CERT, ">$cert_path" or
				die "failed to open '$cert_path'";
			$certs++;
		}
		if ($in_cert) {
			print CERT $_;
		}
		if ($_ eq "-----END CERTIFICATE-----\n") {
			$in_cert = 0;
		}
	}
	close ROOTS;

	my @args = ("openssl", "certhash", $certs_dir);
	system(@args) == 0 or die "certhash failed";
}

if (scalar @ARGV != 2) {
	print("$0 <certs-path> <output-dir>\n");
	exit(1);
}
$certs_path = shift @ARGV;
$output_dir = shift @ARGV;

opendir CERTS, $certs_path or
	die "failed to open certs directory '$certs_path'";
while (readdir CERTS) {
	next if ($_ !~ '^[0-9]+[a-z]?$');

	$roots_file = join("/", $certs_path, $_, "roots.pem");
	$roots_dir = join("/", $output_dir, $_, "roots");

	mkdir "$output_dir";
	mkdir "$output_dir/$_";
	mkdir "$output_dir/$_/roots";

	&root_pem_to_dir($roots_file, $roots_dir);
}
closedir CERTS;
