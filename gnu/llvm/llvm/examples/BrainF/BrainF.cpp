//===-- BrainF.cpp - BrainF compiler example ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class compiles the BrainF language into LLVM assembly.
//
// The BrainF language has 8 commands:
// Command   Equivalent C    Action
// -------   ------------    ------
// ,         *h=getchar();   Read a character from stdin, 255 on EOF
// .         putchar(*h);    Write a character to stdout
// -         --*h;           Decrement tape
// +         ++*h;           Increment tape
// <         --h;            Move head left
// >         ++h;            Move head right
// [         while(*h) {     Start loop
// ]         }               End loop
//
//===----------------------------------------------------------------------===//

#include "BrainF.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include <cstdlib>
#include <iostream>

using namespace llvm;

//Set the constants for naming
const char *BrainF::tapereg = "tape";
const char *BrainF::headreg = "head";
const char *BrainF::label   = "brainf";
const char *BrainF::testreg = "test";

Module *BrainF::parse(std::istream *in1, int mem, CompileFlags cf,
                      LLVMContext& Context) {
  in       = in1;
  memtotal = mem;
  comflag  = cf;

  header(Context);
  readloop(nullptr, nullptr, nullptr, Context);
  delete builder;
  return module;
}

void BrainF::header(LLVMContext& C) {
  module = new Module("BrainF", C);

  //Function prototypes

  //declare void @llvm.memset.p0i8.i32(i8 *, i8, i32, i1)
  Type *Tys[] = {PointerType::getUnqual(C), Type::getInt32Ty(C)};
  Function *memset_func = Intrinsic::getDeclaration(module, Intrinsic::memset,
                                                    Tys);

  //declare i32 @getchar()
  getchar_func =
      module->getOrInsertFunction("getchar", IntegerType::getInt32Ty(C));

  //declare i32 @putchar(i32)
  putchar_func = module->getOrInsertFunction(
      "putchar", IntegerType::getInt32Ty(C), IntegerType::getInt32Ty(C));

  //Function header

  //define void @brainf()
  brainf_func = Function::Create(FunctionType::get(Type::getVoidTy(C), false),
                                 Function::ExternalLinkage, "brainf", module);

  builder = new IRBuilder<>(BasicBlock::Create(C, label, brainf_func));

  //%arr = malloc i8, i32 %d
  ConstantInt *val_mem = ConstantInt::get(C, APInt(32, memtotal));
  Type* IntPtrTy = IntegerType::getInt32Ty(C);
  Type* Int8Ty = IntegerType::getInt8Ty(C);
  Constant* allocsize = ConstantExpr::getSizeOf(Int8Ty);
  allocsize = ConstantExpr::getTruncOrBitCast(allocsize, IntPtrTy);
  ptr_arr = builder->CreateMalloc(IntPtrTy, Int8Ty, allocsize, val_mem, nullptr,
                                  "arr");

  //call void @llvm.memset.p0i8.i32(i8 *%arr, i8 0, i32 %d, i1 0)
  {
    Value *memset_params[] = {
      ptr_arr,
      ConstantInt::get(C, APInt(8, 0)),
      val_mem,
      ConstantInt::get(C, APInt(1, 0))
    };

    CallInst *memset_call = builder->
      CreateCall(memset_func, memset_params);
    memset_call->setTailCall(false);
  }

  //%arrmax = getelementptr i8 *%arr, i32 %d
  if (comflag & flag_arraybounds) {
    ptr_arrmax = builder->CreateGEP(
        Int8Ty, ptr_arr, ConstantInt::get(C, APInt(32, memtotal)), "arrmax");
  }

  //%head.%d = getelementptr i8 *%arr, i32 %d
  curhead = builder->CreateGEP(
      Int8Ty, ptr_arr, ConstantInt::get(C, APInt(32, memtotal / 2)), headreg);

  //Function footer

  //brainf.end:
  endbb = BasicBlock::Create(C, label, brainf_func);

  // call free(i8 *%arr)
  builder->SetInsertPoint(endbb);
  builder->CreateFree(ptr_arr);

  //ret void
  ReturnInst::Create(C, endbb);

  //Error block for array out of bounds
  if (comflag & flag_arraybounds)
  {
    //@aberrormsg = internal constant [%d x i8] c"\00"
    Constant *msg_0 =
      ConstantDataArray::getString(C, "Error: The head has left the tape.",
                                   true);

    GlobalVariable *aberrormsg = new GlobalVariable(
      *module,
      msg_0->getType(),
      true,
      GlobalValue::InternalLinkage,
      msg_0,
      "aberrormsg");

    //declare i32 @puts(i8 *)
    FunctionCallee puts_func = module->getOrInsertFunction(
        "puts", IntegerType::getInt32Ty(C),
        PointerType::getUnqual(IntegerType::getInt8Ty(C)));

    //brainf.aberror:
    aberrorbb = BasicBlock::Create(C, label, brainf_func);

    //call i32 @puts(i8 *getelementptr([%d x i8] *@aberrormsg, i32 0, i32 0))
    {
      Constant *zero_32 = Constant::getNullValue(IntegerType::getInt32Ty(C));

      Constant *gep_params[] = {
        zero_32,
        zero_32
      };

      Constant *msgptr = ConstantExpr::
        getGetElementPtr(aberrormsg->getValueType(), aberrormsg, gep_params);

      Value *puts_params[] = {
        msgptr
      };

      CallInst *puts_call =
        CallInst::Create(puts_func,
                         puts_params,
                         "", aberrorbb);
      puts_call->setTailCall(false);
    }

    //br label %brainf.end
    BranchInst::Create(endbb, aberrorbb);
  }
}

