//! A punctuated sequence of syntax tree nodes separated by punctuation.
//!
//! Lots of things in Rust are punctuated sequences.
//!
//! - The fields of a struct are `Punctuated<Field, Token![,]>`.
//! - The segments of a path are `Punctuated<PathSegment, Token![::]>`.
//! - The bounds on a generic parameter are `Punctuated<TypeParamBound,
//!   Token![+]>`.
//! - The arguments to a function call are `Punctuated<Expr, Token![,]>`.
//!
//! This module provides a common representation for these punctuated sequences
//! in the form of the [`Punctuated<T, P>`] type. We store a vector of pairs of
//! syntax tree node + punctuation, where every node in the sequence is followed
//! by punctuation except for possibly the final one.
//!
//! [`Punctuated<T, P>`]: Punctuated
//!
//! ```text
//! a_function_call(arg1, arg2, arg3);
//!                 ~~~~^ ~~~~^ ~~~~
//! ```

use crate::drops::{NoDrop, TrivialDrop};
#[cfg(feature = "parsing")]
use crate::error::Result;
#[cfg(feature = "parsing")]
use crate::parse::{Parse, ParseStream};
#[cfg(feature = "parsing")]
use crate::token::Token;
#[cfg(feature = "extra-traits")]
use std::fmt::{self, Debug};
#[cfg(feature = "extra-traits")]
use std::hash::{Hash, Hasher};
#[cfg(any(feature = "full", feature = "derive"))]
use std::iter;
use std::ops::{Index, IndexMut};
use std::option;
use std::slice;
use std::vec;

/// **A punctuated sequence of syntax tree nodes of type `T` separated by
/// punctuation of type `P`.**
///
/// Refer to the [module documentation] for details about punctuated sequences.
///
/// [module documentation]: self
pub struct Punctuated<T, P> {
    inner: Vec<(T, P)>,
    last: Option<Box<T>>,
}

impl<T, P> Punctuated<T, P> {
    /// Creates an empty punctuated sequence.
    pub const fn new() -> Self {
        Punctuated {
            inner: Vec::new(),
            last: None,
        }
    }

    /// Determines whether this punctuated sequence is empty, meaning it
    /// contains no syntax tree nodes or punctuation.
    pub fn is_empty(&self) -> bool {
        self.inner.len() == 0 && self.last.is_none()
    }

    /// Returns the number of syntax tree nodes in this punctuated sequence.
    ///
    /// This is the number of nodes of type `T`, not counting the punctuation of
    /// type `P`.
    pub fn len(&self) -> usize {
        self.inner.len() + if self.last.is_some() { 1 } else { 0 }
    }

    /// Borrows the first element in this sequence.
    pub fn first(&self) -> Option<&T> {
        self.iter().next()
    }

    /// Mutably borrows the first element in this sequence.
    pub fn first_mut(&mut self) -> Option<&mut T> {
        self.iter_mut().next()
    }

    /// Borrows the last element in this sequence.
    pub fn last(&self) -> Option<&T> {
        self.iter().next_back()
    }

    /// Mutably borrows the last element in this sequence.
    pub fn last_mut(&mut self) -> Option<&mut T> {
        self.iter_mut().next_back()
    }

    /// Borrows the element at the given index.
    pub fn get(&self, index: usize) -> Option<&T> {
        if let Some((value, _punct)) = self.inner.get(index) {
            Some(value)
        } else if index == self.inner.len() {
            self.last.as_deref()
        } else {
            None
        }
    }

    /// Mutably borrows the element at the given index.
    pub fn get_mut(&mut self, index: usize) -> Option<&mut T> {
        let inner_len = self.inner.len();
        if let Some((value, _punct)) = self.inner.get_mut(index) {
            Some(value)
        } else if index == inner_len {
            self.last.as_deref_mut()
        } else {
            None
        }
    }

    /// Returns an iterator over borrowed syntax tree nodes of type `&T`.
    pub fn iter(&self) -> Iter<T> {
        Iter {
            inner: Box::new(NoDrop::new(PrivateIter {
                inner: self.inner.iter(),
                last: self.last.as_ref().map(Box::as_ref).into_iter(),
            })),
        }
    }

