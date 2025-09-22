include "clang/Basic/ASTNode.td"

class CommentNode<CommentNode base, bit abstract = 0> : ASTNode {
  CommentNode Base = base;
  bit Abstract = abstract;
}

def Comment : CommentNode<?, 1>;
def InlineContentComment : CommentNode<Comment, 1>;
  def TextComment : CommentNode<InlineContentComment>;
  def InlineCommandComment : CommentNode<InlineContentComment>;
  def HTMLTagComment : CommentNode<InlineContentComment, 1>;
    def HTMLStartTagComment : CommentNode<HTMLTagComment>;
    def HTMLEndTagComment : CommentNode<HTMLTagComment>;

def BlockContentComment : CommentNode<Comment, 1>;
  def ParagraphComment : CommentNode<BlockContentComment>;
  def BlockCommandComment : CommentNode<BlockContentComment>;
    def ParamCommandComment : CommentNode<BlockCommandComment>;
    def TParamCommandComment : CommentNode<BlockCommandComment>;
    def VerbatimBlockComment : CommentNode<BlockCommandComment>;
    def VerbatimLineComment : CommentNode<BlockCommandComment>;

def VerbatimBlockLineComment : CommentNode<Comment>;

def FullComment : CommentNode<Comment>;

