Implementation of BLAKE3, originating from https://github.com/BLAKE3-team/BLAKE3/tree/1.3.1/c

# Example

An example program that hashes bytes from standard input and prints the
result:

Using the C++ API:

```c++
#include "llvm/Support/BLAKE3.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
  // Initialize the hasher.
  llvm::BLAKE3 hasher;

  // Read input bytes from stdin.
  char buf[65536];
  while (1) {
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
      hasher.update(llvm::StringRef(buf, n));
    } else if (n == 0) {
      break; // end of file
    } else {
      fprintf(stderr, "read failed: %s\n", strerror(errno));
      exit(1);
    }
  }

  // Finalize the hash. Default output length is 32 bytes.
  auto output = hasher.final();

  // Print the hash as hexadecimal.
  for (uint8_t byte : output) {
    printf("%02x", byte);
  }
  printf("\n");
  return 0;
}
```

Using the C API:

```c
#include "llvm-c/blake3.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main() {
  // Initialize the hasher.
  llvm_blake3_hasher hasher;
  llvm_blake3_hasher_init(&hasher);

  // Read input bytes from stdin.
  unsigned char buf[65536];
  while (1) {
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n > 0) {
      llvm_blake3_hasher_update(&hasher, buf, n);
    } else if (n == 0) {
      break; // end of file
    } else {
      fprintf(stderr, "read failed: %s\n", strerror(errno));
      exit(1);
    }
  }

  // Finalize the hash. LLVM_BLAKE3_OUT_LEN is the default output length, 32 bytes.
  uint8_t output[LLVM_BLAKE3_OUT_LEN];
  llvm_blake3_hasher_finalize(&hasher, output, LLVM_BLAKE3_OUT_LEN);

  // Print the hash as hexadecimal.
  for (size_t i = 0; i < LLVM_BLAKE3_OUT_LEN; i++) {
    printf("%02x", output[i]);
  }
  printf("\n");
  return 0;
}
```

# API

## The Class/Struct

```c++
class BLAKE3 {
  // API
private:
  llvm_blake3_hasher Hasher;
};
```
```c
typedef struct {
  // private fields
} llvm_blake3_hasher;
```