    /// Returns an iterator over mutably borrowed syntax tree nodes of type
    /// `&mut T`.
    pub fn iter_mut(&mut self) -> IterMut<T> {
        IterMut {
            inner: Box::new(NoDrop::new(PrivateIterMut {
                inner: self.inner.iter_mut(),
                last: self.last.as_mut().map(Box::as_mut).into_iter(),
            })),
        }
    }

    /// Returns an iterator over the contents of this sequence as borrowed
    /// punctuated pairs.
    pub fn pairs(&self) -> Pairs<T, P> {
        Pairs {
            inner: self.inner.iter(),
            last: self.last.as_ref().map(Box::as_ref).into_iter(),
        }
    }

    /// Returns an iterator over the contents of this sequence as mutably
    /// borrowed punctuated pairs.
    pub fn pairs_mut(&mut self) -> PairsMut<T, P> {
        PairsMut {
            inner: self.inner.iter_mut(),
            last: self.last.as_mut().map(Box::as_mut).into_iter(),
        }
    }

    /// Returns an iterator over the contents of this sequence as owned
    /// punctuated pairs.
    pub fn into_pairs(self) -> IntoPairs<T, P> {
        IntoPairs {
            inner: self.inner.into_iter(),
            last: self.last.map(|t| *t).into_iter(),
        }
    }

    /// Appends a syntax tree node onto the end of this punctuated sequence. The
    /// sequence must already have a trailing punctuation, or be empty.
    ///
    /// Use [`push`] instead if the punctuated sequence may or may not already
    /// have trailing punctuation.
    ///
    /// [`push`]: Punctuated::push
    ///
    /// # Panics
    ///
    /// Panics if the sequence is nonempty and does not already have a trailing
    /// punctuation.
    pub fn push_value(&mut self, value: T) {
        assert!(
            self.empty_or_trailing(),
            "Punctuated::push_value: cannot push value if Punctuated is missing trailing punctuation",
        );

        self.last = Some(Box::new(value));
    }

    /// Appends a trailing punctuation onto the end of this punctuated sequence.
    /// The sequence must be non-empty and must not already have trailing
    /// punctuation.
    ///
    /// # Panics
    ///
    /// Panics if the sequence is empty or already has a trailing punctuation.
    pub fn push_punct(&mut self, punctuation: P) {
        assert!(
            self.last.is_some(),
            "Punctuated::push_punct: cannot push punctuation if Punctuated is empty or already has trailing punctuation",
        );

        let last = self.last.take().unwrap();
        self.inner.push((*last, punctuation));
    }

    /// Removes the last punctuated pair from this sequence, or `None` if the
    /// sequence is empty.
    pub fn pop(&mut self) -> Option<Pair<T, P>> {
        if self.last.is_some() {
            self.last.take().map(|t| Pair::End(*t))
        } else {
            self.inner.pop().map(|(t, p)| Pair::Punctuated(t, p))
        }
    }

    /// Removes the trailing punctuation from this punctuated sequence, or
    /// `None` if there isn't any.
    pub fn pop_punct(&mut self) -> Option<P> {
        if self.last.is_some() {
            None
        } else {
            let (t, p) = self.inner.pop()?;
            self.last = Some(Box::new(t));
            Some(p)
        }
    }

    /// Determines whether this punctuated sequence ends with a trailing
    /// punctuation.
    pub fn trailing_punct(&self) -> bool {
        self.last.is_none() && !self.is_empty()
    }

    /// Returns true if either this `Punctuated` is empty, or it has a trailing
    /// punctuation.
    ///
    /// Equivalent to `punctuated.is_empty() || punctuated.trailing_punct()`.
    pub fn empty_or_trailing(&self) -> bool {
        self.last.is_none()
    }

    /// Appends a syntax tree node onto the end of this punctuated sequence.
    ///
    /// If there is not a trailing punctuation in this sequence when this method
    /// is called, the default value of punctuation type `P` is inserted before
    /// the given value of type `T`.
    pub fn push(&mut self, value: T)
    where
        P: Default,
    {
        if !self.empty_or_trailing() {
            self.push_punct(Default::default());
        }
        self.push_value(value);
    }

