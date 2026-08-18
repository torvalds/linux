use zerocopy::*;

#[path = "formats/coco_dynamic_padding.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_split_at_dynamic_padding(
    source: &format::CocoPacket,
    len: usize,
) -> Option<Split<&format::CocoPacket>> {
    source.split_at(len)
}
