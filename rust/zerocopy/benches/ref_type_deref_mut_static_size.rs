// Copyright 2026 The Fuchsia Authors
//
// Licensed under a BSD-style license <LICENSE-BSD>, Apache License, Version 2.0
// <LICENSE-APACHE or https://www.apache.org/licenses/LICENSE-2.0>, or the MIT
// license <LICENSE-MIT or https://opensource.org/licenses/MIT>, at your option.
// This file may not be copied, modified, or distributed except according to
// those terms.

use zerocopy::Ref;

#[path = "formats/coco_static_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_ref_type_deref_mut_static_size<'a>(
    source: &'a mut Ref<&mut [u8], format::LocoPacket>,
) -> &'a mut format::LocoPacket {
    core::ops::DerefMut::deref_mut(source)
}
