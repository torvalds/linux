/*
 * Derived from:
 *
 * MDDRIVER.C - test driver for MD2, MD4 and MD5
 */

/*
 *  Copyright (C) 1990-2, RSA Data Security, Inc. Created 1990. All
 *  rights reserved.
 *
 *  RSA Data Security, Inc. makes no representations concerning either
 *  the merchantability of this software or the suitability of this
 *  software for any particular purpose. It is provided "as is"
 *  without express or implied warranty of any kind.
 *
 *  These notices must be retained in any copies of any part of this
 *  documentation and/or software.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <err.h>
#include <fcntl.h>
#include <md5.h>
#include <ripemd.h>
#include <sha.h>
#include <sha224.h>
#include <sha256.h>
#include <sha384.h>
#include <sha512.h>
#include <sha512t.h>
#include <skein.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_CAPSICUM
#include <sys/capsicum.h>
#include <capsicum_helpers.h>
#endif

/*
 * Length of test block, number of test blocks.
 */
#define TEST_BLOCK_LEN 10000
#define TEST_BLOCK_COUNT 100000
#define MDTESTCOUNT 8

static int qflag;
static int rflag;
static int sflag;
static char* checkAgainst;
static int checksFailed;

typedef void (DIGEST_Init)(void *);
typedef void (DIGEST_Update)(void *, const unsigned char *, size_t);
typedef char *(DIGEST_End)(void *, char *);

extern const char *MD5TestOutput[MDTESTCOUNT];
extern const char *SHA1_TestOutput[MDTESTCOUNT];
extern const char *SHA224_TestOutput[MDTESTCOUNT];
extern const char *SHA256_TestOutput[MDTESTCOUNT];
extern const char *SHA384_TestOutput[MDTESTCOUNT];
extern const char *SHA512_TestOutput[MDTESTCOUNT];
extern const char *SHA512t256_TestOutput[MDTESTCOUNT];
extern const char *RIPEMD160_TestOutput[MDTESTCOUNT];
extern const char *SKEIN256_TestOutput[MDTESTCOUNT];
extern const char *SKEIN512_TestOutput[MDTESTCOUNT];
extern const char *SKEIN1024_TestOutput[MDTESTCOUNT];

typedef struct Algorithm_t {
	const char *progname;
	const char *name;
	const char *(*TestOutput)[MDTESTCOUNT];
	DIGEST_Init *Init;
	DIGEST_Update *Update;
	DIGEST_End *End;
	char *(*Data)(const void *, unsigned int, char *);
	char *(*Fd)(int, char *);
} Algorithm_t;

static void MD5_Update(MD5_CTX *, const unsigned char *, size_t);
static void MDString(const Algorithm_t *, const char *);
static void MDTimeTrial(const Algorithm_t *);
static void MDTestSuite(const Algorithm_t *);
static void MDFilter(const Algorithm_t *, int);
static void usage(const Algorithm_t *);

typedef union {
	MD5_CTX md5;
	SHA1_CTX sha1;
	SHA224_CTX sha224;
	SHA256_CTX sha256;
	SHA384_CTX sha384;
	SHA512_CTX sha512;
	RIPEMD160_CTX ripemd160;
	SKEIN256_CTX skein256;
	SKEIN512_CTX skein512;
	SKEIN1024_CTX skein1024;
} DIGEST_CTX;

/* max(MD5_DIGEST_LENGTH, SHA_DIGEST_LENGTH,
	SHA256_DIGEST_LENGTH, SHA512_DIGEST_LENGTH,
	RIPEMD160_DIGEST_LENGTH, SKEIN1024_DIGEST_LENGTH)*2+1 */
#define HEX_DIGEST_LENGTH 257

/* algorithm function table */

