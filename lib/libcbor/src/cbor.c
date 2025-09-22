/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "cbor.h"
#include "cbor/internal/builder_callbacks.h"
#include "cbor/internal/loaders.h"

cbor_item_t *cbor_load(cbor_data source, size_t source_size,
                       struct cbor_load_result *result) {
  /* Context stack */
  static struct cbor_callbacks callbacks = {
      .uint8 = &cbor_builder_uint8_callback,
      .uint16 = &cbor_builder_uint16_callback,
      .uint32 = &cbor_builder_uint32_callback,
      .uint64 = &cbor_builder_uint64_callback,

      .negint8 = &cbor_builder_negint8_callback,
      .negint16 = &cbor_builder_negint16_callback,
      .negint32 = &cbor_builder_negint32_callback,
      .negint64 = &cbor_builder_negint64_callback,

      .byte_string = &cbor_builder_byte_string_callback,
      .byte_string_start = &cbor_builder_byte_string_start_callback,

      .string = &cbor_builder_string_callback,
      .string_start = &cbor_builder_string_start_callback,

      .array_start = &cbor_builder_array_start_callback,
      .indef_array_start = &cbor_builder_indef_array_start_callback,

      .map_start = &cbor_builder_map_start_callback,
      .indef_map_start = &cbor_builder_indef_map_start_callback,

      .tag = &cbor_builder_tag_callback,

      .null = &cbor_builder_null_callback,
      .undefined = &cbor_builder_undefined_callback,
      .boolean = &cbor_builder_boolean_callback,
      .float2 = &cbor_builder_float2_callback,
      .float4 = &cbor_builder_float4_callback,
      .float8 = &cbor_builder_float8_callback,
      .indef_break = &cbor_builder_indef_break_callback};

  if (source_size == 0) {
    result->error.code = CBOR_ERR_NODATA;
    return NULL;
  }
  struct _cbor_stack stack = _cbor_stack_init();

  /* Target for callbacks */
  struct _cbor_decoder_context context = (struct _cbor_decoder_context){
      .stack = &stack, .creation_failed = false, .syntax_error = false};
  struct cbor_decoder_result decode_result;
  *result =
      (struct cbor_load_result){.read = 0, .error = {.code = CBOR_ERR_NONE}};

  do {
    if (source_size > result->read) { /* Check for overflows */
      decode_result =
          cbor_stream_decode(source + result->read, source_size - result->read,
                             &callbacks, &context);
    } else {
      result->error = (struct cbor_error){.code = CBOR_ERR_NOTENOUGHDATA,
                                          .position = result->read};
      goto error;
    }

    switch (decode_result.status) {
      case CBOR_DECODER_FINISHED:
        /* Everything OK */
        {
          result->read += decode_result.read;
          break;
        }
      case CBOR_DECODER_NEDATA:
        /* Data length doesn't match MTB expectation */
        {
          result->error.code = CBOR_ERR_NOTENOUGHDATA;
          goto error;
        }
      case CBOR_DECODER_ERROR:
        /* Reserved/malformed item */
        {
          result->error.code = CBOR_ERR_MALFORMATED;
          goto error;
        }
    }

    if (context.creation_failed) {
      /* Most likely unsuccessful allocation - our callback has failed */
      result->error.code = CBOR_ERR_MEMERROR;
      goto error;
    } else if (context.syntax_error) {
      result->error.code = CBOR_ERR_SYNTAXERROR;
      goto error;
    }
  } while (stack.size > 0);

  return context.root;

error:
  result->error.position = result->read;
  // debug_print("Failed with decoder error %d at %d\n", result->error.code,
  // result->error.position); cbor_describe(stack.top->item, stdout);
  /* Free the stack */
  while (stack.size > 0) {
    cbor_decref(&stack.top->item);
    _cbor_stack_pop(&stack);
  }
  return NULL;
}

static cbor_item_t *_cbor_copy_int(cbor_item_t *item, bool negative) {
  cbor_item_t *res;
  switch (cbor_int_get_width(item)) {
    case CBOR_INT_8:
      res = cbor_build_uint8(cbor_get_uint8(item));
      break;
    case CBOR_INT_16:
      res = cbor_build_uint16(cbor_get_uint16(item));
      break;
    case CBOR_INT_32:
      res = cbor_build_uint32(cbor_get_uint32(item));
      break;
    case CBOR_INT_64:
      res = cbor_build_uint64(cbor_get_uint64(item));
      break;
  }

  if (negative) cbor_mark_negint(res);

  return res;
}

static cbor_item_t *_cbor_copy_float_ctrl(cbor_item_t *item) {
  // cppcheck-suppress missingReturn
  switch (cbor_float_get_width(item)) {
    case CBOR_FLOAT_0:
      return cbor_build_ctrl(cbor_ctrl_value(item));
    case CBOR_FLOAT_16:
      return cbor_build_float2(cbor_float_get_float2(item));
    case CBOR_FLOAT_32:
      return cbor_build_float4(cbor_float_get_float4(item));
    case CBOR_FLOAT_64:
      return cbor_build_float8(cbor_float_get_float8(item));
  }
}