An incremental BLAKE3 hashing state, which can accept any number of
updates. This implementation doesn't allocate any heap memory, but
`sizeof(llvm_blake3_hasher)` itself is relatively large, currently 1912 bytes
on x86-64. This size can be reduced by restricting the maximum input
length, as described in Section 5.4 of [the BLAKE3
spec](https://github.com/BLAKE3-team/BLAKE3-specs/blob/master/blake3.pdf),
but this implementation doesn't currently support that strategy.

## Common API Functions

```c++
BLAKE3::BLAKE3();

void BLAKE3::init();
```
```c
void llvm_blake3_hasher_init(
  llvm_blake3_hasher *self);
```

Initialize a `llvm_blake3_hasher` in the default hashing mode.

---

```c++
void BLAKE3::update(ArrayRef<uint8_t> Data);

void BLAKE3::update(StringRef Str);
```
```c
void llvm_blake3_hasher_update(
  llvm_blake3_hasher *self,
  const void *input,
  size_t input_len);
```

Add input to the hasher. This can be called any number of times.

---

```c++
template <size_t NumBytes = LLVM_BLAKE3_OUT_LEN>
using BLAKE3Result = std::array<uint8_t, NumBytes>;

template <size_t NumBytes = LLVM_BLAKE3_OUT_LEN>
void BLAKE3::final(BLAKE3Result<NumBytes> &Result);

template <size_t NumBytes = LLVM_BLAKE3_OUT_LEN>
BLAKE3Result<NumBytes> BLAKE3::final();
```
```c
void llvm_blake3_hasher_finalize(
  const llvm_blake3_hasher *self,
  uint8_t *out,
  size_t out_len);
```

Finalize the hasher and return an output of any length, given in bytes.
This doesn't modify the hasher itself, and it's possible to finalize
again after adding more input. The constant `LLVM_BLAKE3_OUT_LEN` provides
the default output length, 32 bytes, which is recommended for most
callers.

Outputs shorter than the default length of 32 bytes (256 bits) provide
less security. An N-bit BLAKE3 output is intended to provide N bits of
first and second preimage resistance and N/2 bits of collision
resistance, for any N up to 256. Longer outputs don't provide any
additional security.

Shorter BLAKE3 outputs are prefixes of longer ones. Explicitly
requesting a short output is equivalent to truncating the default-length
output. (Note that this is different between BLAKE2 and BLAKE3.)

## Less Common API Functions

```c
void llvm_blake3_hasher_init_keyed(
  llvm_blake3_hasher *self,
  const uint8_t key[LLVM_BLAKE3_KEY_LEN]);
```

Initialize a `llvm_blake3_hasher` in the keyed hashing mode. The key must be
exactly 32 bytes.

---

```c
void llvm_blake3_hasher_init_derive_key(
  llvm_blake3_hasher *self,
  const char *context);
```

Initialize a `llvm_blake3_hasher` in the key derivation mode. The context
string is given as an initialization parameter, and afterwards input key
material should be given with `llvm_blake3_hasher_update`. The context string
is a null-terminated C string which should be **hardcoded, globally
unique, and application-specific**. The context string should not
include any dynamic input like salts, nonces, or identifiers read from a
database at runtime. A good default format for the context string is
`"[application] [commit timestamp] [purpose]"`, e.g., `"example.com
2019-12-25 16:18:03 session tokens v1"`.

This function is intended for application code written in C. For
language bindings, see `llvm_blake3_hasher_init_derive_key_raw` below.

---

```c
void llvm_blake3_hasher_init_derive_key_raw(
  llvm_blake3_hasher *self,
  const void *context,
  size_t context_len);
```

As `llvm_blake3_hasher_init_derive_key` above, except that the context string
is given as a pointer to an array of arbitrary bytes with a provided
length. This is intended for writing language bindings, where C string
conversion would add unnecessary overhead and new error cases. Unicode
strings should be encoded as UTF-8.

Application code in C should prefer `llvm_blake3_hasher_init_derive_key`,
which takes the context as a C string. If you need to use arbitrary
bytes as a context string in application code, consider whether you're
violating the requirement that context strings should be hardcoded.

---

```c
void llvm_blake3_hasher_finalize_seek(
  const llvm_blake3_hasher *self,
  uint64_t seek,
  uint8_t *out,
  size_t out_len);
```

The same as `llvm_blake3_hasher_finalize`, but with an additional `seek`
parameter for the starting byte position in the output stream. To
efficiently stream a large output without allocating memory, call this
function in a loop, incrementing `seek` by the output length each time.

---

```c
void llvm_blake3_hasher_reset(
  llvm_blake3_hasher *self);
```

Reset the hasher to its initial state, prior to any calls to
`llvm_blake3_hasher_update`. Currently this is no different from calling
`llvm_blake3_hasher_init` or similar again. However, if this implementation gains
multithreading support in the future, and if `llvm_blake3_hasher` holds (optional)
threading resources, this function will reuse those resources.


# Building

This implementation is just C and assembly files.

## x86

Dynamic dispatch is enabled by default on x86. The implementation will
query the CPU at runtime to detect SIMD support, and it will use the
widest instruction set available. By default, `blake3_dispatch.c`
expects to be linked with code for five different instruction sets:
portable C, SSE2, SSE4.1, AVX2, and AVX-512.

For each of the x86 SIMD instruction sets, four versions are available:
three flavors of assembly (Unix, Windows MSVC, and Windows GNU) and one
version using C intrinsics. The assembly versions are generally
preferred. They perform better, they perform more consistently across
different compilers, and they build more quickly. On the other hand, the
assembly versions are x86\_64-only, and you need to select the right
flavor for your target platform.

## ARM NEON

The NEON implementation is enabled by default on AArch64, but not on
other ARM targets, since not all of them support it. To enable it, set
`BLAKE3_USE_NEON=1`.

To explicitiy disable using NEON instructions on AArch64, set
`BLAKE3_USE_NEON=0`.

## Other Platforms

The portable implementation should work on most other architectures.

# Multithreading

The implementation doesn't currently support multithreading.