    /// Inserts an element at position `index`.
    ///
    /// # Panics
    ///
    /// Panics if `index` is greater than the number of elements previously in
    /// this punctuated sequence.
    pub fn insert(&mut self, index: usize, value: T)
    where
        P: Default,
    {
        assert!(
            index <= self.len(),
            "Punctuated::insert: index out of range",
        );

        if index == self.len() {
            self.push(value);
        } else {
            self.inner.insert(index, (value, Default::default()));
        }
    }

    /// Clears the sequence of all values and punctuation, making it empty.
    pub fn clear(&mut self) {
        self.inner.clear();
        self.last = None;
    }

    /// Parses zero or more occurrences of `T` separated by punctuation of type
    /// `P`, with optional trailing punctuation.
    ///
    /// Parsing continues until the end of this parse stream. The entire content
    /// of this parse stream must consist of `T` and `P`.
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_terminated(input: ParseStream) -> Result<Self>
    where
        T: Parse,
        P: Parse,
    {
        Self::parse_terminated_with(input, T::parse)
    }

    /// Parses zero or more occurrences of `T` using the given parse function,
    /// separated by punctuation of type `P`, with optional trailing
    /// punctuation.
    ///
    /// Like [`parse_terminated`], the entire content of this stream is expected
    /// to be parsed.
    ///
    /// [`parse_terminated`]: Punctuated::parse_terminated
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_terminated_with<'a>(
        input: ParseStream<'a>,
        parser: fn(ParseStream<'a>) -> Result<T>,
    ) -> Result<Self>
    where
        P: Parse,
    {
        let mut punctuated = Punctuated::new();

        loop {
            if input.is_empty() {
                break;
            }
            let value = parser(input)?;
            punctuated.push_value(value);
            if input.is_empty() {
                break;
            }
            let punct = input.parse()?;
            punctuated.push_punct(punct);
        }

        Ok(punctuated)
    }

    /// Parses one or more occurrences of `T` separated by punctuation of type
    /// `P`, not accepting trailing punctuation.
    ///
    /// Parsing continues as long as punctuation `P` is present at the head of
    /// the stream. This method returns upon parsing a `T` and observing that it
    /// is not followed by a `P`, even if there are remaining tokens in the
    /// stream.
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_separated_nonempty(input: ParseStream) -> Result<Self>
    where
        T: Parse,
        P: Token + Parse,
    {
        Self::parse_separated_nonempty_with(input, T::parse)
    }

    /// Parses one or more occurrences of `T` using the given parse function,
    /// separated by punctuation of type `P`, not accepting trailing
    /// punctuation.
    ///
    /// Like [`parse_separated_nonempty`], may complete early without parsing
    /// the entire content of this stream.
    ///
    /// [`parse_separated_nonempty`]: Punctuated::parse_separated_nonempty
    #[cfg(feature = "parsing")]
    #[cfg_attr(docsrs, doc(cfg(feature = "parsing")))]
    pub fn parse_separated_nonempty_with<'a>(
        input: ParseStream<'a>,
        parser: fn(ParseStream<'a>) -> Result<T>,
    ) -> Result<Self>
    where
        P: Token + Parse,
    {
        let mut punctuated = Punctuated::new();

        loop {
            let value = parser(input)?;
            punctuated.push_value(value);
            if !P::peek(input.cursor()) {
                break;
            }
            let punct = input.parse()?;
            punctuated.push_punct(punct);
        }

        Ok(punctuated)
    }
}

#[cfg(feature = "clone-impls")]
#[cfg_attr(docsrs, doc(cfg(feature = "clone-impls")))]
impl<T, P> Clone for Punctuated<T, P>
where
    T: Clone,
    P: Clone,
{
    fn clone(&self) -> Self {
        Punctuated {
            inner: self.inner.clone(),
            last: self.last.clone(),
        }
    }

    fn clone_from(&mut self, other: &Self) {
        self.inner.clone_from(&other.inner);
        self.last.clone_from(&other.last);
    }
}