cbor_item_t *cbor_copy(cbor_item_t *item) {
  // cppcheck-suppress missingReturn
  switch (cbor_typeof(item)) {
    case CBOR_TYPE_UINT:
      return _cbor_copy_int(item, false);
    case CBOR_TYPE_NEGINT:
      return _cbor_copy_int(item, true);
    case CBOR_TYPE_BYTESTRING:
      if (cbor_bytestring_is_definite(item)) {
        return cbor_build_bytestring(cbor_bytestring_handle(item),
                                     cbor_bytestring_length(item));
      } else {
        cbor_item_t *res = cbor_new_indefinite_bytestring();
        if (res == NULL) {
          return NULL;
        }

        for (size_t i = 0; i < cbor_bytestring_chunk_count(item); i++) {
          cbor_item_t *chunk_copy =
              cbor_copy(cbor_bytestring_chunks_handle(item)[i]);
          if (chunk_copy == NULL) {
            cbor_decref(&res);
            return NULL;
          }
          if (!cbor_bytestring_add_chunk(res, chunk_copy)) {
            cbor_decref(&chunk_copy);
            cbor_decref(&res);
            return NULL;
          }
          cbor_decref(&chunk_copy);
        }
        return res;
      }
    case CBOR_TYPE_STRING:
      if (cbor_string_is_definite(item)) {
        return cbor_build_stringn((const char *)cbor_string_handle(item),
                                  cbor_string_length(item));
      } else {
        cbor_item_t *res = cbor_new_indefinite_string();
        if (res == NULL) {
          return NULL;
        }

        for (size_t i = 0; i < cbor_string_chunk_count(item); i++) {
          cbor_item_t *chunk_copy =
              cbor_copy(cbor_string_chunks_handle(item)[i]);
          if (chunk_copy == NULL) {
            cbor_decref(&res);
            return NULL;
          }
          if (!cbor_string_add_chunk(res, chunk_copy)) {
            cbor_decref(&chunk_copy);
            cbor_decref(&res);
            return NULL;
          }
          cbor_decref(&chunk_copy);
        }
        return res;
      }
    case CBOR_TYPE_ARRAY: {
      cbor_item_t *res;
      if (cbor_array_is_definite(item)) {
        res = cbor_new_definite_array(cbor_array_size(item));
      } else {
        res = cbor_new_indefinite_array();
      }
      if (res == NULL) {
        return NULL;
      }

      for (size_t i = 0; i < cbor_array_size(item); i++) {
        cbor_item_t *entry_copy = cbor_copy(cbor_move(cbor_array_get(item, i)));
        if (entry_copy == NULL) {
          cbor_decref(&res);
          return NULL;
        }
        if (!cbor_array_push(res, entry_copy)) {
          cbor_decref(&entry_copy);
          cbor_decref(&res);
          return NULL;
        }
        cbor_decref(&entry_copy);
      }
      return res;
    }
    case CBOR_TYPE_MAP: {
      cbor_item_t *res;
      if (cbor_map_is_definite(item)) {
        res = cbor_new_definite_map(cbor_map_size(item));
      } else {
        res = cbor_new_indefinite_map();
      }
      if (res == NULL) {
        return NULL;
      }

      struct cbor_pair *it = cbor_map_handle(item);
      for (size_t i = 0; i < cbor_map_size(item); i++) {
        cbor_item_t *key_copy = cbor_copy(it[i].key);
        if (key_copy == NULL) {
          cbor_decref(&res);
          return NULL;
        }
        cbor_item_t *value_copy = cbor_copy(it[i].value);
        if (value_copy == NULL) {
          cbor_decref(&res);
          cbor_decref(&key_copy);
          return NULL;
        }
        if (!cbor_map_add(res, (struct cbor_pair){.key = key_copy,
                                                  .value = value_copy})) {
          cbor_decref(&res);
          cbor_decref(&key_copy);
          cbor_decref(&value_copy);
          return NULL;
        }
        cbor_decref(&key_copy);
        cbor_decref(&value_copy);
      }
      return res;
    }
    case CBOR_TYPE_TAG: {
      cbor_item_t *item_copy = cbor_copy(cbor_move(cbor_tag_item(item)));
      if (item_copy == NULL) {
        return NULL;
      }
      cbor_item_t *tag = cbor_build_tag(cbor_tag_value(item), item_copy);
      cbor_decref(&item_copy);
      return tag;
    }
    case CBOR_TYPE_FLOAT_CTRL:
      return _cbor_copy_float_ctrl(item);
  }
}

#if CBOR_PRETTY_PRINTER

#include <inttypes.h>
#include <locale.h>
#include <stdlib.h>
#include <wchar.h>

#define __STDC_FORMAT_MACROS

static int _pow(int b, int ex) {
  if (ex == 0) return 1;
  int res = b;
  while (--ex > 0) res *= b;
  return res;
}

