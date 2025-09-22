#   $OpenBSD: tlsfuzzer.py,v 1.57 2025/06/15 09:44:57 tb Exp $
#
# Copyright (c) 2020 Theo Buehler <tb@openbsd.org>
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

import getopt
import os
import subprocess
import sys
from timeit import default_timer as timer

tlsfuzzer_scriptdir = "/usr/local/share/tlsfuzzer/scripts/"

class Test:
    """
    Represents a tlsfuzzer test script.
        name:           the script's name
        args:           arguments to feed to the script
        tls12_args:     override args for a TLSv1.2 server
        tls13_args:     override args for a TLSv1.3 server

    XXX Add client cert support.
    """
    def __init__(self, name="", args=[], tls12_args=[], tls13_args=[]):
        self.name = name
        self.tls12_args = args
        self.tls13_args = args
        if tls12_args:
            self.tls12_args = tls12_args
        if tls13_args:
            self.tls13_args = tls13_args

    def args(self, has_tls1_3: True):
        if has_tls1_3:
            return self.tls13_args
        else:
            return self.tls12_args

    def __repr__(self):
        return "<Test: %s tls12_args: %s tls13_args: %s>" % (
                self.name, self.tls12_args, self.tls13_args
            )

class TestGroup:
    """ A group of Test objects to be run by TestRunner."""
    def __init__(self, title="Tests", tests=[]):
        self.title = title
        self.tests = tests

    def __iter__(self):
        return iter(self.tests)

# argument to pass to several tests
tls13_unsupported_ciphers = [
    "-e", "TLS 1.3 with ffdhe2048",
    "-e", "TLS 1.3 with ffdhe3072",
    "-e", "TLS 1.3 with x448",
]

def substitute_alert(want, got):
    return f"Expected alert description \"{want}\" " \
        + f"does not match received \"{got}\""

