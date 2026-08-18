use zerocopy_derive::*;

#[path = "formats/coco_static_size.rs"]
mod format;

#[derive(IntoBytes, KnownLayout, Immutable)]
#[repr(C, align(2))]
struct MinimalViableSource {
    bytes: [u8; 6],
}

#[unsafe(no_mangle)]
fn bench_transmute_ref_static_size(source: &MinimalViableSource) -> &format::LocoPacket {
    zerocopy::transmute_ref!(source)
}
