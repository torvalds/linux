//===----------------------------------------------------------------------===//
// Define command classes.
//===----------------------------------------------------------------------===//

class Command<string name> {
  string Name = name;
  string EndCommandName = "";

  int NumArgs = 0;

  bit IsInlineCommand = 0;

  bit IsBlockCommand = 0;
  bit IsBriefCommand = 0;
  bit IsReturnsCommand = 0;
  bit IsParamCommand = 0;
  bit IsTParamCommand = 0;
  bit IsThrowsCommand = 0;
  bit IsDeprecatedCommand = 0;
  bit IsHeaderfileCommand = 0;
  bit IsParCommand = 0;

  bit IsEmptyParagraphAllowed = 0;

  bit IsVerbatimBlockCommand = 0;
  bit IsVerbatimBlockEndCommand = 0;
  bit IsVerbatimLineCommand = 0;
  bit IsDeclarationCommand = 0;
  bit IsFunctionDeclarationCommand = 0;
  bit IsRecordLikeDetailCommand = 0;
  bit IsRecordLikeDeclarationCommand = 0;
}

class InlineCommand<string name> : Command<name> {
  let NumArgs = 1;
  let IsInlineCommand = 1;
}

class BlockCommand<string name> : Command<name> {
  let IsBlockCommand = 1;
}

class RecordLikeDetailCommand<string name> : BlockCommand<name> {
  let IsRecordLikeDetailCommand = 1;
}

class VerbatimBlockCommand<string name> : Command<name> {
  let EndCommandName = name;
  let IsVerbatimBlockCommand = 1;
}

multiclass VerbatimBlockCommand<string name, string endCommandName> {
  def Begin : Command<name> {
    let EndCommandName = endCommandName;
    let IsVerbatimBlockCommand = 1;
  }

  def End : Command<endCommandName> {
    let IsVerbatimBlockEndCommand = 1;
  }
}

class VerbatimLineCommand<string name> : Command<name> {
  let IsVerbatimLineCommand = 1;
}

class PropertyCommand<string name> : Command<name> {
  let NumArgs = 0;
  let IsInlineCommand = 1;
}

class DeclarationVerbatimLineCommand<string name> :
      VerbatimLineCommand<name> {
  let IsDeclarationCommand = 1;
}

class FunctionDeclarationVerbatimLineCommand<string name> :
      DeclarationVerbatimLineCommand<name> {
  let IsFunctionDeclarationCommand = 1;
}

class RecordLikeDeclarationVerbatimLineCommand<string name> :
      DeclarationVerbatimLineCommand<name> {
  let IsRecordLikeDeclarationCommand = 1;
}

//===----------------------------------------------------------------------===//
// InlineCommand
//===----------------------------------------------------------------------===//

def B      : InlineCommand<"b">;
def C      : InlineCommand<"c">;
def P      : InlineCommand<"p">;
def A      : InlineCommand<"a">;
def E      : InlineCommand<"e">;
def N      : InlineCommand<"n"> { let NumArgs = 0; }
def Em     : InlineCommand<"em">;
def Emoji  : InlineCommand<"emoji">;

def Anchor  : InlineCommand<"anchor">;
def Ref     : InlineCommand<"ref">;
def RefItem : InlineCommand<"refitem">;
def Cite    : InlineCommand<"cite">;

def CopyBrief   : InlineCommand<"copybrief">;
def CopyDetails : InlineCommand<"copydetails">;
def CopyDoc     : InlineCommand<"copydoc">;

// Typically not used inline, but they take a single word.
def Extends    : InlineCommand<"extends">;
def Implements : InlineCommand<"implements">;
def MemberOf   : InlineCommand<"memberof">;

//===----------------------------------------------------------------------===//
// BlockCommand
//===----------------------------------------------------------------------===//

def Brief : BlockCommand<"brief"> { let IsBriefCommand = 1; }
def Short : BlockCommand<"short"> { let IsBriefCommand = 1; }

// Opposite of \brief, it is the default in our implementation.
def Details : BlockCommand<"details">;

