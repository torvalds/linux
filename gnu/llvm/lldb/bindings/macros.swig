%define STRING_EXTENSION_LEVEL_OUTSIDE(Class, Level)
%extend lldb:: ## Class ## {
  std::string __repr__(){
    lldb::SBStream stream;
    $self->GetDescription (stream, Level);
    const char *desc = stream.GetData();
    size_t desc_len = stream.GetSize();
    if (desc_len > 0 && (desc[desc_len-1] == '\n' || desc[desc_len-1] == '\r')) {
      --desc_len;
    }
    return std::string(desc, desc_len);
  }
}
%enddef

%define STRING_EXTENSION_OUTSIDE(Class)
%extend lldb:: ## Class ## {
  std::string __repr__(){
    lldb::SBStream stream;
    $self->GetDescription (stream);
    const char *desc = stream.GetData();
    size_t desc_len = stream.GetSize();
    if (desc_len > 0 && (desc[desc_len-1] == '\n' || desc[desc_len-1] == '\r')) {
      --desc_len;
    }
    return std::string(desc, desc_len);
  }
}
%enddef
