// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! A linked list implementation.

mod arc;
pub use self::arc::{impl_list_arc_safe, ListArc, ListArcSafe};