#[cfg(feature = "extra-traits")]
#[cfg_attr(docsrs, doc(cfg(feature = "extra-traits")))]
impl<T, P> Eq for Punctuated<T, P>
where
    T: Eq,
    P: Eq,
{
}

#[cfg(feature = "extra-traits")]
#[cfg_attr(docsrs, doc(cfg(feature = "extra-traits")))]
impl<T, P> PartialEq for Punctuated<T, P>
where
    T: PartialEq,
    P: PartialEq,
{
    fn eq(&self, other: &Self) -> bool {
        let Punctuated { inner, last } = self;
        *inner == other.inner && *last == other.last
    }
}

#[cfg(feature = "extra-traits")]
#[cfg_attr(docsrs, doc(cfg(feature = "extra-traits")))]
impl<T, P> Hash for Punctuated<T, P>
where
    T: Hash,
    P: Hash,
{
    fn hash<H: Hasher>(&self, state: &mut H) {
        let Punctuated { inner, last } = self;
        inner.hash(state);
        last.hash(state);
    }
}

#[cfg(feature = "extra-traits")]
#[cfg_attr(docsrs, doc(cfg(feature = "extra-traits")))]
impl<T: Debug, P: Debug> Debug for Punctuated<T, P> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let mut list = f.debug_list();
        for (t, p) in &self.inner {
            list.entry(t);
            list.entry(p);
        }
        if let Some(last) = &self.last {
            list.entry(last);
        }
        list.finish()
    }
}

impl<T, P> FromIterator<T> for Punctuated<T, P>
where
    P: Default,
{
    fn from_iter<I: IntoIterator<Item = T>>(i: I) -> Self {
        let mut ret = Punctuated::new();
        ret.extend(i);
        ret
    }
}

impl<T, P> Extend<T> for Punctuated<T, P>
where
    P: Default,
{
    fn extend<I: IntoIterator<Item = T>>(&mut self, i: I) {
        for value in i {
            self.push(value);
        }
    }
}

impl<T, P> FromIterator<Pair<T, P>> for Punctuated<T, P> {
    fn from_iter<I: IntoIterator<Item = Pair<T, P>>>(i: I) -> Self {
        let mut ret = Punctuated::new();
        do_extend(&mut ret, i.into_iter());
        ret
    }
}

impl<T, P> Extend<Pair<T, P>> for Punctuated<T, P>
where
    P: Default,
{
    fn extend<I: IntoIterator<Item = Pair<T, P>>>(&mut self, i: I) {
        if !self.empty_or_trailing() {
            self.push_punct(P::default());
        }
        do_extend(self, i.into_iter());
    }
}

fn do_extend<T, P, I>(punctuated: &mut Punctuated<T, P>, i: I)
where
    I: Iterator<Item = Pair<T, P>>,
{
    let mut nomore = false;
    for pair in i {
        if nomore {
            panic!("punctuated extended with items after a Pair::End");
        }
        match pair {
            Pair::Punctuated(a, b) => punctuated.inner.push((a, b)),
            Pair::End(a) => {
                punctuated.last = Some(Box::new(a));
                nomore = true;
            }
        }
    }
}

impl<T, P> IntoIterator for Punctuated<T, P> {
    type Item = T;
    type IntoIter = IntoIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        let mut elements = Vec::with_capacity(self.len());
        elements.extend(self.inner.into_iter().map(|pair| pair.0));
        elements.extend(self.last.map(|t| *t));

        IntoIter {
            inner: elements.into_iter(),
        }
    }
}

impl<'a, T, P> IntoIterator for &'a Punctuated<T, P> {
    type Item = &'a T;
    type IntoIter = Iter<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        Punctuated::iter(self)
    }
}

impl<'a, T, P> IntoIterator for &'a mut Punctuated<T, P> {
    type Item = &'a mut T;
    type IntoIter = IterMut<'a, T>;

    fn into_iter(self) -> Self::IntoIter {
        Punctuated::iter_mut(self)
    }
}

