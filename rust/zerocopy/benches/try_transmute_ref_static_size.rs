use zerocopy_derive::*;

#[path = "formats/coco_static_size.rs"]
mod format;

#[derive(IntoBytes, KnownLayout, Immutable)]
#[repr(C, align(2))]
struct MinimalViableSource {
    bytes: [u8; 6],
}

#[unsafe(no_mangle)]
fn bench_try_transmute_ref_static_size(
    source: &MinimalViableSource,
) -> Option<&format::CocoPacket> {
    zerocopy::try_transmute_ref!(source).ok()
}