static const struct Algorithm_t Algorithm[] = {
	{ "md5", "MD5", &MD5TestOutput, (DIGEST_Init*)&MD5Init,
		(DIGEST_Update*)&MD5_Update, (DIGEST_End*)&MD5End,
		&MD5Data, &MD5Fd },
	{ "sha1", "SHA1", &SHA1_TestOutput, (DIGEST_Init*)&SHA1_Init,
		(DIGEST_Update*)&SHA1_Update, (DIGEST_End*)&SHA1_End,
		&SHA1_Data, &SHA1_Fd },
	{ "sha224", "SHA224", &SHA224_TestOutput, (DIGEST_Init*)&SHA224_Init,
		(DIGEST_Update*)&SHA224_Update, (DIGEST_End*)&SHA224_End,
		&SHA224_Data, &SHA224_Fd },
	{ "sha256", "SHA256", &SHA256_TestOutput, (DIGEST_Init*)&SHA256_Init,
		(DIGEST_Update*)&SHA256_Update, (DIGEST_End*)&SHA256_End,
		&SHA256_Data, &SHA256_Fd },
	{ "sha384", "SHA384", &SHA384_TestOutput, (DIGEST_Init*)&SHA384_Init,
		(DIGEST_Update*)&SHA384_Update, (DIGEST_End*)&SHA384_End,
		&SHA384_Data, &SHA384_Fd },
	{ "sha512", "SHA512", &SHA512_TestOutput, (DIGEST_Init*)&SHA512_Init,
		(DIGEST_Update*)&SHA512_Update, (DIGEST_End*)&SHA512_End,
		&SHA512_Data, &SHA512_Fd },
	{ "sha512t256", "SHA512t256", &SHA512t256_TestOutput, (DIGEST_Init*)&SHA512_256_Init,
		(DIGEST_Update*)&SHA512_256_Update, (DIGEST_End*)&SHA512_256_End,
		&SHA512_256_Data, &SHA512_256_Fd },
	{ "rmd160", "RMD160", &RIPEMD160_TestOutput,
		(DIGEST_Init*)&RIPEMD160_Init, (DIGEST_Update*)&RIPEMD160_Update,
		(DIGEST_End*)&RIPEMD160_End, &RIPEMD160_Data, &RIPEMD160_Fd },
	{ "skein256", "Skein256", &SKEIN256_TestOutput,
		(DIGEST_Init*)&SKEIN256_Init, (DIGEST_Update*)&SKEIN256_Update,
		(DIGEST_End*)&SKEIN256_End, &SKEIN256_Data, &SKEIN256_Fd },
	{ "skein512", "Skein512", &SKEIN512_TestOutput,
		(DIGEST_Init*)&SKEIN512_Init, (DIGEST_Update*)&SKEIN512_Update,
		(DIGEST_End*)&SKEIN512_End, &SKEIN512_Data, &SKEIN512_Fd },
	{ "skein1024", "Skein1024", &SKEIN1024_TestOutput,
		(DIGEST_Init*)&SKEIN1024_Init, (DIGEST_Update*)&SKEIN1024_Update,
		(DIGEST_End*)&SKEIN1024_End, &SKEIN1024_Data, &SKEIN1024_Fd }
};

static void
MD5_Update(MD5_CTX *c, const unsigned char *data, size_t len)
{
	MD5Update(c, data, len);
}

/* Main driver.

Arguments (may be any combination):
  -sstring - digests string
  -t       - runs time trial
  -x       - runs test script
  filename - digests file
  (none)   - digests standard input
 */
