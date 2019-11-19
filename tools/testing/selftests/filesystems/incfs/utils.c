// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2018 Google LLC
 */
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

#include "utils.h"

int mount_fs(char *mount_dir, char *backing_dir, int read_timeout_ms)
{
	static const char fs_name[] = INCFS_NAME;
	char mount_options[512];
	int result;

	snprintf(mount_options, ARRAY_SIZE(mount_options),
		 "read_timeout_ms=%u",
		  read_timeout_ms);

	result = mount(backing_dir, mount_dir, fs_name, 0, mount_options);
	if (result != 0)
		perror("Error mounting fs.");
	return result;
}

int mount_fs_opt(char *mount_dir, char *backing_dir, char *opt)
{
	static const char fs_name[] = INCFS_NAME;
	int result;

	result = mount(backing_dir, mount_dir, fs_name, 0, opt);
	if (result != 0)
		perror("Error mounting fs.");
	return result;
}

int unlink_node(int fd, int parent_ino, char *filename)
{
	return 0;
}


static EVP_PKEY *deserialize_private_key(const char *pem_key)
{
	BIO *bio = NULL;
	EVP_PKEY *pkey = NULL;
	int len = strlen(pem_key);

	bio = BIO_new_mem_buf(pem_key, len);
	if (!bio)
		return NULL;

	pkey = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
	BIO_free(bio);
	return pkey;
}

static X509 *deserialize_cert(const char *pem_cert)
{
	BIO *bio = NULL;
	X509 *cert = NULL;
	int len = strlen(pem_cert);

	bio = BIO_new_mem_buf(pem_cert, len);
	if (!bio)
		return NULL;

	cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);
	BIO_free(bio);
	return cert;
}

bool sign_pkcs7(const void *data_to_sign, size_t data_size,
		       char *pkey_pem, char *cert_pem,
		       void **sig_ret, size_t *sig_size_ret)
{
	/*
	 * PKCS#7 signing flags:
	 *
	 * - PKCS7_BINARY	signing binary data, so skip MIME translation
	 *
	 * - PKCS7_NOATTR	omit extra authenticated attributes, such as
	 *			SMIMECapabilities
	 *
	 * - PKCS7_PARTIAL	PKCS7_sign() creates a handle only, then
	 *			PKCS7_sign_add_signer() can add a signer later.
	 *			This is necessary to change the message digest
	 *			algorithm from the default of SHA-1.  Requires
	 *			OpenSSL 1.0.0 or later.
	 */
	int pkcs7_flags = PKCS7_BINARY | PKCS7_NOATTR | PKCS7_PARTIAL;
	void *sig;
	size_t sig_size;
	BIO *bio = NULL;
	PKCS7 *p7 = NULL;
	EVP_PKEY *pkey = NULL;
	X509 *cert = NULL;
	bool ok = false;

	const EVP_MD *md = EVP_sha256();

	pkey = deserialize_private_key(pkey_pem);
	if (!pkey) {
		printf("deserialize_private_key failed\n");
		goto out;
	}

	cert = deserialize_cert(cert_pem);
	if (!cert) {
		printf("deserialize_cert failed\n");
		goto out;
	}

	bio = BIO_new_mem_buf(data_to_sign, data_size);
	if (!bio)
		goto out;

	p7 = PKCS7_sign(NULL, NULL, NULL, bio, pkcs7_flags);
	if (!p7) {
		printf("failed to initialize PKCS#7 signature object\n");
		goto out;
	}

	if (!PKCS7_sign_add_signer(p7, cert, pkey, md, pkcs7_flags)) {
		printf("failed to add signer to PKCS#7 signature object\n");
		goto out;
	}

	if (PKCS7_final(p7, bio, pkcs7_flags) != 1) {
		printf("failed to finalize PKCS#7 signature\n");
		goto out;
	}

	BIO_free(bio);
	bio = BIO_new(BIO_s_mem());
	if (!bio) {
		printf("out of memory\n");
		goto out;
	}

	if (i2d_PKCS7_bio(bio, p7) != 1) {
		printf("failed to DER-encode PKCS#7 signature object\n");
		goto out;
	}

	sig_size = BIO_get_mem_data(bio, &sig);
	*sig_ret = malloc(sig_size);
	memcpy(*sig_ret, sig, sig_size);
	*sig_size_ret = sig_size;
	ok = true;
out:
	PKCS7_free(p7);
	BIO_free(bio);
	return ok;
}

int crypto_emit_file(int fd, char *dir, char *filename, incfs_uuid_t *id_out,
	size_t size, const char *root_hash, char *sig, size_t sig_size,
	char *add_data)
{
	int mode = __S_IFREG | 0555;
	struct incfs_file_signature_info sig_info = {
		.hash_tree_alg = root_hash
					? INCFS_HASH_TREE_SHA256
					: 0,
		.root_hash = ptr_to_u64(root_hash),
		.additional_data = ptr_to_u64(add_data),
		.additional_data_size = strlen(add_data),
		.signature =  ptr_to_u64(sig),
		.signature_size = sig_size,
	};

	struct incfs_new_file_args args = {
			.size = size,
			.mode = mode,
			.file_name = ptr_to_u64(filename),
			.directory_path = ptr_to_u64(dir),
			.signature_info = ptr_to_u64(&sig_info),
			.file_attr = 0,
			.file_attr_len = 0
	};

	md5(filename, strlen(filename), (char *)args.file_id.bytes);

	if (ioctl(fd, INCFS_IOC_CREATE_FILE, &args) != 0)
		return -errno;

	*id_out = args.file_id;
	return 0;
}


