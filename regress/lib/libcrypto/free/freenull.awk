# $OpenBSD: freenull.awk,v 1.4 2023/11/19 13:11:06 tb Exp $
# Copyright (c) 2018 Theo Buehler <tb@openbsd.org>
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

# usage: awk -f freenull.awk < Symbols.list > freenull.c.body

# Skip this function because it calls abort(3).
/^CRYPTO_dbg_free/ {
	next
}

# Skip *_free functions that take more than one or no argument.
/^ASN1_item_ex_free$/				||
/^ASN1_item_free$/				||
/^CONF_modules_free$/				||
/^EVP_PKEY_asn1_set_free$/			||
/^X509V3_section_free$/				||
/^X509V3_string_free$/				||
/^sk_pop_free$/ {
	next
}

# Skip functions that are prototyped in a .c file.
/^BIO_CONNECT_free$/				||
/^CRYPTO_free$/					||
/^EC_PRIVATEKEY_free$/				||
/^ECPARAMETERS_free$/				||
/^ECPKPARAMETERS_free$/				||
/^X9_62_CHARACTERISTIC_TWO_free$/		||
/^X9_62_PENTANOMIAL_free$/ {
	next
}

/_free$/ {
	printf("\t%s(NULL);\n", $0)
}