# test_tls13_finished.py has 70 failing tests that expect a "decode_error"
# instead of the "decrypt_error" sent by tls13_server_finished_recv().
# Both alerts appear to be reasonable in this context, so work around this
# in the test instead of the library.
def generate_test_tls13_finished_args():
    assertion = substitute_alert("decode_error", "decrypt_error");
    paddings = [
        ("TLS_AES_128_GCM_SHA256", 0, 1),
        ("TLS_AES_128_GCM_SHA256", 0, 2),
        ("TLS_AES_128_GCM_SHA256", 0, 4),
        ("TLS_AES_128_GCM_SHA256", 0, 8),
        ("TLS_AES_128_GCM_SHA256", 0, 16),
        ("TLS_AES_128_GCM_SHA256", 0, 32),
        ("TLS_AES_128_GCM_SHA256", 0, 48),
        ("TLS_AES_128_GCM_SHA256", 0, 2**14-4-32),
        ("TLS_AES_128_GCM_SHA256", 0, 0x20000),
        ("TLS_AES_128_GCM_SHA256", 0, 0x30000),
        ("TLS_AES_128_GCM_SHA256", 1, 0),
        ("TLS_AES_128_GCM_SHA256", 2, 0),
        ("TLS_AES_128_GCM_SHA256", 4, 0),
        ("TLS_AES_128_GCM_SHA256", 8, 0),
        ("TLS_AES_128_GCM_SHA256", 16, 0),
        ("TLS_AES_128_GCM_SHA256", 32, 0),
        ("TLS_AES_128_GCM_SHA256", 48, 0),
        ("TLS_AES_128_GCM_SHA256", 2**14-4-32, 0),
        ("TLS_AES_128_GCM_SHA256", 12, 0),
        ("TLS_AES_128_GCM_SHA256", 1, 1),
        ("TLS_AES_128_GCM_SHA256", 8, 8),
        ("TLS_AES_256_GCM_SHA384", 0, 1),
        ("TLS_AES_256_GCM_SHA384", 0, 2),
        ("TLS_AES_256_GCM_SHA384", 0, 4),
        ("TLS_AES_256_GCM_SHA384", 0, 8),
        ("TLS_AES_256_GCM_SHA384", 0, 16),
        ("TLS_AES_256_GCM_SHA384", 0, 32),
        ("TLS_AES_256_GCM_SHA384", 0, 48),
        ("TLS_AES_256_GCM_SHA384", 0, 2**14-4-48),
        ("TLS_AES_256_GCM_SHA384", 0, 0x20000),
        ("TLS_AES_256_GCM_SHA384", 0, 0x30000),
        ("TLS_AES_256_GCM_SHA384", 0, 12),
        ("TLS_AES_256_GCM_SHA384", 1, 0),
        ("TLS_AES_256_GCM_SHA384", 2, 0),
        ("TLS_AES_256_GCM_SHA384", 4, 0),
        ("TLS_AES_256_GCM_SHA384", 8, 0),
        ("TLS_AES_256_GCM_SHA384", 16, 0),
        ("TLS_AES_256_GCM_SHA384", 32, 0),
        ("TLS_AES_256_GCM_SHA384", 48, 0),
        ("TLS_AES_256_GCM_SHA384", 2**14-4-48, 0),
        ("TLS_AES_256_GCM_SHA384", 1, 1),
        ("TLS_AES_256_GCM_SHA384", 8, 8),
    ]
    truncations = [
        ("TLS_AES_128_GCM_SHA256", 0,  -1),
        ("TLS_AES_128_GCM_SHA256", 0,  -2),
        ("TLS_AES_128_GCM_SHA256", 0,  -4),
        ("TLS_AES_128_GCM_SHA256", 0,  -8),
        ("TLS_AES_128_GCM_SHA256", 0,  -16),
        ("TLS_AES_128_GCM_SHA256", 0,  -32),
        ("TLS_AES_128_GCM_SHA256", 0,  12),
        ("TLS_AES_128_GCM_SHA256", 1,  None),
        ("TLS_AES_128_GCM_SHA256", 2,  None),
        ("TLS_AES_128_GCM_SHA256", 4,  None),
        ("TLS_AES_128_GCM_SHA256", 8,  None),
        ("TLS_AES_128_GCM_SHA256", 16, None),
        ("TLS_AES_128_GCM_SHA256", 32, None),
        ("TLS_AES_256_GCM_SHA384", 0,  -1),
        ("TLS_AES_256_GCM_SHA384", 0,  -2),
        ("TLS_AES_256_GCM_SHA384", 0,  -4),
        ("TLS_AES_256_GCM_SHA384", 0,  -8),
        ("TLS_AES_256_GCM_SHA384", 0,  -16),
        ("TLS_AES_256_GCM_SHA384", 0,  -32),
        ("TLS_AES_256_GCM_SHA384", 0,  12),
        ("TLS_AES_256_GCM_SHA384", 1,  None),
        ("TLS_AES_256_GCM_SHA384", 2,  None),
        ("TLS_AES_256_GCM_SHA384", 4,  None),
        ("TLS_AES_256_GCM_SHA384", 8,  None),
        ("TLS_AES_256_GCM_SHA384", 16, None),
        ("TLS_AES_256_GCM_SHA384", 32, None),
    ]

    args = [
            "-x", "empty - cipher TLS_AES_128_GCM_SHA256", "-X", assertion,
            "-x", "empty - cipher TLS_AES_256_GCM_SHA384", "-X", assertion,
    ]
    padding_fmt = "padding - cipher %s, pad_byte 0, pad_left %d, pad_right %d"
    for padding in paddings:
        args += ["-x", padding_fmt % padding, "-X", assertion]
    truncation_fmt = "truncation - cipher %s, start %d, end %s"
    for truncation in truncations:
        args += ["-x", truncation_fmt % truncation, "-X", assertion]
    return args

tls13_tests = TestGroup("TLSv1.3 tests", [
    Test("test_tls13_ccs.py"),
    Test("test_tls13_conversation.py"),
    Test("test_tls13_count_tickets.py"),
    Test("test_tls13_empty_alert.py"),
    Test("test_tls13_finished.py", generate_test_tls13_finished_args()),
    Test("test_tls13_finished_plaintext.py"),
    Test("test_tls13_hrr.py"),
    Test("test_tls13_keyshare_omitted.py"),
    Test("test_tls13_legacy_version.py"),
    Test("test_tls13_nociphers.py"),
    Test("test_tls13_record_padding.py"),
    # Exclude QUIC transport parameters
    Test("test_tls13_shuffled_extentions.py", [ "--exc", "57" ]),
    Test("test_tls13_zero_content_type.py"),

    # The skipped tests fail due to a bug in BIO_gets() which masks the retry
    # signalled from an SSL_read() failure. Testing with httpd(8) shows we're
    # handling these corner cases correctly since tls13_record_layer.c -r1.47.
    Test("test_tls13_zero_length_data.py", [
        "-e", "zero-length app data",
        "-e", "zero-length app data with large padding",
        "-e", "zero-length app data with padding",
    ]),

    # We don't currently handle NSTs
    Test("test_tls13_connection_abort.py", ["-e", "After NewSessionTicket"]),
])