int
main(int argc, char *argv[])
{
#ifdef HAVE_CAPSICUM
	cap_rights_t	rights;
#endif
	int	ch, fd;
	char   *p;
	char	buf[HEX_DIGEST_LENGTH];
	int	failed;
 	unsigned	digest;
 	const char*	progname;

 	if ((progname = strrchr(argv[0], '/')) == NULL)
 		progname = argv[0];
 	else
 		progname++;

 	for (digest = 0; digest < sizeof(Algorithm)/sizeof(*Algorithm); digest++)
 		if (strcasecmp(Algorithm[digest].progname, progname) == 0)
 			break;

 	if (digest == sizeof(Algorithm)/sizeof(*Algorithm))
 		digest = 0;

	failed = 0;
	checkAgainst = NULL;
	checksFailed = 0;
	while ((ch = getopt(argc, argv, "c:pqrs:tx")) != -1)
		switch (ch) {
		case 'c':
			checkAgainst = optarg;
			break;
		case 'p':
			MDFilter(&Algorithm[digest], 1);
			break;
		case 'q':
			qflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 's':
			sflag = 1;
			MDString(&Algorithm[digest], optarg);
			break;
		case 't':
			MDTimeTrial(&Algorithm[digest]);
			break;
		case 'x':
			MDTestSuite(&Algorithm[digest]);
			break;
		default:
			usage(&Algorithm[digest]);
		}
	argc -= optind;
	argv += optind;

#ifdef HAVE_CAPSICUM
	if (caph_limit_stdout() < 0 || caph_limit_stderr() < 0)
		err(1, "unable to limit rights for stdio");
#endif

	if (*argv) {
		do {
			if ((fd = open(*argv, O_RDONLY)) < 0) {
				warn("%s", *argv);
				failed++;
				continue;
			}
			/*
			 * XXX Enter capability mode on the last argv file.
			 * When a casper file service or other approach is
			 * available, switch to that and enter capability mode
			 * earlier.
			 */
			if (*(argv + 1) == NULL) {
#ifdef HAVE_CAPSICUM
				cap_rights_init(&rights, CAP_READ);
				if (caph_rights_limit(fd, &rights) < 0 ||
				    caph_enter() < 0)
					err(1, "capsicum");
#endif
			}
			if ((p = Algorithm[digest].Fd(fd, buf)) == NULL) {
				warn("%s", *argv);
				failed++;
			} else {
				if (qflag)
					printf("%s", p);
				else if (rflag)
					printf("%s %s", p, *argv);
				else
					printf("%s (%s) = %s",
					    Algorithm[digest].name, *argv, p);
				if (checkAgainst && strcasecmp(checkAgainst, p) != 0)
				{
					checksFailed++;
					if (!qflag)
						printf(" [ Failed ]");
				}
				printf("\n");
			}
		} while (*++argv);
	} else if (!sflag && (optind == 1 || qflag || rflag)) {
#ifdef HAVE_CAPSICUM
		if (caph_limit_stdin() < 0 || caph_enter() < 0)
			err(1, "capsicum");
#endif
		MDFilter(&Algorithm[digest], 0);
	}

	if (failed != 0)
		return (1);
	if (checksFailed != 0)
		return (2);

	return (0);
}
/*
 * Digests a string and prints the result.
 */
static void
MDString(const Algorithm_t *alg, const char *string)
{
	size_t len = strlen(string);
	char buf[HEX_DIGEST_LENGTH];

	alg->Data(string,len,buf);
	if (qflag)
		printf("%s", buf);
	else if (rflag)
		printf("%s \"%s\"", buf, string);
	else
		printf("%s (\"%s\") = %s", alg->name, string, buf);
	if (checkAgainst && strcasecmp(buf,checkAgainst) != 0)
	{
		checksFailed++;
		if (!qflag)
			printf(" [ failed ]");
	}
	printf("\n");
}
/*
 * Measures the time to digest TEST_BLOCK_COUNT TEST_BLOCK_LEN-byte blocks.
 */
