use zerocopy::*;

#[path = "formats/coco_dynamic_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_write_to_suffix_dynamic_size(
    source: &format::CocoPacket,
    destination: &mut [u8],
) -> Option<()> {
    source.write_to_suffix(destination).ok()
}