impl<T, P> Default for Punctuated<T, P> {
    fn default() -> Self {
        Punctuated::new()
    }
}

/// An iterator over borrowed pairs of type `Pair<&T, &P>`.
///
/// Refer to the [module documentation] for details about punctuated sequences.
///
/// [module documentation]: self
pub struct Pairs<'a, T: 'a, P: 'a> {
    inner: slice::Iter<'a, (T, P)>,
    last: option::IntoIter<&'a T>,
}

impl<'a, T, P> Iterator for Pairs<'a, T, P> {
    type Item = Pair<&'a T, &'a P>;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner
            .next()
            .map(|(t, p)| Pair::Punctuated(t, p))
            .or_else(|| self.last.next().map(Pair::End))
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.len(), Some(self.len()))
    }
}

impl<'a, T, P> DoubleEndedIterator for Pairs<'a, T, P> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.last
            .next()
            .map(Pair::End)
            .or_else(|| self.inner.next_back().map(|(t, p)| Pair::Punctuated(t, p)))
    }
}

impl<'a, T, P> ExactSizeIterator for Pairs<'a, T, P> {
    fn len(&self) -> usize {
        self.inner.len() + self.last.len()
    }
}

// No Clone bound on T or P.
impl<'a, T, P> Clone for Pairs<'a, T, P> {
    fn clone(&self) -> Self {
        Pairs {
            inner: self.inner.clone(),
            last: self.last.clone(),
        }
    }
}

/// An iterator over mutably borrowed pairs of type `Pair<&mut T, &mut P>`.
///
/// Refer to the [module documentation] for details about punctuated sequences.
///
/// [module documentation]: self
pub struct PairsMut<'a, T: 'a, P: 'a> {
    inner: slice::IterMut<'a, (T, P)>,
    last: option::IntoIter<&'a mut T>,
}

impl<'a, T, P> Iterator for PairsMut<'a, T, P> {
    type Item = Pair<&'a mut T, &'a mut P>;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner
            .next()
            .map(|(t, p)| Pair::Punctuated(t, p))
            .or_else(|| self.last.next().map(Pair::End))
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.len(), Some(self.len()))
    }
}

impl<'a, T, P> DoubleEndedIterator for PairsMut<'a, T, P> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.last
            .next()
            .map(Pair::End)
            .or_else(|| self.inner.next_back().map(|(t, p)| Pair::Punctuated(t, p)))
    }
}

impl<'a, T, P> ExactSizeIterator for PairsMut<'a, T, P> {
    fn len(&self) -> usize {
        self.inner.len() + self.last.len()
    }
}

/// An iterator over owned pairs of type `Pair<T, P>`.
///
/// Refer to the [module documentation] for details about punctuated sequences.
///
/// [module documentation]: self
pub struct IntoPairs<T, P> {
    inner: vec::IntoIter<(T, P)>,
    last: option::IntoIter<T>,
}

impl<T, P> Iterator for IntoPairs<T, P> {
    type Item = Pair<T, P>;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner
            .next()
            .map(|(t, p)| Pair::Punctuated(t, p))
            .or_else(|| self.last.next().map(Pair::End))
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.len(), Some(self.len()))
    }
}

impl<T, P> DoubleEndedIterator for IntoPairs<T, P> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.last
            .next()
            .map(Pair::End)
            .or_else(|| self.inner.next_back().map(|(t, p)| Pair::Punctuated(t, p)))
    }
}

impl<T, P> ExactSizeIterator for IntoPairs<T, P> {
    fn len(&self) -> usize {
        self.inner.len() + self.last.len()
    }
}

impl<T, P> Clone for IntoPairs<T, P>
where
    T: Clone,
    P: Clone,
{
    fn clone(&self) -> Self {
        IntoPairs {
            inner: self.inner.clone(),
            last: self.last.clone(),
        }
    }
}

/// An iterator over owned values of type `T`.
///
/// Refer to the [module documentation] for details about punctuated sequences.
///
/// [module documentation]: self
pub struct IntoIter<T> {
    inner: vec::IntoIter<T>,
}