def Returns : BlockCommand<"returns"> { let IsReturnsCommand = 1; }
def Return  : BlockCommand<"return"> { let IsReturnsCommand = 1; }
def Result  : BlockCommand<"result"> { let IsReturnsCommand = 1; }

def Param : BlockCommand<"param"> { let IsParamCommand = 1; }

// Doxygen command for template parameter documentation.
def Tparam : BlockCommand<"tparam"> { let IsTParamCommand = 1; }

// HeaderDoc command for template parameter documentation.
def Templatefield : BlockCommand<"templatefield"> { let IsTParamCommand = 1; }

def Throws    : BlockCommand<"throws"> { let IsThrowsCommand = 1; let NumArgs = 1; }
def Throw     : BlockCommand<"throw"> { let IsThrowsCommand = 1; let NumArgs = 1; }
def Exception : BlockCommand<"exception"> { let IsThrowsCommand = 1; let NumArgs = 1;}

def Deprecated : BlockCommand<"deprecated"> {
  let IsEmptyParagraphAllowed = 1;
  let IsDeprecatedCommand = 1;
}

def Headerfile : BlockCommand<"headerfile"> { let IsHeaderfileCommand = 1; }

// We don't do any additional semantic analysis for the following
// BlockCommands.  It might be a good idea to do something extra for them, but
// for now we model them as plain BlockCommands.
def Arg        : BlockCommand<"arg">;
def Attention  : BlockCommand<"attention">;
def Author     : BlockCommand<"author">;
def Authors    : BlockCommand<"authors">;
def Bug        : BlockCommand<"bug">;
def Copyright  : BlockCommand<"copyright">;
def Date       : BlockCommand<"date">;
def Invariant  : BlockCommand<"invariant">;
def Li         : BlockCommand<"li">;
def Note       : BlockCommand<"note">;
def Par        : BlockCommand<"par"> { let IsParCommand = 1; let NumArgs = 1; }
def Post       : BlockCommand<"post">;
def Pre        : BlockCommand<"pre">;
def Remark     : BlockCommand<"remark">;
def Remarks    : BlockCommand<"remarks">;
def Retval     : BlockCommand<"retval"> { let NumArgs = 1; }
def Sa         : BlockCommand<"sa">;
def See        : BlockCommand<"see">;
def Since      : BlockCommand<"since">;
def Test       : BlockCommand<"test">;
def Todo       : BlockCommand<"todo">;
def Version    : BlockCommand<"version">;
def Warning    : BlockCommand<"warning">;
def XRefItem   : BlockCommand<"xrefitem"> { let NumArgs = 3; }
// HeaderDoc commands
def Abstract      : BlockCommand<"abstract"> { let IsBriefCommand = 1; }
def ClassDesign   : RecordLikeDetailCommand<"classdesign">;
def CoClass       : RecordLikeDetailCommand<"coclass">;
def Dependency    : RecordLikeDetailCommand<"dependency">;
def Discussion    : BlockCommand<"discussion">;
def Helper        : RecordLikeDetailCommand<"helper">;
def HelperClass   : RecordLikeDetailCommand<"helperclass">;
def Helps         : RecordLikeDetailCommand<"helps">;
def InstanceSize  : RecordLikeDetailCommand<"instancesize">;
def Ownership     : RecordLikeDetailCommand<"ownership">;
def Performance   : RecordLikeDetailCommand<"performance">;
def Security      : RecordLikeDetailCommand<"security">;
def SeeAlso       : BlockCommand<"seealso">;
def SuperClass    : RecordLikeDetailCommand<"superclass">;

//===----------------------------------------------------------------------===//
// VerbatimBlockCommand
//===----------------------------------------------------------------------===//

defm Code      : VerbatimBlockCommand<"code", "endcode">;
defm Verbatim  : VerbatimBlockCommand<"verbatim", "endverbatim">;

defm DocbookOnly : VerbatimBlockCommand<"docbookonly", "enddocbookonly">;
defm Htmlonly  : VerbatimBlockCommand<"htmlonly", "endhtmlonly">;
defm Latexonly : VerbatimBlockCommand<"latexonly", "endlatexonly">;
defm Xmlonly   : VerbatimBlockCommand<"xmlonly", "endxmlonly">;
defm Manonly   : VerbatimBlockCommand<"manonly", "endmanonly">;
defm Rtfonly   : VerbatimBlockCommand<"rtfonly", "endrtfonly">;

