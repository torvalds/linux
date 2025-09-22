include "../../../../include/lldb/Core/PropertiesBase.td"

let Definition = "android" in {
  def PlatformPackageName: Property<"package-name", "String">,
    Global,
    DefaultStringValue<"">,
    Desc<"Specify package name to run adb shell command with 'run-as' as the "
         "package user when necessary (e.g. to get file with 'cat' and 'dd').">;
}
