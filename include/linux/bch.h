/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Generic binary BCH encoding/decoding library
 *
 * Copyright © 2011 Parrot S.A.
 *
 * Author: Ivan Djelic <ivan.djelic@parrot.com>
 *
 * Description:
 *
 * This library provides runtime configurable encoding/decoding of binary
 * Bose-Chaudhuri-Hocquenghem (BCH) codes.
*/
#ifndef _BCH_H
#define _BCH_H

#include <linux/types.h>

/**
 * struct bch_control - BCH control structure
 * @m:          Galois field order
 * @n:          maximum codeword size in bits (= 2^m-1)
 * @t:          error correction capability in bits
 * @ecc_bits:   ecc exact size in bits, i.e. generator polynomial degree (<=m*t)
 * @ecc_bytes:  ecc max size (m*t bits) in bytes
 * @a_pow_tab:  Galois field GF(2^m) exponentiation lookup table
 * @a_log_tab:  Galois field GF(2^m) log lookup table
 * @mod8_tab:   remainder generator polynomial lookup tables
 * @ecc_buf:    ecc parity words buffer
 * @ecc_buf2:   ecc parity words buffer
 * @xi_tab:     GF(2^m) base for solving degree 2 polynomial roots
 * @syn:        syndrome buffer
 * @cache:      log-based polynomial representation buffer
 * @elp:        error locator polynomial
 * @poly_2t:    temporary polynomials of degree 2t
 * @swap_bits:  swap bits within data and syndrome bytes
 */
struct bch_control {
	unsigned int    m;
	unsigned int    n;
	unsigned int    t;
	unsigned int    ecc_bits;
	unsigned int    ecc_bytes;
/* private: */
	uint16_t       *a_pow_tab;
	uint16_t       *a_log_tab;
	uint32_t       *mod8_tab;
	uint32_t       *ecc_buf;
	uint32_t       *ecc_buf2;
	unsigned int   *xi_tab;
	unsigned int   *syn;
	int            *cache;
	struct gf_poly *elp;
	struct gf_poly *poly_2t[4];
	bool		swap_bits;
};

struct bch_control *bch_init(int m, int t, unsigned int prim_poly,
			     bool swap_bits);

void bch_free(struct bch_control *bch);

void bch_encode(struct bch_control *bch, const uint8_t *data,
		unsigned int len, uint8_t *ecc);

int bch_decode(struct bch_control *bch, const uint8_t *data, unsigned int len,
	       const uint8_t *recv_ecc, const uint8_t *calc_ecc,
	       const unsigned int *syn, unsigned int *errloc);

#endif /* _BCH_H */
