use crate::classify;
use crate::expr::Expr;
#[cfg(feature = "full")]
use crate::expr::{
    ExprBreak, ExprRange, ExprRawAddr, ExprReference, ExprReturn, ExprUnary, ExprYield,
};
use crate::precedence::Precedence;
#[cfg(feature = "full")]
use crate::ty::ReturnType;

pub(crate) struct FixupContext {
    #[cfg(feature = "full")]
    previous_operator: Precedence,
    #[cfg(feature = "full")]
    next_operator: Precedence,

    // Print expression such that it can be parsed back as a statement
    // consisting of the original expression.
    //
    // The effect of this is for binary operators in statement position to set
    // `leftmost_subexpression_in_stmt` when printing their left-hand operand.
    //
    //     (match x {}) - 1;  // match needs parens when LHS of binary operator
    //
    //     match x {};  // not when its own statement
    //
    #[cfg(feature = "full")]
    stmt: bool,

    // This is the difference between:
    //
    //     (match x {}) - 1;  // subexpression needs parens
    //
    //     let _ = match x {} - 1;  // no parens
    //
    // There are 3 distinguishable contexts in which `print_expr` might be
    // called with the expression `$match` as its argument, where `$match`
    // represents an expression of kind `ExprKind::Match`:
    //
    //   - stmt=false leftmost_subexpression_in_stmt=false
    //
    //     Example: `let _ = $match - 1;`
    //
    //     No parentheses required.
    //
    //   - stmt=false leftmost_subexpression_in_stmt=true
    //
    //     Example: `$match - 1;`
    //
    //     Must parenthesize `($match)`, otherwise parsing back the output as a
    //     statement would terminate the statement after the closing brace of
    //     the match, parsing `-1;` as a separate statement.
    //
    //   - stmt=true leftmost_subexpression_in_stmt=false
    //
    //     Example: `$match;`
    //
    //     No parentheses required.
    #[cfg(feature = "full")]
    leftmost_subexpression_in_stmt: bool,

    // Print expression such that it can be parsed as a match arm.
    //
    // This is almost equivalent to `stmt`, but the grammar diverges a tiny bit
    // between statements and match arms when it comes to braced macro calls.
    // Macro calls with brace delimiter terminate a statement without a
    // semicolon, but do not terminate a match-arm without comma.
    //
    //     m! {} - 1;  // two statements: a macro call followed by -1 literal
    //
    //     match () {
    //         _ => m! {} - 1,  // binary subtraction operator
    //     }
    //
    #[cfg(feature = "full")]
    match_arm: bool,

    // This is almost equivalent to `leftmost_subexpression_in_stmt`, other than
    // for braced macro calls.
    //
    // If we have `m! {} - 1` as an expression, the leftmost subexpression
    // `m! {}` will need to be parenthesized in the statement case but not the
    // match-arm case.
    //
    //     (m! {}) - 1;  // subexpression needs parens
    //
    //     match () {
    //         _ => m! {} - 1,  // no parens
    //     }
    //
    #[cfg(feature = "full")]
    leftmost_subexpression_in_match_arm: bool,

    // This is the difference between:
    //
    //     if let _ = (Struct {}) {}  // needs parens
    //
    //     match () {
    //         () if let _ = Struct {} => {}  // no parens
    //     }
    //
    #[cfg(feature = "full")]
    condition: bool,

    // This is the difference between:
    //
    //     if break Struct {} == (break) {}  // needs parens
    //
    //     if break break == Struct {} {}  // no parens
    //
    #[cfg(feature = "full")]
    rightmost_subexpression_in_condition: bool,

    // This is the difference between:
    //
    //     if break ({ x }).field + 1 {}  needs parens
    //
    //     if break 1 + { x }.field {}  // no parens
    //
    #[cfg(feature = "full")]
    leftmost_subexpression_in_optional_operand: bool,

    // This is the difference between:
    //
    //     let _ = (return) - 1;  // without paren, this would return -1
    //
    //     let _ = return + 1;  // no paren because '+' cannot begin expr
    //
    #[cfg(feature = "full")]
    next_operator_can_begin_expr: bool,

    // This is the difference between:
    //
    //     let _ = 1 + return 1;  // no parens if rightmost subexpression
    //
    //     let _ = 1 + (return 1) + 1;  // needs parens
    //
    #[cfg(feature = "full")]
    next_operator_can_continue_expr: bool,

