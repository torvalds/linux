#! /bin/sh

export MAKE_FLAGS='-j4'
export EXPORTED_FUNCTIONS_STANDARD='["_malloc","_free","_crypto_aead_chacha20poly1305_abytes","_crypto_aead_chacha20poly1305_decrypt","_crypto_aead_chacha20poly1305_decrypt_detached","_crypto_aead_chacha20poly1305_encrypt","_crypto_aead_chacha20poly1305_encrypt_detached","_crypto_aead_chacha20poly1305_ietf_abytes","_crypto_aead_chacha20poly1305_ietf_decrypt","_crypto_aead_chacha20poly1305_ietf_decrypt_detached","_crypto_aead_chacha20poly1305_ietf_encrypt","_crypto_aead_chacha20poly1305_ietf_encrypt_detached","_crypto_aead_chacha20poly1305_ietf_keybytes","_crypto_aead_chacha20poly1305_ietf_keygen","_crypto_aead_chacha20poly1305_ietf_npubbytes","_crypto_aead_chacha20poly1305_ietf_nsecbytes","_crypto_aead_chacha20poly1305_keybytes","_crypto_aead_chacha20poly1305_keygen","_crypto_aead_chacha20poly1305_npubbytes","_crypto_aead_chacha20poly1305_nsecbytes","_crypto_aead_xchacha20poly1305_ietf_abytes","_crypto_aead_xchacha20poly1305_ietf_decrypt","_crypto_aead_xchacha20poly1305_ietf_decrypt_detached","_crypto_aead_xchacha20poly1305_ietf_encrypt","_crypto_aead_xchacha20poly1305_ietf_encrypt_detached","_crypto_aead_xchacha20poly1305_ietf_keybytes","_crypto_aead_xchacha20poly1305_ietf_keygen","_crypto_aead_xchacha20poly1305_ietf_npubbytes","_crypto_aead_xchacha20poly1305_ietf_nsecbytes","_crypto_auth","_crypto_auth_bytes","_crypto_auth_keybytes","_crypto_auth_keygen","_crypto_auth_verify","_crypto_box_beforenm","_crypto_box_beforenmbytes","_crypto_box_detached","_crypto_box_detached_afternm","_crypto_box_easy","_crypto_box_easy_afternm","_crypto_box_keypair","_crypto_box_macbytes","_crypto_box_noncebytes","_crypto_box_open_detached","_crypto_box_open_detached_afternm","_crypto_box_open_easy","_crypto_box_open_easy_afternm","_crypto_box_publickeybytes","_crypto_box_seal","_crypto_box_seal_open","_crypto_box_sealbytes","_crypto_box_secretkeybytes","_crypto_box_seed_keypair","_crypto_box_seedbytes","_crypto_core_hchacha20","_crypto_core_hchacha20_constbytes","_crypto_core_hchacha20_inputbytes","_crypto_core_hchacha20_keybytes","_crypto_core_hchacha20_outputbytes","_crypto_generichash","_crypto_generichash_bytes","_crypto_generichash_bytes_max","_crypto_generichash_bytes_min","_crypto_generichash_final","_crypto_generichash_init","_crypto_generichash_keybytes","_crypto_generichash_keybytes_max","_crypto_generichash_keybytes_min","_crypto_generichash_keygen","_crypto_generichash_statebytes","_crypto_generichash_update","_crypto_hash","_crypto_hash_bytes","_crypto_kdf_bytes_max","_crypto_kdf_bytes_min","_crypto_kdf_contextbytes","_crypto_kdf_derive_from_key","_crypto_kdf_keybytes","_crypto_kdf_keygen","_crypto_kx_client_session_keys","_crypto_kx_keypair","_crypto_kx_publickeybytes","_crypto_kx_secretkeybytes","_crypto_kx_seed_keypair","_crypto_kx_seedbytes","_crypto_kx_server_session_keys","_crypto_kx_sessionkeybytes","_crypto_pwhash","_crypto_pwhash_alg_argon2i13","_crypto_pwhash_alg_argon2id13","_crypto_pwhash_alg_default","_crypto_pwhash_bytes_max","_crypto_pwhash_bytes_min","_crypto_pwhash_memlimit_interactive","_crypto_pwhash_memlimit_max","_crypto_pwhash_memlimit_min","_crypto_pwhash_memlimit_moderate","_crypto_pwhash_memlimit_sensitive","_crypto_pwhash_opslimit_interactive","_crypto_pwhash_opslimit_max","_crypto_pwhash_opslimit_min","_crypto_pwhash_opslimit_moderate","_crypto_pwhash_opslimit_sensitive","_crypto_pwhash_passwd_max","_crypto_pwhash_passwd_min","_crypto_pwhash_primitive","_crypto_pwhash_saltbytes","_crypto_pwhash_str","_crypto_pwhash_str_alg","_crypto_pwhash_str_needs_rehash","_crypto_pwhash_str_verify","_crypto_pwhash_strbytes","_crypto_pwhash_strprefix","_crypto_scalarmult","_crypto_scalarmult_base","_crypto_scalarmult_bytes","_crypto_scalarmult_scalarbytes","_crypto_secretbox_detached","_crypto_secretbox_easy","_crypto_secretbox_keybytes","_crypto_secretbox_keygen","_crypto_secretbox_macbytes","_crypto_secretbox_noncebytes","_crypto_secretbox_open_detached","_crypto_secretbox_open_easy","_crypto_secretstream_xchacha20poly1305_abytes","_crypto_secretstream_xchacha20poly1305_headerbytes","_crypto_secretstream_xchacha20poly1305_init_pull","_crypto_secretstream_xchacha20poly1305_init_push","_crypto_secretstream_xchacha20poly1305_keybytes","_crypto_secretstream_xchacha20poly1305_keygen","_crypto_secretstream_xchacha20poly1305_messagebytes_max","_crypto_secretstream_xchacha20poly1305_pull","_crypto_secretstream_xchacha20poly1305_push","_crypto_secretstream_xchacha20poly1305_rekey","_crypto_secretstream_xchacha20poly1305_statebytes","_crypto_secretstream_xchacha20poly1305_tag_final","_crypto_secretstream_xchacha20poly1305_tag_message","_crypto_secretstream_xchacha20poly1305_tag_push","_crypto_secretstream_xchacha20poly1305_tag_rekey","_crypto_shorthash","_crypto_shorthash_bytes","_crypto_shorthash_keybytes","_crypto_shorthash_keygen","_crypto_sign","_crypto_sign_bytes","_crypto_sign_detached","_crypto_sign_ed25519_pk_to_curve25519","_crypto_sign_ed25519_sk_to_curve25519","_crypto_sign_final_create","_crypto_sign_final_verify","_crypto_sign_init","_crypto_sign_keypair","_crypto_sign_open","_crypto_sign_publickeybytes","_crypto_sign_secretkeybytes","_crypto_sign_seed_keypair","_crypto_sign_seedbytes","_crypto_sign_statebytes","_crypto_sign_update","_crypto_sign_verify_detached","_crypto_stream_keygen","_randombytes","_randombytes_buf","_randombytes_buf_deterministic","_randombytes_close","_randombytes_random","_randombytes_seedbytes","_randombytes_stir","_randombytes_uniform","_sodium_base642bin","_sodium_base64_encoded_len","_sodium_bin2base64","_sodium_bin2hex","_sodium_hex2bin","_sodium_init","_sodium_library_minimal","_sodium_library_version_major","_sodium_library_version_minor","_sodium_pad","_sodium_unpad","_sodium_version_string"]'
export EXPORTED_FUNCTIONS_SUMO='["_malloc","_free","_crypto_aead_chacha20poly1305_abytes","_crypto_aead_chacha20poly1305_decrypt","_crypto_aead_chacha20poly1305_decrypt_detached","_crypto_aead_chacha20poly1305_encrypt","_crypto_aead_chacha20poly1305_encrypt_detached","_crypto_aead_chacha20poly1305_ietf_abytes","_crypto_aead_chacha20poly1305_ietf_decrypt","_crypto_aead_chacha20poly1305_ietf_decrypt_detached","_crypto_aead_chacha20poly1305_ietf_encrypt","_crypto_aead_chacha20poly1305_ietf_encrypt_detached","_crypto_aead_chacha20poly1305_ietf_keybytes","_crypto_aead_chacha20poly1305_ietf_keygen","_crypto_aead_chacha20poly1305_ietf_npubbytes","_crypto_aead_chacha20poly1305_ietf_nsecbytes","_crypto_aead_chacha20poly1305_keybytes","_crypto_aead_chacha20poly1305_keygen","_crypto_aead_chacha20poly1305_npubbytes","_crypto_aead_chacha20poly1305_nsecbytes","_crypto_aead_xchacha20poly1305_ietf_abytes","_crypto_aead_xchacha20poly1305_ietf_decrypt","_crypto_aead_xchacha20poly1305_ietf_decrypt_detached","_crypto_aead_xchacha20poly1305_ietf_encrypt","_crypto_aead_xchacha20poly1305_ietf_encrypt_detached","_crypto_aead_xchacha20poly1305_ietf_keybytes","_crypto_aead_xchacha20poly1305_ietf_keygen","_crypto_aead_xchacha20poly1305_ietf_npubbytes","_crypto_aead_xchacha20poly1305_ietf_nsecbytes","_crypto_auth","_crypto_auth_bytes","_crypto_auth_hmacsha256","_crypto_auth_hmacsha256_bytes","_crypto_auth_hmacsha256_final","_crypto_auth_hmacsha256_init","_crypto_auth_hmacsha256_keybytes","_crypto_auth_hmacsha256_keygen","_crypto_auth_hmacsha256_statebytes","_crypto_auth_hmacsha256_update","_crypto_auth_hmacsha256_verify","_crypto_auth_hmacsha512","_crypto_auth_hmacsha512256","_crypto_auth_hmacsha512256_bytes","_crypto_auth_hmacsha512256_final","_crypto_auth_hmacsha512256_init","_crypto_auth_hmacsha512256_keybytes","_crypto_auth_hmacsha512256_keygen","_crypto_auth_hmacsha512256_statebytes","_crypto_auth_hmacsha512256_update","_crypto_auth_hmacsha512256_verify","_crypto_auth_hmacsha512_bytes","_crypto_auth_hmacsha512_final","_crypto_auth_hmacsha512_init","_crypto_auth_hmacsha512_keybytes","_crypto_auth_hmacsha512_keygen","_crypto_auth_hmacsha512_statebytes","_crypto_auth_hmacsha512_update","_crypto_auth_hmacsha512_verify","_crypto_auth_keybytes","_crypto_auth_keygen","_crypto_auth_primitive","_crypto_auth_verify","_crypto_box","_crypto_box_afternm","_crypto_box_beforenm","_crypto_box_beforenmbytes","_crypto_box_boxzerobytes","_crypto_box_curve25519xchacha20poly1305_beforenm","_crypto_box_curve25519xchacha20poly1305_beforenmbytes","_crypto_box_curve25519xchacha20poly1305_detached","_crypto_box_curve25519xchacha20poly1305_detached_afternm","_crypto_box_curve25519xchacha20poly1305_easy","_crypto_box_curve25519xchacha20poly1305_easy_afternm","_crypto_box_curve25519xchacha20poly1305_keypair","_crypto_box_curve25519xchacha20poly1305_macbytes","_crypto_box_curve25519xchacha20poly1305_noncebytes","_crypto_box_curve25519xchacha20poly1305_open_detached","_crypto_box_curve25519xchacha20poly1305_open_detached_afternm","_crypto_box_curve25519xchacha20poly1305_open_easy","_crypto_box_curve25519xchacha20poly1305_open_easy_afternm","_crypto_box_curve25519xchacha20poly1305_publickeybytes","_crypto_box_curve25519xchacha20poly1305_seal","_crypto_box_curve25519xchacha20poly1305_seal_open","_crypto_box_curve25519xchacha20poly1305_sealbytes","_crypto_box_curve25519xchacha20poly1305_secretkeybytes","_crypto_box_curve25519xchacha20poly1305_seed_keypair","_crypto_box_curve25519xchacha20poly1305_seedbytes","_crypto_box_curve25519xsalsa20poly1305","_crypto_box_curve25519xsalsa20poly1305_afternm","_crypto_box_curve25519xsalsa20poly1305_beforenm","_crypto_box_curve25519xsalsa20poly1305_beforenmbytes","_crypto_box_curve25519xsalsa20poly1305_boxzerobytes","_crypto_box_curve25519xsalsa20poly1305_keypair","_crypto_box_curve25519xsalsa20poly1305_macbytes","_crypto_box_curve25519xsalsa20poly1305_noncebytes","_crypto_box_curve25519xsalsa20poly1305_open","_crypto_box_curve25519xsalsa20poly1305_open_afternm","_crypto_box_curve25519xsalsa20poly1305_publickeybytes","_crypto_box_curve25519xsalsa20poly1305_secretkeybytes","_crypto_box_curve25519xsalsa20poly1305_seed_keypair","_crypto_box_curve25519xsalsa20poly1305_seedbytes","_crypto_box_curve25519xsalsa20poly1305_zerobytes","_crypto_box_detached","_crypto_box_detached_afternm","_crypto_box_easy","_crypto_box_easy_afternm","_crypto_box_keypair","_crypto_box_macbytes","_crypto_box_noncebytes","_crypto_box_open","_crypto_box_open_afternm","_crypto_box_open_detached","_crypto_box_open_detached_afternm","_crypto_box_open_easy","_crypto_box_open_easy_afternm","_crypto_box_primitive","_crypto_box_publickeybytes","_crypto_box_seal","_crypto_box_seal_open","_crypto_box_sealbytes","_crypto_box_secretkeybytes","_crypto_box_seed_keypair","_crypto_box_seedbytes","_crypto_box_zerobytes","_crypto_core_ed25519_add","_crypto_core_ed25519_bytes","_crypto_core_ed25519_from_uniform","_crypto_core_ed25519_is_valid_point","_crypto_core_ed25519_sub","_crypto_core_ed25519_uniformbytes","_crypto_core_hchacha20","_crypto_core_hchacha20_constbytes","_crypto_core_hchacha20_inputbytes","_crypto_core_hchacha20_keybytes","_crypto_core_hchacha20_outputbytes","_crypto_core_hsalsa20","_crypto_core_hsalsa20_constbytes","_crypto_core_hsalsa20_inputbytes","_crypto_core_hsalsa20_keybytes","_crypto_core_hsalsa20_outputbytes","_crypto_core_salsa20","_crypto_core_salsa2012","_crypto_core_salsa2012_constbytes","_crypto_core_salsa2012_inputbytes","_crypto_core_salsa2012_keybytes","_crypto_core_salsa2012_outputbytes","_crypto_core_salsa208","_crypto_core_salsa208_constbytes","_crypto_core_salsa208_inputbytes","_crypto_core_salsa208_keybytes","_crypto_core_salsa208_outputbytes","_crypto_core_salsa20_constbytes","_crypto_core_salsa20_inputbytes","_crypto_core_salsa20_keybytes","_crypto_core_salsa20_outputbytes","_crypto_generichash","_crypto_generichash_blake2b","_crypto_generichash_blake2b_bytes","_crypto_generichash_blake2b_bytes_max","_crypto_generichash_blake2b_bytes_min","_crypto_generichash_blake2b_final","_crypto_generichash_blake2b_init","_crypto_generichash_blake2b_init_salt_personal","_crypto_generichash_blake2b_keybytes","_crypto_generichash_blake2b_keybytes_max","_crypto_generichash_blake2b_keybytes_min","_crypto_generichash_blake2b_keygen","_crypto_generichash_blake2b_personalbytes","_crypto_generichash_blake2b_salt_personal","_crypto_generichash_blake2b_saltbytes","_crypto_generichash_blake2b_statebytes","_crypto_generichash_blake2b_update","_crypto_generichash_bytes","_crypto_generichash_bytes_max","_crypto_generichash_bytes_min","_crypto_generichash_final","_crypto_generichash_init","_crypto_generichash_keybytes","_crypto_generichash_keybytes_max","_crypto_generichash_keybytes_min","_crypto_generichash_keygen","_crypto_generichash_primitive","_crypto_generichash_statebytes","_crypto_generichash_update","_crypto_hash","_crypto_hash_bytes","_crypto_hash_primitive","_crypto_hash_sha256","_crypto_hash_sha256_bytes","_crypto_hash_sha256_final","_crypto_hash_sha256_init","_crypto_hash_sha256_statebytes","_crypto_hash_sha256_update","_crypto_hash_sha512","_crypto_hash_sha512_bytes","_crypto_hash_sha512_final","_crypto_hash_sha512_init","_crypto_hash_sha512_statebytes","_crypto_hash_sha512_update","_crypto_kdf_blake2b_bytes_max","_crypto_kdf_blake2b_bytes_min","_crypto_kdf_blake2b_contextbytes","_crypto_kdf_blake2b_derive_from_key","_crypto_kdf_blake2b_keybytes","_crypto_kdf_bytes_max","_crypto_kdf_bytes_min","_crypto_kdf_contextbytes","_crypto_kdf_derive_from_key","_crypto_kdf_keybytes","_crypto_kdf_keygen","_crypto_kdf_primitive","_crypto_kx_client_session_keys","_crypto_kx_keypair","_crypto_kx_primitive","_crypto_kx_publickeybytes","_crypto_kx_secretkeybytes","_crypto_kx_seed_keypair","_crypto_kx_seedbytes","_crypto_kx_server_session_keys","_crypto_kx_sessionkeybytes","_crypto_onetimeauth","_crypto_onetimeauth_bytes","_crypto_onetimeauth_final","_crypto_onetimeauth_init","_crypto_onetimeauth_keybytes","_crypto_onetimeauth_keygen","_crypto_onetimeauth_poly1305","_crypto_onetimeauth_poly1305_bytes","_crypto_onetimeauth_poly1305_final","_crypto_onetimeauth_poly1305_init","_crypto_onetimeauth_poly1305_keybytes","_crypto_onetimeauth_poly1305_keygen","_crypto_onetimeauth_poly1305_statebytes","_crypto_onetimeauth_poly1305_update","_crypto_onetimeauth_poly1305_verify","_crypto_onetimeauth_primitive","_crypto_onetimeauth_statebytes","_crypto_onetimeauth_update","_crypto_onetimeauth_verify","_crypto_pwhash","_crypto_pwhash_alg_argon2i13","_crypto_pwhash_alg_argon2id13","_crypto_pwhash_alg_default","_crypto_pwhash_argon2i","_crypto_pwhash_argon2i_alg_argon2i13","_crypto_pwhash_argon2i_bytes_max","_crypto_pwhash_argon2i_bytes_min","_crypto_pwhash_argon2i_memlimit_interactive","_crypto_pwhash_argon2i_memlimit_max","_crypto_pwhash_argon2i_memlimit_min","_crypto_pwhash_argon2i_memlimit_moderate","_crypto_pwhash_argon2i_memlimit_sensitive","_crypto_pwhash_argon2i_opslimit_interactive","_crypto_pwhash_argon2i_opslimit_max","_crypto_pwhash_argon2i_opslimit_min","_crypto_pwhash_argon2i_opslimit_moderate","_crypto_pwhash_argon2i_opslimit_sensitive","_crypto_pwhash_argon2i_passwd_max","_crypto_pwhash_argon2i_passwd_min","_crypto_pwhash_argon2i_saltbytes","_crypto_pwhash_argon2i_str","_crypto_pwhash_argon2i_str_needs_rehash","_crypto_pwhash_argon2i_str_verify","_crypto_pwhash_argon2i_strbytes","_crypto_pwhash_argon2i_strprefix","_crypto_pwhash_argon2id","_crypto_pwhash_argon2id_alg_argon2id13","_crypto_pwhash_argon2id_bytes_max","_crypto_pwhash_argon2id_bytes_min","_crypto_pwhash_argon2id_memlimit_interactive","_crypto_pwhash_argon2id_memlimit_max","_crypto_pwhash_argon2id_memlimit_min","_crypto_pwhash_argon2id_memlimit_moderate","_crypto_pwhash_argon2id_memlimit_sensitive","_crypto_pwhash_argon2id_opslimit_interactive","_crypto_pwhash_argon2id_opslimit_max","_crypto_pwhash_argon2id_opslimit_min","_crypto_pwhash_argon2id_opslimit_moderate","_crypto_pwhash_argon2id_opslimit_sensitive","_crypto_pwhash_argon2id_passwd_max","_crypto_pwhash_argon2id_passwd_min","_crypto_pwhash_argon2id_saltbytes","_crypto_pwhash_argon2id_str","_crypto_pwhash_argon2id_str_needs_rehash","_crypto_pwhash_argon2id_str_verify","_crypto_pwhash_argon2id_strbytes","_crypto_pwhash_argon2id_strprefix","_crypto_pwhash_bytes_max","_crypto_pwhash_bytes_min","_crypto_pwhash_memlimit_interactive","_crypto_pwhash_memlimit_max","_crypto_pwhash_memlimit_min","_crypto_pwhash_memlimit_moderate","_crypto_pwhash_memlimit_sensitive","_crypto_pwhash_opslimit_interactive","_crypto_pwhash_opslimit_max","_crypto_pwhash_opslimit_min","_crypto_pwhash_opslimit_moderate","_crypto_pwhash_opslimit_sensitive","_crypto_pwhash_passwd_max","_crypto_pwhash_passwd_min","_crypto_pwhash_primitive","_crypto_pwhash_saltbytes","_crypto_pwhash_scryptsalsa208sha256","_crypto_pwhash_scryptsalsa208sha256_bytes_max","_crypto_pwhash_scryptsalsa208sha256_bytes_min","_crypto_pwhash_scryptsalsa208sha256_ll","_crypto_pwhash_scryptsalsa208sha256_memlimit_interactive","_crypto_pwhash_scryptsalsa208sha256_memlimit_max","_crypto_pwhash_scryptsalsa208sha256_memlimit_min","_crypto_pwhash_scryptsalsa208sha256_memlimit_sensitive","_crypto_pwhash_scryptsalsa208sha256_opslimit_interactive","_crypto_pwhash_scryptsalsa208sha256_opslimit_max","_crypto_pwhash_scryptsalsa208sha256_opslimit_min","_crypto_pwhash_scryptsalsa208sha256_opslimit_sensitive","_crypto_pwhash_scryptsalsa208sha256_passwd_max","_crypto_pwhash_scryptsalsa208sha256_passwd_min","_crypto_pwhash_scryptsalsa208sha256_saltbytes","_crypto_pwhash_scryptsalsa208sha256_str","_crypto_pwhash_scryptsalsa208sha256_str_needs_rehash","_crypto_pwhash_scryptsalsa208sha256_str_verify","_crypto_pwhash_scryptsalsa208sha256_strbytes","_crypto_pwhash_scryptsalsa208sha256_strprefix","_crypto_pwhash_str","_crypto_pwhash_str_alg","_crypto_pwhash_str_needs_rehash","_crypto_pwhash_str_verify","_crypto_pwhash_strbytes","_crypto_pwhash_strprefix","_crypto_scalarmult","_crypto_scalarmult_base","_crypto_scalarmult_bytes","_crypto_scalarmult_curve25519","_crypto_scalarmult_curve25519_base","_crypto_scalarmult_curve25519_bytes","_crypto_scalarmult_curve25519_scalarbytes","_crypto_scalarmult_ed25519","_crypto_scalarmult_ed25519_base","_crypto_scalarmult_ed25519_bytes","_crypto_scalarmult_ed25519_scalarbytes","_crypto_scalarmult_primitive","_crypto_scalarmult_scalarbytes","_crypto_secretbox","_crypto_secretbox_boxzerobytes","_crypto_secretbox_detached","_crypto_secretbox_easy","_crypto_secretbox_keybytes","_crypto_secretbox_keygen","_crypto_secretbox_macbytes","_crypto_secretbox_noncebytes","_crypto_secretbox_open","_crypto_secretbox_open_detached","_crypto_secretbox_open_easy","_crypto_secretbox_primitive","_crypto_secretbox_xchacha20poly1305_detached","_crypto_secretbox_xchacha20poly1305_easy","_crypto_secretbox_xchacha20poly1305_keybytes","_crypto_secretbox_xchacha20poly1305_macbytes","_crypto_secretbox_xchacha20poly1305_noncebytes","_crypto_secretbox_xchacha20poly1305_open_detached","_crypto_secretbox_xchacha20poly1305_open_easy","_crypto_secretbox_xsalsa20poly1305","_crypto_secretbox_xsalsa20poly1305_boxzerobytes","_crypto_secretbox_xsalsa20poly1305_keybytes","_crypto_secretbox_xsalsa20poly1305_keygen","_crypto_secretbox_xsalsa20poly1305_macbytes","_crypto_secretbox_xsalsa20poly1305_noncebytes","_crypto_secretbox_xsalsa20poly1305_open","_crypto_secretbox_xsalsa20poly1305_zerobytes","_crypto_secretbox_zerobytes","_crypto_secretstream_xchacha20poly1305_abytes","_crypto_secretstream_xchacha20poly1305_headerbytes","_crypto_secretstream_xchacha20poly1305_init_pull","_crypto_secretstream_xchacha20poly1305_init_push","_crypto_secretstream_xchacha20poly1305_keybytes","_crypto_secretstream_xchacha20poly1305_keygen","_crypto_secretstream_xchacha20poly1305_messagebytes_max","_crypto_secretstream_xchacha20poly1305_pull","_crypto_secretstream_xchacha20poly1305_push","_crypto_secretstream_xchacha20poly1305_rekey","_crypto_secretstream_xchacha20poly1305_statebytes","_crypto_secretstream_xchacha20poly1305_tag_final","_crypto_secretstream_xchacha20poly1305_tag_message","_crypto_secretstream_xchacha20poly1305_tag_push","_crypto_secretstream_xchacha20poly1305_tag_rekey","_crypto_shorthash","_crypto_shorthash_bytes","_crypto_shorthash_keybytes","_crypto_shorthash_keygen","_crypto_shorthash_primitive","_crypto_shorthash_siphash24","_crypto_shorthash_siphash24_bytes","_crypto_shorthash_siphash24_keybytes","_crypto_shorthash_siphashx24","_crypto_shorthash_siphashx24_bytes","_crypto_shorthash_siphashx24_keybytes","_crypto_sign","_crypto_sign_bytes","_crypto_sign_detached","_crypto_sign_ed25519","_crypto_sign_ed25519_bytes","_crypto_sign_ed25519_detached","_crypto_sign_ed25519_keypair","_crypto_sign_ed25519_open","_crypto_sign_ed25519_pk_to_curve25519","_crypto_sign_ed25519_publickeybytes","_crypto_sign_ed25519_secretkeybytes","_crypto_sign_ed25519_seed_keypair","_crypto_sign_ed25519_seedbytes","_crypto_sign_ed25519_sk_to_curve25519","_crypto_sign_ed25519_sk_to_pk","_crypto_sign_ed25519_sk_to_seed","_crypto_sign_ed25519_verify_detached","_crypto_sign_ed25519ph_final_create","_crypto_sign_ed25519ph_final_verify","_crypto_sign_ed25519ph_init","_crypto_sign_ed25519ph_statebytes","_crypto_sign_ed25519ph_update","_crypto_sign_final_create","_crypto_sign_final_verify","_crypto_sign_init","_crypto_sign_keypair","_crypto_sign_open","_crypto_sign_primitive","_crypto_sign_publickeybytes","_crypto_sign_secretkeybytes","_crypto_sign_seed_keypair","_crypto_sign_seedbytes","_crypto_sign_statebytes","_crypto_sign_update","_crypto_sign_verify_detached","_crypto_stream","_crypto_stream_chacha20","_crypto_stream_chacha20_ietf","_crypto_stream_chacha20_ietf_keybytes","_crypto_stream_chacha20_ietf_keygen","_crypto_stream_chacha20_ietf_noncebytes","_crypto_stream_chacha20_ietf_xor","_crypto_stream_chacha20_ietf_xor_ic","_crypto_stream_chacha20_keybytes","_crypto_stream_chacha20_keygen","_crypto_stream_chacha20_noncebytes","_crypto_stream_chacha20_xor","_crypto_stream_chacha20_xor_ic","_crypto_stream_keybytes","_crypto_stream_keygen","_crypto_stream_noncebytes","_crypto_stream_primitive","_crypto_stream_salsa20","_crypto_stream_salsa2012","_crypto_stream_salsa2012_keybytes","_crypto_stream_salsa2012_keygen","_crypto_stream_salsa2012_noncebytes","_crypto_stream_salsa2012_xor","_crypto_stream_salsa208","_crypto_stream_salsa208_keybytes","_crypto_stream_salsa208_keygen","_crypto_stream_salsa208_messagebytes_max","_crypto_stream_salsa208_noncebytes","_crypto_stream_salsa208_xor","_crypto_stream_salsa20_keybytes","_crypto_stream_salsa20_keygen","_crypto_stream_salsa20_noncebytes","_crypto_stream_salsa20_xor","_crypto_stream_salsa20_xor_ic","_crypto_stream_xchacha20","_crypto_stream_xchacha20_keybytes","_crypto_stream_xchacha20_keygen","_crypto_stream_xchacha20_noncebytes","_crypto_stream_xchacha20_xor","_crypto_stream_xchacha20_xor_ic","_crypto_stream_xor","_crypto_stream_xsalsa20","_crypto_stream_xsalsa20_keybytes","_crypto_stream_xsalsa20_keygen","_crypto_stream_xsalsa20_noncebytes","_crypto_stream_xsalsa20_xor","_crypto_stream_xsalsa20_xor_ic","_crypto_verify_16","_crypto_verify_16_bytes","_crypto_verify_32","_crypto_verify_32_bytes","_crypto_verify_64","_crypto_verify_64_bytes","_randombytes","_randombytes_buf","_randombytes_buf_deterministic","_randombytes_close","_randombytes_implementation_name","_randombytes_random","_randombytes_seedbytes","_randombytes_stir","_randombytes_uniform","_sodium_base642bin","_sodium_base64_encoded_len","_sodium_bin2base64","_sodium_bin2hex","_sodium_hex2bin","_sodium_init","_sodium_library_minimal","_sodium_library_version_major","_sodium_library_version_minor","_sodium_pad","_sodium_unpad","_sodium_version_string"]'
export EXPORTED_RUNTIME_METHODS='["Pointer_stringify","getValue","setValue"]'
export TOTAL_MEMORY=16777216
export TOTAL_MEMORY_SUMO=83886080
export TOTAL_MEMORY_TESTS=167772160
export LDFLAGS="-s RESERVED_FUNCTION_POINTERS=8"
export LDFLAGS="${LDFLAGS} -s SINGLE_FILE=1"
export LDFLAGS="${LDFLAGS} -s ASSERTIONS=0"
export LDFLAGS="${LDFLAGS} -s AGGRESSIVE_VARIABLE_ELIMINATION=1 -s ALIASING_FUNCTION_POINTERS=1"
export LDFLAGS="${LDFLAGS} -s DISABLE_EXCEPTION_CATCHING=1"
export LDFLAGS="${LDFLAGS} -s ELIMINATE_DUPLICATE_FUNCTIONS=1"
export LDFLAGS_DIST="-s NO_FILESYSTEM=1"
export CFLAGS="-Os"