static void
MDTimeTrial(const Algorithm_t *alg)
{
	DIGEST_CTX context;
	struct rusage before, after;
	struct timeval total;
	float seconds;
	unsigned char block[TEST_BLOCK_LEN];
	unsigned int i;
	char *p, buf[HEX_DIGEST_LENGTH];

	printf("%s time trial. Digesting %d %d-byte blocks ...",
	    alg->name, TEST_BLOCK_COUNT, TEST_BLOCK_LEN);
	fflush(stdout);

	/* Initialize block */
	for (i = 0; i < TEST_BLOCK_LEN; i++)
		block[i] = (unsigned char) (i & 0xff);

	/* Start timer */
	getrusage(RUSAGE_SELF, &before);

	/* Digest blocks */
	alg->Init(&context);
	for (i = 0; i < TEST_BLOCK_COUNT; i++)
		alg->Update(&context, block, TEST_BLOCK_LEN);
	p = alg->End(&context, buf);

	/* Stop timer */
	getrusage(RUSAGE_SELF, &after);
	timersub(&after.ru_utime, &before.ru_utime, &total);
	seconds = total.tv_sec + (float) total.tv_usec / 1000000;

	printf(" done\n");
	printf("Digest = %s", p);
	printf("\nTime = %f seconds\n", seconds);
	printf("Speed = %f MiB/second\n", (float) TEST_BLOCK_LEN * 
		(float) TEST_BLOCK_COUNT / seconds / (1 << 20));
}
/*
 * Digests a reference suite of strings and prints the results.
 */

static const char *MDTestInput[MDTESTCOUNT] = {
	"",
	"a",
	"abc",
	"message digest",
	"abcdefghijklmnopqrstuvwxyz",
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
	"12345678901234567890123456789012345678901234567890123456789012345678901234567890",
	"MD5 has not yet (2001-09-03) been broken, but sufficient attacks have been made \
that its security is in some doubt"
};

const char *MD5TestOutput[MDTESTCOUNT] = {
	"d41d8cd98f00b204e9800998ecf8427e",
	"0cc175b9c0f1b6a831c399e269772661",
	"900150983cd24fb0d6963f7d28e17f72",
	"f96b697d7cb7938d525a2f31aaf161d0",
	"c3fcd3d76192e4007dfb496cca67e13b",
	"d174ab98d277d9f5a5611c2c9f419d9f",
	"57edf4a22be3c955ac49da2e2107b67a",
	"b50663f41d44d92171cb9976bc118538"
};

const char *SHA1_TestOutput[MDTESTCOUNT] = {
	"da39a3ee5e6b4b0d3255bfef95601890afd80709",
	"86f7e437faa5a7fce15d1ddcb9eaeaea377667b8",
	"a9993e364706816aba3e25717850c26c9cd0d89d",
	"c12252ceda8be8994d5fa0290a47231c1d16aae3",
	"32d10c7b8cf96570ca04ce37f2a19d84240d3a89",
	"761c457bf73b14d27e9e9265c46f4b4dda11f940",
	"50abf5706a150990a08b2c5ea40fa0e585554732",
	"18eca4333979c4181199b7b4fab8786d16cf2846"
};

const char *SHA224_TestOutput[MDTESTCOUNT] = {
	"d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f",
	"abd37534c7d9a2efb9465de931cd7055ffdb8879563ae98078d6d6d5",
	"23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7",
	"2cb21c83ae2f004de7e81c3c7019cbcb65b71ab656b22d6d0c39b8eb",
	"45a5f72c39c5cff2522eb3429799e49e5f44b356ef926bcf390dccc2",
	"bff72b4fcb7d75e5632900ac5f90d219e05e97a7bde72e740db393d9",
	"b50aecbe4e9bb0b57bc5f3ae760a8e01db24f203fb3cdcd13148046e",
	"5ae55f3779c8a1204210d7ed7689f661fbe140f96f272ab79e19d470"
};

const char *SHA256_TestOutput[MDTESTCOUNT] = {
	"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
	"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb",
	"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
	"f7846f55cf23e14eebeab5b4e1550cad5b509e3348fbc4efa3a1413d393cb650",
	"71c480df93d6ae2f1efad1447c66c9525e316218cf51fc8d9ed832f2daf18b73",
	"db4bfcbd4da0cd85a60c3c37d3fbd8805c77f15fc6b1fdfe614ee0a7c8fdb4c0",
	"f371bc4a311f2b009eef952dd83ca80e2b60026c8e935592d0f9c308453c813e",
	"e6eae09f10ad4122a0e2a4075761d185a272ebd9f5aa489e998ff2f09cbfdd9f"
};

