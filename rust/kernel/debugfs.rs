// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Google LLC.

//! DebugFS Abstraction
//!
//! C header: [`include/linux/debugfs.h`](srctree/include/linux/debugfs.h)

// When DebugFS is disabled, many parameters are dead. Linting for this isn't helpful.
#![cfg_attr(not(CONFIG_DEBUG_FS), allow(unused_variables))]

use crate::prelude::*;
use crate::str::CStr;
#[cfg(CONFIG_DEBUG_FS)]
use crate::sync::Arc;
use core::marker::PhantomPinned;
use core::ops::Deref;

mod traits;
pub use traits::{Reader, Writer};

mod file_ops;
use file_ops::{FileOps, ReadFile, ReadWriteFile, WriteFile};
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

    /// Creates a DebugFS file which will own the data produced by the initializer provided in
    /// `data`.
    fn create_file<'a, T, E: 'a>(
        &'a self,
        name: &'a CStr,
        data: impl PinInit<T, E> + 'a,
        file_ops: &'static FileOps<T>,
    ) -> impl PinInit<File<T>, E> + 'a
    where
        T: Sync + 'static,
    {
        let scope = Scope::<T>::new(data, move |data| {
            #[cfg(CONFIG_DEBUG_FS)]
            if let Some(parent) = &self.0 {
                // SAFETY: Because data derives from a scope, and our entry will be dropped before
                // the data is dropped, it is guaranteed to outlive the entry we return.
                unsafe { Entry::dynamic_file(name, parent.clone(), data, file_ops) }
            } else {
                Entry::empty()
            }
        });
        try_pin_init! {
            File {
                scope <- scope
            } ? E
        }
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

    /// Creates a read-only file in this directory.
    ///
    /// The file's contents are produced by invoking [`Writer::write`] on the value initialized by
    /// `data`.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::c_str;
    /// # use kernel::debugfs::Dir;
    /// # use kernel::prelude::*;
    /// # let dir = Dir::new(c_str!("my_debugfs_dir"));
    /// let file = KBox::pin_init(dir.read_only_file(c_str!("foo"), 200), GFP_KERNEL)?;
    /// // "my_debugfs_dir/foo" now contains the number 200.
    /// // The file is removed when `file` is dropped.
    /// # Ok::<(), Error>(())
    /// ```
    pub fn read_only_file<'a, T, E: 'a>(
        &'a self,
        name: &'a CStr,
        data: impl PinInit<T, E> + 'a,
    ) -> impl PinInit<File<T>, E> + 'a
    where
        T: Writer + Send + Sync + 'static,
    {
        let file_ops = &<T as ReadFile<_>>::FILE_OPS;
        self.create_file(name, data, file_ops)
    }

    /// Creates a read-write file in this directory.
    ///
    /// Reading the file uses the [`Writer`] implementation.
    /// Writing to the file uses the [`Reader`] implementation.
    pub fn read_write_file<'a, T, E: 'a>(
        &'a self,
        name: &'a CStr,
        data: impl PinInit<T, E> + 'a,
    ) -> impl PinInit<File<T>, E> + 'a
    where
        T: Writer + Reader + Send + Sync + 'static,
    {
        let file_ops = &<T as ReadWriteFile<_>>::FILE_OPS;
        self.create_file(name, data, file_ops)
    }

    /// Creates a write-only file in this directory.
    ///
    /// The file owns its backing data. Writing to the file uses the [`Reader`]
    /// implementation.
    ///
    /// The file is removed when the returned [`File`] is dropped.
    pub fn write_only_file<'a, T, E: 'a>(
        &'a self,
        name: &'a CStr,
        data: impl PinInit<T, E> + 'a,
    ) -> impl PinInit<File<T>, E> + 'a
    where
        T: Reader + Send + Sync + 'static,
    {
        self.create_file(name, data, &T::FILE_OPS)
    }
}

#[pin_data]
/// Handle to a DebugFS scope, which ensures that attached `data` will outlive the provided
/// [`Entry`] without moving.
/// Currently, this is used to back [`File`] so that its `read` and/or `write` implementations
/// can assume that their backing data is still alive.
struct Scope<T> {
    // This order is load-bearing for drops - `_entry` must be dropped before `data`.
    #[cfg(CONFIG_DEBUG_FS)]
    _entry: Entry,
    #[pin]
    data: T,
    // Even if `T` is `Unpin`, we still can't allow it to be moved.
    #[pin]
    _pin: PhantomPinned,
}

#[pin_data]
/// Handle to a DebugFS file, owning its backing data.
///
/// When dropped, the DebugFS file will be removed and the attached data will be dropped.
pub struct File<T> {
    #[pin]
    scope: Scope<T>,
}

#[cfg(not(CONFIG_DEBUG_FS))]
impl<'b, T: 'b> Scope<T> {
    fn new<E: 'b, F>(data: impl PinInit<T, E> + 'b, init: F) -> impl PinInit<Self, E> + 'b
    where
        F: for<'a> FnOnce(&'a T) + 'b,
    {
        try_pin_init! {
            Self {
                data <- data,
                _pin: PhantomPinned
            } ? E
        }
        .pin_chain(|scope| {
            init(&scope.data);
            Ok(())
        })
    }
}

#[cfg(CONFIG_DEBUG_FS)]
impl<'b, T: 'b> Scope<T> {
    fn entry_mut(self: Pin<&mut Self>) -> &mut Entry {
        // SAFETY: _entry is not structurally pinned.
        unsafe { &mut Pin::into_inner_unchecked(self)._entry }
    }

    fn new<E: 'b, F>(data: impl PinInit<T, E> + 'b, init: F) -> impl PinInit<Self, E> + 'b
    where
        F: for<'a> FnOnce(&'a T) -> Entry + 'b,
    {
        try_pin_init! {
            Self {
                _entry: Entry::empty(),
                data <- data,
                _pin: PhantomPinned
            } ? E
        }
        .pin_chain(|scope| {
            *scope.entry_mut() = init(&scope.data);
            Ok(())
        })
    }
}

impl<T> Deref for Scope<T> {
    type Target = T;
    fn deref(&self) -> &T {
        &self.data
    }
}

impl<T> Deref for File<T> {
    type Target = T;
    fn deref(&self) -> &T {
        &self.scope
    }
}
