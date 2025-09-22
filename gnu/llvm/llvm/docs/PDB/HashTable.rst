The PDB Serialized Hash Table Format
====================================

.. contents::
   :local:

.. _hash_intro:

Introduction
============

One of the design goals of the PDB format is to provide accelerated access to
debug information, and for this reason there are several occasions where hash
tables are serialized and embedded directly to the file, rather than requiring
a consumer to read a list of values and reconstruct the hash table on the fly.

The serialization format supports hash tables of arbitrarily large size and
capacity, as well as value types and hash functions.  The only supported key
value type is a uint32.  The only requirement is that the producer and consumer
agree on the hash function.  As such, the hash function can is not discussed
further in this document, it is assumed that for a particular instance of a PDB
file hash table, the appropriate hash function is being used.

On-Disk Format
==============

.. code-block:: none

  .--------------------.-- +0
  |        Size        |
  .--------------------.-- +4
  |      Capacity      |
  .--------------------.-- +8
  | Present Bit Vector |
  .--------------------.-- +N
  | Deleted Bit Vector |
  .--------------------.-- +M                  ─╮
  |        Key         |                        │
  .--------------------.-- +M+4                 │
  |       Value        |                        │
  .--------------------.-- +M+4+sizeof(Value)   │
           ...                                  ├─ |Capacity| Bucket entries
  .--------------------.                        │
  |        Key         |                        │
  .--------------------.                        │
  |       Value        |                        │
  .--------------------.                       ─╯

- **Size** - The number of values contained in the hash table.

- **Capacity** - The number of buckets in the hash table.  Producers should
  maintain a load factor of no greater than ``2/3*Capacity+1``.

- **Present Bit Vector** - A serialized bit vector which contains information
  about which buckets have valid values.  If the bucket has a value, the
  corresponding bit will be set, and if the bucket doesn't have a value (either
  because the bucket is empty or because the value is a tombstone value) the bit
  will be unset.

- **Deleted Bit Vector** - A serialized bit vector which contains information
  about which buckets have tombstone values.  If the entry in this bucket is
  deleted, the bit will be set, otherwise it will be unset.

- **Keys and Values** - A list of ``Capacity`` hash buckets, where the first
  entry is the key (always a uint32), and the second entry is the value.  The
  state of each bucket (valid, empty, deleted) can be determined by examining
  the present and deleted bit vectors.


.. _hash_bit_vectors:

Present and Deleted Bit Vectors
===============================

The bit vectors indicating the status of each bucket are serialized as follows:

.. code-block:: none

  .--------------------.-- +0
  |     Word Count     |
  .--------------------.-- +4
  |        Word_0      |        ─╮
  .--------------------.-- +8    │
  |        Word_1      |         │
  .--------------------.-- +12   ├─ |Word Count| values
           ...                   │
  .--------------------.         │
  |       Word_N       |         │
  .--------------------.        ─╯

The words, when viewed as a contiguous block of bytes, represent a bit vector
with the following layout:

.. code-block:: none

    .------------.         .------------.------------.
    |   Word_N   |   ...   |   Word_1   |   Word_0   |
    .------------.         .------------.------------.
    |            |         |            |            |
  +N*32      +(N-1)*32    +64          +32          +0

where the k'th bit of this bit vector represents the status of the k'th bucket
in the hash table.
