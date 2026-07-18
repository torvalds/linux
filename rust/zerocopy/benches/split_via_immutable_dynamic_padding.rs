use zerocopy::*;

#[path = "formats/coco_dynamic_padding.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_split_via_immutable_dynamic_padding(
    split: Split<&format::CocoPacket>,
) -> (&format::CocoPacket, &[[u8; 3]]) {
    split.via_immutable()
}
