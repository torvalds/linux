// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Google LLC.

use crate::str::CStr;
use crate::sync::Arc;

/// Owning handle to a DebugFS entry.
///
/// # Invariants
///
/// The wrapped pointer will always be `NULL`, an error, or an owned DebugFS `dentry`.
pub(crate) struct Entry {
    entry: *mut bindings::dentry,
    // If we were created with an owning parent, this is the keep-alive
    _parent: Option<Arc<Entry>>,
}

// SAFETY: [`Entry`] is just a `dentry` under the hood, which the API promises can be transferred
// between threads.
unsafe impl Send for Entry {}

// SAFETY: All the C functions we call on the `dentry` pointer are threadsafe.
unsafe impl Sync for Entry {}

impl Entry {
    pub(crate) fn dynamic_dir(name: &CStr, parent: Option<Arc<Self>>) -> Self {
        let parent_ptr = match &parent {
            Some(entry) => entry.as_ptr(),
            None => core::ptr::null_mut(),
        };
        // SAFETY: The invariants of this function's arguments ensure the safety of this call.
        // * `name` is a valid C string by the invariants of `&CStr`.
        // * `parent_ptr` is either `NULL` (if `parent` is `None`), or a pointer to a valid
        //   `dentry` by our invariant. `debugfs_create_dir` handles `NULL` pointers correctly.
        let entry = unsafe { bindings::debugfs_create_dir(name.as_char_ptr(), parent_ptr) };

        Entry {
            entry,
            _parent: parent,
        }
    }

    /// Returns the pointer representation of the DebugFS directory.
    ///
    /// # Guarantees
    ///
    /// Due to the type invariant, the value returned from this function will always be an error
    /// code, NULL, or a live DebugFS directory. If it is live, it will remain live at least as
    /// long as this entry lives.
    pub(crate) fn as_ptr(&self) -> *mut bindings::dentry {
        self.entry
    }
}

impl Drop for Entry {
    fn drop(&mut self) {
        // SAFETY: `debugfs_remove` can take `NULL`, error values, and legal DebugFS dentries.
        // `as_ptr` guarantees that the pointer is of this form.
        unsafe { bindings::debugfs_remove(self.as_ptr()) }
    }
}
