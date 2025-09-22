//===-- AMDGPUPredicateControl.td --------------------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def TruePredicate : Predicate<"">;

// FIXME: Tablegen should specially supports this
def FalsePredicate : Predicate<"false">;

// Prevent using other kinds of predicates where True16 predicates are
// expected by giving them their own class.
class True16PredicateClass<string cond> : Predicate<cond>;
def NoTrue16Predicate : True16PredicateClass<"">;

class PredicateControl {
  Predicate SubtargetPredicate = TruePredicate;
  Predicate AssemblerPredicate = TruePredicate;
  Predicate WaveSizePredicate = TruePredicate;
  True16PredicateClass True16Predicate = NoTrue16Predicate;
  list<Predicate> OtherPredicates = [];
  list<Predicate> Predicates =
      !foldl(OtherPredicates, [SubtargetPredicate, AssemblerPredicate,
                               WaveSizePredicate, True16Predicate],
             preds, p,
             preds # !listremove([p], [TruePredicate, NoTrue16Predicate] # preds));
}
