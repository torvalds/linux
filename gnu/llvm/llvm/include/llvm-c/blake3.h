/*===-- llvm-c/blake3.h - BLAKE3 C Interface ----------------------*- C -*-===*\
|*                                                                            *|
|* Released into the public domain with CC0 1.0                               *|
|* See 'llvm/lib/Support/BLAKE3/LICENSE' for info.                            *|
|* SPDX-License-Identifier: CC0-1.0                                           *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares the C interface to LLVM's BLAKE3 implementation.      *|
|* Original BLAKE3 C API: https://github.com/BLAKE3-team/BLAKE3/tree/1.3.1/c  *|
|*                                                                            *|
|* Symbols are prefixed with 'llvm' to avoid a potential conflict with        *|
|* another BLAKE3 version within the same program.                            *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_BLAKE3_H
#define LLVM_C_BLAKE3_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LLVM_BLAKE3_VERSION_STRING "1.3.1"
#define LLVM_BLAKE3_KEY_LEN 32
#define LLVM_BLAKE3_OUT_LEN 32
#define LLVM_BLAKE3_BLOCK_LEN 64
#define LLVM_BLAKE3_CHUNK_LEN 1024
#define LLVM_BLAKE3_MAX_DEPTH 54

// This struct is a private implementation detail. It has to be here because
// it's part of llvm_blake3_hasher below.
typedef struct {
  uint32_t cv[8];
  uint64_t chunk_counter;
  uint8_t buf[LLVM_BLAKE3_BLOCK_LEN];
  uint8_t buf_len;
  uint8_t blocks_compressed;
  uint8_t flags;
} llvm_blake3_chunk_state;

typedef struct {
  uint32_t key[8];
  llvm_blake3_chunk_state chunk;
  uint8_t cv_stack_len;
  // The stack size is MAX_DEPTH + 1 because we do lazy merging. For example,
  // with 7 chunks, we have 3 entries in the stack. Adding an 8th chunk
  // requires a 4th entry, rather than merging everything down to 1, because we
  // don't know whether more input is coming. This is different from how the
  // reference implementation does things.
  uint8_t cv_stack[(LLVM_BLAKE3_MAX_DEPTH + 1) * LLVM_BLAKE3_OUT_LEN];
} llvm_blake3_hasher;

const char *llvm_blake3_version(void);
void llvm_blake3_hasher_init(llvm_blake3_hasher *self);
void llvm_blake3_hasher_init_keyed(llvm_blake3_hasher *self,
                                   const uint8_t key[LLVM_BLAKE3_KEY_LEN]);
void llvm_blake3_hasher_init_derive_key(llvm_blake3_hasher *self,
                                        const char *context);
void llvm_blake3_hasher_init_derive_key_raw(llvm_blake3_hasher *self,
                                            const void *context,
                                            size_t context_len);
void llvm_blake3_hasher_update(llvm_blake3_hasher *self, const void *input,
                               size_t input_len);
void llvm_blake3_hasher_finalize(const llvm_blake3_hasher *self, uint8_t *out,
                                 size_t out_len);
void llvm_blake3_hasher_finalize_seek(const llvm_blake3_hasher *self,
                                      uint64_t seek, uint8_t *out,
                                      size_t out_len);
void llvm_blake3_hasher_reset(llvm_blake3_hasher *self);

#ifdef __cplusplus
}
#endif

#endif /* LLVM_C_BLAKE3_H */
