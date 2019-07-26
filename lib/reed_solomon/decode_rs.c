// SPDX-License-Identifier: GPL-2.0
/*
 * Generic Reed Solomon encoder / decoder library
 *
 * Copyright 2002, Phil Karn, KA9Q
 * May be used under the terms of the GNU General Public License (GPL)
 *
 * Adaption to the kernel by Thomas Gleixner (tglx@linutronix.de)
 *
 * Generic data width independent code which is included by the wrappers.
 */
{
	struct rs_codec *rs = rsc->codec;
	int deg_lambda, el, deg_omega;
	int i, j, r, k, pad;
	int nn = rs->nn;
	int nroots = rs->nroots;
	int fcr = rs->fcr;
	int prim = rs->prim;
	int iprim = rs->iprim;
	uint16_t *alpha_to = rs->alpha_to;
	uint16_t *index_of = rs->index_of;
	uint16_t u, q, tmp, num1, num2, den, discr_r, syn_error;
	int count = 0;
	uint16_t msk = (uint16_t) rs->nn;

	/*
	 * The decoder buffers are in the rs control struct. They are
	 * arrays sized [nroots + 1]
	 */
	uint16_t *lambda = rsc->buffers + RS_DECODE_LAMBDA * (nroots + 1);
	uint16_t *syn = rsc->buffers + RS_DECODE_SYN * (nroots + 1);
	uint16_t *b = rsc->buffers + RS_DECODE_B * (nroots + 1);
	uint16_t *t = rsc->buffers + RS_DECODE_T * (nroots + 1);
	uint16_t *omega = rsc->buffers + RS_DECODE_OMEGA * (nroots + 1);
	uint16_t *root = rsc->buffers + RS_DECODE_ROOT * (nroots + 1);
	uint16_t *reg = rsc->buffers + RS_DECODE_REG * (nroots + 1);
	uint16_t *loc = rsc->buffers + RS_DECODE_LOC * (nroots + 1);

	/* Check length parameter for validity */
	pad = nn - nroots - len;
	BUG_ON(pad < 0 || pad >= nn);

	/* Does the caller provide the syndrome ? */
	if (s != NULL) {
		for (i = 0; i < nroots; i++) {
			/* The syndrome is in index form,
			 * so nn represents zero
			 */
			if (s[i] != nn)
				goto decode;
		}

		/* syndrome is zero, no errors to correct  */
		return 0;
	}

	/* form the syndromes; i.e., evaluate data(x) at roots of
	 * g(x) */
	for (i = 0; i < nroots; i++)
		syn[i] = (((uint16_t) data[0]) ^ invmsk) & msk;

	for (j = 1; j < len; j++) {
		for (i = 0; i < nroots; i++) {
			if (syn[i] == 0) {
				syn[i] = (((uint16_t) data[j]) ^
					  invmsk) & msk;
			} else {
				syn[i] = ((((uint16_t) data[j]) ^
					   invmsk) & msk) ^
					alpha_to[rs_modnn(rs, index_of[syn[i]] +
						       (fcr + i) * prim)];
			}
		}
	}

	for (j = 0; j < nroots; j++) {
		for (i = 0; i < nroots; i++) {
			if (syn[i] == 0) {
				syn[i] = ((uint16_t) par[j]) & msk;
			} else {
				syn[i] = (((uint16_t) par[j]) & msk) ^
					alpha_to[rs_modnn(rs, index_of[syn[i]] +
						       (fcr+i)*prim)];
			}
		}
	}
	s = syn;

	/* Convert syndromes to index form, checking for nonzero condition */
	syn_error = 0;
	for (i = 0; i < nroots; i++) {
		syn_error |= s[i];
		s[i] = index_of[s[i]];
	}

	if (!syn_error) {
		/* if syndrome is zero, data[] is a codeword and there are no
		 * errors to correct. So return data[] unmodified
		 */
		count = 0;
		goto finish;
	}

 decode:
	memset(&lambda[1], 0, nroots * sizeof(lambda[0]));
	lambda[0] = 1;

	if (no_eras > 0) {
		/* Init lambda to be the erasure locator polynomial */
		lambda[1] = alpha_to[rs_modnn(rs,
					prim * (nn - 1 - (eras_pos[0] + pad)))];
		for (i = 1; i < no_eras; i++) {
			u = rs_modnn(rs, prim * (nn - 1 - (eras_pos[i] + pad)));
			for (j = i + 1; j > 0; j--) {
				tmp = index_of[lambda[j - 1]];
				if (tmp != nn) {
					lambda[j] ^=
						alpha_to[rs_modnn(rs, u + tmp)];
				}
			}
		}
	}

	for (i = 0; i < nroots + 1; i++)
		b[i] = index_of[lambda[i]];

	/*
	 * Begin Berlekamp-Massey algorithm to determine error+erasure
	 * locator polynomial
	 */
	r = no_eras;
	el = no_eras;
	while (++r <= nroots) {	/* r is the step number */
		/* Compute discrepancy at the r-th step in poly-form */
		discr_r = 0;
		for (i = 0; i < r; i++) {
			if ((lambda[i] != 0) && (s[r - i - 1] != nn)) {
				discr_r ^=
					alpha_to[rs_modnn(rs,
							  index_of[lambda[i]] +
							  s[r - i - 1])];
			}
		}
		discr_r = index_of[discr_r];	/* Index form */
		if (discr_r == nn) {
			/* 2 lines below: B(x) <-- x*B(x) */
			memmove (&b[1], b, nroots * sizeof (b[0]));
			b[0] = nn;
		} else {
			/* 7 lines below: T(x) <-- lambda(x)-discr_r*x*b(x) */
			t[0] = lambda[0];
			for (i = 0; i < nroots; i++) {
				if (b[i] != nn) {
					t[i + 1] = lambda[i + 1] ^
						alpha_to[rs_modnn(rs, discr_r +
								  b[i])];
				} else
					t[i + 1] = lambda[i + 1];
			}
			if (2 * el <= r + no_eras - 1) {
				el = r + no_eras - el;
				/*
				 * 2 lines below: B(x) <-- inv(discr_r) *
				 * lambda(x)
				 */
				for (i = 0; i <= nroots; i++) {
					b[i] = (lambda[i] == 0) ? nn :
						rs_modnn(rs, index_of[lambda[i]]
							 - discr_r + nn);
				}
			} else {
				/* 2 lines below: B(x) <-- x*B(x) */
				memmove(&b[1], b, nroots * sizeof(b[0]));
				b[0] = nn;
			}
			memcpy(lambda, t, (nroots + 1) * sizeof(t[0]));
		}
	}

	/* Convert lambda to index form and compute deg(lambda(x)) */
	deg_lambda = 0;
	for (i = 0; i < nroots + 1; i++) {
		lambda[i] = index_of[lambda[i]];
		if (lambda[i] != nn)
			deg_lambda = i;
	}
	/* Find roots of error+erasure locator polynomial by Chien search */
	memcpy(&reg[1], &lambda[1], nroots * sizeof(reg[0]));
	count = 0;		/* Number of roots of lambda(x) */
	for (i = 1, k = iprim - 1; i <= nn; i++, k = rs_modnn(rs, k + iprim)) {
		q = 1;		/* lambda[0] is always 0 */
		for (j = deg_lambda; j > 0; j--) {
			if (reg[j] != nn) {
				reg[j] = rs_modnn(rs, reg[j] + j);
				q ^= alpha_to[reg[j]];
			}
		}
		if (q != 0)
			continue;	/* Not a root */
		/* store root (index-form) and error location number */
		root[count] = i;
		loc[count] = k;
		/* If we've already found max possible roots,
		 * abort the search to save time
		 */
		if (++count == deg_lambda)
			break;
	}
	if (deg_lambda != count) {
		/*
		 * deg(lambda) unequal to number of roots => uncorrectable
		 * error detected
		 */
		count = -EBADMSG;
		goto finish;
	}
	/*
	 * Compute err+eras evaluator poly omega(x) = s(x)*lambda(x) (modulo
	 * x**nroots). in index form. Also find deg(omega).
	 */
	deg_omega = deg_lambda - 1;
	for (i = 0; i <= deg_omega; i++) {
		tmp = 0;
		for (j = i; j >= 0; j--) {
			if ((s[i - j] != nn) && (lambda[j] != nn))
				tmp ^=
				    alpha_to[rs_modnn(rs, s[i - j] + lambda[j])];
		}
		omega[i] = index_of[tmp];
	}

	/*
	 * Compute error values in poly-form. num1 = omega(inv(X(l))), num2 =
	 * inv(X(l))**(fcr-1) and den = lambda_pr(inv(X(l))) all in poly-form
	 */
	for (j = count - 1; j >= 0; j--) {
		num1 = 0;
		for (i = deg_omega; i >= 0; i--) {
			if (omega[i] != nn)
				num1 ^= alpha_to[rs_modnn(rs, omega[i] +
							i * root[j])];
		}
		num2 = alpha_to[rs_modnn(rs, root[j] * (fcr - 1) + nn)];
		den = 0;

		/* lambda[i+1] for i even is the formal derivative
		 * lambda_pr of lambda[i] */
		for (i = min(deg_lambda, nroots - 1) & ~1; i >= 0; i -= 2) {
			if (lambda[i + 1] != nn) {
				den ^= alpha_to[rs_modnn(rs, lambda[i + 1] +
						       i * root[j])];
			}
		}
		/* Apply error to data */
		if (num1 != 0 && loc[j] >= pad) {
			uint16_t cor = alpha_to[rs_modnn(rs,index_of[num1] +
						       index_of[num2] +
						       nn - index_of[den])];
			/* Store the error correction pattern, if a
			 * correction buffer is available */
			if (corr) {
				corr[j] = cor;
			} else {
				/* If a data buffer is given and the
				 * error is inside the message,
				 * correct it */
				if (data && (loc[j] < (nn - nroots)))
					data[loc[j] - pad] ^= cor;
			}
		}
	}

finish:
	if (eras_pos != NULL) {
		for (i = 0; i < count; i++)
			eras_pos[i] = loc[i] - pad;
	}
	return count;

}
