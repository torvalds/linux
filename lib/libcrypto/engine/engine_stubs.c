/*	$OpenBSD: engine_stubs.c,v 1.4 2024/03/27 06:08:45 tb Exp $ */

/*
 * Written by Theo Buehler. Public domain.
 */

#include <openssl/engine.h>

void
ENGINE_load_builtin_engines(void)
{
}
LCRYPTO_ALIAS(ENGINE_load_builtin_engines);

void
ENGINE_load_dynamic(void)
{
}
LCRYPTO_ALIAS(ENGINE_load_dynamic);

void
ENGINE_load_openssl(void)
{
}
LCRYPTO_ALIAS(ENGINE_load_openssl);

int
ENGINE_register_all_complete(void)
{
	return 0;
}
LCRYPTO_ALIAS(ENGINE_register_all_complete);

void
ENGINE_cleanup(void)
{
}
LCRYPTO_ALIAS(ENGINE_cleanup);

ENGINE *
ENGINE_new(void)
{
	return NULL;
}
LCRYPTO_ALIAS(ENGINE_new);

int
ENGINE_free(ENGINE *engine)
{
	return 0;
}
LCRYPTO_ALIAS(ENGINE_free);

int
ENGINE_init(ENGINE *engine)
{
	return 0;
}
LCRYPTO_ALIAS(ENGINE_init);

int
ENGINE_finish(ENGINE *engine)
{
	return 0;
}
LCRYPTO_ALIAS(ENGINE_finish);

ENGINE *
ENGINE_by_id(const char *id)
{
	return NULL;
}
LCRYPTO_ALIAS(ENGINE_by_id);

const char *
ENGINE_get_id(const ENGINE *engine)
{
	return "";
}
LCRYPTO_ALIAS(ENGINE_get_id);

const char *
ENGINE_get_name(const ENGINE *engine)
{
	return "";
}
LCRYPTO_ALIAS(ENGINE_get_name);

int
ENGINE_set_default(ENGINE *engine, unsigned int flags)
{
	return 0;
}
LCRYPTO_ALIAS(ENGINE_set_default);

ENGINE *
ENGINE_get_default_RSA(void)
{
	return NULL;
}
LCRYPTO_ALIAS(ENGINE_get_default_RSA);

int
ENGINE_set_default_RSA(ENGINE *engine)
{
	return 0;
}
LCRYPTO_ALIAS(ENGINE_set_default_RSA);

int
ENGINE_ctrl_cmd(ENGINE *engine, const char *cmd_name, long i, void *p,
    void (*f)(void), int cmd_optional)
{
	return 0;
}
LCRYPTO_ALIAS(ENGINE_ctrl_cmd);

int
ENGINE_ctrl_cmd_string(ENGINE *engine, const char *cmd, const char *arg,
    int cmd_optional)
{
	return 0;
}
LCRYPTO_ALIAS(ENGINE_ctrl_cmd_string);

EVP_PKEY *
ENGINE_load_private_key(ENGINE *engine, const char *key_id,
    UI_METHOD *ui_method, void *callback_data)
{
	return NULL;
}
LCRYPTO_ALIAS(ENGINE_load_private_key);

EVP_PKEY *
ENGINE_load_public_key(ENGINE *engine, const char *key_id,
    UI_METHOD *ui_method, void *callback_data)
{
	return NULL;
}
LCRYPTO_ALIAS(ENGINE_load_public_key);