    // This is the difference between:
    //
    //     let _ = x as u8 + T;
    //
    //     let _ = (x as u8) < T;
    //
    // Without parens, the latter would want to parse `u8<T...` as a type.
    next_operator_can_begin_generics: bool,
}

impl FixupContext {
    /// The default amount of fixing is minimal fixing. Fixups should be turned
    /// on in a targeted fashion where needed.
    pub const NONE: Self = FixupContext {
        #[cfg(feature = "full")]
        previous_operator: Precedence::MIN,
        #[cfg(feature = "full")]
        next_operator: Precedence::MIN,
        #[cfg(feature = "full")]
        stmt: false,
        #[cfg(feature = "full")]
        leftmost_subexpression_in_stmt: false,
        #[cfg(feature = "full")]
        match_arm: false,
        #[cfg(feature = "full")]
        leftmost_subexpression_in_match_arm: false,
        #[cfg(feature = "full")]
        condition: false,
        #[cfg(feature = "full")]
        rightmost_subexpression_in_condition: false,
        #[cfg(feature = "full")]
        leftmost_subexpression_in_optional_operand: false,
        #[cfg(feature = "full")]
        next_operator_can_begin_expr: false,
        #[cfg(feature = "full")]
        next_operator_can_continue_expr: false,
        next_operator_can_begin_generics: false,
    };

    /// Create the initial fixup for printing an expression in statement
    /// position.
    #[cfg(feature = "full")]
    pub fn new_stmt() -> Self {
        FixupContext {
            stmt: true,
            ..FixupContext::NONE
        }
    }

    /// Create the initial fixup for printing an expression as the right-hand
    /// side of a match arm.
    #[cfg(feature = "full")]
    pub fn new_match_arm() -> Self {
        FixupContext {
            match_arm: true,
            ..FixupContext::NONE
        }
    }

    /// Create the initial fixup for printing an expression as the "condition"
    /// of an `if` or `while`. There are a few other positions which are
    /// grammatically equivalent and also use this, such as the iterator
    /// expression in `for` and the scrutinee in `match`.
    #[cfg(feature = "full")]
    pub fn new_condition() -> Self {
        FixupContext {
            condition: true,
            rightmost_subexpression_in_condition: true,
            ..FixupContext::NONE
        }
    }

    /// Transform this fixup into the one that should apply when printing the
    /// leftmost subexpression of the current expression.
    ///
    /// The leftmost subexpression is any subexpression that has the same first
    /// token as the current expression, but has a different last token.
    ///
    /// For example in `$a + $b` and `$a.method()`, the subexpression `$a` is a
    /// leftmost subexpression.
    ///
    /// Not every expression has a leftmost subexpression. For example neither
    /// `-$a` nor `[$a]` have one.
    pub fn leftmost_subexpression_with_operator(
        self,
        expr: &Expr,
        #[cfg(feature = "full")] next_operator_can_begin_expr: bool,
        next_operator_can_begin_generics: bool,
        #[cfg(feature = "full")] precedence: Precedence,
    ) -> (Precedence, Self) {
        let fixup = FixupContext {
            #[cfg(feature = "full")]
            next_operator: precedence,
            #[cfg(feature = "full")]
            stmt: false,
            #[cfg(feature = "full")]
            leftmost_subexpression_in_stmt: self.stmt || self.leftmost_subexpression_in_stmt,
            #[cfg(feature = "full")]
            match_arm: false,
            #[cfg(feature = "full")]
            leftmost_subexpression_in_match_arm: self.match_arm
                || self.leftmost_subexpression_in_match_arm,
            #[cfg(feature = "full")]
            rightmost_subexpression_in_condition: false,
            #[cfg(feature = "full")]
            next_operator_can_begin_expr,
            #[cfg(feature = "full")]
            next_operator_can_continue_expr: true,
            next_operator_can_begin_generics,
            ..self
        };

        (fixup.leftmost_subexpression_precedence(expr), fixup)
    }

