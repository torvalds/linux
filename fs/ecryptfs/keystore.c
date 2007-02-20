/**
 * eCryptfs: Linux filesystem encryption layer
 * In-kernel key management code.  Includes functions to parse and
 * write authentication token-related packets with the underlying
 * file.
 *
 * Copyright (C) 2004-2006 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mhalcrow@us.ibm.com>
 *              Michael C. Thompson <mcthomps@us.ibm.com>
 *              Trevor S. Highland <trevor.highland@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/key.h>
#include <linux/random.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include "ecryptfs_kernel.h"

/**
 * request_key returned an error instead of a valid key address;
 * determine the type of error, make appropriate log entries, and
 * return an error code.
 */
int process_request_key_err(long err_code)
{
	int rc = 0;

	switch (err_code) {
	case ENOKEY:
		ecryptfs_printk(KERN_WARNING, "No key\n");
		rc = -ENOENT;
		break;
	case EKEYEXPIRED:
		ecryptfs_printk(KERN_WARNING, "Key expired\n");
		rc = -ETIME;
		break;
	case EKEYREVOKED:
		ecryptfs_printk(KERN_WARNING, "Key revoked\n");
		rc = -EINVAL;
		break;
	default:
		ecryptfs_printk(KERN_WARNING, "Unknown error code: "
				"[0x%.16x]\n", err_code);
		rc = -EINVAL;
	}
	return rc;
}

/**
 * parse_packet_length
 * @data: Pointer to memory containing length at offset
 * @size: This function writes the decoded size to this memory
 *        address; zero on error
 * @length_size: The number of bytes occupied by the encoded length
 *
 * Returns Zero on success
 */
static int parse_packet_length(unsigned char *data, size_t *size,
			       size_t *length_size)
{
	int rc = 0;

	(*length_size) = 0;
	(*size) = 0;
	if (data[0] < 192) {
		/* One-byte length */
		(*size) = (unsigned char)data[0];
		(*length_size) = 1;
	} else if (data[0] < 224) {
		/* Two-byte length */
		(*size) = (((unsigned char)(data[0]) - 192) * 256);
		(*size) += ((unsigned char)(data[1]) + 192);
		(*length_size) = 2;
	} else if (data[0] == 255) {
		/* Five-byte length; we're not supposed to see this */
		ecryptfs_printk(KERN_ERR, "Five-byte packet length not "
				"supported\n");
		rc = -EINVAL;
		goto out;
	} else {
		ecryptfs_printk(KERN_ERR, "Error parsing packet length\n");
		rc = -EINVAL;
		goto out;
	}
out:
	return rc;
}

/**
 * write_packet_length
 * @dest: The byte array target into which to write the
 *       length. Must have at least 5 bytes allocated.
 * @size: The length to write.
 * @packet_size_length: The number of bytes used to encode the
 *                      packet length is written to this address.
 *
 * Returns zero on success; non-zero on error.
 */
static int write_packet_length(char *dest, size_t size,
			       size_t *packet_size_length)
{
	int rc = 0;

	if (size < 192) {
		dest[0] = size;
		(*packet_size_length) = 1;
	} else if (size < 65536) {
		dest[0] = (((size - 192) / 256) + 192);
		dest[1] = ((size - 192) % 256);
		(*packet_size_length) = 2;
	} else {
		rc = -EINVAL;
		ecryptfs_printk(KERN_WARNING,
				"Unsupported packet size: [%d]\n", size);
	}
	return rc;
}

static int
write_tag_64_packet(char *signature, struct ecryptfs_session_key *session_key,
		    char **packet, size_t *packet_len)
{
	size_t i = 0;
	size_t data_len;
	size_t packet_size_len;
	char *message;
	int rc;

	/*
	 *              ***** TAG 64 Packet Format *****
	 *    | Content Type                       | 1 byte       |
	 *    | Key Identifier Size                | 1 or 2 bytes |
	 *    | Key Identifier                     | arbitrary    |
	 *    | Encrypted File Encryption Key Size | 1 or 2 bytes |
	 *    | Encrypted File Encryption Key      | arbitrary    |
	 */
	data_len = (5 + ECRYPTFS_SIG_SIZE_HEX
		    + session_key->encrypted_key_size);
	*packet = kmalloc(data_len, GFP_KERNEL);
	message = *packet;
	if (!message) {
		ecryptfs_printk(KERN_ERR, "Unable to allocate memory\n");
		rc = -ENOMEM;
		goto out;
	}
	message[i++] = ECRYPTFS_TAG_64_PACKET_TYPE;
	rc = write_packet_length(&message[i], ECRYPTFS_SIG_SIZE_HEX,
				 &packet_size_len);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error generating tag 64 packet "
				"header; cannot generate packet length\n");
		goto out;
	}
	i += packet_size_len;
	memcpy(&message[i], signature, ECRYPTFS_SIG_SIZE_HEX);
	i += ECRYPTFS_SIG_SIZE_HEX;
	rc = write_packet_length(&message[i], session_key->encrypted_key_size,
				 &packet_size_len);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error generating tag 64 packet "
				"header; cannot generate packet length\n");
		goto out;
	}
	i += packet_size_len;
	memcpy(&message[i], session_key->encrypted_key,
	       session_key->encrypted_key_size);
	i += session_key->encrypted_key_size;
	*packet_len = i;
out:
	return rc;
}

static int
parse_tag_65_packet(struct ecryptfs_session_key *session_key, u16 *cipher_code,
		    struct ecryptfs_message *msg)
{
	size_t i = 0;
	char *data;
	size_t data_len;
	size_t m_size;
	size_t message_len;
	u16 checksum = 0;
	u16 expected_checksum = 0;
	int rc;

	/*
	 *              ***** TAG 65 Packet Format *****
	 *         | Content Type             | 1 byte       |
	 *         | Status Indicator         | 1 byte       |
	 *         | File Encryption Key Size | 1 or 2 bytes |
	 *         | File Encryption Key      | arbitrary    |
	 */
	message_len = msg->data_len;
	data = msg->data;
	if (message_len < 4) {
		rc = -EIO;
		goto out;
	}
	if (data[i++] != ECRYPTFS_TAG_65_PACKET_TYPE) {
		ecryptfs_printk(KERN_ERR, "Type should be ECRYPTFS_TAG_65\n");
		rc = -EIO;
		goto out;
	}
	if (data[i++]) {
		ecryptfs_printk(KERN_ERR, "Status indicator has non-zero value "
				"[%d]\n", data[i-1]);
		rc = -EIO;
		goto out;
	}
	rc = parse_packet_length(&data[i], &m_size, &data_len);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error parsing packet length; "
				"rc = [%d]\n", rc);
		goto out;
	}
	i += data_len;
	if (message_len < (i + m_size)) {
		ecryptfs_printk(KERN_ERR, "The received netlink message is "
				"shorter than expected\n");
		rc = -EIO;
		goto out;
	}
	if (m_size < 3) {
		ecryptfs_printk(KERN_ERR,
				"The decrypted key is not long enough to "
				"include a cipher code and checksum\n");
		rc = -EIO;
		goto out;
	}
	*cipher_code = data[i++];
	/* The decrypted key includes 1 byte cipher code and 2 byte checksum */
	session_key->decrypted_key_size = m_size - 3;
	if (session_key->decrypted_key_size > ECRYPTFS_MAX_KEY_BYTES) {
		ecryptfs_printk(KERN_ERR, "key_size [%d] larger than "
				"the maximum key size [%d]\n",
				session_key->decrypted_key_size,
				ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES);
		rc = -EIO;
		goto out;
	}
	memcpy(session_key->decrypted_key, &data[i],
	       session_key->decrypted_key_size);
	i += session_key->decrypted_key_size;
	expected_checksum += (unsigned char)(data[i++]) << 8;
	expected_checksum += (unsigned char)(data[i++]);
	for (i = 0; i < session_key->decrypted_key_size; i++)
		checksum += session_key->decrypted_key[i];
	if (expected_checksum != checksum) {
		ecryptfs_printk(KERN_ERR, "Invalid checksum for file "
				"encryption  key; expected [%x]; calculated "
				"[%x]\n", expected_checksum, checksum);
		rc = -EIO;
	}
out:
	return rc;
}


