/* $FreeBSD$ */
/*
 * The big num stuff is a bit broken at the moment and I've not yet fixed it.
 * The symtom is that odd size big nums will fail.  Test code below (it only
 * uses modexp currently).
 * 
 * --Jason L. Wright
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <machine/endian.h>
#include <sys/time.h>
#include <crypto/cryptodev.h>
#include <openssl/bn.h>

#include <paths.h>
#include <fcntl.h>
#include <err.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

int	crid = CRYPTO_FLAG_HARDWARE;
int	verbose = 0;

static int
devcrypto(void)
{
	static int fd = -1;

	if (fd < 0) {
		fd = open(_PATH_DEV "crypto", O_RDWR, 0);
		if (fd < 0)
			err(1, _PATH_DEV "crypto");
		if (fcntl(fd, F_SETFD, 1) == -1)
			err(1, "fcntl(F_SETFD) (devcrypto)");
	}
	return fd;
}

static int
crlookup(const char *devname)
{
	struct crypt_find_op find;

	find.crid = -1;
	strlcpy(find.name, devname, sizeof(find.name));
	if (ioctl(devcrypto(), CIOCFINDDEV, &find) == -1)
		err(1, "ioctl(CIOCFINDDEV)");
	return find.crid;
}

static const char *
crfind(int crid)
{
	static struct crypt_find_op find;

	bzero(&find, sizeof(find));
	find.crid = crid;
	if (ioctl(devcrypto(), CIOCFINDDEV, &find) == -1)
		err(1, "ioctl(CIOCFINDDEV)");
	return find.name;
}

/*
 * Convert a little endian byte string in 'p' that
 * is 'plen' bytes long to a BIGNUM. If 'dst' is NULL,
 * a new BIGNUM is allocated.  Returns NULL on failure.
 *
 * XXX there has got to be a more efficient way to do
 * this, but I haven't figured out enough of the OpenSSL
 * magic.
 */
BIGNUM *
le_to_bignum(BIGNUM *dst, u_int8_t *p, int plen)
{
	u_int8_t *pd;
	int i;

	if (plen == 0)
		return (NULL);

	if ((pd = (u_int8_t *)malloc(plen)) == NULL)
		return (NULL);

	for (i = 0; i < plen; i++)
		pd[i] = p[plen - i - 1];

	dst = BN_bin2bn(pd, plen, dst);
	free(pd);
	return (dst);
}

/*
 * Convert a BIGNUM to a little endian byte string.
 * If 'rd' is NULL, allocate space for it, otherwise
 * 'rd' is assumed to have room for BN_num_bytes(n)
 * bytes.  Returns NULL on failure.
 */
u_int8_t *
bignum_to_le(BIGNUM *n, u_int8_t *rd)
{
	int i, j, k;
	int blen = BN_num_bytes(n);

	if (blen == 0)
		return (NULL);
	if (rd == NULL)
		rd = (u_int8_t *)malloc(blen);
	if (rd == NULL)
		return (NULL);

	for (i = 0, j = 0; i < n->top; i++) {
		for (k = 0; k < BN_BITS2 / 8; k++) {
			if ((j + k) >= blen)
				goto out;
			rd[j + k] = n->d[i] >> (k * 8);
		}
		j += BN_BITS2 / 8;
	}
out:
	return (rd);
}

