include "../../include/lldb/Core/PropertiesBase.td"

let Definition = "interpreter" in {
  def ExpandRegexAliases: Property<"expand-regex-aliases", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"If true, regular expression alias commands will show the expanded command that will be executed. This can be used to debug new regular expression alias commands.">;
  def PromptOnQuit: Property<"prompt-on-quit", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, LLDB will prompt you before quitting if there are any live processes being debugged. If false, LLDB will quit without asking in any case.">;
  def SaveTranscript: Property<"save-transcript", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"If true, commands will be saved into a transcript buffer for user access.">;
  def SaveSessionOnQuit: Property<"save-session-on-quit", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"If true, LLDB will save the session's transcripts before quitting.">;
  def OpenTranscriptInEditor: Property<"open-transcript-in-editor", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, LLDB will open the saved session's transcripts in the external editor.">;
  def SaveSessionDirectory: Property<"save-session-directory", "FileSpec">,
    DefaultStringValue<"">,
    Desc<"A path where LLDB will save the session's transcripts. This is particularly useful when you can't set the session file, for example when using `save-session-on-quit`.">;
  def StopCmdSourceOnError: Property<"stop-command-source-on-error", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, LLDB will stop running a 'command source' script upon encountering an error.">;
  def SpaceReplPrompts: Property<"space-repl-prompts", "Boolean">,
    Global,
    DefaultFalse,
    Desc<"If true, blank lines will be printed between between REPL submissions.">;
  def EchoCommands: Property<"echo-commands", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, commands will be echoed before they are evaluated.">;
  def EchoCommentCommands: Property<"echo-comment-commands", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, commands will be echoed even if they are pure comment lines.">;
  def RepeatPreviousCommand: Property<"repeat-previous-command", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, LLDB will repeat the previous command if no command was passed to the interpreter. If false, LLDB won't repeat the previous command but only return a new prompt.">;
  def RequireCommandOverwrite: Property<"require-overwrite", "Boolean">,
    Global,
    DefaultTrue,
    Desc<"If true, require --overwrite in 'command script add' before overwriting existing user commands.">;
}
