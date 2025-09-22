Pointer Authentication
======================

.. contents::
   :local:

Introduction
------------

Pointer authentication is a technology which offers strong probabilistic
protection against exploiting a broad class of memory bugs to take control of
program execution.  When adopted consistently in a language ABI, it provides
a form of relatively fine-grained control flow integrity (CFI) check that
resists both return-oriented programming (ROP) and jump-oriented programming
(JOP) attacks.

While pointer authentication can be implemented purely in software, direct
hardware support (e.g. as provided by Armv8.3 PAuth) can dramatically improve
performance and code size.  Similarly, while pointer authentication
can be implemented on any architecture, taking advantage of the (typically)
excess addressing range of a target with 64-bit pointers minimizes the impact
on memory performance and can allow interoperation with existing code (by
disabling pointer authentication dynamically).  This document will generally
attempt to present the pointer authentication feature independent of any
hardware implementation or ABI.  Considerations that are
implementation-specific are clearly identified throughout.

Note that there are several different terms in use:

- **Pointer authentication** is a target-independent language technology.

- **PAuth** (sometimes referred to as **PAC**, for Pointer Authentication
  Codes) is an AArch64 architecture extension that provides hardware support
  for pointer authentication.  Additional extensions either modify some of the
  PAuth instruction behavior (notably FPAC), or provide new instruction
  variants (PAuth_LR).

- **Armv8.3** is an AArch64 architecture revision that makes PAuth mandatory.

- **arm64e** is a specific ABI (not yet fully stable) for implementing pointer
  authentication using PAuth on certain Apple operating systems.

This document serves four purposes:

- It describes the basic ideas of pointer authentication.

- It documents several language extensions that are useful on targets using
  pointer authentication.

- It will eventually present a theory of operation for the security mitigation,
  describing the basic requirements for correctness, various weaknesses in the
  mechanism, and ways in which programmers can strengthen its protections
  (including recommendations for language implementors).

- It will eventually document the language ABIs currently used for C, C++,
  Objective-C, and Swift on arm64e, although these are not yet stable on any
  target.

Basic Concepts
--------------

The simple address of an object or function is a **raw pointer**.  A raw
pointer can be **signed** to produce a **signed pointer**.  A signed pointer
can be then **authenticated** in order to verify that it was **validly signed**
and extract the original raw pointer.  These terms reflect the most likely
implementation technique: computing and storing a cryptographic signature along
with the pointer.

An **abstract signing key** is a name which refers to a secret key which is
used to sign and authenticate pointers.  The concrete key value for a
particular name is consistent throughout a process.

A **discriminator** is an arbitrary value used to **diversify** signed pointers
so that one validly-signed pointer cannot simply be copied over another.
A discriminator is simply opaque data of some implementation-defined size that
is included in the signature as a salt (see `Discriminators`_ for details.)

Nearly all aspects of pointer authentication use just these two primary
operations:

- ``sign(raw_pointer, key, discriminator)`` produces a signed pointer given
  a raw pointer, an abstract signing key, and a discriminator.

- ``auth(signed_pointer, key, discriminator)`` produces a raw pointer given
  a signed pointer, an abstract signing key, and a discriminator.

``auth(sign(raw_pointer, key, discriminator), key, discriminator)`` must
succeed and produce ``raw_pointer``.  ``auth`` applied to a value that was
ultimately produced in any other way is expected to fail, which halts the
program either:

- immediately, on implementations that enforce ``auth`` success (e.g., when
  using compiler-generated ``auth`` failure checks, or Armv8.3 with the FPAC
  extension), or

- when the resulting pointer value is used, on implementations that don't.

However, regardless of the implementation's handling of ``auth`` failures, it
is permitted for ``auth`` to fail to detect that a signed pointer was not
produced in this way, in which case it may return anything; this is what makes
pointer authentication a probabilistic mitigation rather than a perfect one.

There are two secondary operations which are required only to implement certain
intrinsics in ``<ptrauth.h>``:

- ``strip(signed_pointer, key)`` produces a raw pointer given a signed pointer
  and a key without verifying its validity, unlike ``auth``.  This is useful
  for certain kinds of tooling, such as crash backtraces; it should generally
  not be used in the basic language ABI except in very careful ways.

- ``sign_generic(value)`` produces a cryptographic signature for arbitrary
  data, not necessarily a pointer.  This is useful for efficiently verifying
  that non-pointer data has not been tampered with.

Whenever any of these operations is called for, the key value must be known
statically.  This is because the layout of a signed pointer may vary according
to the signing key.  (For example, in Armv8.3, the layout of a signed pointer
depends on whether Top Byte Ignore (TBI) is enabled, which can be set
independently for I and D keys.)