static int
write_tag_66_packet(char *signature, size_t cipher_code,
		    struct ecryptfs_crypt_stat *crypt_stat, char **packet,
		    size_t *packet_len)
{
	size_t i = 0;
	size_t j;
	size_t data_len;
	size_t checksum = 0;
	size_t packet_size_len;
	char *message;
	int rc;

	/*
	 *              ***** TAG 66 Packet Format *****
	 *         | Content Type             | 1 byte       |
	 *         | Key Identifier Size      | 1 or 2 bytes |
	 *         | Key Identifier           | arbitrary    |
	 *         | File Encryption Key Size | 1 or 2 bytes |
	 *         | File Encryption Key      | arbitrary    |
	 */
	data_len = (5 + ECRYPTFS_SIG_SIZE_HEX + crypt_stat->key_size);
	*packet = kmalloc(data_len, GFP_KERNEL);
	message = *packet;
	if (!message) {
		ecryptfs_printk(KERN_ERR, "Unable to allocate memory\n");
		rc = -ENOMEM;
		goto out;
	}
	message[i++] = ECRYPTFS_TAG_66_PACKET_TYPE;
	rc = write_packet_length(&message[i], ECRYPTFS_SIG_SIZE_HEX,
				 &packet_size_len);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error generating tag 66 packet "
				"header; cannot generate packet length\n");
		goto out;
	}
	i += packet_size_len;
	memcpy(&message[i], signature, ECRYPTFS_SIG_SIZE_HEX);
	i += ECRYPTFS_SIG_SIZE_HEX;
	/* The encrypted key includes 1 byte cipher code and 2 byte checksum */
	rc = write_packet_length(&message[i], crypt_stat->key_size + 3,
				 &packet_size_len);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error generating tag 66 packet "
				"header; cannot generate packet length\n");
		goto out;
	}
	i += packet_size_len;
	message[i++] = cipher_code;
	memcpy(&message[i], crypt_stat->key, crypt_stat->key_size);
	i += crypt_stat->key_size;
	for (j = 0; j < crypt_stat->key_size; j++)
		checksum += crypt_stat->key[j];
	message[i++] = (checksum / 256) % 256;
	message[i++] = (checksum % 256);
	*packet_len = i;
out:
	return rc;
}

static int
parse_tag_67_packet(struct ecryptfs_key_record *key_rec,
		    struct ecryptfs_message *msg)
{
	size_t i = 0;
	char *data;
	size_t data_len;
	size_t message_len;
	int rc;

	/*
	 *              ***** TAG 65 Packet Format *****
	 *    | Content Type                       | 1 byte       |
	 *    | Status Indicator                   | 1 byte       |
	 *    | Encrypted File Encryption Key Size | 1 or 2 bytes |
	 *    | Encrypted File Encryption Key      | arbitrary    |
	 */
	message_len = msg->data_len;
	data = msg->data;
	/* verify that everything through the encrypted FEK size is present */
	if (message_len < 4) {
		rc = -EIO;
		goto out;
	}
	if (data[i++] != ECRYPTFS_TAG_67_PACKET_TYPE) {
		ecryptfs_printk(KERN_ERR, "Type should be ECRYPTFS_TAG_67\n");
		rc = -EIO;
		goto out;
	}
	if (data[i++]) {
		ecryptfs_printk(KERN_ERR, "Status indicator has non zero value"
				" [%d]\n", data[i-1]);
		rc = -EIO;
		goto out;
	}
	rc = parse_packet_length(&data[i], &key_rec->enc_key_size, &data_len);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error parsing packet length; "
				"rc = [%d]\n", rc);
		goto out;
	}
	i += data_len;
	if (message_len < (i + key_rec->enc_key_size)) {
		ecryptfs_printk(KERN_ERR, "message_len [%d]; max len is [%d]\n",
				message_len, (i + key_rec->enc_key_size));
		rc = -EIO;
		goto out;
	}
	if (key_rec->enc_key_size > ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES) {
		ecryptfs_printk(KERN_ERR, "Encrypted key_size [%d] larger than "
				"the maximum key size [%d]\n",
				key_rec->enc_key_size,
				ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES);
		rc = -EIO;
		goto out;
	}
	memcpy(key_rec->enc_key, &data[i], key_rec->enc_key_size);
out:
	return rc;
}

/**
 * decrypt_pki_encrypted_session_key - Decrypt the session key with
 * the given auth_tok.
 *
 * Returns Zero on success; non-zero error otherwise.
 */
static int decrypt_pki_encrypted_session_key(
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat,
	struct ecryptfs_auth_tok *auth_tok,
	struct ecryptfs_crypt_stat *crypt_stat)
{
	u16 cipher_code = 0;
	struct ecryptfs_msg_ctx *msg_ctx;
	struct ecryptfs_message *msg = NULL;
	char *netlink_message;
	size_t netlink_message_length;
	int rc;

	rc = write_tag_64_packet(mount_crypt_stat->global_auth_tok_sig,
				 &(auth_tok->session_key),
				 &netlink_message, &netlink_message_length);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Failed to write tag 64 packet");
		goto out;
	}
	rc = ecryptfs_send_message(ecryptfs_transport, netlink_message,
				   netlink_message_length, &msg_ctx);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error sending netlink message\n");
		goto out;
	}
	rc = ecryptfs_wait_for_response(msg_ctx, &msg);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Failed to receive tag 65 packet "
				"from the user space daemon\n");
		rc = -EIO;
		goto out;
	}
	rc = parse_tag_65_packet(&(auth_tok->session_key),
				 &cipher_code, msg);
	if (rc) {
		printk(KERN_ERR "Failed to parse tag 65 packet; rc = [%d]\n",
		       rc);
		goto out;
	}
	auth_tok->session_key.flags |= ECRYPTFS_CONTAINS_DECRYPTED_KEY;
	memcpy(crypt_stat->key, auth_tok->session_key.decrypted_key,
	       auth_tok->session_key.decrypted_key_size);
	crypt_stat->key_size = auth_tok->session_key.decrypted_key_size;
	rc = ecryptfs_cipher_code_to_string(crypt_stat->cipher, cipher_code);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Cipher code [%d] is invalid\n",
				cipher_code)
		goto out;
	}
	crypt_stat->flags |= ECRYPTFS_KEY_VALID;
	if (ecryptfs_verbosity > 0) {
		ecryptfs_printk(KERN_DEBUG, "Decrypted session key:\n");
		ecryptfs_dump_hex(crypt_stat->key,
				  crypt_stat->key_size);
	}
out:
	if (msg)
		kfree(msg);
	return rc;
}

static void wipe_auth_tok_list(struct list_head *auth_tok_list_head)
{
	struct list_head *walker;
	struct ecryptfs_auth_tok_list_item *auth_tok_list_item;

	walker = auth_tok_list_head->next;
	while (walker != auth_tok_list_head) {
		auth_tok_list_item =
		    list_entry(walker, struct ecryptfs_auth_tok_list_item,
			       list);
		walker = auth_tok_list_item->list.next;
		memset(auth_tok_list_item, 0,
		       sizeof(struct ecryptfs_auth_tok_list_item));
		kmem_cache_free(ecryptfs_auth_tok_list_item_cache,
				auth_tok_list_item);
	}
	auth_tok_list_head->next = NULL;
}

struct kmem_cache *ecryptfs_auth_tok_list_item_cache;


/**
 * parse_tag_1_packet
 * @crypt_stat: The cryptographic context to modify based on packet
 *              contents.
 * @data: The raw bytes of the packet.
 * @auth_tok_list: eCryptfs parses packets into authentication tokens;
 *                 a new authentication token will be placed at the end
 *                 of this list for this packet.
 * @new_auth_tok: Pointer to a pointer to memory that this function
 *                allocates; sets the memory address of the pointer to
 *                NULL on error. This object is added to the
 *                auth_tok_list.
 * @packet_size: This function writes the size of the parsed packet
 *               into this memory location; zero on error.
 *
 * Returns zero on success; non-zero on error.
 */