const char *SHA384_TestOutput[MDTESTCOUNT] = {
	"38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b",
	"54a59b9f22b0b80880d8427e548b7c23abd873486e1f035dce9cd697e85175033caa88e6d57bc35efae0b5afd3145f31",
	"cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7",
	"473ed35167ec1f5d8e550368a3db39be54639f828868e9454c239fc8b52e3c61dbd0d8b4de1390c256dcbb5d5fd99cd5",
	"feb67349df3db6f5924815d6c3dc133f091809213731fe5c7b5f4999e463479ff2877f5f2936fa63bb43784b12f3ebb4",
	"1761336e3f7cbfe51deb137f026f89e01a448e3b1fafa64039c1464ee8732f11a5341a6f41e0c202294736ed64db1a84",
	"b12932b0627d1c060942f5447764155655bd4da0c9afa6dd9b9ef53129af1b8fb0195996d2de9ca0df9d821ffee67026",
	"99428d401bf4abcd4ee0695248c9858b7503853acfae21a9cffa7855f46d1395ef38596fcd06d5a8c32d41a839cc5dfb"
};

const char *SHA512_TestOutput[MDTESTCOUNT] = {
	"cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e",
	"1f40fc92da241694750979ee6cf582f2d5d7d28e18335de05abc54d0560e0f5302860c652bf08d560252aa5e74210546f369fbbbce8c12cfc7957b2652fe9a75",
	"ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
	"107dbf389d9e9f71a3a95f6c055b9251bc5268c2be16d6c13492ea45b0199f3309e16455ab1e96118e8a905d5597b72038ddb372a89826046de66687bb420e7c",
	"4dbff86cc2ca1bae1e16468a05cb9881c97f1753bce3619034898faa1aabe429955a1bf8ec483d7421fe3c1646613a59ed5441fb0f321389f77f48a879c7b1f1",
	"1e07be23c26a86ea37ea810c8ec7809352515a970e9253c26f536cfc7a9996c45c8370583e0a78fa4a90041d71a4ceab7423f19c71b9d5a3e01249f0bebd5894",
	"72ec1ef1124a45b047e8b7c75a932195135bb61de24ec0d1914042246e0aec3a2354e093d76f3048b456764346900cb130d2a4fd5dd16abb5e30bcb850dee843",
	"e8a835195e039708b13d9131e025f4441dbdc521ce625f245a436dcd762f54bf5cb298d96235e6c6a304e087ec8189b9512cbdf6427737ea82793460c367b9c3"
};

const char *SHA512t256_TestOutput[MDTESTCOUNT] = {
	"c672b8d1ef56ed28ab87c3622c5114069bdd3ad7b8f9737498d0c01ecef0967a",
	"455e518824bc0601f9fb858ff5c37d417d67c2f8e0df2babe4808858aea830f8",
	"53048e2681941ef99b2e29b76b4c7dabe4c2d0c634fc6d46e0e2f13107e7af23",
	"0cf471fd17ed69d990daf3433c89b16d63dec1bb9cb42a6094604ee5d7b4e9fb",
	"fc3189443f9c268f626aea08a756abe7b726b05f701cb08222312ccfd6710a26",
	"cdf1cc0effe26ecc0c13758f7b4a48e000615df241284185c39eb05d355bb9c8",
	"2c9fdbc0c90bdd87612ee8455474f9044850241dc105b1e8b94b8ddf5fac9148",
	"dd095fc859b336c30a52548b3dc59fcc0d1be8616ebcf3368fad23107db2d736"
};

const char *RIPEMD160_TestOutput[MDTESTCOUNT] = {
	"9c1185a5c5e9fc54612808977ee8f548b2258d31",
	"0bdc9d2d256b3ee9daae347be6f4dc835a467ffe",
	"8eb208f7e05d987a9b044a8e98c6b087f15a0bfc",
	"5d0689ef49d2fae572b881b123a85ffa21595f36",
	"f71c27109c692c1b56bbdceb5b9d2865b3708dbc",
	"b0e20b6e3116640286ed3a87a5713079b21f5189",
	"9b752e45573d4b39f4dbd3323cab82bf63326bfb",
	"5feb69c6bf7c29d95715ad55f57d8ac5b2b7dd32"
};