defm Dot : VerbatimBlockCommand<"dot", "enddot">;
defm Msc : VerbatimBlockCommand<"msc", "endmsc">;
defm Uml : VerbatimBlockCommand<"startuml", "enduml">;

// Actually not verbatim blocks, we should also parse commands within them.
defm Internal   : VerbatimBlockCommand<"internal", "endinternal">;
// TODO: conflicts with HeaderDoc link, /link.
//defm Link       : VerbatimBlockCommand<"link", "endlink">;
defm ParBlock   : VerbatimBlockCommand<"parblock", "endparblock">;
defm SecRefList : VerbatimBlockCommand<"secreflist", "endsecreflist">;

// These three commands have special support in CommentLexer to recognize their
// names.
def  FDollar  : VerbatimBlockCommand<"f$">; // Inline LaTeX formula
defm FParen   : VerbatimBlockCommand<"f(", "f)">; // Inline LaTeX text
defm FBracket : VerbatimBlockCommand<"f[", "f]">; // Displayed LaTeX formula
defm FBrace   : VerbatimBlockCommand<"f{", "f}">; // LaTeX environment

// HeaderDoc commands
defm Textblock    : VerbatimBlockCommand<"textblock", "/textblock">;
defm Link         : VerbatimBlockCommand<"link", "/link">;

//===----------------------------------------------------------------------===//
// VerbatimLineCommand
//===----------------------------------------------------------------------===//

def Defgroup   : VerbatimLineCommand<"defgroup">;
def Ingroup    : VerbatimLineCommand<"ingroup">;
def Addtogroup : VerbatimLineCommand<"addtogroup">;
def Weakgroup  : VerbatimLineCommand<"weakgroup">;
def Name       : VerbatimLineCommand<"name">;

// These actually take a single word, but it's optional.
// And they're used on a separate line typically, not inline.
def Dir  : VerbatimLineCommand<"dir">;
def File : VerbatimLineCommand<"file">;

def Section       : VerbatimLineCommand<"section">;
def Subsection    : VerbatimLineCommand<"subsection">;
def Subsubsection : VerbatimLineCommand<"subsubsection">;
def Paragraph     : VerbatimLineCommand<"paragraph">;
def TableOfContents : VerbatimLineCommand<"tableofcontents">;

def Page     : VerbatimLineCommand<"page">;
def Mainpage : VerbatimLineCommand<"mainpage">;
def Subpage  : VerbatimLineCommand<"subpage">;

def Relates     : VerbatimLineCommand<"relates">;
def Related     : VerbatimLineCommand<"related">;
def RelatesAlso : VerbatimLineCommand<"relatesalso">;
def RelatedAlso : VerbatimLineCommand<"relatedalso">;

def AddIndex : VerbatimLineCommand<"addindex">;

// These take a single argument mostly, but since they include a file they'll
// typically be on their own line.
def DocbookInclude : VerbatimLineCommand<"docbookinclude">;
def DontInclude    : VerbatimLineCommand<"dontinclude">;
def Example        : VerbatimLineCommand<"example">;
def HtmlInclude    : VerbatimLineCommand<"htmlinclude">;
def Include        : VerbatimLineCommand<"include">;
def ManInclude     : VerbatimLineCommand<"maninclude">;
def LatexInclude   : VerbatimLineCommand<"latexinclude">;
def RtfInclude     : VerbatimLineCommand<"rtfinclude">;
def Snippet        : VerbatimLineCommand<"snippet">;
def VerbInclude    : VerbatimLineCommand<"verbinclude">;
def XmlInclude     : VerbatimLineCommand<"xmlinclude">;

def Image   : VerbatimLineCommand<"image">;
def DotFile : VerbatimLineCommand<"dotfile">;
def MscFile : VerbatimLineCommand<"mscfile">;
def DiaFile : VerbatimLineCommand<"diafile">;

