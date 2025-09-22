/* $OpenBSD: asn1x509.c,v 1.5 2023/08/11 22:50:44 tb Exp $ */
/*
 * Copyright (c) 2017 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2023 Theo Buehler <tb@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/dsa.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

const char *dsa_test_key =
    "-----BEGIN DSA PRIVATE KEY-----\n"
    "MIH5AgEAAkEAt+CNNryEe8t2SkjuP0azjOKjSMXsw3GzjLS5c+vFLQKs0zIuPp8F\n"
    "I/z5t8vcNt/D8EyzQZWxgCfoasHqDOJvRwIVAKrJMyIMt9iJtaS31cyIJmIDVlZX\n"
    "AkEAs1/Uy+x0+1C1n7V3eJxuBdO/LUalbrZM5PfcwDshf9kcQNLsRu5zTZkU0OX/\n"
    "8xANz+ue2o6LON2sTAtuEfSM1QJBAIDRt0rQGGrFCRJ4O39Iqlf27yIO6Gq1ppbE\n"
    "Wvsvz4YSIZsG02vlBlzVIhULftNnkpN59MFtIjx8RsbEQ4YTnSICFDXPf/UIRvdH\n"
    "20NV++tnUZYUAXM+\n"
    "-----END DSA PRIVATE KEY-----\n";

unsigned char dsa_test_asn1_pubkey[] = {
	0x30, 0x81, 0xf2, 0x30, 0x81, 0xa9, 0x06, 0x07,
	0x2a, 0x86, 0x48, 0xce, 0x38, 0x04, 0x01, 0x30,
	0x81, 0x9d, 0x02, 0x41, 0x00, 0xb7, 0xe0, 0x8d,
	0x36, 0xbc, 0x84, 0x7b, 0xcb, 0x76, 0x4a, 0x48,
	0xee, 0x3f, 0x46, 0xb3, 0x8c, 0xe2, 0xa3, 0x48,
	0xc5, 0xec, 0xc3, 0x71, 0xb3, 0x8c, 0xb4, 0xb9,
	0x73, 0xeb, 0xc5, 0x2d, 0x02, 0xac, 0xd3, 0x32,
	0x2e, 0x3e, 0x9f, 0x05, 0x23, 0xfc, 0xf9, 0xb7,
	0xcb, 0xdc, 0x36, 0xdf, 0xc3, 0xf0, 0x4c, 0xb3,
	0x41, 0x95, 0xb1, 0x80, 0x27, 0xe8, 0x6a, 0xc1,
	0xea, 0x0c, 0xe2, 0x6f, 0x47, 0x02, 0x15, 0x00,
	0xaa, 0xc9, 0x33, 0x22, 0x0c, 0xb7, 0xd8, 0x89,
	0xb5, 0xa4, 0xb7, 0xd5, 0xcc, 0x88, 0x26, 0x62,
	0x03, 0x56, 0x56, 0x57, 0x02, 0x41, 0x00, 0xb3,
	0x5f, 0xd4, 0xcb, 0xec, 0x74, 0xfb, 0x50, 0xb5,
	0x9f, 0xb5, 0x77, 0x78, 0x9c, 0x6e, 0x05, 0xd3,
	0xbf, 0x2d, 0x46, 0xa5, 0x6e, 0xb6, 0x4c, 0xe4,
	0xf7, 0xdc, 0xc0, 0x3b, 0x21, 0x7f, 0xd9, 0x1c,
	0x40, 0xd2, 0xec, 0x46, 0xee, 0x73, 0x4d, 0x99,
	0x14, 0xd0, 0xe5, 0xff, 0xf3, 0x10, 0x0d, 0xcf,
	0xeb, 0x9e, 0xda, 0x8e, 0x8b, 0x38, 0xdd, 0xac,
	0x4c, 0x0b, 0x6e, 0x11, 0xf4, 0x8c, 0xd5, 0x03,
	0x44, 0x00, 0x02, 0x41, 0x00, 0x80, 0xd1, 0xb7,
	0x4a, 0xd0, 0x18, 0x6a, 0xc5, 0x09, 0x12, 0x78,
	0x3b, 0x7f, 0x48, 0xaa, 0x57, 0xf6, 0xef, 0x22,
	0x0e, 0xe8, 0x6a, 0xb5, 0xa6, 0x96, 0xc4, 0x5a,
	0xfb, 0x2f, 0xcf, 0x86, 0x12, 0x21, 0x9b, 0x06,
	0xd3, 0x6b, 0xe5, 0x06, 0x5c, 0xd5, 0x22, 0x15,
	0x0b, 0x7e, 0xd3, 0x67, 0x92, 0x93, 0x79, 0xf4,
	0xc1, 0x6d, 0x22, 0x3c, 0x7c, 0x46, 0xc6, 0xc4,
	0x43, 0x86, 0x13, 0x9d, 0x22,
};

const unsigned char dsa_test_asn1_pubkey_noparams[] = {
	0x30, 0x51, 0x30, 0x09, 0x06, 0x07, 0x2a, 0x86,
	0x48, 0xce, 0x38, 0x04, 0x01, 0x03, 0x44, 0x00,
	0x02, 0x41, 0x00, 0x80, 0xd1, 0xb7, 0x4a, 0xd0,
	0x18, 0x6a, 0xc5, 0x09, 0x12, 0x78, 0x3b, 0x7f,
	0x48, 0xaa, 0x57, 0xf6, 0xef, 0x22, 0x0e, 0xe8,
	0x6a, 0xb5, 0xa6, 0x96, 0xc4, 0x5a, 0xfb, 0x2f,
	0xcf, 0x86, 0x12, 0x21, 0x9b, 0x06, 0xd3, 0x6b,
	0xe5, 0x06, 0x5c, 0xd5, 0x22, 0x15, 0x0b, 0x7e,
	0xd3, 0x67, 0x92, 0x93, 0x79, 0xf4, 0xc1, 0x6d,
	0x22, 0x3c, 0x7c, 0x46, 0xc6, 0xc4, 0x43, 0x86,
	0x13, 0x9d, 0x22,
};

const char *ec_test_key =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEIEDkF84aPdBNu4vbPE+QV3EP9ULp4Enr1N0lz4vzuc2boAoGCCqGSM49\n"
    "AwEHoUQDQgAEUQGHBjYwbfHvI3QqdDy8ftNU5UvQqh6TH6upIrtz4CVccxnWO2+s\n"
    "qSMOu1z5KnGIOVf2kLQ2S2iMahyFMezr8g==\n"
    "-----END EC PRIVATE KEY-----\n";

unsigned char ec_test_asn1_pubkey[] = {
	0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86,
	0x48, 0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a,
	0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03,
	0x42, 0x00, 0x04, 0x51, 0x01, 0x87, 0x06, 0x36,
	0x30, 0x6d, 0xf1, 0xef, 0x23, 0x74, 0x2a, 0x74,
	0x3c, 0xbc, 0x7e, 0xd3, 0x54, 0xe5, 0x4b, 0xd0,
	0xaa, 0x1e, 0x93, 0x1f, 0xab, 0xa9, 0x22, 0xbb,
	0x73, 0xe0, 0x25, 0x5c, 0x73, 0x19, 0xd6, 0x3b,
	0x6f, 0xac, 0xa9, 0x23, 0x0e, 0xbb, 0x5c, 0xf9,
	0x2a, 0x71, 0x88, 0x39, 0x57, 0xf6, 0x90, 0xb4,
	0x36, 0x4b, 0x68, 0x8c, 0x6a, 0x1c, 0x85, 0x31,
	0xec, 0xeb, 0xf2,
};

const char *rsa_test_key =
    "-----BEGIN PRIVATE KEY-----\n"
    "MIIBVgIBADANBgkqhkiG9w0BAQEFAASCAUAwggE8AgEAAkEA4Fs6ljFFQw/ElDf5\n"
    "LTghVw972PVpQuKPQvwb1cWbV3+7W5sXOcoM/RvwzO7WeppkeltVCBoKaQd+9e2Z\n"
    "BHtYhwIDAQABAkEAhWv7dWIrrGvuHa8D0i51NU8R+b5IMOyHAfDnpMN1VByWcBdb\n"
    "G7ZJsEYlO1Tbx1zFQOVyrDUY2hn0YttPjWys0QIhAP9+FRhHCYye/EY14zSa+lxb\n"
    "ljOPjWgddMdJBcPOVNUNAiEA4M1QUtIcTnTnfvcxvEBIhbmSR8fRvZYAeT5EoTKM\n"
    "puMCIQD9898X8JRHWEg9qZabVWiBoO+ddJUD5jOLWsQGKvMbiQIgBOQyxTqRJxvg\n"
    "FaEnUeNMMKyzBCDS7X8gD4NNVvyUluUCIQC/lnO9xYi6S4BFMwHFEUY0jLr5vgsR\n"
    "+esRU9dLkMqt+w==\n"
    "-----END PRIVATE KEY-----\n";

unsigned char rsa_test_asn1_pubkey[] = {
	0x30, 0x5c, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86,
	0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05,
	0x00, 0x03, 0x4b, 0x00, 0x30, 0x48, 0x02, 0x41,
	0x00, 0xe0, 0x5b, 0x3a, 0x96, 0x31, 0x45, 0x43,
	0x0f, 0xc4, 0x94, 0x37, 0xf9, 0x2d, 0x38, 0x21,
	0x57, 0x0f, 0x7b, 0xd8, 0xf5, 0x69, 0x42, 0xe2,
	0x8f, 0x42, 0xfc, 0x1b, 0xd5, 0xc5, 0x9b, 0x57,
	0x7f, 0xbb, 0x5b, 0x9b, 0x17, 0x39, 0xca, 0x0c,
	0xfd, 0x1b, 0xf0, 0xcc, 0xee, 0xd6, 0x7a, 0x9a,
	0x64, 0x7a, 0x5b, 0x55, 0x08, 0x1a, 0x0a, 0x69,
	0x07, 0x7e, 0xf5, 0xed, 0x99, 0x04, 0x7b, 0x58,
	0x87, 0x02, 0x03, 0x01, 0x00, 0x01,
};

const char dh_test_key[] =
	"-----BEGIN PRIVATE KEY-----\n"
	"MIICJgIBADCCARcGCSqGSIb3DQEDATCCAQgCggEBAIXmHiRswMxVCnVzq4GuaErl\n"
	"2fBPDquOzFaxd/YSN7tVxnz3wcMNfBsHZWqtAXxTBWeyt8ydHcrIWx4EB3XTSwSi\n"
	"Jqh3CEcFhDfqKdo/u7vffxG+43lEsvZZIzZHYMcYsHIpcERRoAu0xnqjHUQTkvoi\n"
	"w7ukbuWr28bJrncPaxFGC8zZvLhSnUst5yzdyAsIddQvHgYBdCn2UEbz6qBx8gvJ\n"
	"lb3Jv1BiVJJ0odL94vpNXRGNZ57PPm5Xlj/n8l8LHpzzxbtjc52MVYbMPpVuWzmv\n"
	"2nWV0eL14708S/XG6e2AWGKb8AX8hCitdtVQ28SbEsf8Yd1dyWNo++oedFvU49sC\n"
	"AQIEggEEAoIBAGywTP/vBwEeuWIgTPnBf1/jWQgfFA5no3HdRIQsHVgo2EEZHErS\n"
	"X82hALavaUTEu+pHu+/yv3BLPr/8Lau6O7LOiqeXMjYX4HtSNmLZIEjugd1aCyCp\n"
	"n+jZjIHQCG0fvnwWFqkKTADe4n4DUz5qxuHYmlFY4NsdMj5yARAh9mn7hqwYX+Mf\n"
	"WhHLhHIHngXKNs7vKdHH/guo638uL6dv6OuTS0wbBsjLMFvQvccVlVUWlUFkH6I8\n"
	"GFt8kAFLdrzz8+oMq3hHsoWIrDSp0GYq6keSu3pBj4q2mTP7ugUU8ag/dZnga5sB\n"
	"Mdt2hicktiw/mQZP578plm6z2Lg0gl5yLxk=\n"
	"-----END PRIVATE KEY-----\n";

const unsigned char dh_test_asn1_pubkey[] = {
	0x30, 0x82, 0x02, 0x24, 0x30, 0x82, 0x01, 0x17,
	0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
	0x01, 0x03, 0x01, 0x30, 0x82, 0x01, 0x08, 0x02,
	0x82, 0x01, 0x01, 0x00, 0x85, 0xe6, 0x1e, 0x24,
	0x6c, 0xc0, 0xcc, 0x55, 0x0a, 0x75, 0x73, 0xab,
	0x81, 0xae, 0x68, 0x4a, 0xe5, 0xd9, 0xf0, 0x4f,
	0x0e, 0xab, 0x8e, 0xcc, 0x56, 0xb1, 0x77, 0xf6,
	0x12, 0x37, 0xbb, 0x55, 0xc6, 0x7c, 0xf7, 0xc1,
	0xc3, 0x0d, 0x7c, 0x1b, 0x07, 0x65, 0x6a, 0xad,
	0x01, 0x7c, 0x53, 0x05, 0x67, 0xb2, 0xb7, 0xcc,
	0x9d, 0x1d, 0xca, 0xc8, 0x5b, 0x1e, 0x04, 0x07,
	0x75, 0xd3, 0x4b, 0x04, 0xa2, 0x26, 0xa8, 0x77,
	0x08, 0x47, 0x05, 0x84, 0x37, 0xea, 0x29, 0xda,
	0x3f, 0xbb, 0xbb, 0xdf, 0x7f, 0x11, 0xbe, 0xe3,
	0x79, 0x44, 0xb2, 0xf6, 0x59, 0x23, 0x36, 0x47,
	0x60, 0xc7, 0x18, 0xb0, 0x72, 0x29, 0x70, 0x44,
	0x51, 0xa0, 0x0b, 0xb4, 0xc6, 0x7a, 0xa3, 0x1d,
	0x44, 0x13, 0x92, 0xfa, 0x22, 0xc3, 0xbb, 0xa4,
	0x6e, 0xe5, 0xab, 0xdb, 0xc6, 0xc9, 0xae, 0x77,
	0x0f, 0x6b, 0x11, 0x46, 0x0b, 0xcc, 0xd9, 0xbc,
	0xb8, 0x52, 0x9d, 0x4b, 0x2d, 0xe7, 0x2c, 0xdd,
	0xc8, 0x0b, 0x08, 0x75, 0xd4, 0x2f, 0x1e, 0x06,
	0x01, 0x74, 0x29, 0xf6, 0x50, 0x46, 0xf3, 0xea,
	0xa0, 0x71, 0xf2, 0x0b, 0xc9, 0x95, 0xbd, 0xc9,
	0xbf, 0x50, 0x62, 0x54, 0x92, 0x74, 0xa1, 0xd2,
	0xfd, 0xe2, 0xfa, 0x4d, 0x5d, 0x11, 0x8d, 0x67,
	0x9e, 0xcf, 0x3e, 0x6e, 0x57, 0x96, 0x3f, 0xe7,
	0xf2, 0x5f, 0x0b, 0x1e, 0x9c, 0xf3, 0xc5, 0xbb,
	0x63, 0x73, 0x9d, 0x8c, 0x55, 0x86, 0xcc, 0x3e,
	0x95, 0x6e, 0x5b, 0x39, 0xaf, 0xda, 0x75, 0x95,
	0xd1, 0xe2, 0xf5, 0xe3, 0xbd, 0x3c, 0x4b, 0xf5,
	0xc6, 0xe9, 0xed, 0x80, 0x58, 0x62, 0x9b, 0xf0,
	0x05, 0xfc, 0x84, 0x28, 0xad, 0x76, 0xd5, 0x50,
	0xdb, 0xc4, 0x9b, 0x12, 0xc7, 0xfc, 0x61, 0xdd,
	0x5d, 0xc9, 0x63, 0x68, 0xfb, 0xea, 0x1e, 0x74,
	0x5b, 0xd4, 0xe3, 0xdb, 0x02, 0x01, 0x02, 0x03,
	0x82, 0x01, 0x05, 0x00, 0x02, 0x82, 0x01, 0x00,
	0x44, 0x30, 0x25, 0xe2, 0xeb, 0x8f, 0xd0, 0x81,
	0x96, 0x3e, 0x7d, 0x1d, 0x9b, 0x82, 0x8a, 0x2d,
	0x0f, 0xb3, 0x2d, 0x9c, 0x2b, 0xb2, 0x88, 0xda,
	0xc6, 0xef, 0x6c, 0x9d, 0x1c, 0x80, 0xf1, 0xee,
	0x9d, 0x6b, 0x31, 0xb7, 0xb1, 0x9f, 0x30, 0x0d,
	0xb7, 0x92, 0xcf, 0x56, 0xeb, 0xfc, 0x91, 0x16,
	0x35, 0x96, 0x0c, 0x7b, 0x95, 0xbc, 0x65, 0x66,
	0x10, 0x81, 0x4b, 0x46, 0x04, 0xee, 0x95, 0xca,
	0xc9, 0x0c, 0xea, 0xc1, 0xd7, 0x3b, 0x83, 0xfb,
	0xce, 0x76, 0x17, 0xb4, 0x15, 0xad, 0x03, 0xd0,
	0x00, 0xef, 0xb2, 0xee, 0x12, 0x3f, 0x75, 0xd1,
	0xb8, 0x6c, 0xfd, 0x87, 0xb5, 0x07, 0xfa, 0x1e,
	0x60, 0x9b, 0x49, 0x6f, 0x89, 0xc2, 0x75, 0x4d,
	0x7d, 0x21, 0xdb, 0xb6, 0x85, 0x78, 0xa5, 0x77,
	0xbe, 0xeb, 0x4d, 0x9e, 0x1c, 0x05, 0xbc, 0x51,
	0x97, 0x0f, 0xe9, 0x68, 0x78, 0x5a, 0xc8, 0x4e,
	0xef, 0x72, 0x8f, 0x53, 0x41, 0x0d, 0x57, 0xf2,
	0xc5, 0x29, 0x33, 0x67, 0xdd, 0x35, 0x43, 0xfc,
	0x13, 0x49, 0x92, 0x1d, 0x14, 0x92, 0x40, 0x14,
	0x38, 0x32, 0xdb, 0x14, 0x95, 0x44, 0x2a, 0x03,
	0xb7, 0x87, 0xa3, 0x5a, 0x5a, 0xe2, 0x3b, 0xc5,
	0x44, 0xa4, 0x06, 0xf6, 0x14, 0xe6, 0x08, 0x9c,
	0x51, 0x09, 0x2a, 0xc4, 0x2e, 0x72, 0xb3, 0x20,
	0x46, 0x77, 0xe2, 0xda, 0x07, 0xd8, 0x10, 0x89,
	0xcf, 0x2b, 0xef, 0x67, 0xa2, 0x48, 0xfd, 0xa3,
	0x71, 0x59, 0xf0, 0x89, 0x3a, 0x35, 0x31, 0x87,
	0xad, 0x45, 0x9e, 0x35, 0xbd, 0x64, 0xec, 0xd1,
	0xd7, 0xea, 0x92, 0xed, 0x72, 0x9c, 0x81, 0x8e,
	0x11, 0x4e, 0xa5, 0xe7, 0x12, 0xe3, 0x7c, 0x53,
	0x2b, 0x31, 0xd4, 0x3d, 0xd5, 0xd9, 0xbd, 0x44,
	0x27, 0xa3, 0x4a, 0x3f, 0x20, 0x87, 0xce, 0x73,
	0x0e, 0xa8, 0x90, 0xcd, 0xfe, 0x32, 0x69, 0x9a,
};

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static int
compare_data(const char *label, const unsigned char *d1, size_t d1_len,
    const unsigned char *d2, size_t d2_len)
{
	if (d1_len != d2_len) {
		fprintf(stderr, "FAIL: got %s with length %zu, want %zu\n",
		    label, d1_len, d2_len);
		return -1;
	}
	if (memcmp(d1, d2, d1_len) != 0) {
		fprintf(stderr, "FAIL: %s differs\n", label);
		fprintf(stderr, "got:\n");
		hexdump(d1, d1_len);
		fprintf(stderr, "want:\n");
		hexdump(d2, d2_len);
		return -1;
	}
	return 0;
}

static int
dh_pubkey_test(void)
{
	EVP_PKEY *pkey = NULL;
	EVP_PKEY *pkey_a = NULL, *pkey_b = NULL;
	unsigned char *out = NULL, *data = NULL;
	DH *dh_a = NULL, *dh_b = NULL;
	const unsigned char *p;
	BIO *bio_mem = NULL;
	int failure = 1;
	int len;

	ERR_clear_error();

	if ((bio_mem = BIO_new_mem_buf(dh_test_key, -1)) == NULL)
		errx(1, "failed to create BIO");

	if ((pkey = PEM_read_bio_PrivateKey(bio_mem, NULL, NULL, NULL)) == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to decode DH key from PEM");
	}

	/*
	 * Test PEM_write_bio_PrivateKey().
	 */
	BIO_free_all(bio_mem);
	if ((bio_mem = BIO_new(BIO_s_mem())) == NULL)
                errx(1, "BIO_new failed for BIO_s_mem");

	if (!PEM_write_bio_PrivateKey(bio_mem, pkey, NULL, NULL, 0, 0, NULL)) {
		fprintf(stderr, "FAIL: PEM_write_bio_PrivateKey failed\n");
		goto done;
	}

	len = BIO_get_mem_data(bio_mem, &data);
	if (compare_data("DH PrivateKey", data, len,
	    dh_test_key, sizeof(dh_test_key) - 1) == -1)
		goto done;

	/*
	 * Test i2d_PUBKEY/d2i_PUBKEY.
	 */

	if ((dh_a = EVP_PKEY_get1_DH(pkey)) == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to get1 DH key from PEM");
	}

	if ((pkey_a = EVP_PKEY_new()) == NULL)
		errx(1, "failed to create EVP_PKEY");
	if (!EVP_PKEY_set1_DH(pkey_a, dh_a))
		errx(1, "failed to set DH on EVP_PKEY");

	if ((len = i2d_PUBKEY(pkey_a, &out)) < 0) {
		fprintf(stderr, "FAIL: i2d_PUBKEY failed\n");
		goto done;
	}
	if (compare_data("DH PUBKEY", out, len, dh_test_asn1_pubkey,
	    sizeof(dh_test_asn1_pubkey)) == -1)
		goto done;

	p = out;
	if ((pkey_b = d2i_PUBKEY(NULL, &p, len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_PUBKEY failed\n");
		goto done;
	}

	if (BN_cmp(DH_get0_pub_key(EVP_PKEY_get0_DH(pkey_a)),
	    DH_get0_pub_key(EVP_PKEY_get0_DH(pkey_b))) != 0) {
		fprintf(stderr, "FAIL: DH public keys mismatch\n");
		goto done;
	}

	failure = 0;

 done:
	BIO_free_all(bio_mem);
	EVP_PKEY_free(pkey);
	DH_free(dh_a);
	DH_free(dh_b);
	EVP_PKEY_free(pkey_a);
	EVP_PKEY_free(pkey_b);
	free(out);

	return failure;
}

static int
dsa_pubkey_test(void)
{
	EVP_PKEY *pkey_a = NULL, *pkey_b = NULL;
	unsigned char *out = NULL, *data = NULL;
	DSA *dsa_a = NULL, *dsa_b = NULL;
	const unsigned char *p;
	BIO *bio_mem = NULL;
	int failure = 1;
	int len, ret;

	ERR_clear_error();

	if ((bio_mem = BIO_new_mem_buf((void *)dsa_test_key, -1)) == NULL)
		errx(1, "failed to create BIO");

	if ((dsa_a = PEM_read_bio_DSAPrivateKey(bio_mem, NULL, NULL, NULL)) == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to decode DSA key from PEM");
	}

	/*
	 * Test i2d_PUBKEY/d2i_PUBKEY.
	 */
	if ((pkey_a = EVP_PKEY_new()) == NULL)
		errx(1, "failed to create EVP_PKEY");
	if (!EVP_PKEY_set1_DSA(pkey_a, dsa_a))
		errx(1, "failed to set DSA on EVP_PKEY");

	if ((len = i2d_PUBKEY(pkey_a, &out)) < 0) {
		fprintf(stderr, "FAIL: i2d_PUBKEY failed\n");
		goto done;
	}
	if (compare_data("DSA PUBKEY", out, len, dsa_test_asn1_pubkey,
	    sizeof(dsa_test_asn1_pubkey)) == -1)
		goto done;

	p = out;
	if ((pkey_b = d2i_PUBKEY(NULL, &p, len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_PUBKEY failed\n");
		goto done;
	}

	if (BN_cmp(DSA_get0_pub_key(EVP_PKEY_get0_DSA(pkey_a)),
	    DSA_get0_pub_key(EVP_PKEY_get0_DSA(pkey_b))) != 0) {
		fprintf(stderr, "FAIL: DSA public keys mismatch\n");
		goto done;
	}

	if (EVP_PKEY_missing_parameters(pkey_b)) {
		fprintf(stderr, "FAIL: DSA pkey_b has missing parameters\n");
		goto done;
	}

	if (!EVP_PKEY_cmp_parameters(pkey_a, pkey_b)) {
		fprintf(stderr, "FAIL: DSA parameters mismatch\n");
		goto done;
	}

	/*
	 * Check save_parameters defaults - EVP_PKEY_save_parameters() returns
	 * the current save_parameters; mode -1 inspects without setting.
	 */
	if ((ret = EVP_PKEY_save_parameters(pkey_b, 0)) != 1) {
		fprintf(stderr, "FAIL: DSA save_parameters want 1, got %d\n", ret);
		goto done;
	}
	if ((ret = EVP_PKEY_save_parameters(pkey_b, -1)) != 0) {
		fprintf(stderr, "FAIL: DSA save_parameters want 0, got %d\n", ret);
		goto done;
	}

	free(out);
	out = NULL;

	if ((len = i2d_PUBKEY(pkey_b, &out)) < 0) {
		fprintf(stderr, "FAIL: i2d_PUBKEY (no params) failed\n");
		goto done;
	}

	if (compare_data("PUBKEY (no params)", dsa_test_asn1_pubkey_noparams,
	    sizeof(dsa_test_asn1_pubkey_noparams), out, len) == -1)
		goto done;

	EVP_PKEY_free(pkey_b);

	p = out;
	if ((pkey_b = d2i_PUBKEY(NULL, &p, len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_PUBKEY (no params) failed\n");
		goto done;
	}

	if (!EVP_PKEY_missing_parameters(pkey_b)) {
		fprintf(stderr, "FAIL: DSA pkey_b has no missing parameters\n");
		goto done;
	}

	if (BN_cmp(DSA_get0_pub_key(EVP_PKEY_get0_DSA(pkey_a)),
	    DSA_get0_pub_key(EVP_PKEY_get0_DSA(pkey_b))) != 0) {
		fprintf(stderr, "FAIL: DSA public keys mismatch\n");
		goto done;
	}

	if (EVP_PKEY_cmp_parameters(pkey_a, pkey_b)) {
		fprintf(stderr, "FAIL: DSA parameters match\n");
		goto done;
	}

	if (EVP_PKEY_cmp(pkey_a, pkey_b)) {
		fprintf(stderr, "FAIL: DSA keys should not match\n");
		goto done;
	}

	if (!EVP_PKEY_copy_parameters(pkey_b, pkey_a)) {
		fprintf(stderr, "FAIL: failed to copy DSA parameters\n");
		goto done;
	}

	if (!EVP_PKEY_cmp(pkey_a, pkey_b)) {
		fprintf(stderr, "FAIL: DSA keys should match\n");
		goto done;
	}

	free(out);
	out = NULL;

	/*
	 * Test i2d_DSA_PUBKEY/d2i_DSA_PUBKEY.
	 */

	if ((len = i2d_DSA_PUBKEY(dsa_a, &out)) < 0) {
		fprintf(stderr, "FAIL: i2d_DSA_PUBKEY failed\n");
		goto done;
	}
	if (compare_data("DSA_PUBKEY", out, len, dsa_test_asn1_pubkey,
	    sizeof(dsa_test_asn1_pubkey)) == -1)
		goto done;

	p = out;
	if ((dsa_b = d2i_DSA_PUBKEY(NULL, &p, len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_DSA_PUBKEY failed\n");
		goto done;
	}

	if (BN_cmp(DSA_get0_pub_key(dsa_a), DSA_get0_pub_key(dsa_b)) != 0) {
		fprintf(stderr, "FAIL: DSA public keys mismatch\n");
		goto done;
	}

	p = out;
	if ((dsa_a = d2i_DSA_PUBKEY(&dsa_a, &p, len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_DSA_PUBKEY failed\n");
		goto done;
	}

	if (BN_cmp(DSA_get0_pub_key(dsa_a), DSA_get0_pub_key(dsa_b)) != 0) {
		fprintf(stderr, "FAIL: DSA public keys mismatch\n");
		goto done;
	}

	/*
	 * Test i2d_DSA_PUBKEY_bio/d2i_DSA_PUBKEY_bio.
	 */
	BIO_free_all(bio_mem);
	if ((bio_mem = BIO_new(BIO_s_mem())) == NULL)
                errx(1, "BIO_new failed for BIO_s_mem");

	if ((len = i2d_DSA_PUBKEY_bio(bio_mem, dsa_a)) < 0) {
		fprintf(stderr, "FAIL: i2d_DSA_PUBKEY_bio failed\n");
		goto done;
	}

	len = BIO_get_mem_data(bio_mem, &data);
	if (compare_data("DSA_PUBKEY", data, len, dsa_test_asn1_pubkey,
	    sizeof(dsa_test_asn1_pubkey)) == -1)
		goto done;

	DSA_free(dsa_b);
	if ((dsa_b = d2i_DSA_PUBKEY_bio(bio_mem, NULL)) == NULL) {
		fprintf(stderr, "FAIL: d2i_DSA_PUBKEY_bio failed\n");
		goto done;
	}

	if (BN_cmp(DSA_get0_pub_key(dsa_a), DSA_get0_pub_key(dsa_b)) != 0) {
		fprintf(stderr, "FAIL: DSA public keys mismatch\n");
		goto done;
	}

	failure = 0;

 done:
	BIO_free_all(bio_mem);
	DSA_free(dsa_a);
	DSA_free(dsa_b);
	EVP_PKEY_free(pkey_a);
	EVP_PKEY_free(pkey_b);
	free(out);

	return (failure);
}

static int
ec_pubkey_test(void)
{
	EVP_PKEY *pkey_a = NULL, *pkey_b = NULL;
	unsigned char *out = NULL, *data = NULL;
	EC_KEY *ec_a = NULL, *ec_b = NULL;
	const unsigned char *p;
	BIO *bio_mem = NULL;
	int failure = 1;
	int len;

	ERR_clear_error();

	if ((bio_mem = BIO_new_mem_buf((void *)ec_test_key, -1)) == NULL)
		errx(1, "failed to create BIO");

	if ((ec_a = PEM_read_bio_ECPrivateKey(bio_mem, NULL, NULL, NULL)) == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to decode EC key from PEM");
	}

	/*
	 * Test i2d_PUBKEY/d2i_PUBKEY.
	 */
	if ((pkey_a = EVP_PKEY_new()) == NULL)
		errx(1, "failed to create EVP_PKEY");
	if (!EVP_PKEY_set1_EC_KEY(pkey_a, ec_a))
		errx(1, "failed to set EC_KEY on EVP_PKEY");

	if ((len = i2d_PUBKEY(pkey_a, &out)) < 0) {
		fprintf(stderr, "FAIL: i2d_PUBKEY failed\n");
		goto done;
	}
	if (compare_data("EC_KEY PUBKEY", out, len, ec_test_asn1_pubkey,
	    sizeof(ec_test_asn1_pubkey)) == -1)
		goto done;

	p = out;
	if ((pkey_b = d2i_PUBKEY(NULL, &p, len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_PUBKEY failed\n");
		goto done;
	}

	if (EC_GROUP_cmp(EC_KEY_get0_group(EVP_PKEY_get0_EC_KEY(pkey_a)),
	    EC_KEY_get0_group(EVP_PKEY_get0_EC_KEY(pkey_b)), NULL) != 0) {
		fprintf(stderr, "FAIL: EC_KEY groups keys mismatch\n");
		goto done;
	}
        if (EC_POINT_cmp(EC_KEY_get0_group(EVP_PKEY_get0_EC_KEY(pkey_a)),
	    EC_KEY_get0_public_key(EVP_PKEY_get0_EC_KEY(pkey_a)),
	    EC_KEY_get0_public_key(EVP_PKEY_get0_EC_KEY(pkey_b)), NULL) != 0) {
		fprintf(stderr, "FAIL: EC_KEY public keys mismatch\n");
		goto done;
	}

	free(out);
	out = NULL;

	/*
	 * Test i2d_EC_PUBKEY/d2i_EC_PUBKEY.
	 */

	if ((len = i2d_EC_PUBKEY(ec_a, &out)) < 0) {
		fprintf(stderr, "FAIL: i2d_EC_PUBKEY failed\n");
		goto done;
	}
	if (compare_data("EC_PUBKEY", out, len, ec_test_asn1_pubkey,
	    sizeof(ec_test_asn1_pubkey)) == -1)
		goto done;

	p = out;
	if ((ec_b = d2i_EC_PUBKEY(NULL, &p, len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_EC_PUBKEY failed\n");
		goto done;
	}

	if (EC_GROUP_cmp(EC_KEY_get0_group(ec_a), EC_KEY_get0_group(ec_b),
	    NULL) != 0) {
		fprintf(stderr, "FAIL: EC_KEY groups keys mismatch\n");
		goto done;
	}
        if (EC_POINT_cmp(EC_KEY_get0_group(ec_a), EC_KEY_get0_public_key(ec_a),
	    EC_KEY_get0_public_key(ec_b), NULL) != 0) {
		fprintf(stderr, "FAIL: EC_KEY public keys mismatch\n");
		goto done;
	}

	p = out;
	if ((ec_a = d2i_EC_PUBKEY(&ec_a, &p, len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_EC_PUBKEY failed\n");
		goto done;
	}

	if (EC_GROUP_cmp(EC_KEY_get0_group(ec_a), EC_KEY_get0_group(ec_b),
	    NULL) != 0) {
		fprintf(stderr, "FAIL: EC_KEY groups keys mismatch\n");
		goto done;
	}
        if (EC_POINT_cmp(EC_KEY_get0_group(ec_a), EC_KEY_get0_public_key(ec_a),
	    EC_KEY_get0_public_key(ec_b), NULL) != 0) {
		fprintf(stderr, "FAIL: EC_KEY public keys mismatch\n");
		goto done;
	}

	/*
	 * Test i2d_EC_PUBKEY_bio/d2i_EC_PUBKEY_bio.
	 */
	BIO_free_all(bio_mem);
	if ((bio_mem = BIO_new(BIO_s_mem())) == NULL)
                errx(1, "BIO_new failed for BIO_s_mem");

	if ((len = i2d_EC_PUBKEY_bio(bio_mem, ec_a)) < 0) {
		fprintf(stderr, "FAIL: i2d_EC_PUBKEY_bio failed\n");
		goto done;
	}

	len = BIO_get_mem_data(bio_mem, &data);
	if (compare_data("EC_PUBKEY", data, len, ec_test_asn1_pubkey,
	    sizeof(ec_test_asn1_pubkey)) == -1)
		goto done;

	EC_KEY_free(ec_b);
	if ((ec_b = d2i_EC_PUBKEY_bio(bio_mem, NULL)) == NULL) {
		fprintf(stderr, "FAIL: d2i_EC_PUBKEY_bio failed\n");
		goto done;
	}

	if (EC_GROUP_cmp(EC_KEY_get0_group(ec_a), EC_KEY_get0_group(ec_b),
	    NULL) != 0) {
		fprintf(stderr, "FAIL: EC_KEY groups keys mismatch\n");
		goto done;
	}
        if (EC_POINT_cmp(EC_KEY_get0_group(ec_a), EC_KEY_get0_public_key(ec_a),
	    EC_KEY_get0_public_key(ec_b), NULL) != 0) {
		fprintf(stderr, "FAIL: EC_KEY public keys mismatch\n");
		goto done;
	}

	failure = 0;

 done:
	BIO_free_all(bio_mem);
	EC_KEY_free(ec_a);
	EC_KEY_free(ec_b);
	EVP_PKEY_free(pkey_a);
	EVP_PKEY_free(pkey_b);
	free(out);

	return (failure);
}

static int
rsa_pubkey_test(void)
{
	EVP_PKEY *pkey_a = NULL, *pkey_b = NULL;
	RSA *rsa_a = NULL, *rsa_b = NULL;
	unsigned char *out = NULL, *data = NULL;
	const unsigned char *p;
	BIO *bio_mem = NULL;
	int failure = 1;
	int len;

	ERR_clear_error();

	if ((bio_mem = BIO_new_mem_buf((void *)rsa_test_key, -1)) == NULL)
		errx(1, "failed to create BIO");

	if ((rsa_a = PEM_read_bio_RSAPrivateKey(bio_mem, NULL, NULL, NULL)) == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to decode RSA key from PEM");
	}

	/*
	 * Test i2d_PUBKEY/d2i_PUBKEY.
	 */
	if ((pkey_a = EVP_PKEY_new()) == NULL)
		errx(1, "failed to create EVP_PKEY");
	if (!EVP_PKEY_set1_RSA(pkey_a, rsa_a))
		errx(1, "failed to set RSA on EVP_PKEY");

	if ((len = i2d_PUBKEY(pkey_a, &out)) < 0) {
		fprintf(stderr, "FAIL: i2d_PUBKEY failed\n");
		goto done;
	}
	if (compare_data("RSA PUBKEY", out, len, rsa_test_asn1_pubkey,
	    sizeof(rsa_test_asn1_pubkey)) == -1)
		goto done;

	p = out;
	if ((pkey_b = d2i_PUBKEY(NULL, &p, len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_PUBKEY failed\n");
		goto done;
	}

	if (BN_cmp(RSA_get0_n(EVP_PKEY_get0_RSA(pkey_a)),
	    RSA_get0_n(EVP_PKEY_get0_RSA(pkey_b))) != 0 ||
	    BN_cmp(RSA_get0_e(EVP_PKEY_get0_RSA(pkey_a)),
	    RSA_get0_e(EVP_PKEY_get0_RSA(pkey_b))) != 0) {
		fprintf(stderr, "FAIL: RSA public keys mismatch\n");
		goto done;
	}

	free(out);
	out = NULL;

	/*
	 * Test i2d_RSA_PUBKEY/d2i_RSA_PUBKEY.
	 */

	if ((len = i2d_RSA_PUBKEY(rsa_a, &out)) < 0) {
		fprintf(stderr, "FAIL: i2d_RSA_PUBKEY failed\n");
		goto done;
	}
	if (compare_data("RSA_PUBKEY", out, len, rsa_test_asn1_pubkey,
	    sizeof(rsa_test_asn1_pubkey)) == -1)
		goto done;

	p = out;
	if ((rsa_b = d2i_RSA_PUBKEY(NULL, &p, len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_RSA_PUBKEY failed\n");
		goto done;
	}

	if (BN_cmp(RSA_get0_n(rsa_a), RSA_get0_n(rsa_b)) != 0 ||
	    BN_cmp(RSA_get0_e(rsa_a), RSA_get0_e(rsa_b)) != 0) {
		fprintf(stderr, "FAIL: RSA public keys mismatch\n");
		goto done;
	}

	p = out;
	if ((rsa_a = d2i_RSA_PUBKEY(&rsa_a, &p, len)) == NULL) {
		fprintf(stderr, "FAIL: d2i_RSA_PUBKEY failed\n");
		goto done;
	}

	if (BN_cmp(RSA_get0_n(rsa_a), RSA_get0_n(rsa_b)) != 0 ||
	    BN_cmp(RSA_get0_e(rsa_a), RSA_get0_e(rsa_b)) != 0) {
		fprintf(stderr, "FAIL: RSA public keys mismatch\n");
		goto done;
	}

	/*
	 * Test i2d_RSA_PUBKEY_bio/d2i_RSA_PUBKEY_bio.
	 */
	BIO_free_all(bio_mem);
	if ((bio_mem = BIO_new(BIO_s_mem())) == NULL)
                errx(1, "BIO_new failed for BIO_s_mem");

	if ((len = i2d_RSA_PUBKEY_bio(bio_mem, rsa_a)) < 0) {
		fprintf(stderr, "FAIL: i2d_RSA_PUBKEY_bio failed\n");
		goto done;
	}

	len = BIO_get_mem_data(bio_mem, &data);
	if (compare_data("RSA_PUBKEY", data, len, rsa_test_asn1_pubkey,
	    sizeof(rsa_test_asn1_pubkey)) == -1)
		goto done;

	RSA_free(rsa_b);
	if ((rsa_b = d2i_RSA_PUBKEY_bio(bio_mem, NULL)) == NULL) {
		fprintf(stderr, "FAIL: d2i_RSA_PUBKEY_bio failed\n");
		goto done;
	}

	if (BN_cmp(RSA_get0_n(rsa_a), RSA_get0_n(rsa_b)) != 0 ||
	    BN_cmp(RSA_get0_e(rsa_a), RSA_get0_e(rsa_b)) != 0) {
		fprintf(stderr, "FAIL: RSA public keys mismatch\n");
		goto done;
	}

	failure = 0;

 done:
	BIO_free_all(bio_mem);
	RSA_free(rsa_a);
	RSA_free(rsa_b);
	EVP_PKEY_free(pkey_a);
	EVP_PKEY_free(pkey_b);
	free(out);

	return (failure);
}

int
main(int argc, char **argv)
{
	int failed = 0;

	ERR_load_crypto_strings();

	failed |= dh_pubkey_test();
	failed |= dsa_pubkey_test();
	failed |= ec_pubkey_test();
	failed |= rsa_pubkey_test();

	return (failed);
}