static int
parse_tag_1_packet(struct ecryptfs_crypt_stat *crypt_stat,
		   unsigned char *data, struct list_head *auth_tok_list,
		   struct ecryptfs_auth_tok **new_auth_tok,
		   size_t *packet_size, size_t max_packet_size)
{
	size_t body_size;
	struct ecryptfs_auth_tok_list_item *auth_tok_list_item;
	size_t length_size;
	int rc = 0;

	(*packet_size) = 0;
	(*new_auth_tok) = NULL;

	/* we check that:
	 *   one byte for the Tag 1 ID flag
	 *   two bytes for the body size
	 * do not exceed the maximum_packet_size
	 */
	if (unlikely((*packet_size) + 3 > max_packet_size)) {
		ecryptfs_printk(KERN_ERR, "Packet size exceeds max\n");
		rc = -EINVAL;
		goto out;
	}
	/* check for Tag 1 identifier - one byte */
	if (data[(*packet_size)++] != ECRYPTFS_TAG_1_PACKET_TYPE) {
		ecryptfs_printk(KERN_ERR, "Enter w/ first byte != 0x%.2x\n",
				ECRYPTFS_TAG_1_PACKET_TYPE);
		rc = -EINVAL;
		goto out;
	}
	/* Released: wipe_auth_tok_list called in ecryptfs_parse_packet_set or
	 * at end of function upon failure */
	auth_tok_list_item =
		kmem_cache_alloc(ecryptfs_auth_tok_list_item_cache,
				 GFP_KERNEL);
	if (!auth_tok_list_item) {
		ecryptfs_printk(KERN_ERR, "Unable to allocate memory\n");
		rc = -ENOMEM;
		goto out;
	}
	memset(auth_tok_list_item, 0,
	       sizeof(struct ecryptfs_auth_tok_list_item));
	(*new_auth_tok) = &auth_tok_list_item->auth_tok;
	/* check for body size - one to two bytes
	 *
	 *              ***** TAG 1 Packet Format *****
	 *    | version number                     | 1 byte       |
	 *    | key ID                             | 8 bytes      |
	 *    | public key algorithm               | 1 byte       |
	 *    | encrypted session key              | arbitrary    |
	 */
	rc = parse_packet_length(&data[(*packet_size)], &body_size,
				 &length_size);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error parsing packet length; "
				"rc = [%d]\n", rc);
		goto out_free;
	}
	if (unlikely(body_size < (0x02 + ECRYPTFS_SIG_SIZE))) {
		ecryptfs_printk(KERN_WARNING, "Invalid body size ([%d])\n",
				body_size);
		rc = -EINVAL;
		goto out_free;
	}
	(*packet_size) += length_size;
	if (unlikely((*packet_size) + body_size > max_packet_size)) {
		ecryptfs_printk(KERN_ERR, "Packet size exceeds max\n");
		rc = -EINVAL;
		goto out_free;
	}
	/* Version 3 (from RFC2440) - one byte */
	if (unlikely(data[(*packet_size)++] != 0x03)) {
		ecryptfs_printk(KERN_DEBUG, "Unknown version number "
				"[%d]\n", data[(*packet_size) - 1]);
		rc = -EINVAL;
		goto out_free;
	}
	/* Read Signature */
	ecryptfs_to_hex((*new_auth_tok)->token.private_key.signature,
			&data[(*packet_size)], ECRYPTFS_SIG_SIZE);
	*packet_size += ECRYPTFS_SIG_SIZE;
	/* This byte is skipped because the kernel does not need to
	 * know which public key encryption algorithm was used */
	(*packet_size)++;
	(*new_auth_tok)->session_key.encrypted_key_size =
		body_size - (0x02 + ECRYPTFS_SIG_SIZE);
	if ((*new_auth_tok)->session_key.encrypted_key_size
	    > ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES) {
		ecryptfs_printk(KERN_ERR, "Tag 1 packet contains key larger "
				"than ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES");
		rc = -EINVAL;
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, "Encrypted key size = [%d]\n",
			(*new_auth_tok)->session_key.encrypted_key_size);
	memcpy((*new_auth_tok)->session_key.encrypted_key,
	       &data[(*packet_size)], (body_size - 0x02 - ECRYPTFS_SIG_SIZE));
	(*packet_size) += (*new_auth_tok)->session_key.encrypted_key_size;
	(*new_auth_tok)->session_key.flags &=
		~ECRYPTFS_CONTAINS_DECRYPTED_KEY;
	(*new_auth_tok)->session_key.flags |=
		ECRYPTFS_CONTAINS_ENCRYPTED_KEY;
	(*new_auth_tok)->token_type = ECRYPTFS_PRIVATE_KEY;
	(*new_auth_tok)->flags |= ECRYPTFS_PRIVATE_KEY;
	/* TODO: Why are we setting this flag here? Don't we want the
	 * userspace to decrypt the session key? */
	(*new_auth_tok)->session_key.flags &=
		~(ECRYPTFS_USERSPACE_SHOULD_TRY_TO_DECRYPT);
	(*new_auth_tok)->session_key.flags &=
		~(ECRYPTFS_USERSPACE_SHOULD_TRY_TO_ENCRYPT);
	list_add(&auth_tok_list_item->list, auth_tok_list);
	goto out;
out_free:
	(*new_auth_tok) = NULL;
	memset(auth_tok_list_item, 0,
	       sizeof(struct ecryptfs_auth_tok_list_item));
	kmem_cache_free(ecryptfs_auth_tok_list_item_cache,
			auth_tok_list_item);
out:
	if (rc)
		(*packet_size) = 0;
	return rc;
}

/**
 * parse_tag_3_packet
 * @crypt_stat: The cryptographic context to modify based on packet
 *              contents.
 * @data: The raw bytes of the packet.
 * @auth_tok_list: eCryptfs parses packets into authentication tokens;
 *                 a new authentication token will be placed at the end
 *                 of this list for this packet.
 * @new_auth_tok: Pointer to a pointer to memory that this function
 *                allocates; sets the memory address of the pointer to
 *                NULL on error. This object is added to the
 *                auth_tok_list.
 * @packet_size: This function writes the size of the parsed packet
 *               into this memory location; zero on error.
 * @max_packet_size: maximum number of bytes to parse
 *
 * Returns zero on success; non-zero on error.
 */