# Tests that take a lot of time (> ~30s on an x280)
tls13_slow_tests = TestGroup("slow TLSv1.3 tests", [
    # XXX: Investigate the occasional message
    # "Got shared secret with 1 most significant bytes equal to zero."
    Test("test_tls13_dhe_shared_secret_padding.py", tls13_unsupported_ciphers),

    Test("test_tls13_invalid_ciphers.py"),
    Test("test_tls13_serverhello_random.py", tls13_unsupported_ciphers),

    # Mark two tests cases as xfail for now. The tests expect an arguably
    # correct decode_error while we send a decrypt_error (like fizz/boring).
    Test("test_tls13_record_layer_limits.py", [
        "-x", "max size payload (2**14) of Finished msg, with 16348 bytes of left padding, cipher TLS_AES_128_GCM_SHA256",
        "-X", substitute_alert("decode_error", "decrypt_error"),
        "-x", "max size payload (2**14) of Finished msg, with 16348 bytes of left padding, cipher TLS_CHACHA20_POLY1305_SHA256",
        "-X", substitute_alert("decode_error", "decrypt_error"),
    ]),
    # We don't accept an empty ECPF extension since it must advertise the
    # uncompressed point format. Exclude this extension type from the test.
    Test(
        "test_tls13_large_number_of_extensions.py",
        tls13_args = ["--exc", "11"],
    ),
])

tls13_extra_cert_tests = TestGroup("TLSv1.3 certificate tests", [
    # need to set up client certs to run these
    Test("test_tls13_certificate_request.py"),
    Test("test_tls13_certificate_verify.py"),
    Test("test_tls13_ecdsa_in_certificate_verify.py"),
    Test("test_tls13_eddsa_in_certificate_verify.py"),

    # Test expects the server to have installed three certificates:
    # with P-256, P-384 and P-521 curve. Also SHA1+ECDSA is verified
    # to not work.
    Test("test_tls13_ecdsa_support.py"),
])

tls13_failing_tests = TestGroup("failing TLSv1.3 tests", [
    # Some tests fail because we fail later than the scripts expect us to.
    # With X25519, we accept weak peer public keys and fail when we actually
    # compute the keyshare.  Other tests seem to indicate that we could be
    # stricter about what keyshares we accept.
    Test("test_tls13_crfg_curves.py", [
        '-e', 'all zero x448 key share',
        '-e', 'empty x448 key share',
        '-e', 'sanity x448 with compression ansiX962_compressed_char2',
        '-e', 'sanity x448 with compression ansiX962_compressed_prime',
        '-e', 'sanity x448 with compression uncompressed',
        '-e', 'too big x448 key share',
        '-e', 'too small x448 key share',
        '-e', 'x448 key share of "1"',
    ]),
    Test("test_tls13_ecdhe_curves.py", [
        '-e', 'sanity - x448',
        '-e', 'x448 - key share from other curve',
        '-e', 'x448 - point at infinity',
        '-e', 'x448 - right 0-padded key_share',
        '-e', 'x448 - right-truncated key_share',
    ]),

    # The test sends records with protocol version 0x0300 instead of 0x0303
    # and currently fails with OpenSSL and LibreSSL for this reason.
    # We have the logic corresponding to NSS's fix for CVE-2020-25648
    # https://hg.mozilla.org/projects/nss/rev/57bbefa793232586d27cee83e74411171e128361
    # so should not be affected by this issue.
    Test("test_tls13_multiple_ccs_messages.py"),

    # https://github.com/openssl/openssl/issues/8369
    Test("test_tls13_obsolete_curves.py"),

    # 3 failing rsa_pss_pss tests
    Test("test_tls13_rsa_signatures.py"),

    # The failing tests all expect an ri extension.  What's up with that?
    Test("test_tls13_version_negotiation.py"),
])

tls13_slow_failing_tests = TestGroup("slow, failing TLSv1.3 tests", [
    # Other test failures bugs in keyshare/tlsext negotiation?
    Test("test_tls13_unrecognised_groups.py"),    # unexpected closure

    # 5 occasional failures:
    #   'app data split, conversation with KeyUpdate msg'
    #   'fragmented keyupdate msg'
    #   'multiple KeyUpdate messages'
    #   'post-handshake KeyUpdate msg with update_not_request'
    #   'post-handshake KeyUpdate msg with update_request'
    Test("test_tls13_keyupdate.py"),

    Test("test_tls13_symetric_ciphers.py"),       # unexpected message from peer

    # 6 tests fail: 'rsa_pkcs1_{md5,sha{1,224,256,384,512}} signature'
    # We send server hello, but the test expects handshake_failure
    Test("test_tls13_pkcs_signature.py"),
    # 8 tests fail: 'tls13 signature rsa_pss_{pss,rsae}_sha{256,384,512}
    Test("test_tls13_rsapss_signatures.py"),
])