void BrainF::readloop(PHINode *phi, BasicBlock *oldbb, BasicBlock *testbb,
                      LLVMContext &C) {
  Symbol cursym = SYM_NONE;
  int curvalue = 0;
  Symbol nextsym = SYM_NONE;
  int nextvalue = 0;
  char c;
  int loop;
  int direction;
  Type *Int8Ty = IntegerType::getInt8Ty(C);

  while(cursym != SYM_EOF && cursym != SYM_ENDLOOP) {
    // Write out commands
    switch(cursym) {
      case SYM_NONE:
        // Do nothing
        break;

      case SYM_READ:
        {
          //%tape.%d = call i32 @getchar()
          CallInst *getchar_call =
              builder->CreateCall(getchar_func, {}, tapereg);
          getchar_call->setTailCall(false);
          Value *tape_0 = getchar_call;

          //%tape.%d = trunc i32 %tape.%d to i8
          Value *tape_1 = builder->
            CreateTrunc(tape_0, IntegerType::getInt8Ty(C), tapereg);

          //store i8 %tape.%d, i8 *%head.%d
          builder->CreateStore(tape_1, curhead);
        }
        break;

      case SYM_WRITE:
        {
          //%tape.%d = load i8 *%head.%d
          LoadInst *tape_0 = builder->CreateLoad(Int8Ty, curhead, tapereg);

          //%tape.%d = sext i8 %tape.%d to i32
          Value *tape_1 = builder->
            CreateSExt(tape_0, IntegerType::getInt32Ty(C), tapereg);

          //call i32 @putchar(i32 %tape.%d)
          Value *putchar_params[] = {
            tape_1
          };
          CallInst *putchar_call = builder->
            CreateCall(putchar_func,
                       putchar_params);
          putchar_call->setTailCall(false);
        }
        break;

      case SYM_MOVE:
        {
          //%head.%d = getelementptr i8 *%head.%d, i32 %d
          curhead = builder->CreateGEP(Int8Ty, curhead,
                                       ConstantInt::get(C, APInt(32, curvalue)),
                                       headreg);

          //Error block for array out of bounds
          if (comflag & flag_arraybounds)
          {
            //%test.%d = icmp uge i8 *%head.%d, %arrmax
            Value *test_0 = builder->
              CreateICmpUGE(curhead, ptr_arrmax, testreg);

            //%test.%d = icmp ult i8 *%head.%d, %arr
            Value *test_1 = builder->
              CreateICmpULT(curhead, ptr_arr, testreg);

            //%test.%d = or i1 %test.%d, %test.%d
            Value *test_2 = builder->
              CreateOr(test_0, test_1, testreg);

            //br i1 %test.%d, label %main.%d, label %main.%d
            BasicBlock *nextbb = BasicBlock::Create(C, label, brainf_func);
            builder->CreateCondBr(test_2, aberrorbb, nextbb);

            //main.%d:
            builder->SetInsertPoint(nextbb);
          }
        }
        break;

      case SYM_CHANGE:
        {
          //%tape.%d = load i8 *%head.%d
          LoadInst *tape_0 = builder->CreateLoad(Int8Ty, curhead, tapereg);

          //%tape.%d = add i8 %tape.%d, %d
          Value *tape_1 = builder->
            CreateAdd(tape_0, ConstantInt::get(C, APInt(8, curvalue)), tapereg);

          //store i8 %tape.%d, i8 *%head.%d\n"
          builder->CreateStore(tape_1, curhead);
        }
        break;

      case SYM_LOOP:
        {
          //br label %main.%d
          BasicBlock *testbb = BasicBlock::Create(C, label, brainf_func);
          builder->CreateBr(testbb);

          //main.%d:
          BasicBlock *bb_0 = builder->GetInsertBlock();
          BasicBlock *bb_1 = BasicBlock::Create(C, label, brainf_func);
          builder->SetInsertPoint(bb_1);

          // Make part of PHI instruction now, wait until end of loop to finish
          PHINode *phi_0 = PHINode::Create(PointerType::getUnqual(Int8Ty), 2,
                                           headreg, testbb);
          phi_0->addIncoming(curhead, bb_0);
          curhead = phi_0;

          readloop(phi_0, bb_1, testbb, C);
        }
        break;

      default:
        std::cerr << "Error: Unknown symbol.\n";
        abort();
        break;
    }

    cursym = nextsym;
    curvalue = nextvalue;
    nextsym = SYM_NONE;

    // Reading stdin loop
    loop = (cursym == SYM_NONE)
        || (cursym == SYM_MOVE)
        || (cursym == SYM_CHANGE);
    while(loop) {
      *in>>c;
      if (in->eof()) {
        if (cursym == SYM_NONE) {
          cursym = SYM_EOF;
        } else {
          nextsym = SYM_EOF;
        }
        loop = 0;
      } else {
        direction = 1;
        switch(c) {
          case '-':
            direction = -1;
            [[fallthrough]];

          case '+':
            if (cursym == SYM_CHANGE) {
              curvalue += direction;
              // loop = 1
            } else {
              if (cursym == SYM_NONE) {
                cursym = SYM_CHANGE;
                curvalue = direction;
                // loop = 1
              } else {
                nextsym = SYM_CHANGE;
                nextvalue = direction;
                loop = 0;
              }
            }
            break;

          case '<':
            direction = -1;
            [[fallthrough]];

          case '>':
            if (cursym == SYM_MOVE) {
              curvalue += direction;
              // loop = 1
            } else {
              if (cursym == SYM_NONE) {
                cursym = SYM_MOVE;
                curvalue = direction;
                // loop = 1
              } else {
                nextsym = SYM_MOVE;
                nextvalue = direction;
                loop = 0;
              }
            }
            break;

          case ',':
            if (cursym == SYM_NONE) {
              cursym = SYM_READ;
            } else {
              nextsym = SYM_READ;
            }
            loop = 0;
            break;

          case '.':
            if (cursym == SYM_NONE) {
              cursym = SYM_WRITE;
            } else {
              nextsym = SYM_WRITE;
            }
            loop = 0;
            break;

          case '[':
            if (cursym == SYM_NONE) {
              cursym = SYM_LOOP;
            } else {
              nextsym = SYM_LOOP;
            }
            loop = 0;
            break;

          case ']':
            if (cursym == SYM_NONE) {
              cursym = SYM_ENDLOOP;
            } else {
              nextsym = SYM_ENDLOOP;
            }
            loop = 0;
            break;

          // Ignore other characters
          default:
            break;
        }
      }
    }
  }

  if (cursym == SYM_ENDLOOP) {
    if (!phi) {
      std::cerr << "Error: Extra ']'\n";
      abort();
    }

    // Write loop test
    {
      //br label %main.%d
      builder->CreateBr(testbb);

      //main.%d:

      //%head.%d = phi i8 *[%head.%d, %main.%d], [%head.%d, %main.%d]
      //Finish phi made at beginning of loop
      phi->addIncoming(curhead, builder->GetInsertBlock());
      Value *head_0 = phi;

      //%tape.%d = load i8 *%head.%d
      LoadInst *tape_0 = new LoadInst(Int8Ty, head_0, tapereg, testbb);

      //%test.%d = icmp eq i8 %tape.%d, 0
      ICmpInst *test_0 = new ICmpInst(testbb, ICmpInst::ICMP_EQ, tape_0,
                                    ConstantInt::get(C, APInt(8, 0)), testreg);

      //br i1 %test.%d, label %main.%d, label %main.%d
      BasicBlock *bb_0 = BasicBlock::Create(C, label, brainf_func);
      BranchInst::Create(bb_0, oldbb, test_0, testbb);

      //main.%d:
      builder->SetInsertPoint(bb_0);

      //%head.%d = phi i8 *[%head.%d, %main.%d]
      PHINode *phi_1 =
          builder->CreatePHI(PointerType::getUnqual(Int8Ty), 1, headreg);
      phi_1->addIncoming(head_0, testbb);
      curhead = phi_1;
    }

    return;
  }

  //End of the program, so go to return block
  builder->CreateBr(endbb);

  if (phi) {
    std::cerr << "Error: Missing ']'\n";
    abort();
  }
}