.. admonition:: Note for API designers and language implementors

  These are the *primitive* operations of pointer authentication, provided for
  clarity of description.  They are not suitable either as high-level
  interfaces or as primitives in a compiler IR because they expose raw
  pointers.  Raw pointers require special attention in the language
  implementation to avoid the accidental creation of exploitable code
  sequences.

The following details are all implementation-defined:

- the nature of a signed pointer
- the size of a discriminator
- the number and nature of the signing keys
- the implementation of the ``sign``, ``auth``, ``strip``, and ``sign_generic``
  operations

While the use of the terms "sign" and "signed pointer" suggest the use of
a cryptographic signature, other implementations may be possible.  See
`Alternative implementations`_ for an exploration of implementation options.

.. admonition:: Implementation example: Armv8.3

  Readers may find it helpful to know how these terms map to Armv8.3 PAuth:

  - A signed pointer is a pointer with a signature stored in the
    otherwise-unused high bits.  The kernel configures the address width based
    on the system's addressing needs, and enables TBI for I or D keys as
    needed.  The bits above the address bits and below the TBI bits (if
    enabled) are unused.  The signature width then depends on this addressing
    configuration.

  - A discriminator is a 64-bit integer.  Constant discriminators are 16-bit
    integers.  Blending a constant discriminator into an address consists of
    replacing the top 16 bits of the pointer containing the address with the
    constant.  Pointers used for blending purposes should only have address
    bits, since higher bits will be at least partially overwritten with the
    constant discriminator.

  - There are five 128-bit signing-key registers, each of which can only be
    directly read or set by privileged code.  Of these, four are used for
    signing pointers, and the fifth is used only for ``sign_generic``.  The key
    data is simply a pepper added to the hash, not an encryption key, and so
    can be initialized using random data.

  - ``sign`` computes a cryptographic hash of the pointer, discriminator, and
    signing key, and stores it in the high bits as the signature. ``auth``
    removes the signature, computes the same hash, and compares the result with
    the stored signature.  ``strip`` removes the signature without
    authenticating it.  While ``aut*`` instructions do not themselves trap on
    failure in Armv8.3 PAuth, they do with the later optional FPAC extension.
    An implementation can also choose to emulate this trapping behavior by
    emitting additional instructions around ``aut*``.

  - ``sign_generic`` corresponds to the ``pacga`` instruction, which takes two
    64-bit values and produces a 64-bit cryptographic hash. Implementations of
    this instruction are not required to produce meaningful data in all bits of
    the result.

Discriminators
~~~~~~~~~~~~~~

A discriminator is arbitrary extra data which alters the signature calculated
for a pointer.  When two pointers are signed differently --- either with
different keys or with different discriminators --- an attacker cannot simply
replace one pointer with the other.

To use standard cryptographic terminology, a discriminator acts as a
`salt <https://en.wikipedia.org/wiki/Salt_(cryptography)>`_ in the signing of a
pointer, and the key data acts as a
`pepper <https://en.wikipedia.org/wiki/Pepper_(cryptography)>`_.  That is,
both the discriminator and key data are ultimately just added as inputs to the
signing algorithm along with the pointer, but they serve significantly
different roles.  The key data is a common secret added to every signature,
whereas the discriminator is a value that can be derived from
the context in which a specific pointer is signed.  However, unlike a password
salt, it's important that discriminators be *independently* derived from the
circumstances of the signing; they should never simply be stored alongside
a pointer.  Discriminators are then re-derived in authentication operations.

The intrinsic interface in ``<ptrauth.h>`` allows an arbitrary discriminator
value to be provided, but can only be used when running normal code.  The
discriminators used by language ABIs must be restricted to make it feasible for
the loader to sign pointers stored in global memory without needing excessive
amounts of metadata.  Under these restrictions, a discriminator may consist of
either or both of the following:

- The address at which the pointer is stored in memory.  A pointer signed with
  a discriminator which incorporates its storage address is said to have
  **address diversity**.  In general, using address diversity means that
  a pointer cannot be reliably copied by an attacker to or from a different
  memory location.  However, an attacker may still be able to attack a larger
  call sequence if they can alter the address through which the pointer is
  accessed.  Furthermore, some situations cannot use address diversity because
  of language or other restrictions.

- A constant integer, called a **constant discriminator**. A pointer signed
  with a non-zero constant discriminator is said to have **constant
  diversity**.  If the discriminator is specific to a single declaration, it is
  said to have **declaration diversity**; if the discriminator is specific to
  a type of value, it is said to have **type diversity**.  For example, C++
  v-tables on arm64e sign their component functions using a hash of their
  method names and signatures, which provides declaration diversity; similarly,
  C++ member function pointers sign their invocation functions using a hash of
  the member pointer type, which provides type diversity.