static int
parse_tag_3_packet(struct ecryptfs_crypt_stat *crypt_stat,
		   unsigned char *data, struct list_head *auth_tok_list,
		   struct ecryptfs_auth_tok **new_auth_tok,
		   size_t *packet_size, size_t max_packet_size)
{
	size_t body_size;
	struct ecryptfs_auth_tok_list_item *auth_tok_list_item;
	size_t length_size;
	int rc = 0;

	(*packet_size) = 0;
	(*new_auth_tok) = NULL;

	/* we check that:
	 *   one byte for the Tag 3 ID flag
	 *   two bytes for the body size
	 * do not exceed the maximum_packet_size
	 */
	if (unlikely((*packet_size) + 3 > max_packet_size)) {
		ecryptfs_printk(KERN_ERR, "Packet size exceeds max\n");
		rc = -EINVAL;
		goto out;
	}

	/* check for Tag 3 identifyer - one byte */
	if (data[(*packet_size)++] != ECRYPTFS_TAG_3_PACKET_TYPE) {
		ecryptfs_printk(KERN_ERR, "Enter w/ first byte != 0x%.2x\n",
				ECRYPTFS_TAG_3_PACKET_TYPE);
		rc = -EINVAL;
		goto out;
	}
	/* Released: wipe_auth_tok_list called in ecryptfs_parse_packet_set or
	 * at end of function upon failure */
	auth_tok_list_item =
	    kmem_cache_zalloc(ecryptfs_auth_tok_list_item_cache, GFP_KERNEL);
	if (!auth_tok_list_item) {
		ecryptfs_printk(KERN_ERR, "Unable to allocate memory\n");
		rc = -ENOMEM;
		goto out;
	}
	(*new_auth_tok) = &auth_tok_list_item->auth_tok;

	/* check for body size - one to two bytes */
	rc = parse_packet_length(&data[(*packet_size)], &body_size,
				 &length_size);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "Error parsing packet length; "
				"rc = [%d]\n", rc);
		goto out_free;
	}
	if (unlikely(body_size < (0x05 + ECRYPTFS_SALT_SIZE))) {
		ecryptfs_printk(KERN_WARNING, "Invalid body size ([%d])\n",
				body_size);
		rc = -EINVAL;
		goto out_free;
	}
	(*packet_size) += length_size;

	/* now we know the length of the remainting Tag 3 packet size:
	 *   5 fix bytes for: version string, cipher, S2K ID, hash algo,
	 *                    number of hash iterations
	 *   ECRYPTFS_SALT_SIZE bytes for salt
	 *   body_size bytes minus the stuff above is the encrypted key size
	 */
	if (unlikely((*packet_size) + body_size > max_packet_size)) {
		ecryptfs_printk(KERN_ERR, "Packet size exceeds max\n");
		rc = -EINVAL;
		goto out_free;
	}

	/* There are 5 characters of additional information in the
	 * packet */
	(*new_auth_tok)->session_key.encrypted_key_size =
		body_size - (0x05 + ECRYPTFS_SALT_SIZE);
	ecryptfs_printk(KERN_DEBUG, "Encrypted key size = [%d]\n",
			(*new_auth_tok)->session_key.encrypted_key_size);

	/* Version 4 (from RFC2440) - one byte */
	if (unlikely(data[(*packet_size)++] != 0x04)) {
		ecryptfs_printk(KERN_DEBUG, "Unknown version number "
				"[%d]\n", data[(*packet_size) - 1]);
		rc = -EINVAL;
		goto out_free;
	}

	/* cipher - one byte */
	ecryptfs_cipher_code_to_string(crypt_stat->cipher,
				       (u16)data[(*packet_size)]);
	/* A little extra work to differentiate among the AES key
	 * sizes; see RFC2440 */
	switch(data[(*packet_size)++]) {
	case RFC2440_CIPHER_AES_192:
		crypt_stat->key_size = 24;
		break;
	default:
		crypt_stat->key_size =
			(*new_auth_tok)->session_key.encrypted_key_size;
	}
	ecryptfs_init_crypt_ctx(crypt_stat);
	/* S2K identifier 3 (from RFC2440) */
	if (unlikely(data[(*packet_size)++] != 0x03)) {
		ecryptfs_printk(KERN_ERR, "Only S2K ID 3 is currently "
				"supported\n");
		rc = -ENOSYS;
		goto out_free;
	}

	/* TODO: finish the hash mapping */
	/* hash algorithm - one byte */
	switch (data[(*packet_size)++]) {
	case 0x01: /* See RFC2440 for these numbers and their mappings */
		/* Choose MD5 */
		/* salt - ECRYPTFS_SALT_SIZE bytes */
		memcpy((*new_auth_tok)->token.password.salt,
		       &data[(*packet_size)], ECRYPTFS_SALT_SIZE);
		(*packet_size) += ECRYPTFS_SALT_SIZE;

		/* This conversion was taken straight from RFC2440 */
		/* number of hash iterations - one byte */
		(*new_auth_tok)->token.password.hash_iterations =
			((u32) 16 + (data[(*packet_size)] & 15))
				<< ((data[(*packet_size)] >> 4) + 6);
		(*packet_size)++;

		/* encrypted session key -
		 *   (body_size-5-ECRYPTFS_SALT_SIZE) bytes */
		memcpy((*new_auth_tok)->session_key.encrypted_key,
		       &data[(*packet_size)],
		       (*new_auth_tok)->session_key.encrypted_key_size);
		(*packet_size) +=
			(*new_auth_tok)->session_key.encrypted_key_size;
		(*new_auth_tok)->session_key.flags &=
			~ECRYPTFS_CONTAINS_DECRYPTED_KEY;
		(*new_auth_tok)->session_key.flags |=
			ECRYPTFS_CONTAINS_ENCRYPTED_KEY;
		(*new_auth_tok)->token.password.hash_algo = 0x01;
		break;
	default:
		ecryptfs_printk(KERN_ERR, "Unsupported hash algorithm: "
				"[%d]\n", data[(*packet_size) - 1]);
		rc = -ENOSYS;
		goto out_free;
	}
	(*new_auth_tok)->token_type = ECRYPTFS_PASSWORD;
	/* TODO: Parametarize; we might actually want userspace to
	 * decrypt the session key. */
	(*new_auth_tok)->session_key.flags &=
			    ~(ECRYPTFS_USERSPACE_SHOULD_TRY_TO_DECRYPT);
	(*new_auth_tok)->session_key.flags &=
			    ~(ECRYPTFS_USERSPACE_SHOULD_TRY_TO_ENCRYPT);
	list_add(&auth_tok_list_item->list, auth_tok_list);
	goto out;
out_free:
	(*new_auth_tok) = NULL;
	memset(auth_tok_list_item, 0,
	       sizeof(struct ecryptfs_auth_tok_list_item));
	kmem_cache_free(ecryptfs_auth_tok_list_item_cache,
			auth_tok_list_item);
out:
	if (rc)
		(*packet_size) = 0;
	return rc;
}

/**
 * parse_tag_11_packet
 * @data: The raw bytes of the packet
 * @contents: This function writes the data contents of the literal
 *            packet into this memory location
 * @max_contents_bytes: The maximum number of bytes that this function
 *                      is allowed to write into contents
 * @tag_11_contents_size: This function writes the size of the parsed
 *                        contents into this memory location; zero on
 *                        error
 * @packet_size: This function writes the size of the parsed packet
 *               into this memory location; zero on error
 * @max_packet_size: maximum number of bytes to parse
 *
 * Returns zero on success; non-zero on error.
 */
static int
parse_tag_11_packet(unsigned char *data, unsigned char *contents,
		    size_t max_contents_bytes, size_t *tag_11_contents_size,
		    size_t *packet_size, size_t max_packet_size)
{
	size_t body_size;
	size_t length_size;
	int rc = 0;

	(*packet_size) = 0;
	(*tag_11_contents_size) = 0;

	/* check that:
	 *   one byte for the Tag 11 ID flag
	 *   two bytes for the Tag 11 length
	 * do not exceed the maximum_packet_size
	 */
	if (unlikely((*packet_size) + 3 > max_packet_size)) {
		ecryptfs_printk(KERN_ERR, "Packet size exceeds max\n");
		rc = -EINVAL;
		goto out;
	}

	/* check for Tag 11 identifyer - one byte */
	if (data[(*packet_size)++] != ECRYPTFS_TAG_11_PACKET_TYPE) {
		ecryptfs_printk(KERN_WARNING,
				"Invalid tag 11 packet format\n");
		rc = -EINVAL;
		goto out;
	}

	/* get Tag 11 content length - one or two bytes */
	rc = parse_packet_length(&data[(*packet_size)], &body_size,
				 &length_size);
	if (rc) {
		ecryptfs_printk(KERN_WARNING,
				"Invalid tag 11 packet format\n");
		goto out;
	}
	(*packet_size) += length_size;

	if (body_size < 13) {
		ecryptfs_printk(KERN_WARNING, "Invalid body size ([%d])\n",
				body_size);
		rc = -EINVAL;
		goto out;
	}
	/* We have 13 bytes of surrounding packet values */
	(*tag_11_contents_size) = (body_size - 13);

	/* now we know the length of the remainting Tag 11 packet size:
	 *   14 fix bytes for: special flag one, special flag two,
	 *   		       12 skipped bytes
	 *   body_size bytes minus the stuff above is the Tag 11 content
	 */
	/* FIXME why is the body size one byte smaller than the actual
	 * size of the body?
	 * this seems to be an error here as well as in
	 * write_tag_11_packet() */
	if (unlikely((*packet_size) + body_size + 1 > max_packet_size)) {
		ecryptfs_printk(KERN_ERR, "Packet size exceeds max\n");
		rc = -EINVAL;
		goto out;
	}

	/* special flag one - one byte */
	if (data[(*packet_size)++] != 0x62) {
		ecryptfs_printk(KERN_WARNING, "Unrecognizable packet\n");
		rc = -EINVAL;
		goto out;
	}

	/* special flag two - one byte */
	if (data[(*packet_size)++] != 0x08) {
		ecryptfs_printk(KERN_WARNING, "Unrecognizable packet\n");
		rc = -EINVAL;
		goto out;
	}

	/* skip the next 12 bytes */
	(*packet_size) += 12; /* We don't care about the filename or
			       * the timestamp */

	/* get the Tag 11 contents - tag_11_contents_size bytes */
	memcpy(contents, &data[(*packet_size)], (*tag_11_contents_size));
	(*packet_size) += (*tag_11_contents_size);

out:
	if (rc) {
		(*packet_size) = 0;
		(*tag_11_contents_size) = 0;
	}
	return rc;
}

