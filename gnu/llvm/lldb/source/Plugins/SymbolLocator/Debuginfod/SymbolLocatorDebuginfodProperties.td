include "../../../../include/lldb/Core/PropertiesBase.td"

let Definition = "symbollocatordebuginfod" in {
  def ServerURLs : Property<"server-urls", "Array">,
    ElementType<"String">,
    Desc<"An ordered list of Debuginfod server URLs to query for symbols. This defaults to the contents of the DEBUGINFOD_URLS environment variable.">;
  def SymbolCachePath: Property<"cache-path", "String">,
    DefaultStringValue<"">,
    Desc<"The path where symbol files should be cached. This defaults to LLDB's system cache location.">;
  def Timeout : Property<"timeout", "UInt64">,
    DefaultUnsignedValue<0>,
    Desc<"Timeout (in seconds) for requests made to a DEBUGINFOD server. A value of zero means we use the debuginfod default timeout: DEBUGINFOD_TIMEOUT if the environment variable is set and 90 seconds otherwise.">;
}
