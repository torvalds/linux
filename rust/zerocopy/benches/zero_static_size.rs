use zerocopy::*;

#[path = "formats/coco_static_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_zero_static_size(source: &mut format::LocoPacket) {
    source.zero()
}
