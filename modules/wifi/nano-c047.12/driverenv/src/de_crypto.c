#include <linux/types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>

#include "driverenv.h"

#define TR_CRYPTO TR_WPA

static int
DriverEnvironment_HMAC(const char *algo, 
                       const void *key,
                       size_t key_len,
                       const void *data,
                       size_t data_len,
                       void *result,
                       size_t result_len)
{
   struct crypto_hash *tfm;
   struct scatterlist sg[1];
   struct hash_desc desc;
   int ret;

   tfm = crypto_alloc_hash(algo, 0, CRYPTO_ALG_ASYNC);
   if(IS_ERR(tfm)) {
      DE_TRACE_INT(TR_CRYPTO, "failed to allocate hash (%ld)\n", PTR_ERR(tfm));
      return WIFI_ENGINE_FAILURE;
   }

   if(crypto_hash_digestsize(tfm) > result_len) {
      crypto_free_hash(tfm);
      return WIFI_ENGINE_FAILURE_INVALID_LENGTH;
   }

   sg_init_one(&sg[0], data, data_len);

   crypto_hash_clear_flags(tfm, ~0);

   ret = crypto_hash_setkey(tfm, key, key_len);
   if(ret != 0) {
      DE_TRACE_INT(TR_CRYPTO, "failed to set key (%d)\n", ret);
      crypto_free_hash(tfm);
      return WIFI_ENGINE_FAILURE;
   }

   desc.tfm = tfm;
   desc.flags = 0;

   ret = crypto_hash_digest(&desc, sg, data_len, result);
   if(ret != 0) {
      DE_TRACE_INT(TR_CRYPTO, "faild to digest (%d)\n", ret);
      crypto_free_hash(tfm);
      return WIFI_ENGINE_FAILURE;
   }

   crypto_free_hash(tfm);

   return WIFI_ENGINE_SUCCESS;
}
                           
int
DriverEnvironment_HMAC_MD5(const void *key,
                           size_t key_len,
                           const void *data,
                           size_t data_len,
                           void *result,
                           size_t result_len)
{
   return DriverEnvironment_HMAC("hmac(md5)", 
                                 key, key_len, 
                                 data, data_len, 
                                 result, result_len);
}

int
DriverEnvironment_HMAC_SHA1(const void *key,
                            size_t key_len,
                            const void *data,
                            size_t data_len,
                            void *result,
                            size_t result_len)
{
   return DriverEnvironment_HMAC("hmac(sha1)", 
                                 key, key_len, 
                                 data, data_len, 
                                 result, result_len);
}
