use zerocopy::*;

#[path = "formats/coco_static_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_as_bytes_static_size(source: &format::CocoPacket) -> &[u8] {
    source.as_bytes()
}