    /// Transform this fixup into the one that should apply when printing a
    /// leftmost subexpression followed by a `.` or `?` token, which confer
    /// different statement boundary rules compared to other leftmost
    /// subexpressions.
    pub fn leftmost_subexpression_with_dot(self, expr: &Expr) -> (Precedence, Self) {
        let fixup = FixupContext {
            #[cfg(feature = "full")]
            next_operator: Precedence::Unambiguous,
            #[cfg(feature = "full")]
            stmt: self.stmt || self.leftmost_subexpression_in_stmt,
            #[cfg(feature = "full")]
            leftmost_subexpression_in_stmt: false,
            #[cfg(feature = "full")]
            match_arm: self.match_arm || self.leftmost_subexpression_in_match_arm,
            #[cfg(feature = "full")]
            leftmost_subexpression_in_match_arm: false,
            #[cfg(feature = "full")]
            rightmost_subexpression_in_condition: false,
            #[cfg(feature = "full")]
            next_operator_can_begin_expr: false,
            #[cfg(feature = "full")]
            next_operator_can_continue_expr: true,
            next_operator_can_begin_generics: false,
            ..self
        };

        (fixup.leftmost_subexpression_precedence(expr), fixup)
    }

    fn leftmost_subexpression_precedence(self, expr: &Expr) -> Precedence {
        #[cfg(feature = "full")]
        if !self.next_operator_can_begin_expr || self.next_operator == Precedence::Range {
            if let Scan::Bailout = scan_right(expr, self, Precedence::MIN, 0, 0) {
                if scan_left(expr, self) {
                    return Precedence::Unambiguous;
                }
            }
        }

        self.precedence(expr)
    }

    /// Transform this fixup into the one that should apply when printing the
    /// rightmost subexpression of the current expression.
    ///
    /// The rightmost subexpression is any subexpression that has a different
    /// first token than the current expression, but has the same last token.
    ///
    /// For example in `$a + $b` and `-$b`, the subexpression `$b` is a
    /// rightmost subexpression.
    ///
    /// Not every expression has a rightmost subexpression. For example neither
    /// `[$b]` nor `$a.f($b)` have one.
    pub fn rightmost_subexpression(
        self,
        expr: &Expr,
        #[cfg(feature = "full")] precedence: Precedence,
    ) -> (Precedence, Self) {
        let fixup = self.rightmost_subexpression_fixup(
            #[cfg(feature = "full")]
            false,
            #[cfg(feature = "full")]
            false,
            #[cfg(feature = "full")]
            precedence,
        );
        (fixup.rightmost_subexpression_precedence(expr), fixup)
    }

    pub fn rightmost_subexpression_fixup(
        self,
        #[cfg(feature = "full")] reset_allow_struct: bool,
        #[cfg(feature = "full")] optional_operand: bool,
        #[cfg(feature = "full")] precedence: Precedence,
    ) -> Self {
        FixupContext {
            #[cfg(feature = "full")]
            previous_operator: precedence,
            #[cfg(feature = "full")]
            stmt: false,
            #[cfg(feature = "full")]
            leftmost_subexpression_in_stmt: false,
            #[cfg(feature = "full")]
            match_arm: false,
            #[cfg(feature = "full")]
            leftmost_subexpression_in_match_arm: false,
            #[cfg(feature = "full")]
            condition: self.condition && !reset_allow_struct,
            #[cfg(feature = "full")]
            leftmost_subexpression_in_optional_operand: self.condition && optional_operand,
            ..self
        }
    }

    pub fn rightmost_subexpression_precedence(self, expr: &Expr) -> Precedence {
        let default_prec = self.precedence(expr);

        #[cfg(feature = "full")]
        if match self.previous_operator {
            Precedence::Assign | Precedence::Let | Precedence::Prefix => {
                default_prec < self.previous_operator
            }
            _ => default_prec <= self.previous_operator,
        } && match self.next_operator {
            Precedence::Range | Precedence::Or | Precedence::And => true,
            _ => !self.next_operator_can_begin_expr,
        } {
            if let Scan::Bailout | Scan::Fail = scan_right(expr, self, self.previous_operator, 1, 0)
            {
                if scan_left(expr, self) {
                    return Precedence::Prefix;
                }
            }
        }

        default_prec
    }

