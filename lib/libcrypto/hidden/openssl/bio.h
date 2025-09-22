/* $OpenBSD: bio.h,v 1.9 2025/07/16 15:59:26 tb Exp $ */
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

#ifndef _LIBCRYPTO_BIO_H
#define _LIBCRYPTO_BIO_H

#ifndef _MSC_VER
#include_next <openssl/bio.h>
#else
#include "../include/openssl/bio.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(BIO_set_flags);
LCRYPTO_USED(BIO_test_flags);
LCRYPTO_USED(BIO_clear_flags);
LCRYPTO_USED(BIO_get_callback);
LCRYPTO_USED(BIO_set_callback);
LCRYPTO_USED(BIO_get_callback_ex);
LCRYPTO_USED(BIO_set_callback_ex);
LCRYPTO_USED(BIO_get_callback_arg);
LCRYPTO_USED(BIO_set_callback_arg);
LCRYPTO_USED(BIO_method_name);
LCRYPTO_USED(BIO_method_type);
LCRYPTO_USED(BIO_meth_new);
LCRYPTO_USED(BIO_meth_free);
LCRYPTO_USED(BIO_meth_get_write);
LCRYPTO_USED(BIO_meth_set_write);
LCRYPTO_USED(BIO_meth_get_read);
LCRYPTO_USED(BIO_meth_set_read);
LCRYPTO_USED(BIO_meth_get_puts);
LCRYPTO_USED(BIO_meth_set_puts);
LCRYPTO_USED(BIO_meth_get_gets);
LCRYPTO_USED(BIO_meth_set_gets);
LCRYPTO_USED(BIO_meth_get_ctrl);
LCRYPTO_USED(BIO_meth_set_ctrl);
LCRYPTO_USED(BIO_meth_get_create);
LCRYPTO_USED(BIO_meth_set_create);
LCRYPTO_USED(BIO_meth_get_destroy);
LCRYPTO_USED(BIO_meth_set_destroy);
LCRYPTO_USED(BIO_meth_get_callback_ctrl);
LCRYPTO_USED(BIO_meth_set_callback_ctrl);
LCRYPTO_USED(BIO_ctrl_pending);
LCRYPTO_USED(BIO_ctrl_wpending);
LCRYPTO_USED(BIO_ctrl_get_write_guarantee);
LCRYPTO_USED(BIO_ctrl_get_read_request);
LCRYPTO_USED(BIO_ctrl_reset_read_request);
LCRYPTO_USED(BIO_set_ex_data);
LCRYPTO_USED(BIO_get_ex_data);
LCRYPTO_USED(BIO_number_read);
LCRYPTO_USED(BIO_number_written);
LCRYPTO_USED(BIO_get_new_index);
LCRYPTO_USED(BIO_s_file);
LCRYPTO_USED(BIO_new_file);
LCRYPTO_USED(BIO_new_fp);
LCRYPTO_USED(BIO_new);
LCRYPTO_USED(BIO_free);
LCRYPTO_USED(BIO_up_ref);
LCRYPTO_USED(BIO_get_data);
LCRYPTO_USED(BIO_set_data);
LCRYPTO_USED(BIO_get_init);
LCRYPTO_USED(BIO_set_init);
LCRYPTO_USED(BIO_get_shutdown);
LCRYPTO_USED(BIO_set_shutdown);
LCRYPTO_USED(BIO_vfree);
LCRYPTO_USED(BIO_read);
LCRYPTO_USED(BIO_gets);
LCRYPTO_USED(BIO_write);
LCRYPTO_USED(BIO_puts);
LCRYPTO_USED(BIO_indent);
LCRYPTO_USED(BIO_ctrl);
LCRYPTO_USED(BIO_callback_ctrl);
LCRYPTO_USED(BIO_ptr_ctrl);
LCRYPTO_USED(BIO_int_ctrl);
LCRYPTO_USED(BIO_push);
LCRYPTO_USED(BIO_pop);
LCRYPTO_USED(BIO_free_all);
LCRYPTO_USED(BIO_find_type);
LCRYPTO_USED(BIO_next);
LCRYPTO_USED(BIO_set_next);
LCRYPTO_USED(BIO_get_retry_BIO);
LCRYPTO_USED(BIO_get_retry_reason);
LCRYPTO_USED(BIO_set_retry_reason);
LCRYPTO_USED(BIO_dup_chain);
LCRYPTO_USED(BIO_debug_callback);
LCRYPTO_USED(BIO_s_mem);
LCRYPTO_USED(BIO_new_mem_buf);
LCRYPTO_USED(BIO_s_socket);
LCRYPTO_USED(BIO_s_connect);
LCRYPTO_USED(BIO_s_accept);
LCRYPTO_USED(BIO_s_fd);
LCRYPTO_USED(BIO_s_bio);
LCRYPTO_USED(BIO_s_null);
LCRYPTO_USED(BIO_f_null);
LCRYPTO_USED(BIO_f_buffer);
LCRYPTO_USED(BIO_f_nbio_test);
LCRYPTO_USED(BIO_s_datagram);
LCRYPTO_USED(BIO_sock_should_retry);
LCRYPTO_USED(BIO_sock_non_fatal_error);
LCRYPTO_USED(BIO_dgram_non_fatal_error);
LCRYPTO_USED(BIO_fd_should_retry);
LCRYPTO_USED(BIO_fd_non_fatal_error);
LCRYPTO_USED(BIO_dump);
LCRYPTO_USED(BIO_dump_indent);
LCRYPTO_USED(BIO_gethostbyname);
LCRYPTO_USED(BIO_sock_error);
LCRYPTO_USED(BIO_socket_ioctl);
LCRYPTO_USED(BIO_socket_nbio);
LCRYPTO_USED(BIO_get_port);
LCRYPTO_USED(BIO_get_host_ip);
LCRYPTO_USED(BIO_get_accept_socket);
LCRYPTO_USED(BIO_accept);
LCRYPTO_USED(BIO_sock_init);
LCRYPTO_USED(BIO_sock_cleanup);
LCRYPTO_USED(BIO_set_tcp_ndelay);
LCRYPTO_USED(BIO_new_socket);
LCRYPTO_USED(BIO_new_dgram);
LCRYPTO_USED(BIO_new_fd);
LCRYPTO_USED(BIO_new_connect);
LCRYPTO_USED(BIO_new_accept);
LCRYPTO_USED(BIO_copy_next_retry);
LCRYPTO_USED(BIO_printf);
LCRYPTO_USED(ERR_load_BIO_strings);
LCRYPTO_USED(BIO_get_ex_new_index);
LCRYPTO_USED(BIO_new_bio_pair);

#endif /* _LIBCRYPTO_BIO_H */