echo
if [ "x$1" = "x--standard" ]; then
  export EXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS_STANDARD"
  export LDFLAGS="${LDFLAGS} ${LDFLAGS_DIST} -s TOTAL_MEMORY=${TOTAL_MEMORY}"
  export PREFIX="$(pwd)/libsodium-js"
  export DONE_FILE="$(pwd)/js.done"
  export CONFIG_EXTRA="--enable-minimal"
  export DIST='yes'
  echo "Building a standard distribution in [${PREFIX}]"
elif [ "x$1" = "x--sumo" ]; then
  export EXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS_SUMO"
  export LDFLAGS="${LDFLAGS} ${LDFLAGS_DIST} -s TOTAL_MEMORY=${TOTAL_MEMORY_SUMO}"
  export PREFIX="$(pwd)/libsodium-js-sumo"
  export DONE_FILE="$(pwd)/js-sumo.done"
  export DIST='yes'
  echo "Building a sumo distribution in [${PREFIX}]"
elif [ "x$1" = "x--browser-tests" ]; then
  export EXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS_SUMO"
  export LDFLAGS="${LDFLAGS} -s TOTAL_MEMORY=${TOTAL_MEMORY_TESTS}"
  export PREFIX="$(pwd)/libsodium-js-tests"
  export DONE_FILE="$(pwd)/js-tests-browser.done"
  export BROWSER_TESTS='yes'
  export DIST='no'
  echo "Building tests for web browsers in [${PREFIX}]"