impl<T> Iterator for IntoIter<T> {
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next()
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.len(), Some(self.len()))
    }
}

impl<T> DoubleEndedIterator for IntoIter<T> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.inner.next_back()
    }
}

impl<T> ExactSizeIterator for IntoIter<T> {
    fn len(&self) -> usize {
        self.inner.len()
    }
}

impl<T> Clone for IntoIter<T>
where
    T: Clone,
{
    fn clone(&self) -> Self {
        IntoIter {
            inner: self.inner.clone(),
        }
    }
}

/// An iterator over borrowed values of type `&T`.
///
/// Refer to the [module documentation] for details about punctuated sequences.
///
/// [module documentation]: self
pub struct Iter<'a, T: 'a> {
    inner: Box<NoDrop<dyn IterTrait<'a, T> + 'a>>,
}

trait IterTrait<'a, T: 'a>: Iterator<Item = &'a T> + DoubleEndedIterator + ExactSizeIterator {
    fn clone_box(&self) -> Box<NoDrop<dyn IterTrait<'a, T> + 'a>>;
}

struct PrivateIter<'a, T: 'a, P: 'a> {
    inner: slice::Iter<'a, (T, P)>,
    last: option::IntoIter<&'a T>,
}

impl<'a, T, P> TrivialDrop for PrivateIter<'a, T, P>
where
    slice::Iter<'a, (T, P)>: TrivialDrop,
    option::IntoIter<&'a T>: TrivialDrop,
{
}

#[cfg(any(feature = "full", feature = "derive"))]
pub(crate) fn empty_punctuated_iter<'a, T>() -> Iter<'a, T> {
    Iter {
        inner: Box::new(NoDrop::new(iter::empty())),
    }
}

// No Clone bound on T.
impl<'a, T> Clone for Iter<'a, T> {
    fn clone(&self) -> Self {
        Iter {
            inner: self.inner.clone_box(),
        }
    }
}

impl<'a, T> Iterator for Iter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next()
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.len(), Some(self.len()))
    }
}

impl<'a, T> DoubleEndedIterator for Iter<'a, T> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.inner.next_back()
    }
}

impl<'a, T> ExactSizeIterator for Iter<'a, T> {
    fn len(&self) -> usize {
        self.inner.len()
    }
}

impl<'a, T, P> Iterator for PrivateIter<'a, T, P> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner
            .next()
            .map(|pair| &pair.0)
            .or_else(|| self.last.next())
    }
}

impl<'a, T, P> DoubleEndedIterator for PrivateIter<'a, T, P> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.last
            .next()
            .or_else(|| self.inner.next_back().map(|pair| &pair.0))
    }
}

impl<'a, T, P> ExactSizeIterator for PrivateIter<'a, T, P> {
    fn len(&self) -> usize {
        self.inner.len() + self.last.len()
    }
}

// No Clone bound on T or P.
impl<'a, T, P> Clone for PrivateIter<'a, T, P> {
    fn clone(&self) -> Self {
        PrivateIter {
            inner: self.inner.clone(),
            last: self.last.clone(),
        }
    }
}

impl<'a, T, I> IterTrait<'a, T> for I
where
    T: 'a,
    I: DoubleEndedIterator<Item = &'a T>
        + ExactSizeIterator<Item = &'a T>
        + Clone
        + TrivialDrop
        + 'a,
{
    fn clone_box(&self) -> Box<NoDrop<dyn IterTrait<'a, T> + 'a>> {
        Box::new(NoDrop::new(self.clone()))
    }
}

/// An iterator over mutably borrowed values of type `&mut T`.
///
/// Refer to the [module documentation] for details about punctuated sequences.
///
/// [module documentation]: self
pub struct IterMut<'a, T: 'a> {
    inner: Box<NoDrop<dyn IterMutTrait<'a, T, Item = &'a mut T> + 'a>>,
}

trait IterMutTrait<'a, T: 'a>:
    DoubleEndedIterator<Item = &'a mut T> + ExactSizeIterator<Item = &'a mut T>
{
}

