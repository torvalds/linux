include "../../../../source/Commands/OptionsBase.td"

let Command = "thread trace export ctf" in {
  def thread_trace_export_ctf: Option<"tid", "t">,
    Group<1>,
    Arg<"ThreadIndex">,
    Desc<"Export the trace for the specified thread index. Otherwise, the "
         "currently selected thread will be used.">;
  def thread_trace_export_file: Option<"file", "f">, Required,
    Group<1>,
    Arg<"Filename">,
    Desc<"Path of the file to export the trace data">;
}
