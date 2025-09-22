include "clang/Basic/ASTNode.td"

class DeclNode<DeclNode base, string diagSpelling = "", bit abstract = 0>
    : ASTNode, AttrSubject {
  DeclNode Base = base;
  bit Abstract = abstract;
  string DiagSpelling = diagSpelling;
}

class DeclContext {}

def Decl : DeclNode<?, "", 1>;
def TranslationUnit : DeclNode<Decl>, DeclContext;
def PragmaComment : DeclNode<Decl>;
def PragmaDetectMismatch : DeclNode<Decl>;
def ExternCContext : DeclNode<Decl>, DeclContext;
def Named : DeclNode<Decl, "named declarations", 1>;
  def Namespace : DeclNode<Named, "namespaces">, DeclContext;
  def UsingDirective : DeclNode<Named>;
  def NamespaceAlias : DeclNode<Named>;
  def Label : DeclNode<Named, "labels">;
  def Type : DeclNode<Named, "types", 1>;
    def TypedefName : DeclNode<Type, "typedefs", 1>;
      def Typedef : DeclNode<TypedefName>;
      def TypeAlias : DeclNode<TypedefName>;
      def ObjCTypeParam : DeclNode<TypedefName>;
    def UnresolvedUsingTypename : DeclNode<Type>;
    def Tag : DeclNode<Type, "tag types", 1>, DeclContext;
      def Enum : DeclNode<Tag, "enums">;
      def Record : DeclNode<Tag, "structs, unions, classes">;
        def CXXRecord : DeclNode<Record, "classes">;
          def ClassTemplateSpecialization : DeclNode<CXXRecord>;
            def ClassTemplatePartialSpecialization
              : DeclNode<ClassTemplateSpecialization>;
    def TemplateTypeParm : DeclNode<Type>;
  def Value : DeclNode<Named, "value declarations", 1>;
    def EnumConstant : DeclNode<Value, "enumerators">;
    def UnresolvedUsingValue : DeclNode<Value>;
    def IndirectField : DeclNode<Value>;
    def Binding : DeclNode<Value>;
    def OMPDeclareReduction : DeclNode<Value>, DeclContext;
    def OMPDeclareMapper : DeclNode<Value>, DeclContext;
    def MSGuid : DeclNode<Value>;
    def UnnamedGlobalConstant : DeclNode<Value>;
    def TemplateParamObject : DeclNode<Value>;
    def Declarator : DeclNode<Value, "declarators", 1>;
      def Field : DeclNode<Declarator, "non-static data members">;
        def ObjCIvar : DeclNode<Field>;
        def ObjCAtDefsField : DeclNode<Field>;
      def MSProperty : DeclNode<Declarator>;
      def Function : DeclNode<Declarator, "functions">, DeclContext;
        def CXXDeductionGuide : DeclNode<Function>;
        def CXXMethod : DeclNode<Function>;
          def CXXConstructor : DeclNode<CXXMethod>;
          def CXXDestructor : DeclNode<CXXMethod>;
          def CXXConversion : DeclNode<CXXMethod>;
      def Var : DeclNode<Declarator, "variables">;
        def VarTemplateSpecialization : DeclNode<Var>;
          def VarTemplatePartialSpecialization
            : DeclNode<VarTemplateSpecialization>;
        def ImplicitParam : DeclNode<Var>;
        def ParmVar : DeclNode<Var, "parameters">;
        def Decomposition : DeclNode<Var>;
        def OMPCapturedExpr : DeclNode<Var>;
      def NonTypeTemplateParm : DeclNode<Declarator>;
  def Template : DeclNode<Named, "templates", 1>;
    def RedeclarableTemplate : DeclNode<Template, "redeclarable templates", 1>;
      def FunctionTemplate : DeclNode<RedeclarableTemplate>;
      def ClassTemplate : DeclNode<RedeclarableTemplate>;
      def VarTemplate : DeclNode<RedeclarableTemplate>;
      def TypeAliasTemplate : DeclNode<RedeclarableTemplate>;
    def TemplateTemplateParm : DeclNode<Template>;
    def BuiltinTemplate : DeclNode<Template>;
    def Concept : DeclNode<Template>;
  def BaseUsing : DeclNode<Named, "", 1>;
    def Using : DeclNode<BaseUsing>;
    def UsingEnum : DeclNode<BaseUsing>;
  def UsingPack : DeclNode<Named>;
  def UsingShadow : DeclNode<Named>;
    def ConstructorUsingShadow : DeclNode<UsingShadow>;
  def UnresolvedUsingIfExists : DeclNode<Named>;
  def ObjCMethod : DeclNode<Named, "Objective-C methods">, DeclContext;
  def ObjCContainer : DeclNode<Named, "Objective-C containers", 1>, DeclContext;
    def ObjCCategory : DeclNode<ObjCContainer>;
    def ObjCProtocol : DeclNode<ObjCContainer, "Objective-C protocols">;
    def ObjCInterface : DeclNode<ObjCContainer, "Objective-C interfaces">;
    def ObjCImpl
        : DeclNode<ObjCContainer, "Objective-C implementation declarations", 1>;
      def ObjCCategoryImpl : DeclNode<ObjCImpl>;
      def ObjCImplementation : DeclNode<ObjCImpl>;
  def ObjCProperty : DeclNode<Named, "Objective-C properties">;
  def ObjCCompatibleAlias : DeclNode<Named>;
def ImplicitConceptSpecialization : DeclNode<Decl>;
def LinkageSpec : DeclNode<Decl>, DeclContext;
def Export : DeclNode<Decl>, DeclContext;
def ObjCPropertyImpl : DeclNode<Decl>;
def FileScopeAsm : DeclNode<Decl>;
def TopLevelStmt : DeclNode<Decl>, DeclContext;
def AccessSpec : DeclNode<Decl>;
def Friend : DeclNode<Decl>;
def FriendTemplate : DeclNode<Decl>;
def StaticAssert : DeclNode<Decl>;
def Block : DeclNode<Decl, "blocks">, DeclContext;
def Captured : DeclNode<Decl>, DeclContext;
def Import : DeclNode<Decl>;
def OMPThreadPrivate : DeclNode<Decl>;
def OMPAllocate : DeclNode<Decl>;
def OMPRequires : DeclNode<Decl>;
def Empty : DeclNode<Decl>;
def RequiresExprBody : DeclNode<Decl>, DeclContext;
def LifetimeExtendedTemporary : DeclNode<Decl>;
def HLSLBuffer : DeclNode<Named, "HLSLBuffer">, DeclContext;