tls13_unsupported_tests = TestGroup("TLSv1.3 tests for unsupported features", [
    # Tests for features we don't support
    Test("test_tls13_0rtt_garbage.py"),
    Test("test_tls13_ffdhe_groups.py"),
    Test("test_tls13_ffdhe_sanity.py"),
    Test("test_tls13_psk_dhe_ke.py"),
    Test("test_tls13_psk_ke.py"),

    # need server to react to HTTP GET for /keyupdate
    Test("test_tls13_keyupdate_from_server.py"),

    # needs an echo server
    Test("test_tls13_lengths.py"),

    # Weird test: tests servers that don't support 1.3
    Test("test_tls13_non_support.py"),

    # broken test script
    # UnboundLocalError: local variable 'cert' referenced before assignment
    Test("test_tls13_post_handshake_auth.py"),

    # ExpectNewSessionTicket
    Test("test_tls13_session_resumption.py"),

    # Server must be configured to support only rsa_pss_rsae_sha512
    Test("test_tls13_signature_algorithms.py"),
])

tls12_exclude_legacy_protocols = [
    # all these have BIO_read timeouts against TLSv1.3
    "-e", "Protocol (3, 0)",
    "-e", "Protocol (3, 1)",
    "-e", "Protocol (3, 2)",
    "-e", "Protocol (3, 0) in SSLv2 compatible ClientHello",
    # the following only fail with TLSv1.3
    "-e", "Protocol (3, 1) in SSLv2 compatible ClientHello",
    "-e", "Protocol (3, 2) in SSLv2 compatible ClientHello",
    "-e", "Protocol (3, 3) in SSLv2 compatible ClientHello",
    "-e", "Protocol (3, 1) with x448 group",
    "-e", "Protocol (3, 2) with x448 group",
    "-e", "Protocol (3, 3) with x448 group",
    # These don't work without TLSv1.0 and TLSv1.1
    "-e", "Protocol (3, 1) with secp256r1 group",
    "-e", "Protocol (3, 1) with secp384r1 group",
    "-e", "Protocol (3, 1) with secp521r1 group",
    "-e", "Protocol (3, 1) with x25519 group",
    "-e", "Protocol (3, 2) with secp256r1 group",
    "-e", "Protocol (3, 2) with secp384r1 group",
    "-e", "Protocol (3, 2) with secp521r1 group",
    "-e", "Protocol (3, 2) with x25519 group",
]