static void _cbor_nested_describe(cbor_item_t *item, FILE *out, int indent) {
  setlocale(LC_ALL, "");
  switch (cbor_typeof(item)) {
    case CBOR_TYPE_UINT: {
      fprintf(out, "%*s[CBOR_TYPE_UINT] ", indent, " ");
      fprintf(out, "Width: %dB, ", _pow(2, cbor_int_get_width(item)));
      fprintf(out, "Value: %" PRIu64 "\n", cbor_get_int(item));
      break;
    }
    case CBOR_TYPE_NEGINT: {
      fprintf(out, "%*s[CBOR_TYPE_NEGINT] ", indent, " ");
      fprintf(out, "Width: %dB, ", _pow(2, cbor_int_get_width(item)));
      fprintf(out, "Value: -%" PRIu64 " -1\n", cbor_get_int(item));
      break;
    }
    case CBOR_TYPE_BYTESTRING: {
      fprintf(out, "%*s[CBOR_TYPE_BYTESTRING] ", indent, " ");
      if (cbor_bytestring_is_indefinite(item)) {
        fprintf(out, "Indefinite, with %zu chunks:\n",
                cbor_bytestring_chunk_count(item));
        for (size_t i = 0; i < cbor_bytestring_chunk_count(item); i++)
          _cbor_nested_describe(cbor_bytestring_chunks_handle(item)[i], out,
                                indent + 4);
      } else {
        fprintf(out, "Definite, length %zuB\n", cbor_bytestring_length(item));
      }
      break;
    }
    case CBOR_TYPE_STRING: {
      fprintf(out, "%*s[CBOR_TYPE_STRING] ", indent, " ");
      if (cbor_string_is_indefinite(item)) {
        fprintf(out, "Indefinite, with %zu chunks:\n",
                cbor_string_chunk_count(item));
        for (size_t i = 0; i < cbor_string_chunk_count(item); i++)
          _cbor_nested_describe(cbor_string_chunks_handle(item)[i], out,
                                indent + 4);
      } else {
        fprintf(out, "Definite, length %zuB, %zu codepoints\n",
                cbor_string_length(item), cbor_string_codepoint_count(item));
        /* Careful - this doesn't support multibyte characters! */
        /* Printing those is out of the scope of this demo :) */
        /* libICU is your friend */
        fprintf(out, "%*s", indent + 4, " ");
        /* XXX: no null at the end -> confused vprintf */
        fwrite(cbor_string_handle(item), (int)cbor_string_length(item), 1, out);
        fprintf(out, "\n");
      }
      break;
    }
    case CBOR_TYPE_ARRAY: {
      fprintf(out, "%*s[CBOR_TYPE_ARRAY] ", indent, " ");
      if (cbor_array_is_definite(item)) {
        fprintf(out, "Definite, size: %zu\n", cbor_array_size(item));
      } else {
        fprintf(out, "Indefinite, size:  %zu\n", cbor_array_size(item));
      }

      for (size_t i = 0; i < cbor_array_size(item); i++)
        _cbor_nested_describe(cbor_array_handle(item)[i], out, indent + 4);
      break;
    }
    case CBOR_TYPE_MAP: {
      fprintf(out, "%*s[CBOR_TYPE_MAP] ", indent, " ");
      if (cbor_map_is_definite(item)) {
        fprintf(out, "Definite, size: %zu\n", cbor_map_size(item));
      } else {
        fprintf(out, "Indefinite, size:  %zu\n", cbor_map_size(item));
      }

      for (size_t i = 0; i < cbor_map_size(item); i++) {
        _cbor_nested_describe(cbor_map_handle(item)[i].key, out, indent + 4);
        _cbor_nested_describe(cbor_map_handle(item)[i].value, out, indent + 4);
      }
      break;
    }
    case CBOR_TYPE_TAG: {
      fprintf(out, "%*s[CBOR_TYPE_TAG] ", indent, " ");
      fprintf(out, "Value: %" PRIu64 "\n", cbor_tag_value(item));
      _cbor_nested_describe(cbor_move(cbor_tag_item(item)), out, indent + 4);
      break;
    }
    case CBOR_TYPE_FLOAT_CTRL: {
      fprintf(out, "%*s[CBOR_TYPE_FLOAT_CTRL] ", indent, " ");
      if (cbor_float_ctrl_is_ctrl(item)) {
        if (cbor_is_bool(item))
          fprintf(out, "Bool: %s\n", cbor_get_bool(item) ? "true" : "false");
        else if (cbor_is_undef(item))
          fprintf(out, "Undefined\n");
        else if (cbor_is_null(item))
          fprintf(out, "Null\n");
        else
          fprintf(out, "Simple value %d\n", cbor_ctrl_value(item));
      } else {
        fprintf(out, "Width: %dB, ", _pow(2, cbor_float_get_width(item)));
        fprintf(out, "value: %lf\n", cbor_float_get_float(item));
      }
      break;
    }
  }
}

void cbor_describe(cbor_item_t *item, FILE *out) {
  _cbor_nested_describe(item, out, 0);
}

#endif
