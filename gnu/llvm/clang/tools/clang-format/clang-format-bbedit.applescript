-- In this file, change "/path/to/" to the path where you installed clang-format
-- and save it to ~/Library/Application Support/BBEdit/Scripts. You can then
-- select the script from the Script menu and clang-format will format the
-- selection. Note that you can rename the menu item by renaming the script, and
-- can assign the menu item a keyboard shortcut in the BBEdit preferences, under
-- Menus & Shortcuts.
on urlToPOSIXPath(theURL)
	return do shell script "python -c \"import urllib, urlparse, sys; print urllib.unquote(urlparse.urlparse(sys.argv[1])[2])\" " & quoted form of theURL
end urlToPOSIXPath

tell application "BBEdit"
	set selectionOffset to characterOffset of selection
	set selectionLength to length of selection
	set fileURL to URL of text document 1
end tell

set filePath to urlToPOSIXPath(fileURL)
set newContents to do shell script "/path/to/clang-format -offset=" & selectionOffset & " -length=" & selectionLength & " " & quoted form of filePath

tell application "BBEdit"
	-- "set contents of text document 1 to newContents" scrolls to the bottom while
	-- replacing a selection flashes a bit but doesn't affect the scroll position.
	set currentLength to length of contents of text document 1
	select characters 1 thru currentLength of text document 1
	set text of selection to newContents
	select characters selectionOffset thru (selectionOffset + selectionLength - 1) of text document 1
end tell