tls12_tests = TestGroup("TLSv1.2 tests", [
    # Tests that pass as they are.
    Test("test_aes_gcm_nonces.py"),
    Test("test_connection_abort.py"),
    Test("test_conversation.py"),
    Test("test_cve_2016_2107.py"),
    Test("test_cve_2016_6309.py"),
    Test("test_dhe_rsa_key_exchange.py"),
    Test("test_early_application_data.py"),
    Test("test_empty_extensions.py"),
    Test("test_extensions.py"),
    Test("test_fuzzed_MAC.py"),
    Test("test_fuzzed_ciphertext.py"),
    Test("test_fuzzed_finished.py"),
    Test("test_fuzzed_padding.py"),
    Test("test_fuzzed_plaintext.py"), # fails once in a while
    Test("test_hello_request_by_client.py"),
    Test("test_invalid_cipher_suites.py"),
    Test("test_invalid_content_type.py"),
    Test("test_invalid_session_id.py"),
    Test("test_invalid_version.py"),
    Test("test_large_number_of_extensions.py"),
    Test("test_lucky13.py"),
    Test("test_message_skipping.py"),
    Test("test_no_heartbeat.py"),
    Test("test_record_layer_fragmentation.py"),
    Test("test_sslv2_connection.py"),
    Test("test_truncating_of_finished.py"),
    Test("test_truncating_of_kRSA_client_key_exchange.py"),
    Test("test_unsupported_curve_fallback.py"),
    Test("test_version_numbers.py"),
    Test("test_zero_length_data.py"),

    # Tests that need tweaking for unsupported features and ciphers.
    Test(
        "test_atypical_padding.py", [
            "-e", "sanity - encrypt then MAC",
            "-e", "2^14 bytes of AppData with 256 bytes of padding (SHA1 + Encrypt then MAC)",
        ]
    ),
    Test(
        "test_ccs.py", [
            "-x", "two bytes long CCS",
            "-X", substitute_alert("unexpected_message", "decode_error"),
        ]
    ),
    Test(
        "test_dhe_rsa_key_exchange_signatures.py", [
            "-e", "TLS_DHE_RSA_WITH_3DES_EDE_CBC_SHA sha224 signature",
            "-e", "TLS_DHE_RSA_WITH_AES_128_CBC_SHA256 sha224 signature",
            "-e", "TLS_DHE_RSA_WITH_AES_128_CBC_SHA sha224 signature",
            "-e", "TLS_DHE_RSA_WITH_AES_256_CBC_SHA256 sha224 signature",
            "-e", "TLS_DHE_RSA_WITH_AES_256_CBC_SHA sha224 signature",
        ]
    ),
    Test("test_dhe_rsa_key_exchange_with_bad_messages.py", [
        "-x", "invalid dh_Yc value - missing",
        "-X", substitute_alert("decode_error", "illegal_parameter"),
    ]),
    Test("test_dhe_key_share_random.py", tls12_exclude_legacy_protocols),
    Test("test_export_ciphers_rejected.py", ["--min-ver", "TLSv1.2"]),
    Test(
        "test_downgrade_protection.py",
        tls12_args = ["--server-max-protocol", "TLSv1.2"],
        tls13_args = [
            "--server-max-protocol", "TLSv1.3",
            "-e", "TLS 1.3 downgrade check for Protocol (3, 1)",
            "-e", "TLS 1.3 downgrade check for Protocol (3, 2)",
        ]
    ),
    Test(
        "test_fallback_scsv.py",
        tls13_args = [
            "--tls-1.3",
            "-e", "FALLBACK - hello TLSv1.1 - pos 0",
            "-e", "FALLBACK - hello TLSv1.1 - pos 1",
            "-e", "FALLBACK - hello TLSv1.1 - pos 2",
            "-e", "FALLBACK - record TLSv1.1 hello TLSv1.1 - pos 0",
            "-e", "FALLBACK - record TLSv1.1 hello TLSv1.1 - pos 1",
            "-e", "FALLBACK - record TLSv1.1 hello TLSv1.1 - pos 2",
            "-e", "record TLSv1.1 hello TLSv1.1",
            "-e", "sanity - TLSv1.1",
        ]
    ),

    Test("test_invalid_compression_methods.py", [
        "-x", "invalid compression methods",
        "-X", substitute_alert("illegal_parameter", "decode_error"),
        "-x", "only deflate compression method",
        "-X", substitute_alert("illegal_parameter", "decode_error"),
    ]),

    # Skip extended_master_secret test. Since we don't support this
    # extension, we don't notice that it was dropped.
    Test("test_renegotiation_changed_clienthello.py", [
        "-e", "drop extended_master_secret in renegotiation",
    ]),

    Test("test_sessionID_resumption.py", [
        "-x", "Client Hello too long session ID",
        "-X", substitute_alert("decode_error", "illegal_parameter"),
    ]),

    # Without --sig-algs-drop-ok, two tests fail since we do not currently
    # implement the signature_algorithms_cert extension (although we MUST).
    Test("test_sig_algs_renegotiation_resumption.py", ["--sig-algs-drop-ok"]),

    Test("test_serverhello_random.py", args = tls12_exclude_legacy_protocols),

    Test("test_chacha20.py", [ "-e", "Chacha20 in TLS1.1" ]),
])

tls12_slow_tests = TestGroup("slow TLSv1.2 tests", [
    Test("test_cve_2016_7054.py"),
    Test("test_dhe_no_shared_secret_padding.py", tls12_exclude_legacy_protocols),
    Test("test_ecdhe_padded_shared_secret.py", tls12_exclude_legacy_protocols),
    Test("test_ecdhe_rsa_key_share_random.py", tls12_exclude_legacy_protocols),
    # Start at extension number 58 to avoid QUIC transport parameters (57)
    Test("test_large_hello.py", [ "-m", "58" ]),
])

