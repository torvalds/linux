// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Google LLC.

//! DebugFS Abstraction
//!
//! C header: [`include/linux/debugfs.h`](srctree/include/linux/debugfs.h)

// When DebugFS is disabled, many parameters are dead. Linting for this isn't helpful.
#![cfg_attr(not(CONFIG_DEBUG_FS), allow(unused_variables))]

#[cfg(CONFIG_DEBUG_FS)]
use crate::prelude::*;
use crate::str::CStr;
#[cfg(CONFIG_DEBUG_FS)]
use crate::sync::Arc;

#[cfg(CONFIG_DEBUG_FS)]
mod entry;
#[cfg(CONFIG_DEBUG_FS)]
use entry::Entry;

/// Owning handle to a DebugFS directory.
///
/// The directory in the filesystem represented by [`Dir`] will be removed when handle has been
/// dropped *and* all children have been removed.
// If we have a parent, we hold a reference to it in the `Entry`. This prevents the `dentry`
// we point to from being cleaned up if our parent `Dir`/`Entry` is dropped before us.
//
// The `None` option indicates that the `Arc` could not be allocated, so our children would not be
// able to refer to us. In this case, we need to silently fail. All future child directories/files
// will silently fail as well.
#[derive(Clone)]
pub struct Dir(#[cfg(CONFIG_DEBUG_FS)] Option<Arc<Entry>>);

impl Dir {
    /// Create a new directory in DebugFS. If `parent` is [`None`], it will be created at the root.
    fn create(name: &CStr, parent: Option<&Dir>) -> Self {
        #[cfg(CONFIG_DEBUG_FS)]
        {
            let parent_entry = match parent {
                // If the parent couldn't be allocated, just early-return
                Some(Dir(None)) => return Self(None),
                Some(Dir(Some(entry))) => Some(entry.clone()),
                None => None,
            };
            Self(
                // If Arc creation fails, the `Entry` will be dropped, so the directory will be
                // cleaned up.
                Arc::new(Entry::dynamic_dir(name, parent_entry), GFP_KERNEL).ok(),
            )
        }
        #[cfg(not(CONFIG_DEBUG_FS))]
        Self()
    }

    /// Create a new directory in DebugFS at the root.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::c_str;
    /// # use kernel::debugfs::Dir;
    /// let debugfs = Dir::new(c_str!("parent"));
    /// ```
    pub fn new(name: &CStr) -> Self {
        Dir::create(name, None)
    }

    /// Creates a subdirectory within this directory.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::c_str;
    /// # use kernel::debugfs::Dir;
    /// let parent = Dir::new(c_str!("parent"));
    /// let child = parent.subdir(c_str!("child"));
    /// ```
    pub fn subdir(&self, name: &CStr) -> Self {
        Dir::create(name, Some(self))
    }
}
