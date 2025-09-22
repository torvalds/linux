/* $OpenBSD: cms.h,v 1.4 2024/07/09 06:12:45 beck Exp $ */
/*
 * Copyright (c) 2023 Bob Beck <beck@openbsd.org>
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

#ifndef _LIBCRYPTO_CMS_H
#define _LIBCRYPTO_CMS_H

#ifndef _MSC_VER
#include_next <openssl/cms.h>
#else
#include "../include/openssl/cms.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(CMS_ContentInfo_new);
LCRYPTO_USED(CMS_ContentInfo_free);
LCRYPTO_USED(d2i_CMS_ContentInfo);
LCRYPTO_USED(i2d_CMS_ContentInfo);
LCRYPTO_USED(CMS_ReceiptRequest_new);
LCRYPTO_USED(CMS_ReceiptRequest_free);
LCRYPTO_USED(d2i_CMS_ReceiptRequest);
LCRYPTO_USED(i2d_CMS_ReceiptRequest);
LCRYPTO_USED(CMS_ContentInfo_print_ctx);
LCRYPTO_USED(CMS_get0_type);
LCRYPTO_USED(CMS_get_version);
LCRYPTO_USED(CMS_SignerInfo_get_version);
LCRYPTO_USED(CMS_dataInit);
LCRYPTO_USED(CMS_dataFinal);
LCRYPTO_USED(CMS_get0_content);
LCRYPTO_USED(CMS_is_detached);
LCRYPTO_USED(CMS_set_detached);
LCRYPTO_USED(PEM_read_bio_CMS);
LCRYPTO_USED(PEM_read_CMS);
LCRYPTO_USED(PEM_write_bio_CMS);
LCRYPTO_USED(PEM_write_CMS);
LCRYPTO_USED(CMS_stream);
LCRYPTO_USED(d2i_CMS_bio);
LCRYPTO_USED(i2d_CMS_bio);
LCRYPTO_USED(BIO_new_CMS);
LCRYPTO_USED(i2d_CMS_bio_stream);
LCRYPTO_USED(PEM_write_bio_CMS_stream);
LCRYPTO_USED(SMIME_read_CMS);
LCRYPTO_USED(SMIME_write_CMS);
LCRYPTO_USED(CMS_final);
LCRYPTO_USED(CMS_sign);
LCRYPTO_USED(CMS_sign_receipt);
LCRYPTO_USED(CMS_data);
LCRYPTO_USED(CMS_data_create);
LCRYPTO_USED(CMS_digest_verify);
LCRYPTO_USED(CMS_digest_create);
LCRYPTO_USED(CMS_EncryptedData_decrypt);
LCRYPTO_USED(CMS_EncryptedData_encrypt);
LCRYPTO_USED(CMS_EncryptedData_set1_key);
LCRYPTO_USED(CMS_verify);
LCRYPTO_USED(CMS_verify_receipt);
LCRYPTO_USED(CMS_get0_signers);
LCRYPTO_USED(CMS_encrypt);
LCRYPTO_USED(CMS_decrypt);
LCRYPTO_USED(CMS_decrypt_set1_pkey);
LCRYPTO_USED(CMS_decrypt_set1_key);
LCRYPTO_USED(CMS_decrypt_set1_password);
LCRYPTO_USED(CMS_get0_RecipientInfos);
LCRYPTO_USED(CMS_RecipientInfo_type);
LCRYPTO_USED(CMS_RecipientInfo_get0_pkey_ctx);
LCRYPTO_USED(CMS_EnvelopedData_create);
LCRYPTO_USED(CMS_add1_recipient_cert);
LCRYPTO_USED(CMS_RecipientInfo_set0_pkey);
LCRYPTO_USED(CMS_RecipientInfo_ktri_cert_cmp);
LCRYPTO_USED(CMS_RecipientInfo_ktri_get0_algs);
LCRYPTO_USED(CMS_RecipientInfo_ktri_get0_signer_id);
LCRYPTO_USED(CMS_add0_recipient_key);
LCRYPTO_USED(CMS_RecipientInfo_kekri_get0_id);
LCRYPTO_USED(CMS_RecipientInfo_set0_key);
LCRYPTO_USED(CMS_RecipientInfo_kekri_id_cmp);
LCRYPTO_USED(CMS_RecipientInfo_set0_password);
LCRYPTO_USED(CMS_add0_recipient_password);
LCRYPTO_USED(CMS_RecipientInfo_decrypt);
LCRYPTO_USED(CMS_RecipientInfo_encrypt);
LCRYPTO_USED(CMS_uncompress);
LCRYPTO_USED(CMS_compress);
LCRYPTO_USED(CMS_set1_eContentType);
LCRYPTO_USED(CMS_get0_eContentType);
LCRYPTO_USED(CMS_add0_CertificateChoices);
LCRYPTO_USED(CMS_add0_cert);
LCRYPTO_USED(CMS_add1_cert);
LCRYPTO_USED(CMS_get1_certs);
LCRYPTO_USED(CMS_add0_RevocationInfoChoice);
LCRYPTO_USED(CMS_add0_crl);
LCRYPTO_USED(CMS_add1_crl);
LCRYPTO_USED(CMS_get1_crls);
LCRYPTO_USED(CMS_SignedData_init);
LCRYPTO_USED(CMS_add1_signer);
LCRYPTO_USED(CMS_SignerInfo_get0_pkey_ctx);
LCRYPTO_USED(CMS_SignerInfo_get0_md_ctx);
LCRYPTO_USED(CMS_get0_SignerInfos);
LCRYPTO_USED(CMS_SignerInfo_set1_signer_cert);
LCRYPTO_USED(CMS_SignerInfo_get0_signer_id);
LCRYPTO_USED(CMS_SignerInfo_cert_cmp);
LCRYPTO_USED(CMS_set1_signers_certs);
LCRYPTO_USED(CMS_SignerInfo_get0_algs);
LCRYPTO_USED(CMS_SignerInfo_get0_signature);
LCRYPTO_USED(CMS_SignerInfo_sign);
LCRYPTO_USED(CMS_SignerInfo_verify);
LCRYPTO_USED(CMS_SignerInfo_verify_content);
LCRYPTO_USED(CMS_add_smimecap);
LCRYPTO_USED(CMS_add_simple_smimecap);
LCRYPTO_USED(CMS_add_standard_smimecap);
LCRYPTO_USED(CMS_signed_get_attr_count);
LCRYPTO_USED(CMS_signed_get_attr_by_NID);
LCRYPTO_USED(CMS_signed_get_attr_by_OBJ);
LCRYPTO_USED(CMS_signed_get_attr);
LCRYPTO_USED(CMS_signed_delete_attr);
LCRYPTO_USED(CMS_signed_add1_attr);
LCRYPTO_USED(CMS_signed_add1_attr_by_OBJ);
LCRYPTO_USED(CMS_signed_add1_attr_by_NID);
LCRYPTO_USED(CMS_signed_add1_attr_by_txt);
LCRYPTO_USED(CMS_signed_get0_data_by_OBJ);
LCRYPTO_USED(CMS_unsigned_get_attr_count);
LCRYPTO_USED(CMS_unsigned_get_attr_by_NID);
LCRYPTO_USED(CMS_unsigned_get_attr_by_OBJ);
LCRYPTO_USED(CMS_unsigned_get_attr);
LCRYPTO_USED(CMS_unsigned_delete_attr);
LCRYPTO_USED(CMS_unsigned_add1_attr);
LCRYPTO_USED(CMS_unsigned_add1_attr_by_OBJ);
LCRYPTO_USED(CMS_unsigned_add1_attr_by_NID);
LCRYPTO_USED(CMS_unsigned_add1_attr_by_txt);
LCRYPTO_USED(CMS_unsigned_get0_data_by_OBJ);
LCRYPTO_USED(CMS_get1_ReceiptRequest);
LCRYPTO_USED(CMS_ReceiptRequest_create0);
LCRYPTO_USED(CMS_add1_ReceiptRequest);
LCRYPTO_USED(CMS_ReceiptRequest_get0_values);
LCRYPTO_USED(CMS_RecipientInfo_kari_get0_alg);
LCRYPTO_USED(CMS_RecipientInfo_kari_get0_reks);
LCRYPTO_USED(CMS_RecipientInfo_kari_get0_orig_id);
LCRYPTO_USED(CMS_RecipientInfo_kari_orig_id_cmp);
LCRYPTO_USED(CMS_RecipientEncryptedKey_get0_id);
LCRYPTO_USED(CMS_RecipientEncryptedKey_cert_cmp);
LCRYPTO_USED(CMS_RecipientInfo_kari_set0_pkey);
LCRYPTO_USED(CMS_RecipientInfo_kari_get0_ctx);
LCRYPTO_USED(CMS_RecipientInfo_kari_decrypt);
LCRYPTO_USED(CMS_SharedInfo_encode);
LCRYPTO_USED(ERR_load_CMS_strings);
#if defined(LIBRESSL_NAMESPACE)
extern LCRYPTO_USED(CMS_ContentInfo_it);
extern LCRYPTO_USED(CMS_ReceiptRequest_it);
#endif

#endif /* _LIBCRYPTO_CMS_H */