tls12_failing_tests = TestGroup("failing TLSv1.2 tests", [
    # no shared cipher
    Test("test_aesccm.py"),
    # need server to set up alpn
    Test("test_alpn_negotiation.py"),
    # Failing on TLS_RSA_WITH_AES_128_CBC_SHA because server does not support it.
    Test("test_bleichenbacher_timing_pregenerate.py"),
    # many tests fail due to unexpected server_name extension
    Test("test_bleichenbacher_workaround.py"),

    # need client key and cert plus extra server setup
    Test("test_certificate_malformed.py"),
    Test("test_certificate_request.py"),
    Test("test_certificate_verify_malformed_sig.py"),
    Test("test_certificate_verify_malformed.py"),
    Test("test_certificate_verify.py"),
    Test("test_ecdsa_in_certificate_verify.py"),
    Test("test_eddsa_in_certificate_verify.py"),
    Test("test_renegotiation_disabled_client_cert.py"),
    Test("test_rsa_pss_sigs_on_certificate_verify.py"),
    Test("test_rsa_sigs_on_certificate_verify.py"),

    # test doesn't expect session ticket
    Test("test_client_compatibility.py"),
    # abrupt closure
    Test("test_client_hello_max_size.py"),
    # unknown signature algorithms
    Test("test_clienthello_md5.py"),

    # Tests expect an illegal_parameter or a decode_error alert.  Should be
    # added to ssl3_get_client_key_exchange on kex function failure.
    Test("test_ecdhe_rsa_key_exchange_with_bad_messages.py"),

    # We send a handshake_failure due to no shared ciphers while the
    # test expects to succeed.
    Test("test_ecdhe_rsa_key_exchange.py"),

    # no shared cipher
    Test("test_ecdsa_sig_flexibility.py"),

    # Tests expect SH but we send unexpected_message or handshake_failure
    #   'Application data inside Client Hello'
    #   'Application data inside Client Key Exchange'
    #   'Application data inside Finished'
    Test("test_interleaved_application_data_and_fragmented_handshakes_in_renegotiation.py"),
    # Tests expect SH but we send handshake_failure
    #   'Application data before Change Cipher Spec'
    #   'Application data before Client Key Exchange'
    #   'Application data before Finished'
    Test("test_interleaved_application_data_in_renegotiation.py"),

    # broken test script
    # TypeError: '<' not supported between instances of 'int' and 'NoneType'
    Test("test_invalid_client_hello_w_record_overflow.py"),

    # Lots of failures. abrupt closure
    Test("test_invalid_client_hello.py"),

    # abrupt closure
    # 'encrypted premaster set to all zero (n)' n in 256 384 512
    Test("test_invalid_rsa_key_exchange_messages.py"),

    # test expects illegal_parameter, we send unrecognized_name (which seems
    # correct according to rfc 6066?)
    Test("test_invalid_server_name_extension_resumption.py"),
    # let through some server names without sending an alert
    # again illegal_parameter vs unrecognized_name
    Test("test_invalid_server_name_extension.py"),

    # 4 failures:
    #   'insecure (legacy) renegotiation with GET after 2nd handshake'
    #   'insecure (legacy) renegotiation with incomplete GET'
    #   'secure renegotiation with GET after 2nd handshake'
    #   'secure renegotiation with incomplete GET'
    Test("test_legacy_renegotiation.py"),

    # 1 failure (timeout): we don't send the unexpected_message alert
    # 'duplicate change cipher spec after Finished'
    Test("test_message_duplication.py"),

    # server should send status_request
    Test("test_ocsp_stapling.py"),

    # unexpected closure
    Test("test_openssl_3712.py"),

    # failed: 3 (expect an alert, we send AD)
    # 'try insecure (legacy) renegotiation with incomplete GET'
    # 'try secure renegotiation with GET after 2nd CH'
    # 'try secure renegotiation with incomplete GET'
    Test("test_renegotiation_disabled.py"),

    # 'resumption of safe session with NULL cipher'
    # 'resumption with cipher from old CH but not selected by server'
    Test("test_resumption_with_wrong_ciphers.py"),

    # 'session resumption with empty session_id'
    # 'session resumption with random session_id'
    # 'session resumption with renegotiation'
    # AssertionError: Server did not send extension(s): session_ticket
    Test("test_session_ticket_resumption.py"),

    # 5 failures:
    #   'empty sigalgs'
    #   'only undefined sigalgs'
    #   'rsa_pss_pss_sha256 only'
    #   'rsa_pss_pss_sha384 only'
    #   'rsa_pss_pss_sha512 only'
    Test("test_sig_algs.py"),

    # 13 failures:
    #   'duplicated n non-rsa schemes' for n in 202 2342 8119 23741 32744
    #   'empty list of signature methods'
    #   'tolerance n RSA or ECDSA methods' for n in 215 2355 8132 23754
    #   'tolerance 32758 methods with sig_alg_cert'
    #   'tolerance max 32744 number of methods with sig_alg_cert'
    #   'tolerance max (32760) number of methods'
    Test("test_signature_algorithms.py"),

    # times out
    Test("test_ssl_death_alert.py"),

    # 17 pass, 13 fail. padding and truncation
    Test("test_truncating_of_client_hello.py"),

    # x448 tests need disabling plus x25519 corner cases need sorting out
    Test("test_x25519.py"),

    # Needs TLS 1.0 or 1.1
    Test("test_TLSv1_2_rejected_without_TLSv1_2.py"),
])