elif [ "x$1" = "x--tests" ]; then
  echo "Building for testing"
  export EXPORTED_FUNCTIONS="$EXPORTED_FUNCTIONS_SUMO"
  export CPPFLAGS="${CPPFLAGS} -DBENCHMARKS -DITERATIONS=10"
  export LDFLAGS="${LDFLAGS} -s TOTAL_MEMORY=${TOTAL_MEMORY_TESTS}"
  export PREFIX="$(pwd)/libsodium-js-tests"
  export DONE_FILE="$(pwd)/js-tests.done"
  export DIST='no'
  echo "Building for testing in [${PREFIX}]"
else
  echo "Usage: $0 <build_type>"
  echo "<build_type> := --standard | --sumo | --browser-tests | --tests"
  echo
  exit 1
fi
export JS_EXPORTS_FLAGS="-s EXPORTED_FUNCTIONS=${EXPORTED_FUNCTIONS} -s EXTRA_EXPORTED_RUNTIME_METHODS=${EXPORTED_RUNTIME_METHODS}"

rm -f "$DONE_FILE"

echo

emconfigure ./configure $CONFIG_EXTRA --disable-shared --prefix="$PREFIX" \
                        --without-pthreads \
                        --disable-ssp --disable-asm --disable-pie \
                        CFLAGS="$CFLAGS" && \