The implementation may need to restrict constant discriminators to be
significantly smaller than the full size of a discriminator.  For example, on
arm64e, constant discriminators are only 16-bit values.  This is believed to
not significantly weaken the mitigation, since collisions remain uncommon.

The algorithm for blending a constant discriminator with a storage address is
implementation-defined.

.. _Signing schemas:

Signing Schemas
~~~~~~~~~~~~~~~

Correct use of pointer authentication requires the signing code and the
authenticating code to agree about the **signing schema** for the pointer:

- the abstract signing key with which the pointer should be signed and
- an algorithm for computing the discriminator.

As described in the section above on `Discriminators`_, in most situations, the
discriminator is produced by taking a constant discriminator and optionally
blending it with the storage address of the pointer.  In these situations, the
signing schema breaks down even more simply:

- the abstract signing key,
- a constant discriminator, and
- whether to use address diversity.

It is important that the signing schema be independently derived at all signing
and authentication sites.  Preferably, the schema should be hard-coded
everywhere it is needed, but at the very least, it must not be derived by
inspecting information stored along with the pointer.

Language Features
-----------------

There is currently one main pointer authentication language feature:

- The language provides the ``<ptrauth.h>`` intrinsic interface for manually
  signing and authenticating pointers in code.  These can be used in
  circumstances where very specific behavior is required.


Language Extensions
~~~~~~~~~~~~~~~~~~~

Feature Testing
^^^^^^^^^^^^^^^

Whether the current target uses pointer authentication can be tested for with
a number of different tests.

- ``__has_feature(ptrauth_intrinsics)`` is true if ``<ptrauth.h>`` provides its
  normal interface.  This may be true even on targets where pointer
  authentication is not enabled by default.

``<ptrauth.h>``
~~~~~~~~~~~~~~~

This header defines the following types and operations:

``ptrauth_key``
^^^^^^^^^^^^^^^

This ``enum`` is the type of abstract signing keys.  In addition to defining
the set of implementation-specific signing keys (for example, Armv8.3 defines
``ptrauth_key_asia``), it also defines some portable aliases for those keys.
For example, ``ptrauth_key_function_pointer`` is the key generally used for
C function pointers, which will generally be suitable for other
function-signing schemas.

In all the operation descriptions below, key values must be constant values
corresponding to one of the implementation-specific abstract signing keys from
this ``enum``.

``ptrauth_extra_data_t``
^^^^^^^^^^^^^^^^^^^^^^^^

This is a ``typedef`` of a standard integer type of the correct size to hold
a discriminator value.

In the signing and authentication operation descriptions below, discriminator
values must have either pointer type or integer type. If the discriminator is
an integer, it will be coerced to ``ptrauth_extra_data_t``.

``ptrauth_blend_discriminator``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

  ptrauth_blend_discriminator(pointer, integer)

Produce a discriminator value which blends information from the given pointer
and the given integer.

Implementations may ignore some bits from each value, which is to say, the
blending algorithm may be chosen for speed and convenience over theoretical
strength as a hash-combining algorithm.  For example, arm64e simply overwrites
the high 16 bits of the pointer with the low 16 bits of the integer, which can
be done in a single instruction with an immediate integer.

``pointer`` must have pointer type, and ``integer`` must have integer type. The
result has type ``ptrauth_extra_data_t``.

``ptrauth_string_discriminator``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

  ptrauth_string_discriminator(string)

Compute a constant discriminator from the given string.

``string`` must be a string literal of ``char`` character type.  The result has
type ``ptrauth_extra_data_t``.

The result value is never zero and always within range for both the
``__ptrauth`` qualifier and ``ptrauth_blend_discriminator``.

This can be used in constant expressions.

``ptrauth_strip``
^^^^^^^^^^^^^^^^^

.. code-block:: c

  ptrauth_strip(signedPointer, key)

Given that ``signedPointer`` matches the layout for signed pointers signed with
the given key, extract the raw pointer from it.  This operation does not trap
and cannot fail, even if the pointer is not validly signed.

``ptrauth_sign_constant``
^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

  ptrauth_sign_constant(pointer, key, discriminator)

Return a signed pointer for a constant address in a manner which guarantees
a non-attackable sequence.

``pointer`` must be a constant expression of pointer type which evaluates to
a non-null pointer.
``key``  must be a constant expression of type ``ptrauth_key``.
``discriminator`` must be a constant expression of pointer or integer type;
if an integer, it will be coerced to ``ptrauth_extra_data_t``.
The result will have the same type as ``pointer``.

This can be used in constant expressions.

``ptrauth_sign_unauthenticated``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

  ptrauth_sign_unauthenticated(pointer, key, discriminator)

Produce a signed pointer for the given raw pointer without applying any
authentication or extra treatment.  This operation is not required to have the
same behavior on a null pointer that the language implementation would.