/**
 * decrypt_session_key - Decrypt the session key with the given auth_tok.
 *
 * Returns Zero on success; non-zero error otherwise.
 */
static int decrypt_session_key(struct ecryptfs_auth_tok *auth_tok,
			       struct ecryptfs_crypt_stat *crypt_stat)
{
	struct ecryptfs_password *password_s_ptr;
	struct scatterlist src_sg[2], dst_sg[2];
	struct mutex *tfm_mutex = NULL;
	char *encrypted_session_key;
	char *session_key;
	struct blkcipher_desc desc = {
		.flags = CRYPTO_TFM_REQ_MAY_SLEEP
	};
	int rc = 0;

	password_s_ptr = &auth_tok->token.password;
	if (password_s_ptr->flags & ECRYPTFS_SESSION_KEY_ENCRYPTION_KEY_SET)
		ecryptfs_printk(KERN_DEBUG, "Session key encryption key "
				"set; skipping key generation\n");
	ecryptfs_printk(KERN_DEBUG, "Session key encryption key (size [%d])"
			":\n",
			password_s_ptr->session_key_encryption_key_bytes);
	if (ecryptfs_verbosity > 0)
		ecryptfs_dump_hex(password_s_ptr->session_key_encryption_key,
				  password_s_ptr->
				  session_key_encryption_key_bytes);
	if (!strcmp(crypt_stat->cipher,
		    crypt_stat->mount_crypt_stat->global_default_cipher_name)
	    && crypt_stat->mount_crypt_stat->global_key_tfm) {
		desc.tfm = crypt_stat->mount_crypt_stat->global_key_tfm;
		tfm_mutex = &crypt_stat->mount_crypt_stat->global_key_tfm_mutex;
	} else {
		char *full_alg_name;

		rc = ecryptfs_crypto_api_algify_cipher_name(&full_alg_name,
							    crypt_stat->cipher,
							    "ecb");
		if (rc)
			goto out;
		desc.tfm = crypto_alloc_blkcipher(full_alg_name, 0,
						  CRYPTO_ALG_ASYNC);
		kfree(full_alg_name);
		if (IS_ERR(desc.tfm)) {
			rc = PTR_ERR(desc.tfm);
			printk(KERN_ERR "Error allocating crypto context; "
			       "rc = [%d]\n", rc);
			goto out;
		}
		crypto_blkcipher_set_flags(desc.tfm, CRYPTO_TFM_REQ_WEAK_KEY);
	}
	if (tfm_mutex)
		mutex_lock(tfm_mutex);
	rc = crypto_blkcipher_setkey(desc.tfm,
				     password_s_ptr->session_key_encryption_key,
				     crypt_stat->key_size);
	if (rc < 0) {
		printk(KERN_ERR "Error setting key for crypto context\n");
		rc = -EINVAL;
		goto out_free_tfm;
	}
	/* TODO: virt_to_scatterlist */
	encrypted_session_key = (char *)__get_free_page(GFP_KERNEL);
	if (!encrypted_session_key) {
		ecryptfs_printk(KERN_ERR, "Out of memory\n");
		rc = -ENOMEM;
		goto out_free_tfm;
	}
	session_key = (char *)__get_free_page(GFP_KERNEL);
	if (!session_key) {
		kfree(encrypted_session_key);
		ecryptfs_printk(KERN_ERR, "Out of memory\n");
		rc = -ENOMEM;
		goto out_free_tfm;
	}
	memcpy(encrypted_session_key, auth_tok->session_key.encrypted_key,
	       auth_tok->session_key.encrypted_key_size);
	src_sg[0].page = virt_to_page(encrypted_session_key);
	src_sg[0].offset = 0;
	BUG_ON(auth_tok->session_key.encrypted_key_size > PAGE_CACHE_SIZE);
	src_sg[0].length = auth_tok->session_key.encrypted_key_size;
	dst_sg[0].page = virt_to_page(session_key);
	dst_sg[0].offset = 0;
	auth_tok->session_key.decrypted_key_size =
	    auth_tok->session_key.encrypted_key_size;
	dst_sg[0].length = auth_tok->session_key.encrypted_key_size;
	rc = crypto_blkcipher_decrypt(&desc, dst_sg, src_sg,
				      auth_tok->session_key.encrypted_key_size);
	if (rc) {
		printk(KERN_ERR "Error decrypting; rc = [%d]\n", rc);
		goto out_free_memory;
	}
	auth_tok->session_key.decrypted_key_size =
	    auth_tok->session_key.encrypted_key_size;
	memcpy(auth_tok->session_key.decrypted_key, session_key,
	       auth_tok->session_key.decrypted_key_size);
	auth_tok->session_key.flags |= ECRYPTFS_CONTAINS_DECRYPTED_KEY;
	memcpy(crypt_stat->key, auth_tok->session_key.decrypted_key,
	       auth_tok->session_key.decrypted_key_size);
	crypt_stat->flags |= ECRYPTFS_KEY_VALID;
	ecryptfs_printk(KERN_DEBUG, "Decrypted session key:\n");
	if (ecryptfs_verbosity > 0)
		ecryptfs_dump_hex(crypt_stat->key,
				  crypt_stat->key_size);
out_free_memory:
	memset(encrypted_session_key, 0, PAGE_CACHE_SIZE);
	free_page((unsigned long)encrypted_session_key);
	memset(session_key, 0, PAGE_CACHE_SIZE);
	free_page((unsigned long)session_key);
out_free_tfm:
	if (tfm_mutex)
		mutex_unlock(tfm_mutex);
	else
		crypto_free_blkcipher(desc.tfm);
out:
	return rc;
}

/**
 * ecryptfs_parse_packet_set
 * @dest: The header page in memory
 * @version: Version of file format, to guide parsing behavior
 *
 * Get crypt_stat to have the file's session key if the requisite key
 * is available to decrypt the session key.
 *
 * Returns Zero if a valid authentication token was retrieved and
 * processed; negative value for file not encrypted or for error
 * conditions.
 */
int ecryptfs_parse_packet_set(struct ecryptfs_crypt_stat *crypt_stat,
			      unsigned char *src,
			      struct dentry *ecryptfs_dentry)
{
	size_t i = 0;
	size_t found_auth_tok = 0;
	size_t next_packet_is_auth_tok_packet;
	char sig[ECRYPTFS_SIG_SIZE_HEX];
	struct list_head auth_tok_list;
	struct list_head *walker;
	struct ecryptfs_auth_tok *chosen_auth_tok = NULL;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat =
		&ecryptfs_superblock_to_private(
			ecryptfs_dentry->d_sb)->mount_crypt_stat;
	struct ecryptfs_auth_tok *candidate_auth_tok = NULL;
	size_t packet_size;
	struct ecryptfs_auth_tok *new_auth_tok;
	unsigned char sig_tmp_space[ECRYPTFS_SIG_SIZE];
	size_t tag_11_contents_size;
	size_t tag_11_packet_size;
	int rc = 0;

