/*	$OpenBSD: bn_mod_inverse.c,v 1.2 2023/06/04 07:14:47 tb Exp $ */

/*
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
#include <stdio.h>

#include <openssl/bn.h>

BIGNUM *BN_mod_inverse(BIGNUM *, const BIGNUM *, const BIGNUM *, BN_CTX *);

static const struct mod_inv_test {
	const char *i;
	const char *a;
	const char *m;
} mod_inv_tests[] = {
	{
		.i = "0",
		.a = "0",
		.m = "1",
	},
	{
		.i = "0",
		.a = "1",
		.m = "1",
	},
	{
		.i = "0",
		.a = "2",
		.m = "1",
	},
	{
		.i = "0",
		.a = "3",
		.m = "1",
	},
	{
		.i = "64",
		.a = "54",
		.m = "e3",
	},
	{
		.i = "13",
		.a = "2b",
		.m = "30",
	},
	{
		.i = "2f",
		.a = "30",
		.m = "37",
	},
	{
		.i = "4",
		.a = "13",
		.m = "4b",
	},
	{
		.i = "1c47",
		.a = "cd4",
		.m = "6a21",
	},
	{
		.i = "2b97",
		.a = "8e7",
		.m = "49c0",
	},
	{
		.i = "29b9",
		.a = "fcb",
		.m = "3092",
	},
	{
		.i = "a83",
		.a = "14bf",
		.m = "41ae",
	},
	{
		.i = "18f15fe1",
		.a = "11b5d53e",
		.m = "322e92a1",
	},
	{
		.i = "32f9453b",
		.a = "8af6df6",
		.m = "33d45eb7",
	},
	{
		.i = "d696369",
		.a = "c5f89dd5",
		.m = "fc09c17c",
	},
	{
		.i = "622839d8",
		.a = "60c2526",
		.m = "74200493",
	},
	{
		.i = "fb5a8aee7bbc4ef",
		.a = "24ebd835a70be4e2",
		.m = "9c7256574e0c5e93",
	},
	{
		.i = "846bc225402419c",
		.a = "23026003ab1fbdb",
		.m = "1683cbe32779c59b",
	},
	{
		.i = "5ff84f63a78982f9",
		.a = "4a2420dc733e1a0f",
		.m = "a73c6bfabefa09e6",
	},
	{
		.i = "133e74d28ef42b43",
		.a = "2e9511ae29cdd41",
		.m = "15234df99f19fcda",
	},
	{
		.i = "46ae1fabe9521e4b99b198fc84396090"
		     "23aa69be2247c0d1e27c2a0ea332f9c5",
		.a = "6331fec5f01014046788c919ed50dc86"
		     "ac7a80c085f1b6f645dd179c0f0dc9cd",
		.m = "8ef409de82318259a8655a39293b1e76"
		     "2fa2cc7e0aeb4c59713a1e1fff6af640",
	},
	{
		.i = "444ccea3a7b21677dd294d34de53cc8a"
		     "5b51e69b37782310a00fc6bcc975709b",
		.a = "679280bd880994c08322143a4ea8a082"
		     "5d0466fda1bb6b3eb86fc8e90747512b",
		.m = "e4fecab84b365c63a0dab4244ce3f921"
		     "a9c87ec64d69a2031939f55782e99a2e",
	},
	{
		.i = "1ac7d7a03ceec5f690f567c9d61bf346"
		     "9c078285bcc5cf00ac944596e887ca17",
		.a = "1593ef32d9c784f5091bdff952f5c5f5"
		     "92a3aed6ba8ea865efa6d7df87be1805",
		.m = "1e276882f90c95e0c1976eb079f97af0"
		     "75445b1361c02018d6bd7191162e67b2",
	},
	{
		.i = "639108b90dfe946f498be21303058413"
		     "bbb0e59d0bd6a6115788705abd0666d6",
		.a = "9258d6238e4923d120b2d1033573ffca"
		     "c691526ad0842a3b174dccdbb79887bd",
		.m = "ce62909c39371d463aaba3d4b72ea6da"
		     "49cb9b529e39e1972ef3ccd9a66fe08f",
	},
	{
		.i = "aebde7654cb17833a106231c4b9e2f51"
		     "9140e85faee1bfb4192830f03f385e77"
		     "3c0f4767e93e874ffdc3b7a6b7e6a710"
		     "e5619901c739ee8760a26128e8c91ef8"
		     "cf761d0e505d8b28ae078d17e6071c37"
		     "2893bb7b72538e518ebc57efa70b7615"
		     "e406756c49729b7c6e74f84aed7a316b"
		     "6fa748ff4b9f143129d29dad1bff98bb",
		.a = "a29dacaf5487d354280fdd2745b9ace4"
		     "cd50f2bde41d0ee529bf26a1913244f7"
		     "08085452ff32feab19a7418897990da4"
		     "6a0633f7c8375d583367319091bbbe06"
		     "9b0052c5e48a7daac9fb650db5af768c"
		     "d2508ec3e2cda7456d4b9ce1c3945962"
		     "7a8b77e038b826cd7e326d0685b0cd0c"
		     "b50f026f18300dae9f5fd42aa150ee8b",
		.m = "d686f9b86697313251685e995c09b9f1"
		     "e337ddfaa050bd2df15bf4ca1dc46c55"
		     "65021314765299c434ea1a6ec42bf92a"
		     "29a7d1ffff599f4e50b79a82243fb248"
		     "13060580c770d4c1140aeb2ab2685007"
		     "e948b6f1f62e8001a0545619477d4981"
		     "32c907774479f6d95899e6251e7136f7"
		     "9ab6d3b7c82e4aca421e7d22fe7db19c",
	},
	{
		.i = "1ec872f4f20439e203597ca4de9d1296"
		     "743f95781b2fe85d5def808558bbadef"
		     "02a46b8955f47c83e1625f8bb40228ea"
		     "b09cad2a35c9ad62ab77a30e39328729"
		     "59c5898674162da244a0ec1f68c0ed89"
		     "f4b0f3572bfdc658ad15bf1b1c6e1176"
		     "b0784c9935bd3ff1f49bb43753eacee1"
		     "d8ca1c0b652d39ec727da83984fe3a0f",
		.a = "2e527b0a1dc32460b2dd94ec446c6929"
		     "89f7b3c7451a5cbeebf69fc0ea9c4871"
		     "fbe78682d5dc5b66689f7ed889b52161"
		     "cd9830b589a93d21ab26dbede6c33959"
		     "f5a0f0d107169e2daaac78bac8cf2d41"
		     "a1eb1369cb6dc9e865e73bb2e51b886f"
		     "4e896082db199175e3dde0c4ed826468"
		     "f238a77bd894245d0918efc9ca84f945",
		.m = "b13133a9ebe0645f987d170c077eea2a"
		     "a44e85c9ab10386d02867419a590cb18"
		     "2d9826a882306c212dbe75225adde23f"
		     "80f5b37ca75ed09df20fc277cc7fbbfa"
		     "c8d9ef37a50f6b68ea158f5447283618"
		     "e64e1426406d26ea85232afb22bf546c"
		     "75018c1c55cb84c374d58d9d44c0a13b"
		     "a88ac2e387765cb4c3269e3a983250fa",
	},
	{
		.i = "30ffa1876313a69de1e4e6ee132ea1d3"
		     "a3da32f3b56f5cfb11402b0ad517dce6"
		     "05cf8e91d69fa375dd887fa8507bd8a2"
		     "8b2d5ce745799126e86f416047709f93"
		     "f07fbd88918a047f13100ea71b1d48f6"
		     "fc6d12e5c917646df3041b302187af64"
		     "1eaedf4908abc36f12c204e1526a7d80"
		     "e96e302fb0779c28d7da607243732f26",
		.a = "31157208bde6b85ebecaa63735947b3b"
		     "36fa351b5c47e9e1c40c947339b78bf9"
		     "6066e5dbe21bb42629e6fcdb81f5f88d"
		     "b590bfdd5f4c0a6a0c3fc6377e5c1fd8"
		     "235e46e291c688b6d6ecfb36604891c2"
		     "a7c9cbcc58c26e44b43beecb9c5044b5"
		     "8bb58e35de3cf1128f3c116534fe4e42"
		     "1a33f83603c3df1ae36ec88092f67f2a",
		.m = "53408b23d6cb733e6c9bc3d1e2ea2286"
		     "a5c83cc4e3e7470f8af3a1d9f28727f5"
		     "b1f8ae348c1678f5d1105dc3edf2de64"
		     "e65b9c99545c47e64b770b17c8b4ef5c"
		     "f194b43a0538053e87a6b95ade1439ce"
		     "bf3d34c6aa72a11c1497f58f76011e16"
		     "c5be087936d88aba7a740113120e939e"
		     "27bd3ddcb6580c2841aa406566e33c35",
	},
	{
		.i = "87355002f305c81ba0dc97ca2234a2bc"
		     "02528cefde38b94ac5bd95efc7bf4c14"
		     "0899107fff47f0df9e3c6aa70017ebc9"
		     "0610a750f112cd4f475b9c76b204a953"
		     "444b4e7196ccf17e93fdaed160b7345c"
		     "a9b397eddf9446e8ea8ee3676102ce70"
		     "eaafbe9038a34639789e6f2f1e3f3526"
		     "38f2e8a8f5fc56aaea7ec705ee068dd5",
		.a = "42a25d0bc96f71750f5ac8a51a1605a4"
		     "1b506cca51c9a7ecf80cad713e56f70f"
		     "1b4b6fa51cbb101f55fd74f318adefb3"
		     "af04e0c8a7e281055d5a40dd40913c0e"
		     "1211767c5be915972c73886106dc4932"
		     "5df6c2df49e9eea4536f0343a8e7d332"
		     "c6159e4f5bdb20d89f90e67597c4a2a6"
		     "32c31b2ef2534080a9ac61f52303990d",
		.m = "d3d3f95d50570351528a76ab1e806bae"
		     "1968bd420899bdb3d87c823fac439a43"
		     "54c31f6c888c939784f18fe10a95e6d2"
		     "03b1901caa18937ba6f8be033af10c35"
		     "fc869cf3d16bef479f280f53b3499e64"
		     "5d0387554623207ca4989e5de00bfeaa"
		     "5e9ab56474fc60dd4967b100e0832eaa"
		     "f2fcb2ef82a181567057b880b3afef62",
	},
	{
		.i = "9b8c28a4",
		.a = "135935f57",
		.m = "c24242ff",
	},
};

#define N_MOD_INV_TESTS (sizeof(mod_inv_tests) / sizeof(mod_inv_tests[0]))

static int
bn_mod_inverse_test(const struct mod_inv_test *test, BN_CTX *ctx, int flags)
{
	BIGNUM *i, *a, *m, *inv, *elt, *mod;
	int failed_step;
	int failed = 0;

	BN_CTX_start(ctx);

	if ((i = BN_CTX_get(ctx)) == NULL)
		errx(1, "i = BN_CTX_get()");
	if ((a = BN_CTX_get(ctx)) == NULL)
		errx(1, "a = BN_CTX_get()");
	if ((m = BN_CTX_get(ctx)) == NULL)
		errx(1, "m = BN_CTX_get()");
	if ((inv = BN_CTX_get(ctx)) == NULL)
		errx(1, "inv = BN_CTX_get()");
	if ((elt = BN_CTX_get(ctx)) == NULL)
		errx(1, "elt = BN_CTX_get()");
	if ((mod = BN_CTX_get(ctx)) == NULL)
		errx(1, "mod = BN_CTX_get()");

	BN_set_flags(i, flags);
	BN_set_flags(a, flags);
	BN_set_flags(m, flags);
	BN_set_flags(inv, flags);
	BN_set_flags(elt, flags);
	BN_set_flags(mod, flags);

	if (BN_hex2bn(&i, test->i) == 0)
		errx(1, "BN_hex2bn(%s)", test->i);
	if (BN_hex2bn(&a, test->a) == 0)
		errx(1, "BN_hex2bn(%s)", test->a);
	if (BN_hex2bn(&m, test->m) == 0)
		errx(1, "BN_hex2bn(%s)", test->m);

	if (BN_copy(elt, a) == NULL)
		errx(1, "BN_copy(elt, a)");
	if (BN_copy(mod, m) == NULL)
		errx(1, "BN_copy(mod, m)");

	if (BN_mod_inverse(inv, elt, mod, ctx) == NULL)
		errx(1, "BN_mod_inverse(inv, elt, mod)");

	failed_step = BN_cmp(i, inv) != 0;
	if (failed_step)
		fprintf(stderr, "FAIL (simple), %x:\ni: %s\na: %s\nm: %s\n",
		    flags, test->i, test->a, test->m);
	failed |= failed_step;

	if (BN_copy(elt, a) == NULL)
		errx(1, "BN_copy(elt, a)");
	if (BN_copy(inv, m) == NULL)
		errx(1, "BN_copy(inv, m)");

	if (BN_mod_inverse(inv, elt, inv, ctx) == NULL)
		errx(1, "BN_mod_inverse(inv, elt, inv)");
	failed_step = BN_cmp(i, inv) != 0;
	if (failed_step)
		fprintf(stderr, "FAIL (inv == mod), %x:\ni: %s\na: %s\nm: %s\n",
		    flags, test->i, test->a, test->m);
	failed |= failed_step;

	if (BN_copy(inv, a) == NULL)
		errx(1, "BN_copy(elt, a)");
	if (BN_copy(mod, m) == NULL)
		errx(1, "BN_copy(inv, m)");

	if (BN_mod_inverse(inv, inv, mod, ctx) == NULL)
		errx(1, "BN_mod_inverse(inv, inv, mod)");
	failed_step = BN_cmp(i, inv) != 0;
	if (failed_step)
		fprintf(stderr, "FAIL (inv == elt), %x:\ni: %s\na: %s\nm: %s\n",
		    flags, test->i, test->a, test->m);
	failed |= failed_step;

	BN_CTX_end(ctx);

	return failed;
}

static int
test_bn_mod_inverse(void)
{
	BN_CTX *ctx;
	size_t i;
	int failed = 0;

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "BN_CTX_new");

	for (i = 0; i < N_MOD_INV_TESTS; i++)
		failed |= bn_mod_inverse_test(&mod_inv_tests[i], ctx, 0);

	for (i = 0; i < N_MOD_INV_TESTS; i++)
		failed |= bn_mod_inverse_test(&mod_inv_tests[i], ctx,
		    BN_FLG_CONSTTIME);

	BN_CTX_free(ctx);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= test_bn_mod_inverse();

	return failed;
}