emmake make clean
[ $? = 0 ] || exit 1

if [ "$DIST" = yes ]; then
  emccLibsodium () {
    outFile="${1}"
    shift
    emcc "$CFLAGS" --llvm-lto 1 $CPPFLAGS $LDFLAGS $JS_EXPORTS_FLAGS ${@} \
      "${PREFIX}/lib/libsodium.a" -o "${outFile}" || exit 1
  }
  emmake make $MAKE_FLAGS install || exit 1
  emccLibsodium "${PREFIX}/lib/libsodium.asm.tmp.js" -Oz -s WASM=0 -s RUNNING_JS_OPTS=1
  emccLibsodium "${PREFIX}/lib/libsodium.wasm.tmp.js" -O3 -s WASM=1

  cat > "${PREFIX}/lib/libsodium.js" <<- EOM
    var Module;
    if (typeof Module === 'undefined') {
      Module = {};
    }
    var root = Module;
    if (typeof root['sodium'] !== 'object') {
      if (typeof global === 'object') {
        root = global;
      } else if (typeof window === 'object') {
        root = window;
      }
    }
    if (typeof root['sodium'] === 'object' && typeof root['sodium']['totalMemory'] === 'number') {
      Module['TOTAL_MEMORY'] = root['sodium']['totalMemory'];
    }
    var _Module = Module;
    Module.ready = new Promise(function (resolve, reject) {
      var Module = _Module;
      Module.onAbort = reject;
      Module.onRuntimeInitialized = function () {
        try {
          /* Test arbitrary wasm function */
          Module._crypto_secretbox_keybytes();
          resolve();
        } catch (err) {
          reject(err);
        }
      };
      $(cat "${PREFIX}/lib/libsodium.wasm.tmp.js")
    }).catch(function () {
      var Module = _Module;
      Module.onAbort = undefined;
      Module.onRuntimeInitialized = undefined;
      $(cat "${PREFIX}/lib/libsodium.asm.tmp.js" | sed 's|use asm||g')
    });