	INIT_LIST_HEAD(&auth_tok_list);
	/* Parse the header to find as many packets as we can, these will be
	 * added the our &auth_tok_list */
	next_packet_is_auth_tok_packet = 1;
	while (next_packet_is_auth_tok_packet) {
		size_t max_packet_size = ((PAGE_CACHE_SIZE - 8) - i);

		switch (src[i]) {
		case ECRYPTFS_TAG_3_PACKET_TYPE:
			rc = parse_tag_3_packet(crypt_stat,
						(unsigned char *)&src[i],
						&auth_tok_list, &new_auth_tok,
						&packet_size, max_packet_size);
			if (rc) {
				ecryptfs_printk(KERN_ERR, "Error parsing "
						"tag 3 packet\n");
				rc = -EIO;
				goto out_wipe_list;
			}
			i += packet_size;
			rc = parse_tag_11_packet((unsigned char *)&src[i],
						 sig_tmp_space,
						 ECRYPTFS_SIG_SIZE,
						 &tag_11_contents_size,
						 &tag_11_packet_size,
						 max_packet_size);
			if (rc) {
				ecryptfs_printk(KERN_ERR, "No valid "
						"(ecryptfs-specific) literal "
						"packet containing "
						"authentication token "
						"signature found after "
						"tag 3 packet\n");
				rc = -EIO;
				goto out_wipe_list;
			}
			i += tag_11_packet_size;
			if (ECRYPTFS_SIG_SIZE != tag_11_contents_size) {
				ecryptfs_printk(KERN_ERR, "Expected "
						"signature of size [%d]; "
						"read size [%d]\n",
						ECRYPTFS_SIG_SIZE,
						tag_11_contents_size);
				rc = -EIO;
				goto out_wipe_list;
			}
			ecryptfs_to_hex(new_auth_tok->token.password.signature,
					sig_tmp_space, tag_11_contents_size);
			new_auth_tok->token.password.signature[
				ECRYPTFS_PASSWORD_SIG_SIZE] = '\0';
			crypt_stat->flags |= ECRYPTFS_ENCRYPTED;
			break;
		case ECRYPTFS_TAG_1_PACKET_TYPE:
			rc = parse_tag_1_packet(crypt_stat,
						(unsigned char *)&src[i],
						&auth_tok_list, &new_auth_tok,
						&packet_size, max_packet_size);
			if (rc) {
				ecryptfs_printk(KERN_ERR, "Error parsing "
						"tag 1 packet\n");
				rc = -EIO;
				goto out_wipe_list;
			}
			i += packet_size;
			crypt_stat->flags |= ECRYPTFS_ENCRYPTED;
			break;
		case ECRYPTFS_TAG_11_PACKET_TYPE:
			ecryptfs_printk(KERN_WARNING, "Invalid packet set "
					"(Tag 11 not allowed by itself)\n");
			rc = -EIO;
			goto out_wipe_list;
			break;
		default:
			ecryptfs_printk(KERN_DEBUG, "No packet at offset "
					"[%d] of the file header; hex value of "
					"character is [0x%.2x]\n", i, src[i]);
			next_packet_is_auth_tok_packet = 0;
		}
	}
	if (list_empty(&auth_tok_list)) {
		rc = -EINVAL; /* Do not support non-encrypted files in
			       * the 0.1 release */
		goto out;
	}
	/* If we have a global auth tok, then we should try to use
	 * it */
	if (mount_crypt_stat->global_auth_tok) {
		memcpy(sig, mount_crypt_stat->global_auth_tok_sig,
		       ECRYPTFS_SIG_SIZE_HEX);
		chosen_auth_tok = mount_crypt_stat->global_auth_tok;
	} else
		BUG(); /* We should always have a global auth tok in
			* the 0.1 release */
	/* Scan list to see if our chosen_auth_tok works */
	list_for_each(walker, &auth_tok_list) {
		struct ecryptfs_auth_tok_list_item *auth_tok_list_item;
		auth_tok_list_item =
		    list_entry(walker, struct ecryptfs_auth_tok_list_item,
			       list);
		candidate_auth_tok = &auth_tok_list_item->auth_tok;
		if (unlikely(ecryptfs_verbosity > 0)) {
			ecryptfs_printk(KERN_DEBUG,
					"Considering cadidate auth tok:\n");
			ecryptfs_dump_auth_tok(candidate_auth_tok);
		}
		/* TODO: Replace ECRYPTFS_SIG_SIZE_HEX w/ dynamic value */
		if (candidate_auth_tok->token_type == ECRYPTFS_PASSWORD
		    && !strncmp(candidate_auth_tok->token.password.signature,
				sig, ECRYPTFS_SIG_SIZE_HEX)) {
			found_auth_tok = 1;
			goto leave_list;
			/* TODO: Transfer the common salt into the
			 * crypt_stat salt */
		} else if ((candidate_auth_tok->token_type
			    == ECRYPTFS_PRIVATE_KEY)
			   && !strncmp(candidate_auth_tok->token.private_key.signature,
				     sig, ECRYPTFS_SIG_SIZE_HEX)) {
			found_auth_tok = 1;
			goto leave_list;
		}
	}
	if (!found_auth_tok) {
		ecryptfs_printk(KERN_ERR, "Could not find authentication "
				"token on temporary list for sig [%.*s]\n",
				ECRYPTFS_SIG_SIZE_HEX, sig);
		rc = -EIO;
		goto out_wipe_list;
	}
leave_list:
	rc = -ENOTSUPP;
	if (candidate_auth_tok->token_type == ECRYPTFS_PRIVATE_KEY) {
		memcpy(&(candidate_auth_tok->token.private_key),
		       &(chosen_auth_tok->token.private_key),
		       sizeof(struct ecryptfs_private_key));
		rc = decrypt_pki_encrypted_session_key(mount_crypt_stat,
						       candidate_auth_tok,
						       crypt_stat);
	} else if (candidate_auth_tok->token_type == ECRYPTFS_PASSWORD) {
		memcpy(&(candidate_auth_tok->token.password),
		       &(chosen_auth_tok->token.password),
		       sizeof(struct ecryptfs_password));
		rc = decrypt_session_key(candidate_auth_tok, crypt_stat);
	}
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error decrypting the "
				"session key; rc = [%d]\n", rc);
		goto out_wipe_list;
	}
	rc = ecryptfs_compute_root_iv(crypt_stat);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error computing "
				"the root IV\n");
		goto out_wipe_list;
	}
	rc = ecryptfs_init_crypt_ctx(crypt_stat);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error initializing crypto "
				"context for cipher [%s]; rc = [%d]\n",
				crypt_stat->cipher, rc);
	}
out_wipe_list:
	wipe_auth_tok_list(&auth_tok_list);
out:
	return rc;
}
static int
pki_encrypt_session_key(struct ecryptfs_auth_tok *auth_tok,
			struct ecryptfs_crypt_stat *crypt_stat,
			struct ecryptfs_key_record *key_rec)
{
	struct ecryptfs_msg_ctx *msg_ctx = NULL;
	char *netlink_payload;
	size_t netlink_payload_length;
	struct ecryptfs_message *msg;
	int rc;

	rc = write_tag_66_packet(auth_tok->token.private_key.signature,
				 ecryptfs_code_for_cipher_string(crypt_stat),
				 crypt_stat, &netlink_payload,
				 &netlink_payload_length);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error generating tag 66 packet\n");
		goto out;
	}
	rc = ecryptfs_send_message(ecryptfs_transport, netlink_payload,
				   netlink_payload_length, &msg_ctx);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error sending netlink message\n");
		goto out;
	}
	rc = ecryptfs_wait_for_response(msg_ctx, &msg);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Failed to receive tag 67 packet "
				"from the user space daemon\n");
		rc = -EIO;
		goto out;
	}
	rc = parse_tag_67_packet(key_rec, msg);
	if (rc)
		ecryptfs_printk(KERN_ERR, "Error parsing tag 67 packet\n");
	kfree(msg);
out:
	if (netlink_payload)
		kfree(netlink_payload);
	return rc;
}
/**
 * write_tag_1_packet - Write an RFC2440-compatible tag 1 (public key) packet
 * @dest: Buffer into which to write the packet
 * @max: Maximum number of bytes that can be writtn
 * @packet_size: This function will write the number of bytes that end
 *               up constituting the packet; set to zero on error
 *
 * Returns zero on success; non-zero on error.
 */
static int
write_tag_1_packet(char *dest, size_t max, struct ecryptfs_auth_tok *auth_tok,
		   struct ecryptfs_crypt_stat *crypt_stat,
		   struct ecryptfs_mount_crypt_stat *mount_crypt_stat,
		   struct ecryptfs_key_record *key_rec, size_t *packet_size)
{
	size_t i;
	size_t encrypted_session_key_valid = 0;
	size_t key_rec_size;
	size_t packet_size_length;
	int rc = 0;

	(*packet_size) = 0;
	ecryptfs_from_hex(key_rec->sig, auth_tok->token.private_key.signature,
			  ECRYPTFS_SIG_SIZE);
	encrypted_session_key_valid = 0;
	for (i = 0; i < crypt_stat->key_size; i++)
		encrypted_session_key_valid |=
			auth_tok->session_key.encrypted_key[i];
	if (encrypted_session_key_valid) {
		memcpy(key_rec->enc_key,
		       auth_tok->session_key.encrypted_key,
		       auth_tok->session_key.encrypted_key_size);
		goto encrypted_session_key_set;
	}
	if (auth_tok->session_key.encrypted_key_size == 0)
		auth_tok->session_key.encrypted_key_size =
			auth_tok->token.private_key.key_size;
	rc = pki_encrypt_session_key(auth_tok, crypt_stat, key_rec);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Failed to encrypt session key "
				"via a pki");
		goto out;
	}
	if (ecryptfs_verbosity > 0) {
		ecryptfs_printk(KERN_DEBUG, "Encrypted key:\n");
		ecryptfs_dump_hex(key_rec->enc_key, key_rec->enc_key_size);
	}
