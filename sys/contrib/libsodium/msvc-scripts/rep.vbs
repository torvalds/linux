Dim pat, patparts, rxp, inp
pat = WScript.Arguments(0)
patparts = Split(pat, "/")
Set rxp = new RegExp
rxp.Global = True
rxp.Multiline = False
rxp.Pattern = patparts(1)
Do While Not WScript.StdIn.AtEndOfStream
  inp = WScript.StdIn.ReadLine()
  WScript.Echo rxp.Replace(inp, patparts(2))
Loop

