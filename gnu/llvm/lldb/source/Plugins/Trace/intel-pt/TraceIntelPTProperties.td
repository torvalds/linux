include "../../../../include/lldb/Core/PropertiesBase.td"

let Definition = "traceintelpt" in {
  def InfiniteDecodingLoopVerificationThreshold:
      Property<"infinite-decoding-loop-verification-threshold", "UInt64">,
    Global,
    DefaultUnsignedValue<10000>,
    Desc<"Specify how many instructions following an individual Intel PT "
      "packet must have been decoded before triggering the verification of "
      "infinite decoding loops. If no decoding loop has been found after this "
      "threshold T, another attempt will be done after 2T instructions, then "
      "4T, 8T and so on, which guarantees a total linear time spent checking "
      "this anomaly. If a loop is found, then decoding of the corresponding "
      "PSB block is stopped. An error is hence emitted in the trace and "
      "decoding is resumed in the next PSB block.">;
  def ExtremelyLargeDecodingThreshold:
      Property<"extremely-large-decoding-threshold", "UInt64">,
    Global,
    DefaultUnsignedValue<500000>,
    Desc<"Specify how many instructions following an individual Intel PT "
      "packet must have been decoded before stopping the decoding of the "
      "corresponding PSB block. An error is hence emitted in the trace and "
      "decoding is resumed in the next PSB block.">;
}