const char *SKEIN256_TestOutput[MDTESTCOUNT] = {
	"c8877087da56e072870daa843f176e9453115929094c3a40c463a196c29bf7ba",
	"7fba44ff1a31d71a0c1f82e6e82fb5e9ac6c92a39c9185b9951fed82d82fe635",
	"258bdec343b9fde1639221a5ae0144a96e552e5288753c5fec76c05fc2fc1870",
	"4d2ce0062b5eb3a4db95bc1117dd8aa014f6cd50fdc8e64f31f7d41f9231e488",
	"46d8440685461b00e3ddb891b2ecc6855287d2bd8834a95fb1c1708b00ea5e82",
	"7c5eb606389556b33d34eb2536459528dc0af97adbcd0ce273aeb650f598d4b2",
	"4def7a7e5464a140ae9c3a80279fbebce4bd00f9faad819ab7e001512f67a10d",
	"d9c017dbe355f318d036469eb9b5fbe129fc2b5786a9dc6746a516eab6fe0126"
};

const char *SKEIN512_TestOutput[MDTESTCOUNT] = {
	"bc5b4c50925519c290cc634277ae3d6257212395cba733bbad37a4af0fa06af41fca7903d06564fea7a2d3730dbdb80c1f85562dfcc070334ea4d1d9e72cba7a",
	"b1cd8d33f61b3737adfd59bb13ad82f4a9548e92f22956a8976cca3fdb7fee4fe91698146c4197cec85d38b83c5d93bdba92c01fd9a53870d0c7f967bc62bdce",
	"8f5dd9ec798152668e35129496b029a960c9a9b88662f7f9482f110b31f9f93893ecfb25c009baad9e46737197d5630379816a886aa05526d3a70df272d96e75",
	"15b73c158ffb875fed4d72801ded0794c720b121c0c78edf45f900937e6933d9e21a3a984206933d504b5dbb2368000411477ee1b204c986068df77886542fcc",
	"23793ad900ef12f9165c8080da6fdfd2c8354a2929b8aadf83aa82a3c6470342f57cf8c035ec0d97429b626c4d94f28632c8f5134fd367dca5cf293d2ec13f8c",
	"0c6bed927e022f5ddcf81877d42e5f75798a9f8fd3ede3d83baac0a2f364b082e036c11af35fe478745459dd8f5c0b73efe3c56ba5bb2009208d5a29cc6e469c",
	"2ca9fcffb3456f297d1b5f407014ecb856f0baac8eb540f534b1f187196f21e88f31103128c2f03fcc9857d7a58eb66f9525e2302d88833ee069295537a434ce",
	"1131f2aaa0e97126c9314f9f968cc827259bbfabced2943bb8c9274448998fb3b78738b4580dd500c76105fd3c03e465e1414f2c29664286b1f79d3e51128125"
};