encrypted_session_key_set:
	/* Now we have a valid key_rec.  Append it to the
	 * key_rec set. */
	key_rec_size = (sizeof(struct ecryptfs_key_record)
			- ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES
			+ (key_rec->enc_key_size));
	/* TODO: Include a packet size limit as a parameter to this
	 * function once we have multi-packet headers (for versions
	 * later than 0.1 */
	if (key_rec_size >= ECRYPTFS_MAX_KEYSET_SIZE) {
		ecryptfs_printk(KERN_ERR, "Keyset too large\n");
		rc = -EINVAL;
		goto out;
	}
	/*              ***** TAG 1 Packet Format *****
	 *    | version number                     | 1 byte       |
	 *    | key ID                             | 8 bytes      |
	 *    | public key algorithm               | 1 byte       |
	 *    | encrypted session key              | arbitrary    |
	 */
	if ((0x02 + ECRYPTFS_SIG_SIZE + key_rec->enc_key_size) >= max) {
		ecryptfs_printk(KERN_ERR,
				"Authentication token is too large\n");
		rc = -EINVAL;
		goto out;
	}
	dest[(*packet_size)++] = ECRYPTFS_TAG_1_PACKET_TYPE;
	/* This format is inspired by OpenPGP; see RFC 2440
	 * packet tag 1 */
	rc = write_packet_length(&dest[(*packet_size)],
				 (0x02 + ECRYPTFS_SIG_SIZE +
				 key_rec->enc_key_size),
				 &packet_size_length);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error generating tag 1 packet "
				"header; cannot generate packet length\n");
		goto out;
	}
	(*packet_size) += packet_size_length;
	dest[(*packet_size)++] = 0x03; /* version 3 */
	memcpy(&dest[(*packet_size)], key_rec->sig, ECRYPTFS_SIG_SIZE);
	(*packet_size) += ECRYPTFS_SIG_SIZE;
	dest[(*packet_size)++] = RFC2440_CIPHER_RSA;
	memcpy(&dest[(*packet_size)], key_rec->enc_key,
	       key_rec->enc_key_size);
	(*packet_size) += key_rec->enc_key_size;
out:
	if (rc)
		(*packet_size) = 0;
	return rc;
}

/**
 * write_tag_11_packet
 * @dest: Target into which Tag 11 packet is to be written
 * @max: Maximum packet length
 * @contents: Byte array of contents to copy in
 * @contents_length: Number of bytes in contents
 * @packet_length: Length of the Tag 11 packet written; zero on error
 *
 * Returns zero on success; non-zero on error.
 */
static int
write_tag_11_packet(char *dest, int max, char *contents, size_t contents_length,
		    size_t *packet_length)
{
	size_t packet_size_length;
	int rc = 0;

	(*packet_length) = 0;
	if ((13 + contents_length) > max) {
		rc = -EINVAL;
		ecryptfs_printk(KERN_ERR, "Packet length larger than "
				"maximum allowable\n");
		goto out;
	}
	/* General packet header */
	/* Packet tag */
	dest[(*packet_length)++] = ECRYPTFS_TAG_11_PACKET_TYPE;
	/* Packet length */
	rc = write_packet_length(&dest[(*packet_length)],
				 (13 + contents_length), &packet_size_length);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error generating tag 11 packet "
				"header; cannot generate packet length\n");
		goto out;
	}
	(*packet_length) += packet_size_length;
	/* Tag 11 specific */
	/* One-octet field that describes how the data is formatted */
	dest[(*packet_length)++] = 0x62; /* binary data */
	/* One-octet filename length followed by filename */
	dest[(*packet_length)++] = 8;
	memcpy(&dest[(*packet_length)], "_CONSOLE", 8);
	(*packet_length) += 8;
	/* Four-octet number indicating modification date */
	memset(&dest[(*packet_length)], 0x00, 4);
	(*packet_length) += 4;
	/* Remainder is literal data */
	memcpy(&dest[(*packet_length)], contents, contents_length);
	(*packet_length) += contents_length;
 out:
	if (rc)
		(*packet_length) = 0;
	return rc;
}

/**
 * write_tag_3_packet
 * @dest: Buffer into which to write the packet
 * @max: Maximum number of bytes that can be written
 * @auth_tok: Authentication token
 * @crypt_stat: The cryptographic context
 * @key_rec: encrypted key
 * @packet_size: This function will write the number of bytes that end
 *               up constituting the packet; set to zero on error
 *
 * Returns zero on success; non-zero on error.
 */
static int
write_tag_3_packet(char *dest, size_t max, struct ecryptfs_auth_tok *auth_tok,
		   struct ecryptfs_crypt_stat *crypt_stat,
		   struct ecryptfs_key_record *key_rec, size_t *packet_size)
{
	size_t i;
	size_t encrypted_session_key_valid = 0;
	char session_key_encryption_key[ECRYPTFS_MAX_KEY_BYTES];
	struct scatterlist dest_sg[2];
	struct scatterlist src_sg[2];
	struct mutex *tfm_mutex = NULL;
	size_t key_rec_size;
	size_t packet_size_length;
	size_t cipher_code;
	struct blkcipher_desc desc = {
		.tfm = NULL,
		.flags = CRYPTO_TFM_REQ_MAY_SLEEP
	};
	int rc = 0;