struct PrivateIterMut<'a, T: 'a, P: 'a> {
    inner: slice::IterMut<'a, (T, P)>,
    last: option::IntoIter<&'a mut T>,
}

impl<'a, T, P> TrivialDrop for PrivateIterMut<'a, T, P>
where
    slice::IterMut<'a, (T, P)>: TrivialDrop,
    option::IntoIter<&'a mut T>: TrivialDrop,
{
}

#[cfg(any(feature = "full", feature = "derive"))]
pub(crate) fn empty_punctuated_iter_mut<'a, T>() -> IterMut<'a, T> {
    IterMut {
        inner: Box::new(NoDrop::new(iter::empty())),
    }
}

impl<'a, T> Iterator for IterMut<'a, T> {
    type Item = &'a mut T;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next()
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.len(), Some(self.len()))
    }
}

impl<'a, T> DoubleEndedIterator for IterMut<'a, T> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.inner.next_back()
    }
}

impl<'a, T> ExactSizeIterator for IterMut<'a, T> {
    fn len(&self) -> usize {
        self.inner.len()
    }
}

impl<'a, T, P> Iterator for PrivateIterMut<'a, T, P> {
    type Item = &'a mut T;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner
            .next()
            .map(|pair| &mut pair.0)
            .or_else(|| self.last.next())
    }
}

impl<'a, T, P> DoubleEndedIterator for PrivateIterMut<'a, T, P> {
    fn next_back(&mut self) -> Option<Self::Item> {
        self.last
            .next()
            .or_else(|| self.inner.next_back().map(|pair| &mut pair.0))
    }
}

impl<'a, T, P> ExactSizeIterator for PrivateIterMut<'a, T, P> {
    fn len(&self) -> usize {
        self.inner.len() + self.last.len()
    }
}

impl<'a, T, I> IterMutTrait<'a, T> for I
where
    T: 'a,
    I: DoubleEndedIterator<Item = &'a mut T> + ExactSizeIterator<Item = &'a mut T> + 'a,
{
}

/// A single syntax tree node of type `T` followed by its trailing punctuation
/// of type `P` if any.
///
/// Refer to the [module documentation] for details about punctuated sequences.
///
/// [module documentation]: self
pub enum Pair<T, P> {
    Punctuated(T, P),
    End(T),
}

impl<T, P> Pair<T, P> {
    /// Extracts the syntax tree node from this punctuated pair, discarding the
    /// following punctuation.
    pub fn into_value(self) -> T {
        match self {
            Pair::Punctuated(t, _) | Pair::End(t) => t,
        }
    }

    /// Borrows the syntax tree node from this punctuated pair.
    pub fn value(&self) -> &T {
        match self {
            Pair::Punctuated(t, _) | Pair::End(t) => t,
        }
    }

    /// Mutably borrows the syntax tree node from this punctuated pair.
    pub fn value_mut(&mut self) -> &mut T {
        match self {
            Pair::Punctuated(t, _) | Pair::End(t) => t,
        }
    }

    /// Borrows the punctuation from this punctuated pair, unless this pair is
    /// the final one and there is no trailing punctuation.
    pub fn punct(&self) -> Option<&P> {
        match self {
            Pair::Punctuated(_, p) => Some(p),
            Pair::End(_) => None,
        }
    }

    /// Mutably borrows the punctuation from this punctuated pair, unless the
    /// pair is the final one and there is no trailing punctuation.
    ///
    /// # Example
    ///
    /// ```
    /// # use proc_macro2::Span;
    /// # use syn::punctuated::Punctuated;
    /// # use syn::{parse_quote, Token, TypeParamBound};
    /// #
    /// # let mut punctuated = Punctuated::<TypeParamBound, Token![+]>::new();
    /// # let span = Span::call_site();
    /// #
    /// punctuated.insert(0, parse_quote!('lifetime));
    /// if let Some(punct) = punctuated.pairs_mut().next().unwrap().punct_mut() {
    ///     punct.span = span;
    /// }
    /// ```
    pub fn punct_mut(&mut self) -> Option<&mut P> {
        match self {
            Pair::Punctuated(_, p) => Some(p),
            Pair::End(_) => None,
        }
    }

