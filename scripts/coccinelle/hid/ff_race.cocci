/// Detect HID drivers that initialize force-feedback after hid_hw_start()
/// when HID_CONNECT_HIDINPUT is used. This is a lifecycle violation as
/// the input device is already registered.
//
// Confidence: High
// Copyright: (C) 2026 Gemini. GPLv2.

virtual report

@r@
identifier probe_fn;
expression hdev, flags;
position p1, p2;
@@

probe_fn(struct hid_device *hdev, ...) {
  <...
  hid_hw_start@p1(hdev, flags)
  ...
  \(input_ff_create\|input_ff_create_memless\)@p2(...)
  ...>
}

@script:python depends on report@
p1 << r.p1;
p2 << r.p2;
flags << r.flags;
@@

# Check if flags include HID_CONNECT_HIDINPUT (0x01) or HID_CONNECT_DEFAULT (0x0f)
# Note: HID_CONNECT_DEFAULT is 0x0f, HID_CONNECT_HIDINPUT is 0x01
if "HID_CONNECT_HIDINPUT" in flags or "HID_CONNECT_DEFAULT" in flags:
    msg = "WARNING: force-feedback initialized after hid_hw_start() with HID_CONNECT_HIDINPUT. Input device is already registered at this point. Use .input_configured() instead."
    coccilib.report.print_report(p2[0], msg)