int emit_file(int fd, char *dir, char *filename, incfs_uuid_t *id_out,
		size_t size, char *attr)
{
	int mode = __S_IFREG | 0555;
	struct incfs_file_signature_info sig_info = {
		.hash_tree_alg = 0,
		.root_hash = ptr_to_u64(NULL)
	};
	struct incfs_new_file_args args = {
			.size = size,
			.mode = mode,
			.file_name = ptr_to_u64(filename),
			.directory_path = ptr_to_u64(dir),
			.signature_info = ptr_to_u64(&sig_info),
			.file_attr = ptr_to_u64(attr),
			.file_attr_len = attr ? strlen(attr) : 0
	};

	md5(filename, strlen(filename), (char *)args.file_id.bytes);

	if (ioctl(fd, INCFS_IOC_CREATE_FILE, &args) != 0)
		return -errno;

	*id_out = args.file_id;
	return 0;
}

int get_file_bmap(int cmd_fd, int ino, unsigned char *buf, int buf_size)
{
	return 0;
}

int get_file_signature(int fd, unsigned char *buf, int buf_size)
{
	struct incfs_get_file_sig_args args = {
		.file_signature = ptr_to_u64(buf),
		.file_signature_buf_size = buf_size
	};

	if (ioctl(fd, INCFS_IOC_READ_FILE_SIGNATURE, &args) == 0)
		return args.file_signature_len_out;
	return -errno;
}

loff_t get_file_size(char *name)
{
	struct stat st;

	if (stat(name, &st) == 0)
		return st.st_size;
	return -ENOENT;
}

int open_commands_file(char *mount_dir)
{
	char cmd_file[255];
	int cmd_fd;

	snprintf(cmd_file, ARRAY_SIZE(cmd_file),
			"%s/%s", mount_dir, INCFS_PENDING_READS_FILENAME);
	cmd_fd = open(cmd_file, O_RDONLY);

	if (cmd_fd < 0)
		perror("Can't open commands file");
	return cmd_fd;
}

int open_log_file(char *mount_dir)
{
	char cmd_file[255];
	int cmd_fd;

	snprintf(cmd_file, ARRAY_SIZE(cmd_file), "%s/.log", mount_dir);
	cmd_fd = open(cmd_file, O_RDWR);
	if (cmd_fd < 0)
		perror("Can't open log file");
	return cmd_fd;
}

int wait_for_pending_reads(int fd, int timeout_ms,
	struct incfs_pending_read_info *prs, int prs_count)
{
	ssize_t read_res = 0;

	if (timeout_ms > 0) {
		int poll_res = 0;
		struct pollfd pollfd = {
			.fd = fd,
			.events = POLLIN
		};

		poll_res = poll(&pollfd, 1, timeout_ms);
		if (poll_res < 0)
			return -errno;
		if (poll_res == 0)
			return 0;
		if (!(pollfd.revents | POLLIN))
			return 0;
	}

	read_res = read(fd, prs, prs_count * sizeof(*prs));
	if (read_res < 0)
		return -errno;

	return read_res / sizeof(*prs);
}

char *concat_file_name(const char *dir, char *file)
{
	char full_name[FILENAME_MAX] = "";

	if (snprintf(full_name, ARRAY_SIZE(full_name), "%s/%s", dir, file) < 0)
		return NULL;
	return strdup(full_name);
}

int delete_dir_tree(const char *dir_path)
{
	DIR *dir = NULL;
	struct dirent *dp;
	int result = 0;

	dir = opendir(dir_path);
	if (!dir) {
		result = -errno;
		goto out;
	}

	while ((dp = readdir(dir))) {
		char *full_path;

		if (!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, ".."))
			continue;

		full_path = concat_file_name(dir_path, dp->d_name);
		if (dp->d_type == DT_DIR)
			result = delete_dir_tree(full_path);
		else
			result = unlink(full_path);
		free(full_path);
		if (result)
			goto out;
	}

out:
	if (dir)
		closedir(dir);
	if (!result)
		rmdir(dir_path);
	return result;
}

void sha256(char *data, size_t dsize, char *hash)
{
	SHA256_CTX ctx;

	SHA256_Init(&ctx);
	SHA256_Update(&ctx, data, dsize);
	SHA256_Final((unsigned char *)hash, &ctx);
}

void md5(char *data, size_t dsize, char *hash)
{
	MD5_CTX ctx;

	MD5_Init(&ctx);
	MD5_Update(&ctx, data, dsize);
	MD5_Final((unsigned char *)hash, &ctx);
}
