use zerocopy::*;

#[path = "formats/coco_dynamic_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_as_bytes_dynamic_size(source: &format::CocoPacket) -> &[u8] {
    source.as_bytes()
}