def Line     : VerbatimLineCommand<"line">;
def Skip     : VerbatimLineCommand<"skip">;
def SkipLine : VerbatimLineCommand<"skipline">;
def Until    : VerbatimLineCommand<"until">;

def NoOp : VerbatimLineCommand<"noop">;

// We might also build proper support for if/ifnot/else/elseif/endif.
def If     : VerbatimLineCommand<"if">;
def IfNot  : VerbatimLineCommand<"ifnot">;
def Else   : VerbatimLineCommand<"else">;
def ElseIf : VerbatimLineCommand<"elseif">;
def Endif  : VerbatimLineCommand<"endif">;

// Not treated as VerbatimBlockCommand because it spans multiple comments.
def Cond    : VerbatimLineCommand<"cond">;
def EndCond : VerbatimLineCommand<"endcond">;

//===----------------------------------------------------------------------===//
// PropertyCommand
//===----------------------------------------------------------------------===//

def CallGraph       : PropertyCommand<"callgraph">;
def HideCallGraph   : PropertyCommand<"hidecallgraph">;
def CallerGraph     : PropertyCommand<"callergraph">;
def HideCallerGraph : PropertyCommand<"hidecallergraph">;
def ShowInitializer : PropertyCommand<"showinitializer">;
def HideInitializer : PropertyCommand<"hideinitializer">;
def ShowRefBy       : PropertyCommand<"showrefby">;
def HideRefBy       : PropertyCommand<"hiderefby">;
def ShowRefs        : PropertyCommand<"showrefs">;
def HideRefs        : PropertyCommand<"hiderefs">;

def Private   : PropertyCommand<"private">;
def Protected : PropertyCommand<"protected">;
def Public    : PropertyCommand<"public">;
def Pure      : PropertyCommand<"pure">;
def Static    : PropertyCommand<"static">;

def NoSubgrouping    : PropertyCommand<"nosubgrouping">;
def PrivateSection   : PropertyCommand<"privatesection">;
def ProtectedSection : PropertyCommand<"protectedsection">;
def PublicSection    : PropertyCommand<"publicsection">;

//===----------------------------------------------------------------------===//
// DeclarationVerbatimLineCommand
//===----------------------------------------------------------------------===//

// Doxygen commands.
def Concept   : DeclarationVerbatimLineCommand<"concept">;
def Def       : DeclarationVerbatimLineCommand<"def">;
def Fn        : DeclarationVerbatimLineCommand<"fn">;
def IDLExcept : DeclarationVerbatimLineCommand<"idlexcept">;
def Namespace : DeclarationVerbatimLineCommand<"namespace">;
def Overload  : DeclarationVerbatimLineCommand<"overload">;
def Property  : DeclarationVerbatimLineCommand<"property">;
def Typedef   : DeclarationVerbatimLineCommand<"typedef">;
def Var       : DeclarationVerbatimLineCommand<"var">;

// HeaderDoc commands.
def Class     : RecordLikeDeclarationVerbatimLineCommand<"class">;
def Interface : RecordLikeDeclarationVerbatimLineCommand<"interface">;
def Protocol  : RecordLikeDeclarationVerbatimLineCommand<"protocol">;
def Struct    : RecordLikeDeclarationVerbatimLineCommand<"struct">;
def Union     : RecordLikeDeclarationVerbatimLineCommand<"union">;
def Category  : DeclarationVerbatimLineCommand<"category">;
def Template  : DeclarationVerbatimLineCommand<"template">;
def Function  : FunctionDeclarationVerbatimLineCommand<"function">;
def FunctionGroup  : FunctionDeclarationVerbatimLineCommand<"functiongroup">;
def Method    : FunctionDeclarationVerbatimLineCommand<"method">;
def MethodGroup    : FunctionDeclarationVerbatimLineCommand<"methodgroup">;
def Callback  : FunctionDeclarationVerbatimLineCommand<"callback">;
def Const     : DeclarationVerbatimLineCommand<"const">;
def Constant  : DeclarationVerbatimLineCommand<"constant">;
def Enum      : DeclarationVerbatimLineCommand<"enum">;
