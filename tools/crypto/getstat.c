/* Heavily copied from libkcapi 2015 - 2017, Stephan Mueller <smueller@chronox.de> */
#include <errno.h>
#include <linux/cryptouser.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CR_RTA(x)  ((struct rtattr *)(((char *)(x)) + NLMSG_ALIGN(sizeof(struct crypto_user_alg))))

static int get_stat(const char *drivername)
{
	struct {
		struct nlmsghdr n;
		struct crypto_user_alg cru;
	} req;
	struct sockaddr_nl nl;
	int sd = 0, ret;
	socklen_t addr_len;
	struct iovec iov;
	struct msghdr msg;
	char buf[4096];
	struct nlmsghdr *res_n = (struct nlmsghdr *)buf;
	struct crypto_user_alg *cru_res = NULL;
	int res_len = 0;
	struct rtattr *tb[CRYPTOCFGA_MAX + 1];
	struct rtattr *rta;
	struct nlmsgerr *errmsg;

	memset(&req, 0, sizeof(req));
	memset(&buf, 0, sizeof(buf));
	memset(&msg, 0, sizeof(msg));

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(req.cru));
	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.n.nlmsg_type = CRYPTO_MSG_GETSTAT;
	req.n.nlmsg_seq = time(NULL);

	strncpy(req.cru.cru_driver_name, drivername, strlen(drivername));

	sd =  socket(AF_NETLINK, SOCK_RAW, NETLINK_CRYPTO);
	if (sd < 0) {
		fprintf(stderr, "Netlink error: cannot open netlink socket");
		return -errno;
	}
	memset(&nl, 0, sizeof(nl));
	nl.nl_family = AF_NETLINK;
	if (bind(sd, (struct sockaddr *)&nl, sizeof(nl)) < 0) {
		ret = -errno;
		fprintf(stderr, "Netlink error: cannot bind netlink socket");
		goto out;
	}

	/* sanity check that netlink socket was successfully opened */
	addr_len = sizeof(nl);
	if (getsockname(sd, (struct sockaddr *)&nl, &addr_len) < 0) {
		ret = -errno;
		printf("Netlink error: cannot getsockname");
		goto out;
	}
	if (addr_len != sizeof(nl)) {
		ret = -errno;
		printf("Netlink error: wrong address length %d", addr_len);
		goto out;
	}
	if (nl.nl_family != AF_NETLINK) {
		ret = -errno;
		printf("Netlink error: wrong address family %d",
				nl.nl_family);
		goto out;
	}

	memset(&nl, 0, sizeof(nl));
	nl.nl_family = AF_NETLINK;
	iov.iov_base = (void *)&req.n;
	iov.iov_len = req.n.nlmsg_len;
	msg.msg_name = &nl;
	msg.msg_namelen = sizeof(nl);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	if (sendmsg(sd, &msg, 0) < 0) {
		ret = -errno;
		printf("Netlink error: sendmsg failed");
		goto out;
	}
	memset(buf, 0, sizeof(buf));
	iov.iov_base = buf;
	while (1) {
		iov.iov_len = sizeof(buf);
		ret = recvmsg(sd, &msg, 0);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			ret = -errno;
			printf("Netlink error: netlink receive error");
			goto out;
		}
		if (ret == 0) {
			ret = -errno;
			printf("Netlink error: no data");
			goto out;
		}
		if (ret > sizeof(buf)) {
			ret = -errno;
			printf("Netlink error: received too much data");
			goto out;
		}
		break;
	}

	ret = -EFAULT;
	res_len = res_n->nlmsg_len;
	if (res_n->nlmsg_type == NLMSG_ERROR) {
		errmsg = NLMSG_DATA(res_n);
		fprintf(stderr, "Fail with %d\n", errmsg->error);
		ret = errmsg->error;
		goto out;
	}

	if (res_n->nlmsg_type == CRYPTO_MSG_GETSTAT) {
		cru_res = NLMSG_DATA(res_n);
		res_len -= NLMSG_SPACE(sizeof(*cru_res));
	}
	if (res_len < 0) {
		printf("Netlink error: nlmsg len %d\n", res_len);
		goto out;
	}

	if (!cru_res) {
		ret = -EFAULT;
		printf("Netlink error: no cru_res\n");
		goto out;
	}

	rta = CR_RTA(cru_res);
	memset(tb, 0, sizeof(struct rtattr *) * (CRYPTOCFGA_MAX + 1));
	while (RTA_OK(rta, res_len)) {
		if ((rta->rta_type <= CRYPTOCFGA_MAX) && (!tb[rta->rta_type]))
			tb[rta->rta_type] = rta;
		rta = RTA_NEXT(rta, res_len);
	}
	if (res_len) {
		printf("Netlink error: unprocessed data %d",
				res_len);
		goto out;
	}

	if (tb[CRYPTOCFGA_STAT_HASH]) {
		struct rtattr *rta = tb[CRYPTOCFGA_STAT_HASH];
		struct crypto_stat *rhash =
			(struct crypto_stat *)RTA_DATA(rta);
		printf("%s\tHash\n\tHash: %u bytes: %llu\n\tErrors: %u\n",
			drivername,
			rhash->stat_hash_cnt, rhash->stat_hash_tlen,
			rhash->stat_hash_err_cnt);
	} else if (tb[CRYPTOCFGA_STAT_COMPRESS]) {
		struct rtattr *rta = tb[CRYPTOCFGA_STAT_COMPRESS];
		struct crypto_stat *rblk =
			(struct crypto_stat *)RTA_DATA(rta);
		printf("%s\tCompress\n\tCompress: %u bytes: %llu\n\tDecompress: %u bytes: %llu\n\tErrors: %u\n",
			drivername,
			rblk->stat_compress_cnt, rblk->stat_compress_tlen,
			rblk->stat_decompress_cnt, rblk->stat_decompress_tlen,
			rblk->stat_compress_err_cnt);
	} else if (tb[CRYPTOCFGA_STAT_ACOMP]) {
		struct rtattr *rta = tb[CRYPTOCFGA_STAT_ACOMP];
		struct crypto_stat *rcomp =
			(struct crypto_stat *)RTA_DATA(rta);
		printf("%s\tACompress\n\tCompress: %u bytes: %llu\n\tDecompress: %u bytes: %llu\n\tErrors: %u\n",
			drivername,
			rcomp->stat_compress_cnt, rcomp->stat_compress_tlen,
			rcomp->stat_decompress_cnt, rcomp->stat_decompress_tlen,
			rcomp->stat_compress_err_cnt);
	} else if (tb[CRYPTOCFGA_STAT_AEAD]) {
		struct rtattr *rta = tb[CRYPTOCFGA_STAT_AEAD];
		struct crypto_stat *raead =
			(struct crypto_stat *)RTA_DATA(rta);
		printf("%s\tAEAD\n\tEncrypt: %u bytes: %llu\n\tDecrypt: %u bytes: %llu\n\tErrors: %u\n",
			drivername,
			raead->stat_encrypt_cnt, raead->stat_encrypt_tlen,
			raead->stat_decrypt_cnt, raead->stat_decrypt_tlen,
			raead->stat_aead_err_cnt);
	} else if (tb[CRYPTOCFGA_STAT_BLKCIPHER]) {
		struct rtattr *rta = tb[CRYPTOCFGA_STAT_BLKCIPHER];
		struct crypto_stat *rblk =
			(struct crypto_stat *)RTA_DATA(rta);
		printf("%s\tCipher\n\tEncrypt: %u bytes: %llu\n\tDecrypt: %u bytes: %llu\n\tErrors: %u\n",
			drivername,
			rblk->stat_encrypt_cnt, rblk->stat_encrypt_tlen,
			rblk->stat_decrypt_cnt, rblk->stat_decrypt_tlen,
			rblk->stat_cipher_err_cnt);
	} else if (tb[CRYPTOCFGA_STAT_AKCIPHER]) {
		struct rtattr *rta = tb[CRYPTOCFGA_STAT_AKCIPHER];
		struct crypto_stat *rblk =
			(struct crypto_stat *)RTA_DATA(rta);
		printf("%s\tAkcipher\n\tEncrypt: %u bytes: %llu\n\tDecrypt: %u bytes: %llu\n\tSign: %u\n\tVerify: %u\n\tErrors: %u\n",
			drivername,
			rblk->stat_encrypt_cnt, rblk->stat_encrypt_tlen,
			rblk->stat_decrypt_cnt, rblk->stat_decrypt_tlen,
			rblk->stat_sign_cnt, rblk->stat_verify_cnt,
			rblk->stat_akcipher_err_cnt);
	} else if (tb[CRYPTOCFGA_STAT_CIPHER]) {
		struct rtattr *rta = tb[CRYPTOCFGA_STAT_CIPHER];
		struct crypto_stat *rblk =
			(struct crypto_stat *)RTA_DATA(rta);
		printf("%s\tcipher\n\tEncrypt: %u bytes: %llu\n\tDecrypt: %u bytes: %llu\n\tErrors: %u\n",
			drivername,
			rblk->stat_encrypt_cnt, rblk->stat_encrypt_tlen,
			rblk->stat_decrypt_cnt, rblk->stat_decrypt_tlen,
			rblk->stat_cipher_err_cnt);
	} else if (tb[CRYPTOCFGA_STAT_RNG]) {
		struct rtattr *rta = tb[CRYPTOCFGA_STAT_RNG];
		struct crypto_stat *rrng =
			(struct crypto_stat *)RTA_DATA(rta);
		printf("%s\tRNG\n\tSeed: %u\n\tGenerate: %u bytes: %llu\n\tErrors: %u\n",
			drivername,
			rrng->stat_seed_cnt,
			rrng->stat_generate_cnt, rrng->stat_generate_tlen,
			rrng->stat_rng_err_cnt);
	} else if (tb[CRYPTOCFGA_STAT_KPP]) {
		struct rtattr *rta = tb[CRYPTOCFGA_STAT_KPP];
		struct crypto_stat *rkpp =
			(struct crypto_stat *)RTA_DATA(rta);
		printf("%s\tKPP\n\tSetsecret: %u\n\tGenerate public key: %u\n\tCompute_shared_secret: %u\n\tErrors: %u\n",
			drivername,
			rkpp->stat_setsecret_cnt,
			rkpp->stat_generate_public_key_cnt,
			rkpp->stat_compute_shared_secret_cnt,
			rkpp->stat_kpp_err_cnt);
	} else {
		fprintf(stderr, "%s is of an unknown algorithm\n", drivername);
	}
	ret = 0;
