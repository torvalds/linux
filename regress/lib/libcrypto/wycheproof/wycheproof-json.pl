# $OpenBSD: wycheproof-json.pl,v 1.3 2025/09/05 14:36:03 tb Exp $

# Copyright (c) 2022 Joel Sing <jsing@openbsd.org>
# Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
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

use JSON::PP;

$test_vector_path = "/usr/local/share/wycheproof/testvectors_v1";

open JSON, "$test_vector_path/primality_test.json" or die;
@json = <JSON>;
close JSON;

$tv = JSON::PP::decode_json(join "\n", @json);
$test_groups = %$tv{"testGroups"};

my $wycheproof_struct = <<"EOL";
struct wycheproof_testcase {
	int id;
	const char *value;
	int acceptable;
	int result;
};

struct wycheproof_testcase testcases[] = {
EOL

print $wycheproof_struct;

foreach $test_group (@$test_groups) {
	$test_group_type = %$test_group{"type"};
	$test_group_tests = %$test_group{"tests"};

	foreach $test_case (@$test_group_tests) {
		%tc = %$test_case;

		$tc_id = $tc{"tcId"};
		$tc_value = $tc{"value"};
		$tc_result = $tc{"result"};
		$tc_flags = @{$tc{"flags"}};

		my $result = $tc_result eq "valid" ? 1 : 0;

		print "\t{\n";
		print "\t\t.id = $tc_id,\n";
		print "\t\t.value = \"$tc_value\",\n";
		print "\t\t.result = $result,\n";

		if ($tc_result eq "acceptable") {
			print "\t\t.acceptable = 1,\n";
		}

		print "\t},\n";
	}
}

print "};\n\n";

print "#define N_TESTS (sizeof(testcases) / sizeof(testcases[0]))\n"