    /// Determine whether parentheses are needed around the given expression to
    /// head off the early termination of a statement or condition.
    #[cfg(feature = "full")]
    pub fn parenthesize(self, expr: &Expr) -> bool {
        (self.leftmost_subexpression_in_stmt && !classify::requires_semi_to_be_stmt(expr))
            || ((self.stmt || self.leftmost_subexpression_in_stmt) && matches!(expr, Expr::Let(_)))
            || (self.leftmost_subexpression_in_match_arm
                && !classify::requires_comma_to_be_match_arm(expr))
            || (self.condition && matches!(expr, Expr::Struct(_)))
            || (self.rightmost_subexpression_in_condition
                && matches!(
                    expr,
                    Expr::Return(ExprReturn { expr: None, .. })
                        | Expr::Yield(ExprYield { expr: None, .. })
                ))
            || (self.rightmost_subexpression_in_condition
                && !self.condition
                && matches!(
                    expr,
                    Expr::Break(ExprBreak { expr: None, .. })
                        | Expr::Path(_)
                        | Expr::Range(ExprRange { end: None, .. })
                ))
            || (self.leftmost_subexpression_in_optional_operand
                && matches!(expr, Expr::Block(expr) if expr.attrs.is_empty() && expr.label.is_none()))
    }

    /// Determines the effective precedence of a subexpression. Some expressions
    /// have higher or lower precedence when adjacent to particular operators.
    fn precedence(self, expr: &Expr) -> Precedence {
        #[cfg(feature = "full")]
        if self.next_operator_can_begin_expr {
            // Decrease precedence of value-less jumps when followed by an
            // operator that would otherwise get interpreted as beginning a
            // value for the jump.
            if let Expr::Break(ExprBreak { expr: None, .. })
            | Expr::Return(ExprReturn { expr: None, .. })
            | Expr::Yield(ExprYield { expr: None, .. }) = expr
            {
                return Precedence::Jump;
            }
        }

        #[cfg(feature = "full")]
        if !self.next_operator_can_continue_expr {
            match expr {
                // Increase precedence of expressions that extend to the end of
                // current statement or group.
                Expr::Break(_)
                | Expr::Closure(_)
                | Expr::Let(_)
                | Expr::Return(_)
                | Expr::Yield(_) => {
                    return Precedence::Prefix;
                }
                Expr::Range(e) if e.start.is_none() => return Precedence::Prefix,
                _ => {}
            }
        }

        if self.next_operator_can_begin_generics {
            if let Expr::Cast(cast) = expr {
                if classify::trailing_unparameterized_path(&cast.ty) {
                    return Precedence::MIN;
                }
            }
        }

        Precedence::of(expr)
    }
}

impl Copy for FixupContext {}

impl Clone for FixupContext {
    fn clone(&self) -> Self {
        *self
    }
}

#[cfg(feature = "full")]
enum Scan {
    Fail,
    Bailout,
    Consume,
}

#[cfg(feature = "full")]
impl Copy for Scan {}

#[cfg(feature = "full")]
impl Clone for Scan {
    fn clone(&self) -> Self {
        *self
    }
}

#[cfg(feature = "full")]
impl PartialEq for Scan {
    fn eq(&self, other: &Self) -> bool {
        *self as u8 == *other as u8
    }
}

#[cfg(feature = "full")]
fn scan_left(expr: &Expr, fixup: FixupContext) -> bool {
    match expr {
        Expr::Assign(_) => fixup.previous_operator <= Precedence::Assign,
        Expr::Binary(e) => match Precedence::of_binop(&e.op) {
            Precedence::Assign => fixup.previous_operator <= Precedence::Assign,
            binop_prec => fixup.previous_operator < binop_prec,
        },
        Expr::Cast(_) => fixup.previous_operator < Precedence::Cast,
        Expr::Range(e) => e.start.is_none() || fixup.previous_operator < Precedence::Assign,
        _ => true,
    }
}