out:
	close(sd);
	return ret;
}

int main(int argc, const char *argv[])
{
	char buf[4096];
	FILE *procfd;
	int i, lastspace;
	int ret;

	procfd = fopen("/proc/crypto", "r");
	if (!procfd) {
		ret = errno;
		fprintf(stderr, "Cannot open /proc/crypto %s\n", strerror(errno));
		return ret;
	}
	if (argc > 1) {
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			printf("Usage: %s [-h|--help] display this help\n", argv[0]);
			printf("Usage: %s display all crypto statistics\n", argv[0]);
			printf("Usage: %s drivername1 drivername2 ... = display crypto statistics about drivername1 ...\n", argv[0]);
			return 0;
		}
		for (i = 1; i < argc; i++) {
			ret = get_stat(argv[i]);
			if (ret) {
				fprintf(stderr, "Failed with %s\n", strerror(-ret));
				return ret;
			}
		}
		return 0;
	}

	while (fgets(buf, sizeof(buf), procfd)) {
		if (!strncmp(buf, "driver", 6)) {
			lastspace = 0;
			i = 0;
			while (i < strlen(buf)) {
				i++;
				if (buf[i] == ' ')
					lastspace = i;
			}
			buf[strlen(buf) - 1] = '\0';
			ret = get_stat(buf + lastspace + 1);
			if (ret) {
				fprintf(stderr, "Failed with %s\n", strerror(-ret));
				goto out;
			}
		}
	}
out:
	fclose(procfd);
	return ret;
}
