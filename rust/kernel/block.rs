// SPDX-License-Identifier: GPL-2.0

//! Types for working with the block layer.

pub mod mq;

/// Bit mask for masking out [`SECTOR_SIZE`].
pub const SECTOR_MASK: u32 = bindings::SECTOR_MASK;

/// Sectors are size `1 << SECTOR_SHIFT`.
pub const SECTOR_SHIFT: u32 = bindings::SECTOR_SHIFT;

/// Size of a sector.
pub const SECTOR_SIZE: u32 = bindings::SECTOR_SIZE;

/// The difference between the size of a page and the size of a sector,
/// expressed as a power of two.
pub const PAGE_SECTORS_SHIFT: u32 = bindings::PAGE_SECTORS_SHIFT;
