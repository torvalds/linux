# Sandbox IR: A transactional layer over LLVM IR

Sandbox IR is an IR layer on top of LLVM IR that allows you to save/restore its state.

# API
The Sandbox IR API is designed to feel like LLVM, replicating many common API classes and functions to mirror the LLVM API.
The class hierarchy is similar (but in the `llvm::sandboxir` namespace).
For example here is a small part of it:
```
namespace sandboxir {
              Value
              /  \
            User BasicBlock ...
           /   \
  Instruction Constant
        /
     ...
}
```

# Design

## Sandbox IR Value <-> LLVM IR Value Mapping
Each LLVM IR Value maps to a single Sandbox IR Value.
The reverse is also true in most cases, except for Sandbox IR Instructions that map to more than one LLVM IR Instruction.
Such instructions can be defined in extensions of the base Sandbox IR.

- Forward mapping: Sandbox IR Value -> LLVM IR Value
Each Sandbox IR Value contains an `llvm::Value *Val` member variable that points to the corresponding LLVM IR Value.

- Reverse mapping: LLVM IR Value -> Sandbox IR Value
This mapping is stored in `sandboxir::Context::LLVMValueToValue`.

For example `sandboxir::User::getOperand(OpIdx)` for a `sandboxir::User *U` works as follows:
- First we find the LLVM User: `llvm::User *LLVMU = U->Val`.
- Next we get the LLVM Value operand: `llvm::Value *LLVMOp = LLVMU->getOperand(OpIdx)`
- Finally we get the Sandbox IR operand that corresponds to `LLVMOp` by querying the map in the Sandbox IR context: `retrun Ctx.getValue(LLVMOp)`.

## Sandbox IR is Write-Through
Sandbox IR is designed to rely on LLVM IR for its state.
So any change made to Sandbox IR objects directly updates the corresponding LLVM IR.

This has the following benefits:
- It minimizes the replication of state, and
- It makes sure that Sandbox IR and LLVM IR are always in sync, which helps avoid bugs and removes the need for a lowering step.
- No need for serialization/de-serialization infrastructure as we can rely on LLVM IR for it.
- One can pass actual `llvm::Instruction`s to cost modeling APIs.

Sandbox IR API functions that modify the IR state call the corresponding LLVM IR function that modifies the LLVM IR's state.
For example, for `sandboxir::User::setOperand(OpIdx, sandboxir::Value *Op)`:
- We get the corresponding LLVM User: `llvm::User *LLVMU = cast<llvm::User>(Val)`
- Next we get the corresponding LLVM Operand: `llvm::Value *LLVMOp = Op->Val`
- Finally we modify `LLVMU`'s operand: `LLVMU->setOperand(OpIdx, LLVMOp)

## IR Change Tracking
Sandbox IR's state can be saved and restored.
This is done with the help of the tracker component that is tightly coupled to the public Sandbox IR API functions.
Please note that nested saves/restores are currently not supported.

To save the state and enable tracking the user needs to call `sandboxir::Context::save()`.
From this point on any change made to the Sandbox IR state will automatically create a change object and register it with the tracker, without any intervention from the user.
The changes are accumulated in a vector within the tracker.

To rollback to the saved state the user needs to call `sandboxir::Context::revert()`.
Reverting back to the saved state is a matter of going over all the accumulated changes in reverse and undoing each individual change.

To accept the changes made to the IR the user needs to call `sandboxir::Context::accept()`.
Internally this will go through the changes and run any finalization required.

Please note that after a call to `revert()` or `accept()` tracking will stop.
To start tracking again, the user needs to call `save()`.