    /// Creates a punctuated pair out of a syntax tree node and an optional
    /// following punctuation.
    pub fn new(t: T, p: Option<P>) -> Self {
        match p {
            Some(p) => Pair::Punctuated(t, p),
            None => Pair::End(t),
        }
    }

    /// Produces this punctuated pair as a tuple of syntax tree node and
    /// optional following punctuation.
    pub fn into_tuple(self) -> (T, Option<P>) {
        match self {
            Pair::Punctuated(t, p) => (t, Some(p)),
            Pair::End(t) => (t, None),
        }
    }
}

#[cfg(feature = "clone-impls")]
#[cfg_attr(docsrs, doc(cfg(feature = "clone-impls")))]
impl<T, P> Pair<&T, &P> {
    pub fn cloned(self) -> Pair<T, P>
    where
        T: Clone,
        P: Clone,
    {
        match self {
            Pair::Punctuated(t, p) => Pair::Punctuated(t.clone(), p.clone()),
            Pair::End(t) => Pair::End(t.clone()),
        }
    }
}

#[cfg(feature = "clone-impls")]
#[cfg_attr(docsrs, doc(cfg(feature = "clone-impls")))]
impl<T, P> Clone for Pair<T, P>
where
    T: Clone,
    P: Clone,
{
    fn clone(&self) -> Self {
        match self {
            Pair::Punctuated(t, p) => Pair::Punctuated(t.clone(), p.clone()),
            Pair::End(t) => Pair::End(t.clone()),
        }
    }
}

#[cfg(feature = "clone-impls")]
#[cfg_attr(docsrs, doc(cfg(feature = "clone-impls")))]
impl<T, P> Copy for Pair<T, P>
where
    T: Copy,
    P: Copy,
{
}

impl<T, P> Index<usize> for Punctuated<T, P> {
    type Output = T;

    fn index(&self, index: usize) -> &Self::Output {
        if index == self.len() - 1 {
            match &self.last {
                Some(t) => t,
                None => &self.inner[index].0,
            }
        } else {
            &self.inner[index].0
        }
    }
}

impl<T, P> IndexMut<usize> for Punctuated<T, P> {
    fn index_mut(&mut self, index: usize) -> &mut Self::Output {
        if index == self.len() - 1 {
            match &mut self.last {
                Some(t) => t,
                None => &mut self.inner[index].0,
            }
        } else {
            &mut self.inner[index].0
        }
    }
}

#[cfg(all(feature = "fold", any(feature = "full", feature = "derive")))]
pub(crate) fn fold<T, P, V, F>(
    punctuated: Punctuated<T, P>,
    fold: &mut V,
    mut f: F,
) -> Punctuated<T, P>
where
    V: ?Sized,
    F: FnMut(&mut V, T) -> T,
{
    Punctuated {
        inner: punctuated
            .inner
            .into_iter()
            .map(|(t, p)| (f(fold, t), p))
            .collect(),
        last: match punctuated.last {
            Some(t) => Some(Box::new(f(fold, *t))),
            None => None,
        },
    }
}

#[cfg(feature = "printing")]
mod printing {
    use crate::punctuated::{Pair, Punctuated};
    use proc_macro2::TokenStream;
    use quote::{ToTokens, TokenStreamExt};

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl<T, P> ToTokens for Punctuated<T, P>
    where
        T: ToTokens,
        P: ToTokens,
    {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            tokens.append_all(self.pairs());
        }
    }

    #[cfg_attr(docsrs, doc(cfg(feature = "printing")))]
    impl<T, P> ToTokens for Pair<T, P>
    where
        T: ToTokens,
        P: ToTokens,
    {
        fn to_tokens(&self, tokens: &mut TokenStream) {
            match self {
                Pair::Punctuated(a, b) => {
                    a.to_tokens(tokens);
                    b.to_tokens(tokens);
                }
                Pair::End(a) => a.to_tokens(tokens),
            }
        }
    }
}