tls12_unsupported_tests = TestGroup("TLSv1.2 for unsupported features", [
    # protocol_version
    Test("test_SSLv3_padding.py"),
    # we don't do RSA key exchanges
    Test("test_bleichenbacher_timing.py"),
    # no encrypt-then-mac
    Test("test_encrypt_then_mac_renegotiation.py"),
    Test("test_encrypt_then_mac.py"),
    # no EME support
    Test("test_extended_master_secret_extension_with_client_cert.py"),
    Test("test_extended_master_secret_extension.py"),
    # no ffdhe
    Test("test_ffdhe_expected_params.py"),
    Test("test_ffdhe_negotiation.py"),
    # record_size_limit/max_fragment_length extension (RFC 8449)
    Test("test_record_size_limit.py"),
    # expects the server to send the heartbeat extension
    Test("test_heartbeat.py"),
    # needs an echo server
    Test("test_lengths.py"),
])

# These tests take a ton of time to fail against an 1.3 server,
# so don't run them against 1.3 pending further investigation.
legacy_tests = TestGroup("Legacy protocol tests", [
    Test("test_sslv2_force_cipher_3des.py"),
    Test("test_sslv2_force_cipher_non3des.py"),
    Test("test_sslv2_force_cipher.py"),
    Test("test_sslv2_force_export_cipher.py"),
    Test("test_sslv2hello_protocol.py"),
])

all_groups = [
    tls13_tests,
    tls13_slow_tests,
    tls13_extra_cert_tests,
    tls13_failing_tests,
    tls13_slow_failing_tests,
    tls13_unsupported_tests,
    tls12_tests,
    tls12_slow_tests,
    tls12_failing_tests,
    tls12_unsupported_tests,
    legacy_tests,
]

failing_groups = [
    tls13_failing_tests,
    tls13_slow_failing_tests,
    tls12_failing_tests,
]

class TestRunner:
    """ Runs the given tests against a server and displays stats. """

    def __init__(
        self, timing=False, verbose=False, host="localhost", port=4433,
        use_tls1_3=True, dry_run=False, tests=[], scriptdir=tlsfuzzer_scriptdir,
    ):
        self.tests = []

        self.dryrun = dry_run
        self.use_tls1_3 = use_tls1_3
        self.host = host
        self.port = str(port)
        self.scriptdir = scriptdir

        self.stats = []
        self.failed = []
        self.missing = []

        self.timing = timing
        self.verbose = verbose

    def add(self, title="tests", tests=[]):
        # tests.sort(key=lambda test: test.name)
        self.tests.append(TestGroup(title, tests))

    def add_group(self, group):
        self.tests.append(group)

    def run_script(self, test):
        script = test.name
        args = ["-h"] + [self.host] + ["-p"] + [self.port] + test.args(self.use_tls1_3)

        if self.dryrun:
            if not self.verbose:
                args = []
            print(script , end=' ' if args else '')
            print(' '.join([f"\"{arg}\"" for arg in args]))
            return

        if self.verbose:
            print(script)
        else:
            print(f"{script[:68]:<72}", end=" ", flush=True)
        start = timer()
        scriptpath = os.path.join(self.scriptdir, script)
        if not os.path.exists(scriptpath):
            self.missing.append(script)
            print("MISSING")
            return
        test = subprocess.run(
            ["python3", scriptpath] + args,
            capture_output=not self.verbose,
            text=True,
        )
        end = timer()
        self.stats.append((script, end - start))
        if test.returncode == 0:
            print("OK")
            return
        print("FAILED")
        self.failed.append(script)

        if self.verbose:
            return

        print('\n'.join(test.stdout.split("Test end\n", 1)[1:]), end="")

    def run(self):
        for group in self:
            print(f"Running {group.title} ...")
            for test in group:
                self.run_script(test)
        return not self.failed

    def __iter__(self):
        return iter(self.tests)

    def __del__(self):
        if self.timing and self.stats:
            total = 0.0
            for (script, time) in self.stats:
                print(f"{round(time, 2):6.2f} {script}")
                total += time
            print(f"{round(total, 2):6.2f} total")

        if self.failed:
            print("Failed tests:")
            print('\n'.join(self.failed))

        if self.missing:
            print("Missing tests (outdated package?):")
            print('\n'.join(self.missing))