EOM

  rm "${PREFIX}/lib/libsodium.asm.tmp.js" "${PREFIX}/lib/libsodium.wasm.tmp.js"
  touch -r "${PREFIX}/lib/libsodium.js" "$DONE_FILE"
  ls -l "${PREFIX}/lib/libsodium.js"
  exit 0
fi

if test "x$NODE" = x; then
  for candidate in /usr/local/bin/node /usr/local/bin/nodejs /usr/bin/node /usr/bin/nodejs node nodejs; do
    case $($candidate --version 2>&1) in #(
      v*)
        NODE=$candidate
        break ;;
    esac
  done
fi

if [ "x$BROWSER_TESTS" != "x" ]; then
  echo 'Compiling the test suite for web browsers...' && \
    emmake make $MAKE_FLAGS CPPFLAGS="$CPPFLAGS -DBROWSER_TESTS=1" check > /dev/null 2>&1
else
  if test "x$NODE" = x; then
    echo 'node.js not found - test suite skipped' >&2
    exit 1
  fi
  echo "Using [${NODE}] as a Javascript runtime"
  echo 'Compiling the test suite...' && \
    emmake make $MAKE_FLAGS check > /dev/null 2>&1
fi

if [ "x$BROWSER_TESTS" != "x" ]; then
  echo 'Creating the test suite for web browsers'
  (
    cd test/default && \
    mkdir -p browser && \
    rm -f browser/tests.txt && \
    for file in *.js; do
      fgrep -v "#! /usr/bin/env ${NODE}" "$file" > "browser/${file}"
      tname=$(echo "$file" | sed 's/.js$//')
      cp -f "${tname}.exp" "browser/${tname}.exp"
      sed "s/{{tname}}/${tname}/" index.html.tpl > "browser/${tname}.html"
      echo "${tname}.html" >> "browser/tests.txt"
    done
    touch "$DONE_FILE"
  )
else
  echo 'Running the test suite'
  (
    cd test/default && \
    for file in *.js; do
      echo "#! /usr/bin/env ${NODE}" > "${file}.tmp"
      fgrep -v "#! /usr/bin/env ${NODE}" "$file" >> "${file}.tmp"
      chmod +x "${file}.tmp"
      mv -f "${file}.tmp" "$file"
    done
  )
  make $MAKE_FLAGS check || exit 1
  touch "$DONE_FILE"
fi

echo 'Done.'
