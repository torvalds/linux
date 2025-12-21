// SPDX-License-Identifier: Apache-2.0 OR MIT

#[cfg(feature = "parsing")]
pub(crate) mod lookahead {
    pub trait Sealed: Copy {}
}