class TlsServer:
    """ Spawns an s_server listening on localhost:port if necessary. """

    def __init__(self, host="localhost", port=4433):
        self.spawn = True
        # Check whether a server is already listening on localhost:port
        self.spawn = subprocess.run(
            ["nc", "-c", "-z", "-T", "noverify", host, str(port)],
            stderr=subprocess.DEVNULL,
        ).returncode != 0

        if self.spawn:
            self.server = subprocess.Popen(
                [
                    "openssl",
                    "s_server",
                    "-accept",
                    str(port),
                    "-groups",
                    "X25519:P-256:P-521:P-384",
                    "-key",
                    "localhost.key",
                    "-cert",
                    "localhost.crt",
                    "-www",
                ],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                text=True,
            )

        # Check whether the server talks TLSv1.3
        self.has_tls1_3 = True or subprocess.run(
            [
                "nc",
                "-c",
                "-z",
                "-T",
                "noverify",
                "-T",
                "protocols=TLSv1.3",
                "localhost",
                str(port),
            ],
            stderr=subprocess.DEVNULL,
        ).returncode == 0

        self.check()

    def check(self):
        if self.spawn and self.server.poll() is not None:
            print(self.server.stderr.read())
            raise RuntimeError(
                f"openssl s_server died. Return code: {self.server.returncode}."
            )
        if self.spawn:
            self.server.stderr.detach()

    def __del__(self):
        if self.spawn:
            self.server.terminate()

# Extract the arguments we pass to script
def defaultargs(script, has_tls1_3):
    return next(
        (test for group in all_groups for test in group if test.name == script),
        Test()
    ).args(has_tls1_3)

def list_or_missing(missing=True):
    tests = [test.name for group in all_groups for test in group]

    if missing:
        scripts = {
            f for f in os.listdir(tlsfuzzer_scriptdir) if f != "__pycache__"
        }
        missing = scripts - set(tests)
        if missing:
            print('\n'.join(sorted(missing)))
        exit(0)

    tests.sort()
    print('\n'.join(tests))
    exit(0)

def usage():
    print("Usage: python3 tlsfuzzer.py [-flmnstv] [-p port] [script [test...]]")
    print(" --help      help")
    print(" -f          run failing tests")
    print(" -l          list tests")
    print(" -m          list new tests after package update")
    print(" -n          do not run tests, but list the ones that would be run")
    print(" -p port     connect to this port - defaults to 4433")
    print(" -s          run slow tests")
    print(" -t          show timing stats at end")
    print(" -v          verbose output")
    exit(0)

def main():
    failing = False
    list = False
    missing = False
    dryrun = False
    host = "localhost"
    port = 4433
    slow = False
    timing = False
    verbose = False

    argv = sys.argv[1:]
    opts, args = getopt.getopt(argv, "fh:lmnp:stv", ["help"])
    for opt, arg in opts:
        if opt == '--help':
            usage()
        elif opt == '-f':
            failing = True
        elif opt == '-h':
            host = arg
        elif opt == '-l':
            list = True
        elif opt == '-m':
            missing = True
        elif opt == '-n':
            dryrun = True
        elif opt == '-p':
            port = int(arg)
        elif opt == '-s':
            slow = True
        elif opt == '-t':
            timing = True
        elif opt == '-v':
            verbose = True
        else:
            raise ValueError(f"Unknown option: {opt}")

    if not os.path.exists(tlsfuzzer_scriptdir):
        print("package py3-tlsfuzzer is required for this regress")
        exit(1)

    if list and failing:
        failing = [test.name for group in failing_groups for test in group]
        failing.sort()
        print('\n'.join(failing))
        exit(0)

    if list or missing:
        list_or_missing(missing)

    tls_server = TlsServer(host, port)

    tests = TestRunner(timing, verbose, host, port, tls_server.has_tls1_3, dryrun)

    if args:
        (dir, script) = os.path.split(args[0])
        if dir and not dir == '.':
            tests.scriptdir = dir

        testargs = defaultargs(script, tls_server.has_tls1_3)

        tests.verbose = True
        tests.add("test from command line", [Test(script, testargs + args[1:])])

        exit(not tests.run())

    if failing:
        if tls_server.has_tls1_3:
            tests.add_group(tls13_failing_tests)
            if slow:
                tests.add_group(tls13_slow_failing_tests)
        tests.add_group(tls12_failing_tests)

    if tls_server.has_tls1_3:
        tests.add_group(tls13_tests)
        if slow:
            tests.add_group(tls13_slow_tests)
    else:
        tests.add_group(legacy_tests)

    tests.add_group(tls12_tests)
    if slow:
        tests.add_group(tls12_slow_tests)

    success = tests.run()
    del tests

    if not success:
        print("FAILED")
        exit(1)

if __name__ == "__main__":
    main()
