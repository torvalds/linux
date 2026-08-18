use zerocopy::*;

#[path = "formats/coco_dynamic_padding.rs"]
mod format;

#[unsafe(no_mangle)]
unsafe fn bench_split_at_unchecked_dynamic_padding(
    source: &format::CocoPacket,
    len: usize,
) -> Split<&format::CocoPacket> {
    unsafe { source.split_at_unchecked(len) }
}