#[cfg(feature = "full")]
fn scan_right(
    expr: &Expr,
    fixup: FixupContext,
    precedence: Precedence,
    fail_offset: u8,
    bailout_offset: u8,
) -> Scan {
    let consume_by_precedence = if match precedence {
        Precedence::Assign | Precedence::Compare => precedence <= fixup.next_operator,
        _ => precedence < fixup.next_operator,
    } || fixup.next_operator == Precedence::MIN
    {
        Scan::Consume
    } else {
        Scan::Bailout
    };
    if fixup.parenthesize(expr) {
        return consume_by_precedence;
    }
    match expr {
        Expr::Assign(e) if e.attrs.is_empty() => {
            if match fixup.next_operator {
                Precedence::Unambiguous => fail_offset >= 2,
                _ => bailout_offset >= 1,
            } {
                return Scan::Consume;
            }
            let right_fixup = fixup.rightmost_subexpression_fixup(false, false, Precedence::Assign);
            let scan = scan_right(
                &e.right,
                right_fixup,
                Precedence::Assign,
                match fixup.next_operator {
                    Precedence::Unambiguous => fail_offset,
                    _ => 1,
                },
                1,
            );
            if let Scan::Bailout | Scan::Consume = scan {
                Scan::Consume
            } else if let Precedence::Unambiguous = fixup.next_operator {
                Scan::Fail
            } else {
                Scan::Bailout
            }
        }
        Expr::Binary(e) if e.attrs.is_empty() => {
            if match fixup.next_operator {
                Precedence::Unambiguous => {
                    fail_offset >= 2
                        && (consume_by_precedence == Scan::Consume || bailout_offset >= 1)
                }
                _ => bailout_offset >= 1,
            } {
                return Scan::Consume;
            }
            let binop_prec = Precedence::of_binop(&e.op);
            if binop_prec == Precedence::Compare && fixup.next_operator == Precedence::Compare {
                return Scan::Consume;
            }
            let right_fixup = fixup.rightmost_subexpression_fixup(false, false, binop_prec);
            let scan = scan_right(
                &e.right,
                right_fixup,
                binop_prec,
                match fixup.next_operator {
                    Precedence::Unambiguous => fail_offset,
                    _ => 1,
                },
                consume_by_precedence as u8 - Scan::Bailout as u8,
            );
            match scan {
                Scan::Fail => {}
                Scan::Bailout => return consume_by_precedence,
                Scan::Consume => return Scan::Consume,
            }
            let right_needs_group = binop_prec != Precedence::Assign
                && right_fixup.rightmost_subexpression_precedence(&e.right) <= binop_prec;
            if right_needs_group {
                consume_by_precedence
            } else if let (Scan::Fail, Precedence::Unambiguous) = (scan, fixup.next_operator) {
                Scan::Fail
            } else {
                Scan::Bailout
            }
        }
        Expr::RawAddr(ExprRawAddr { expr, .. })
        | Expr::Reference(ExprReference { expr, .. })
        | Expr::Unary(ExprUnary { expr, .. }) => {
            if match fixup.next_operator {
                Precedence::Unambiguous => {
                    fail_offset >= 2
                        && (consume_by_precedence == Scan::Consume || bailout_offset >= 1)
                }
                _ => bailout_offset >= 1,
            } {
                return Scan::Consume;
            }
            let right_fixup = fixup.rightmost_subexpression_fixup(false, false, Precedence::Prefix);
            let scan = scan_right(
                expr,
                right_fixup,
                precedence,
                match fixup.next_operator {
                    Precedence::Unambiguous => fail_offset,
                    _ => 1,
                },
                consume_by_precedence as u8 - Scan::Bailout as u8,
            );
            match scan {
                Scan::Fail => {}
                Scan::Bailout => return consume_by_precedence,
                Scan::Consume => return Scan::Consume,
            }
            if right_fixup.rightmost_subexpression_precedence(expr) < Precedence::Prefix {
                consume_by_precedence
            } else if let (Scan::Fail, Precedence::Unambiguous) = (scan, fixup.next_operator) {
                Scan::Fail
            } else {
                Scan::Bailout
            }
        }
        Expr::Range(e) if e.attrs.is_empty() => match &e.end {
            Some(end) => {
                if fail_offset >= 2 {
                    return Scan::Consume;
                }
                let right_fixup =
                    fixup.rightmost_subexpression_fixup(false, true, Precedence::Range);
                let scan = scan_right(
                    end,
                    right_fixup,
                    Precedence::Range,
                    fail_offset,
                    match fixup.next_operator {
                        Precedence::Assign | Precedence::Range => 0,
                        _ => 1,
                    },
                );
                if match (scan, fixup.next_operator) {
                    (Scan::Fail, _) => false,
                    (Scan::Bailout, Precedence::Assign | Precedence::Range) => false,
                    (Scan::Bailout | Scan::Consume, _) => true,
                } {
                    return Scan::Consume;
                }
                if right_fixup.rightmost_subexpression_precedence(end) <= Precedence::Range {
                    Scan::Consume
                } else {
                    Scan::Fail
                }
            }
            None => {
                if fixup.next_operator_can_begin_expr {
                    Scan::Consume
                } else {
                    Scan::Fail
                }
            }
        },
        Expr::Break(e) => match &e.expr {
            Some(value) => {
                if bailout_offset >= 1 || e.label.is_none() && classify::expr_leading_label(value) {
                    return Scan::Consume;
                }
                let right_fixup = fixup.rightmost_subexpression_fixup(true, true, Precedence::Jump);
                match scan_right(value, right_fixup, Precedence::Jump, 1, 1) {
                    Scan::Fail => Scan::Bailout,
                    Scan::Bailout | Scan::Consume => Scan::Consume,
                }
            }
            None => match fixup.next_operator {
                Precedence::Assign if precedence > Precedence::Assign => Scan::Fail,
                _ => Scan::Consume,
            },
        },
        Expr::Return(ExprReturn { expr, .. }) | Expr::Yield(ExprYield { expr, .. }) => match expr {
            Some(e) => {
                if bailout_offset >= 1 {
                    return Scan::Consume;
                }
                let right_fixup =
                    fixup.rightmost_subexpression_fixup(true, false, Precedence::Jump);
                match scan_right(e, right_fixup, Precedence::Jump, 1, 1) {
                    Scan::Fail => Scan::Bailout,
                    Scan::Bailout | Scan::Consume => Scan::Consume,
                }
            }
            None => match fixup.next_operator {
                Precedence::Assign if precedence > Precedence::Assign => Scan::Fail,
                _ => Scan::Consume,
            },
        },
        Expr::Closure(e) => {
            if matches!(e.output, ReturnType::Default)
                || matches!(&*e.body, Expr::Block(body) if body.attrs.is_empty() && body.label.is_none())
            {
                if bailout_offset >= 1 {
                    return Scan::Consume;
                }
                let right_fixup =
                    fixup.rightmost_subexpression_fixup(false, false, Precedence::Jump);
                match scan_right(&e.body, right_fixup, Precedence::Jump, 1, 1) {
                    Scan::Fail => Scan::Bailout,
                    Scan::Bailout | Scan::Consume => Scan::Consume,
                }
            } else {
                Scan::Consume
            }
        }
        Expr::Let(e) => {
            if bailout_offset >= 1 {
                return Scan::Consume;
            }
            let right_fixup = fixup.rightmost_subexpression_fixup(false, false, Precedence::Let);
            let scan = scan_right(
                &e.expr,
                right_fixup,
                Precedence::Let,
                1,
                if fixup.next_operator < Precedence::Let {
                    0
                } else {
                    1
                },
            );
            match scan {
                Scan::Fail | Scan::Bailout if fixup.next_operator < Precedence::Let => {
                    return Scan::Bailout;
                }
                Scan::Consume => return Scan::Consume,
                _ => {}
            }
            if right_fixup.rightmost_subexpression_precedence(&e.expr) < Precedence::Let {
                Scan::Consume
            } else if let Scan::Fail = scan {
                Scan::Bailout
            } else {
                Scan::Consume
            }
        }
        Expr::Array(_)
        | Expr::Assign(_)
        | Expr::Async(_)
        | Expr::Await(_)
        | Expr::Binary(_)
        | Expr::Block(_)
        | Expr::Call(_)
        | Expr::Cast(_)
        | Expr::Const(_)
        | Expr::Continue(_)
        | Expr::Field(_)
        | Expr::ForLoop(_)
        | Expr::Group(_)
        | Expr::If(_)
        | Expr::Index(_)
        | Expr::Infer(_)
        | Expr::Lit(_)
        | Expr::Loop(_)
        | Expr::Macro(_)
        | Expr::Match(_)
        | Expr::MethodCall(_)
        | Expr::Paren(_)
        | Expr::Path(_)
        | Expr::Range(_)
        | Expr::Repeat(_)
        | Expr::Struct(_)
        | Expr::Try(_)
        | Expr::TryBlock(_)
        | Expr::Tuple(_)
        | Expr::Unsafe(_)
        | Expr::Verbatim(_)
        | Expr::While(_) => match fixup.next_operator {
            Precedence::Assign | Precedence::Range if precedence == Precedence::Range => Scan::Fail,
            _ if precedence == Precedence::Let && fixup.next_operator < Precedence::Let => {
                Scan::Fail
            }
            _ => consume_by_precedence,
        },
    }
}