This is a treacherous operation that can easily result in signing oracles.
Programs should use it seldom and carefully.

``ptrauth_auth_and_resign``
^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

  ptrauth_auth_and_resign(pointer, oldKey, oldDiscriminator, newKey, newDiscriminator)

Authenticate that ``pointer`` is signed with ``oldKey`` and
``oldDiscriminator`` and then resign the raw-pointer result of that
authentication with ``newKey`` and ``newDiscriminator``.

``pointer`` must have pointer type.  The result will have the same type as
``pointer``.  This operation is not required to have the same behavior on
a null pointer that the language implementation would.

The code sequence produced for this operation must not be directly attackable.
However, if the discriminator values are not constant integers, their
computations may still be attackable.  In the future, Clang should be enhanced
to guaranteed non-attackability if these expressions are safely-derived.

``ptrauth_auth_data``
^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

  ptrauth_auth_data(pointer, key, discriminator)

Authenticate that ``pointer`` is signed with ``key`` and ``discriminator`` and
remove the signature.

``pointer`` must have object pointer type.  The result will have the same type
as ``pointer``.  This operation is not required to have the same behavior on
a null pointer that the language implementation would.

In the future when Clang makes safe derivation guarantees, the result of
this operation should be considered safely-derived.

``ptrauth_sign_generic_data``
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: c

  ptrauth_sign_generic_data(value1, value2)

Computes a signature for the given pair of values, incorporating a secret
signing key.

This operation can be used to verify that arbitrary data has not been tampered
with by computing a signature for the data, storing that signature, and then
repeating this process and verifying that it yields the same result.  This can
be reasonably done in any number of ways; for example, a library could compute
an ordinary checksum of the data and just sign the result in order to get the
tamper-resistance advantages of the secret signing key (since otherwise an
attacker could reliably overwrite both the data and the checksum).

``value1`` and ``value2`` must be either pointers or integers.  If the integers
are larger than ``uintptr_t`` then data not representable in ``uintptr_t`` may
be discarded.

The result will have type ``ptrauth_generic_signature_t``, which is an integer
type.  Implementations are not required to make all bits of the result equally
significant; in particular, some implementations are known to not leave
meaningful data in the low bits.



Alternative Implementations
---------------------------

Signature Storage
~~~~~~~~~~~~~~~~~

It is not critical for the security of pointer authentication that the
signature be stored "together" with the pointer, as it is in Armv8.3. An
implementation could just as well store the signature in a separate word, so
that the ``sizeof`` a signed pointer would be larger than the ``sizeof`` a raw
pointer.

Storing the signature in the high bits, as Armv8.3 does, has several trade-offs:

- Disadvantage: there are substantially fewer bits available for the signature,
  weakening the mitigation by making it much easier for an attacker to simply
  guess the correct signature.

- Disadvantage: future growth of the address space will necessarily further
  weaken the mitigation.

- Advantage: memory layouts don't change, so it's possible for
  pointer-authentication-enabled code (for example, in a system library) to
  efficiently interoperate with existing code, as long as pointer
  authentication can be disabled dynamically.

- Advantage: the size of a signed pointer doesn't grow, which might
  significantly increase memory requirements, code size, and register pressure.

- Advantage: the size of a signed pointer is the same as a raw pointer, so
  generic APIs which work in types like `void *` (such as `dlsym`) can still
  return signed pointers.  This means that clients of these APIs will not
  require insecure code in order to correctly receive a function pointer.

Hashing vs. Encrypting Pointers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Armv8.3 implements ``sign`` by computing a cryptographic hash and storing that
in the spare bits of the pointer.  This means that there are relatively few
possible values for the valid signed pointer, since the bits corresponding to
the raw pointer are known.  Together with an ``auth`` oracle, this can make it
computationally feasible to discover the correct signature with brute force.
(The implementation should of course endeavor not to introduce ``auth``
oracles, but this can be difficult, and attackers can be devious.)

If the implementation can instead *encrypt* the pointer during ``sign`` and
*decrypt* it during ``auth``, this brute-force attack becomes far less
feasible, even with an ``auth`` oracle.  However, there are several problems
with this idea:

- It's unclear whether this kind of encryption is even possible without
  increasing the storage size of a signed pointer.  If the storage size can be
  increased, brute-force atacks can be equally well mitigated by simply storing
  a larger signature.

- It would likely be impossible to implement a ``strip`` operation, which might
  make debuggers and other out-of-process tools far more difficult to write, as
  well as generally making primitive debugging more challenging.

- Implementations can benefit from being able to extract the raw pointer
  immediately from a signed pointer.  An Armv8.3 processor executing an
  ``auth``-and-load instruction can perform the load and ``auth`` in parallel;
  a processor which instead encrypted the pointer would be forced to perform
  these operations serially.