const char *SKEIN1024_TestOutput[MDTESTCOUNT] = {
	"0fff9563bb3279289227ac77d319b6fff8d7e9f09da1247b72a0a265cd6d2a62645ad547ed8193db48cff847c06494a03f55666d3b47eb4c20456c9373c86297d630d5578ebd34cb40991578f9f52b18003efa35d3da6553ff35db91b81ab890bec1b189b7f52cb2a783ebb7d823d725b0b4a71f6824e88f68f982eefc6d19c6",
	"6ab4c4ba9814a3d976ec8bffa7fcc638ceba0544a97b3c98411323ffd2dc936315d13dc93c13c4e88cda6f5bac6f2558b2d8694d3b6143e40d644ae43ca940685cb37f809d3d0550c56cba8036dee729a4f8fb960732e59e64d57f7f7710f8670963cdcdc95b41daab4855fcf8b6762a64b173ee61343a2c7689af1d293eba97",
	"35a599a0f91abcdb4cb73c19b8cb8d947742d82c309137a7caed29e8e0a2ca7a9ff9a90c34c1908cc7e7fd99bb15032fb86e76df21b72628399b5f7c3cc209d7bb31c99cd4e19465622a049afbb87c03b5ce3888d17e6e667279ec0aa9b3e2712624c01b5f5bbe1a564220bdcf6990af0c2539019f313fdd7406cca3892a1f1f",
	"ea891f5268acd0fac97467fc1aa89d1ce8681a9992a42540e53babee861483110c2d16f49e73bac27653ff173003e40cfb08516cd34262e6af95a5d8645c9c1abb3e813604d508b8511b30f9a5c1b352aa0791c7d2f27b2706dccea54bc7de6555b5202351751c3299f97c09cf89c40f67187e2521c0fad82b30edbb224f0458",
	"f23d95c2a25fbcd0e797cd058fec39d3c52d2b5afd7a9af1df934e63257d1d3dcf3246e7329c0f1104c1e51e3d22e300507b0c3b9f985bb1f645ef49835080536becf83788e17fed09c9982ba65c3cb7ffe6a5f745b911c506962adf226e435c42f6f6bc08d288f9c810e807e3216ef444f3db22744441deefa4900982a1371f",
	"cf3889e8a8d11bfd3938055d7d061437962bc5eac8ae83b1b71c94be201b8cf657fdbfc38674997a008c0c903f56a23feb3ae30e012377f1cfa080a9ca7fe8b96138662653fb3335c7d06595bf8baf65e215307532094cfdfa056bd8052ab792a3944a2adaa47b30335b8badb8fe9eb94fe329cdca04e58bbc530f0af709f469",
	"cf21a613620e6c119eca31fdfaad449a8e02f95ca256c21d2a105f8e4157048f9fe1e897893ea18b64e0e37cb07d5ac947f27ba544caf7cbc1ad094e675aed77a366270f7eb7f46543bccfa61c526fd628408058ed00ed566ac35a9761d002e629c4fb0d430b2f4ad016fcc49c44d2981c4002da0eecc42144160e2eaea4855a",
	"e6799b78db54085a2be7ff4c8007f147fa88d326abab30be0560b953396d8802feee9a15419b48a467574e9283be15685ca8a079ee52b27166b64dd70b124b1d4e4f6aca37224c3f2685e67e67baef9f94b905698adc794a09672aba977a61b20966912acdb08c21a2c37001785355dc884751a21f848ab36e590331ff938138"
};

static void
MDTestSuite(const Algorithm_t *alg)
{
	int i;
	char buffer[HEX_DIGEST_LENGTH];

	printf("%s test suite:\n", alg->name);
	for (i = 0; i < MDTESTCOUNT; i++) {
		(*alg->Data)(MDTestInput[i], strlen(MDTestInput[i]), buffer);
		printf("%s (\"%s\") = %s", alg->name, MDTestInput[i], buffer);
		if (strcmp(buffer, (*alg->TestOutput)[i]) == 0)
			printf(" - verified correct\n");
		else
			printf(" - INCORRECT RESULT!\n");
	}
}

/*
 * Digests the standard input and prints the result.
 */
static void
MDFilter(const Algorithm_t *alg, int tee)
{
	DIGEST_CTX context;
	unsigned int len;
	unsigned char buffer[BUFSIZ];
	char buf[HEX_DIGEST_LENGTH];

	alg->Init(&context);
	while ((len = fread(buffer, 1, BUFSIZ, stdin))) {
		if (tee && len != fwrite(buffer, 1, len, stdout))
			err(1, "stdout");
		alg->Update(&context, buffer, len);
	}
	printf("%s\n", alg->End(&context, buf));
}

static void
usage(const Algorithm_t *alg)
{

	fprintf(stderr, "usage: %s [-pqrtx] [-c string] [-s string] [files ...]\n", alg->progname);
	exit(1);
}