int
UB_mod_exp(BIGNUM *res, BIGNUM *a, BIGNUM *b, BIGNUM *c, BN_CTX *ctx)
{
	struct crypt_kop kop;
	u_int8_t *ale, *ble, *cle;
	static int crypto_fd = -1;

	if (crypto_fd == -1 && ioctl(devcrypto(), CRIOGET, &crypto_fd) == -1)
		err(1, "CRIOGET");

	if ((ale = bignum_to_le(a, NULL)) == NULL)
		err(1, "bignum_to_le, a");
	if ((ble = bignum_to_le(b, NULL)) == NULL)
		err(1, "bignum_to_le, b");
	if ((cle = bignum_to_le(c, NULL)) == NULL)
		err(1, "bignum_to_le, c");

	bzero(&kop, sizeof(kop));
	kop.crk_op = CRK_MOD_EXP;
	kop.crk_iparams = 3;
	kop.crk_oparams = 1;
	kop.crk_crid = crid;
	kop.crk_param[0].crp_p = ale;
	kop.crk_param[0].crp_nbits = BN_num_bytes(a) * 8;
	kop.crk_param[1].crp_p = ble;
	kop.crk_param[1].crp_nbits = BN_num_bytes(b) * 8;
	kop.crk_param[2].crp_p = cle;
	kop.crk_param[2].crp_nbits = BN_num_bytes(c) * 8;
	kop.crk_param[3].crp_p = cle;
	kop.crk_param[3].crp_nbits = BN_num_bytes(c) * 8;

	if (ioctl(crypto_fd, CIOCKEY2, &kop) == -1)
		err(1, "CIOCKEY2");
	if (verbose)
		printf("device = %s\n", crfind(kop.crk_crid));

	bzero(ale, BN_num_bytes(a));
	free(ale);
	bzero(ble, BN_num_bytes(b));
	free(ble);

	if (kop.crk_status != 0) {
		printf("error %d\n", kop.crk_status);
		bzero(cle, BN_num_bytes(c));
		free(cle);
		return (-1);
	} else {
		res = le_to_bignum(res, cle, BN_num_bytes(c));
		bzero(cle, BN_num_bytes(c));
		free(cle);
		if (res == NULL)
			err(1, "le_to_bignum");
		return (0);
	}
	return (0);
}

void
show_result(a, b, c, sw, hw)
BIGNUM *a, *b, *c, *sw, *hw;
{
	printf("\n");

	printf("A = ");
	BN_print_fp(stdout, a);
	printf("\n");

	printf("B = ");
	BN_print_fp(stdout, b);
	printf("\n");

	printf("C = ");
	BN_print_fp(stdout, c);
	printf("\n");

	printf("sw= ");
	BN_print_fp(stdout, sw);
	printf("\n");

	printf("hw= ");
	BN_print_fp(stdout, hw);
	printf("\n");

	printf("\n");
}

void
testit(void)
{
	BIGNUM *a, *b, *c, *r1, *r2;
	BN_CTX *ctx;

	ctx = BN_CTX_new();

	a = BN_new();
	b = BN_new();
	c = BN_new();
	r1 = BN_new();
	r2 = BN_new();

	BN_pseudo_rand(a, 1023, 0, 0);
	BN_pseudo_rand(b, 1023, 0, 0);
	BN_pseudo_rand(c, 1024, 0, 0);

	if (BN_cmp(a, c) > 0) {
		BIGNUM *rem = BN_new();

		BN_mod(rem, a, c, ctx);
		UB_mod_exp(r2, rem, b, c, ctx);
		BN_free(rem);
	} else {
		UB_mod_exp(r2, a, b, c, ctx);
	}
	BN_mod_exp(r1, a, b, c, ctx);

	if (BN_cmp(r1, r2) != 0) {
		show_result(a, b, c, r1, r2);
	}

	BN_free(r2);
	BN_free(r1);
	BN_free(c);
	BN_free(b);
	BN_free(a);
	BN_CTX_free(ctx);
}

static void
usage(const char* cmd)
{
	printf("usage: %s [-d dev] [-v] [count]\n", cmd);
	printf("count is the number of bignum ops to do\n");
	printf("\n");
	printf("-d use specific device\n");
	printf("-v be verbose\n");
	exit(-1);
}

int
main(int argc, char *argv[])
{
	int c, i;

	while ((c = getopt(argc, argv, "d:v")) != -1) {
		switch (c) {
		case 'd':
			crid = crlookup(optarg);
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage(argv[0]);
		}
	}
	argc -= optind, argv += optind;

	for (i = 0; i < 1000; i++) {
		fprintf(stderr, "test %d\n", i);
		testit();
	}
	return (0);
}
