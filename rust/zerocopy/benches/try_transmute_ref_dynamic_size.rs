use zerocopy_derive::*;

#[path = "formats/coco_dynamic_size.rs"]
mod format;

#[derive(IntoBytes, KnownLayout, Immutable)]
#[repr(C, align(2))]
struct MinimalViableSource {
    header: [u8; 6],
    trailer: [[u8; 2]],
}

#[unsafe(no_mangle)]
fn bench_try_transmute_ref_dynamic_size(
    source: &MinimalViableSource,
) -> Option<&format::CocoPacket> {
    zerocopy::try_transmute_ref!(source).ok()
}