	(*packet_size) = 0;
	ecryptfs_from_hex(key_rec->sig, auth_tok->token.password.signature,
			  ECRYPTFS_SIG_SIZE);
	encrypted_session_key_valid = 0;
	for (i = 0; i < crypt_stat->key_size; i++)
		encrypted_session_key_valid |=
			auth_tok->session_key.encrypted_key[i];
	if (encrypted_session_key_valid) {
		memcpy(key_rec->enc_key,
		       auth_tok->session_key.encrypted_key,
		       auth_tok->session_key.encrypted_key_size);
		goto encrypted_session_key_set;
	}
	if (auth_tok->session_key.encrypted_key_size == 0)
		auth_tok->session_key.encrypted_key_size =
			crypt_stat->key_size;
	if (crypt_stat->key_size == 24
	    && strcmp("aes", crypt_stat->cipher) == 0) {
		memset((crypt_stat->key + 24), 0, 8);
		auth_tok->session_key.encrypted_key_size = 32;
	}
	key_rec->enc_key_size =
		auth_tok->session_key.encrypted_key_size;
	if (auth_tok->token.password.flags &
	    ECRYPTFS_SESSION_KEY_ENCRYPTION_KEY_SET) {
		ecryptfs_printk(KERN_DEBUG, "Using previously generated "
				"session key encryption key of size [%d]\n",
				auth_tok->token.password.
				session_key_encryption_key_bytes);
		memcpy(session_key_encryption_key,
		       auth_tok->token.password.session_key_encryption_key,
		       crypt_stat->key_size);
		ecryptfs_printk(KERN_DEBUG,
				"Cached session key " "encryption key: \n");
		if (ecryptfs_verbosity > 0)
			ecryptfs_dump_hex(session_key_encryption_key, 16);
	}
	if (unlikely(ecryptfs_verbosity > 0)) {
		ecryptfs_printk(KERN_DEBUG, "Session key encryption key:\n");
		ecryptfs_dump_hex(session_key_encryption_key, 16);
	}
	rc = virt_to_scatterlist(crypt_stat->key,
				 key_rec->enc_key_size, src_sg, 2);
	if (!rc) {
		ecryptfs_printk(KERN_ERR, "Error generating scatterlist "
				"for crypt_stat session key\n");
		rc = -ENOMEM;
		goto out;
	}
	rc = virt_to_scatterlist(key_rec->enc_key,
				 key_rec->enc_key_size, dest_sg, 2);
	if (!rc) {
		ecryptfs_printk(KERN_ERR, "Error generating scatterlist "
				"for crypt_stat encrypted session key\n");
		rc = -ENOMEM;
		goto out;
	}
	if (!strcmp(crypt_stat->cipher,
		    crypt_stat->mount_crypt_stat->global_default_cipher_name)
	    && crypt_stat->mount_crypt_stat->global_key_tfm) {
		desc.tfm = crypt_stat->mount_crypt_stat->global_key_tfm;
		tfm_mutex = &crypt_stat->mount_crypt_stat->global_key_tfm_mutex;
	} else {
		char *full_alg_name;

		rc = ecryptfs_crypto_api_algify_cipher_name(&full_alg_name,
							    crypt_stat->cipher,
							    "ecb");
		if (rc)
			goto out;
		desc.tfm = crypto_alloc_blkcipher(full_alg_name, 0,
						  CRYPTO_ALG_ASYNC);
		kfree(full_alg_name);
		if (IS_ERR(desc.tfm)) {
			rc = PTR_ERR(desc.tfm);
			ecryptfs_printk(KERN_ERR, "Could not initialize crypto "
					"context for cipher [%s]; rc = [%d]\n",
					crypt_stat->cipher, rc);
			goto out;
		}
		crypto_blkcipher_set_flags(desc.tfm, CRYPTO_TFM_REQ_WEAK_KEY);
	}
	if (tfm_mutex)
		mutex_lock(tfm_mutex);
	rc = crypto_blkcipher_setkey(desc.tfm, session_key_encryption_key,
				     crypt_stat->key_size);
	if (rc < 0) {
		if (tfm_mutex)
			mutex_unlock(tfm_mutex);
		ecryptfs_printk(KERN_ERR, "Error setting key for crypto "
				"context; rc = [%d]\n", rc);
		goto out;
	}
	rc = 0;
	ecryptfs_printk(KERN_DEBUG, "Encrypting [%d] bytes of the key\n",
			crypt_stat->key_size);
	rc = crypto_blkcipher_encrypt(&desc, dest_sg, src_sg,
				      (*key_rec).enc_key_size);
	if (rc) {
		printk(KERN_ERR "Error encrypting; rc = [%d]\n", rc);
		goto out;
	}
	if (tfm_mutex)
		mutex_unlock(tfm_mutex);
	ecryptfs_printk(KERN_DEBUG, "This should be the encrypted key:\n");
	if (ecryptfs_verbosity > 0)
		ecryptfs_dump_hex(key_rec->enc_key,
				  key_rec->enc_key_size);
encrypted_session_key_set:
	/* Now we have a valid key_rec.  Append it to the
	 * key_rec set. */
	key_rec_size = (sizeof(struct ecryptfs_key_record)
			- ECRYPTFS_MAX_ENCRYPTED_KEY_BYTES
			+ (key_rec->enc_key_size));
	/* TODO: Include a packet size limit as a parameter to this
	 * function once we have multi-packet headers (for versions
	 * later than 0.1 */
	if (key_rec_size >= ECRYPTFS_MAX_KEYSET_SIZE) {
		ecryptfs_printk(KERN_ERR, "Keyset too large\n");
		rc = -EINVAL;
		goto out;
	}
	/* TODO: Packet size limit */
	/* We have 5 bytes of surrounding packet data */
	if ((0x05 + ECRYPTFS_SALT_SIZE
	     + key_rec->enc_key_size) >= max) {
		ecryptfs_printk(KERN_ERR, "Authentication token is too "
				"large\n");
		rc = -EINVAL;
		goto out;
	}
	/* This format is inspired by OpenPGP; see RFC 2440
	 * packet tag 3 */
	dest[(*packet_size)++] = ECRYPTFS_TAG_3_PACKET_TYPE;
	/* ver+cipher+s2k+hash+salt+iter+enc_key */
	rc = write_packet_length(&dest[(*packet_size)],
				 (0x05 + ECRYPTFS_SALT_SIZE
				  + key_rec->enc_key_size),
				 &packet_size_length);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error generating tag 3 packet "
				"header; cannot generate packet length\n");
		goto out;
	}
	(*packet_size) += packet_size_length;
	dest[(*packet_size)++] = 0x04; /* version 4 */
	cipher_code = ecryptfs_code_for_cipher_string(crypt_stat);
	if (cipher_code == 0) {
		ecryptfs_printk(KERN_WARNING, "Unable to generate code for "
				"cipher [%s]\n", crypt_stat->cipher);
		rc = -EINVAL;
		goto out;
	}
	dest[(*packet_size)++] = cipher_code;
	dest[(*packet_size)++] = 0x03;	/* S2K */
	dest[(*packet_size)++] = 0x01;	/* MD5 (TODO: parameterize) */
	memcpy(&dest[(*packet_size)], auth_tok->token.password.salt,
	       ECRYPTFS_SALT_SIZE);
	(*packet_size) += ECRYPTFS_SALT_SIZE;	/* salt */
	dest[(*packet_size)++] = 0x60;	/* hash iterations (65536) */
	memcpy(&dest[(*packet_size)], key_rec->enc_key,
	       key_rec->enc_key_size);
	(*packet_size) += key_rec->enc_key_size;
out:
	if (desc.tfm && !tfm_mutex)
		crypto_free_blkcipher(desc.tfm);
	if (rc)
		(*packet_size) = 0;
	return rc;
}

struct kmem_cache *ecryptfs_key_record_cache;

/**
 * ecryptfs_generate_key_packet_set
 * @dest: Virtual address from which to write the key record set
 * @crypt_stat: The cryptographic context from which the
 *              authentication tokens will be retrieved
 * @ecryptfs_dentry: The dentry, used to retrieve the mount crypt stat
 *                   for the global parameters
 * @len: The amount written
 * @max: The maximum amount of data allowed to be written
 *
 * Generates a key packet set and writes it to the virtual address
 * passed in.
 *
 * Returns zero on success; non-zero on error.
 */
int
ecryptfs_generate_key_packet_set(char *dest_base,
				 struct ecryptfs_crypt_stat *crypt_stat,
				 struct dentry *ecryptfs_dentry, size_t *len,
				 size_t max)
{
	struct ecryptfs_auth_tok *auth_tok;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat =
		&ecryptfs_superblock_to_private(
			ecryptfs_dentry->d_sb)->mount_crypt_stat;
	size_t written;
	struct ecryptfs_key_record *key_rec;
	int rc = 0;

	(*len) = 0;
	key_rec = kmem_cache_alloc(ecryptfs_key_record_cache, GFP_KERNEL);
	if (!key_rec) {
		rc = -ENOMEM;
		goto out;
	}
	if (mount_crypt_stat->global_auth_tok) {
		auth_tok = mount_crypt_stat->global_auth_tok;
		if (auth_tok->token_type == ECRYPTFS_PASSWORD) {
			rc = write_tag_3_packet((dest_base + (*len)),
						max, auth_tok,
						crypt_stat, key_rec,
						&written);
			if (rc) {
				ecryptfs_printk(KERN_WARNING, "Error "
						"writing tag 3 packet\n");
				goto out_free;
			}
			(*len) += written;
			/* Write auth tok signature packet */
			rc = write_tag_11_packet(
				(dest_base + (*len)),
				(max - (*len)),
				key_rec->sig, ECRYPTFS_SIG_SIZE, &written);
			if (rc) {
				ecryptfs_printk(KERN_ERR, "Error writing "
						"auth tok signature packet\n");
				goto out_free;
			}
			(*len) += written;
		} else if (auth_tok->token_type == ECRYPTFS_PRIVATE_KEY) {
			rc = write_tag_1_packet(dest_base + (*len),
						max, auth_tok,
						crypt_stat,mount_crypt_stat,
						key_rec, &written);
			if (rc) {
				ecryptfs_printk(KERN_WARNING, "Error "
						"writing tag 1 packet\n");
				goto out_free;
			}
			(*len) += written;
		} else {
			ecryptfs_printk(KERN_WARNING, "Unsupported "
					"authentication token type\n");
			rc = -EINVAL;
			goto out_free;
		}
	} else
		BUG();
	if (likely((max - (*len)) > 0)) {
		dest_base[(*len)] = 0x00;
	} else {
		ecryptfs_printk(KERN_ERR, "Error writing boundary byte\n");
		rc = -EIO;
	}

out_free:
	kmem_cache_free(ecryptfs_key_record_cache, key_rec);
out:
	if (rc)
		(*len) = 0;
	return rc;
}
