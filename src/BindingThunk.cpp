#include "BindingThunk.hpp"
#include "Context.hpp"
#include <stdexcept>
#include <string>
#include <vector>

FuncSignature ShiftSignature(const FuncSignature& InSignature);
FThunkPtr GenerateSimpleShift(void *ToFn, void *BindParam, FuncArgInfo& Src, FuncArgInfo& Dest);
FThunkPtr GenerateComplexShift(void *ToFn, void *BindParam, FuncArgInfo& Src, FuncArgInfo& Dest);
FThunkPtr GenerateShiftWithRegisterContext(void *ToFn, void *BindParam, FuncArgInfo& Src, FuncArgInfo& Dest);

FThunkPtr GenerateBindingThunk(void *ToFn, void *BindParam, FuncSignature Signature, EBindingThunkType Type) {
    auto InvokeSignature = ShiftSignature(Signature);
    FuncArgInfo SrcSig{Signature};
    FuncArgInfo DestSig{InvokeSignature};

    switch (Type) {
        case EBindingThunkType::Default: {
            auto StackAllocSize = DestSig.Detail().arg_stack_size() - DestSig.Detail().red_zone_size() - DestSig.Detail().spill_zone_size();
            return StackAllocSize > 0 ? GenerateComplexShift(ToFn, BindParam, SrcSig, DestSig) : GenerateSimpleShift(ToFn, BindParam, SrcSig, DestSig);
        }
        case EBindingThunkType::WithRegisterContext: {
            return GenerateShiftWithRegisterContext(ToFn, BindParam, SrcSig, DestSig);
        }
        default: {
            return nullptr;
        }
    }
}

FThunkPtr GenerateSimpleShift(void* ToFn, void* BindParam, FuncArgInfo& Src, FuncArgInfo& Dest) {
    if (Src.Signature().arg_count() >= Dest.Signature().arg_count()) throw std::runtime_error("GenerateSimpleShift Src arg count is >= Dest arg count!");

    asmjit::CodeHolder Code{};
    Code.set_logger(GetLogger());
    Code.set_error_handler(GetErrorHandler());
    Code.init(GetJitRuntime().environment(), GetJitRuntime().cpu_features());
    Assembler TheAssembler { &Code };

    for (int32_t ArgIndex = Src.GetArguments().size() - 1; ArgIndex >= 0; --ArgIndex) {
        const auto& SrcValue = Src.GetArguments()[ArgIndex];
        const auto& DestValue = Dest.GetArguments()[ArgIndex + 1];
        if (SrcValue.type_id() != DestValue.type_id()) throw std::runtime_error("Malformed Dest value at index " + std::to_string(ArgIndex) + " in GenerateSimpleShift!");
        if (SrcValue.reg_type() >= asmjit::RegType::kGp8Lo && SrcValue.reg_type() <= asmjit::RegType::kGp64) {
            TheAssembler.mov(asmjit::x86::gpq(DestValue.reg_id()), asmjit::x86::gpq(SrcValue.reg_id()));
        }
        else {
            TheAssembler.movaps(asmjit::x86::xmm(DestValue.reg_id()), asmjit::x86::xmm(SrcValue.reg_id()));
        }
    }

    TheAssembler.mov(asmjit::x86::gpq(Dest.GetArguments()[0].reg_id()), BindParam);
    TheAssembler.jmp(ToFn);
    if (TheAssembler.finalize() != asmjit::Error::kOk) return nullptr;
    void* Temp{};
    if (GetJitRuntime().add(&Temp, &Code) != asmjit::Error::kOk) return nullptr;
    return FThunkPtr { Temp };
}

FThunkPtr GenerateComplexShift(void *ToFn, void* BindParam, FuncArgInfo& Src, FuncArgInfo& Dest) {
    asmjit::CodeHolder Code{};
    Code.set_logger(GetLogger());
    Code.set_error_handler(GetErrorHandler());
    Code.init(GetJitRuntime().environment(), GetJitRuntime().cpu_features());
    asmjit::x86::Compiler TheCompiler { &Code };

    asmjit::InvokeNode* Invoker{};
    auto* ThisFunc = TheCompiler.add_func(Src.Signature());
    TheCompiler.invoke(asmjit::Out(Invoker), ToFn, Dest.Signature());

    // ReSharper disable once CppDFAConstantConditions
    if (!Invoker) return nullptr;

    // ReSharper disable once CppDFAUnreachableCode
    Invoker->set_arg(0, BindParam);

    for (uint32_t InArgIndex = 0; InArgIndex < Src.Signature().arg_count(); ++InArgIndex) {
        if (const auto Id = Src.Signature().arg(InArgIndex); Id >= asmjit::TypeId::_kIntStart && Id <= asmjit::TypeId::_kIntEnd) {
            auto VReg = TheCompiler.new_gp64();
            ThisFunc->set_arg(InArgIndex, VReg);
            Invoker->set_arg(InArgIndex + 1, VReg);
        }
        else {
            auto VReg = TheCompiler.new_xmm();
            ThisFunc->set_arg(InArgIndex, VReg);
            Invoker->set_arg(InArgIndex + 1, VReg);
        }
    }

    if (Src.Signature().has_ret()) {
        if (const auto Id = Src.Signature().ret(); Id >= asmjit::TypeId::_kIntStart && Id <= asmjit::TypeId::_kIntEnd) {
            auto VReg = TheCompiler.new_gp64();
            Invoker->set_arg(0, VReg);
            TheCompiler.ret(VReg);
        }
        else {
            auto VReg = TheCompiler.new_xmm();
            Invoker->set_arg(0, VReg);
            TheCompiler.ret(VReg);
        }
    }
    else TheCompiler.ret();

    if (TheCompiler.finalize() != asmjit::Error::kOk) return nullptr;
    void* Temp{};
    if (GetJitRuntime().add(&Temp, &Code) != asmjit::Error::kOk) return nullptr;
    return FThunkPtr { Temp };
}

FThunkPtr GenerateShiftWithRegisterContext(void* ToFn, void* BindParam, FuncArgInfo& Src, FuncArgInfo& Dest) {
    using namespace asmjit;
    using namespace asmjit::x86;

    CodeHolder Code{};
    Code.set_logger(GetLogger());
    Code.set_error_handler(GetErrorHandler());
    Code.init(GetJitRuntime().environment(), GetJitRuntime().cpu_features());
    Assembler TheAssembler { &Code };

    TheAssembler.sub(rsp, sizeof(RegisterContext) - 8);
    int BasePtrOffset = sizeof(RegisterContext) - 8;
    int ContextOffset = 0;
    auto CalcCallPadding = [&BasePtrOffset] { return BasePtrOffset % 16 == 8 ? 0 : 8; };

#define GpMov(reg) TheAssembler.mov(ptr(rsp, offsetof(RegisterContext, reg) - 8), reg)
    GpMov(rax);
    GpMov(rcx);
    GpMov(rdx);
    GpMov(r8);
    GpMov(r9);
    GpMov(r10);
    GpMov(r11);
    GpMov(r12);
    GpMov(r13);
    GpMov(r14);
    GpMov(r15);
    GpMov(rsi);
    GpMov(rdi);
    GpMov(rbx);
#undef GpMov
#define VecMov(reg) TheAssembler.movdqu(ptr(rsp, offsetof(RegisterContext, reg) - 8), reg)
    VecMov(xmm0);
    VecMov(xmm1);
    VecMov(xmm2);
    VecMov(xmm3);
    VecMov(xmm4);
    VecMov(xmm5);
    VecMov(xmm6);
    VecMov(xmm7);
    VecMov(xmm8);
    VecMov(xmm9);
    VecMov(xmm10);
    VecMov(xmm11);
    VecMov(xmm12);
    VecMov(xmm13);
    VecMov(xmm14);
    VecMov(xmm15);
#undef VecMov
    TheAssembler.pushfq();
    BasePtrOffset += 8; // context offset is still at 0

    TheAssembler.mov(rcx, rsp); //todo use platform first arg instead of windows first arg
    auto raxused = (1u << rax.id()) & Dest.Detail().used_regs(RegGroup::kGp);
    TheAssembler.sub(rsp, 32 + CalcCallPadding());
    TheAssembler.call(&RegisterContextStack::Push);

    auto ArgStackSize = Dest.Detail().arg_stack_size();
    ArgStackSize += ArgStackSize % 16 == 0 ? CalcCallPadding() : CalcCallPadding() == 8 ? 0 : 8; // if the space we allocate for the next call is a multiple of 16, it doesn't change the math returned by CalcCallPadding, otherwise it inverts it
    const int Diff = 32 + CalcCallPadding() - ArgStackSize;
    Diff < 0 ? TheAssembler.sub(rsp, -Diff) : Diff > 0 ? TheAssembler.add(rsp, Diff) : static_cast<Error>(0);
    BasePtrOffset += ArgStackSize;
    ContextOffset += ArgStackSize;

    // argument slots are presumed to be 64 bits each. if an argument is 16 bytes long and get split into different registers,
    // they occupy 2 slots (system v), or it's coerced to a pointer (windows) and uses one. each slot corresponds to either a register or an 8 byte stack segment.
    for (int SrcArgIndex = static_cast<int>(Src.GetArguments().size()) - 1; SrcArgIndex >= 0; --SrcArgIndex) {
        auto& SrcArg = Src.GetArguments()[SrcArgIndex];
        auto& DestArg = Dest.GetArguments()[SrcArgIndex + 1];
        if (DestArg.is_stack() && SrcArg.is_stack()) {
            // move from src stack slot to dest stack slot using r11 an intermediate register (valid for windows and linux)
            TheAssembler.mov(r11, ptr(rsp, BasePtrOffset + SrcArg.stack_offset() + 8)); // +8 is because stack_offset() doesn't account for return address
            TheAssembler.mov(ptr(rsp, DestArg.stack_offset()), r11);
        }
        else if (DestArg.is_stack() && SrcArg.is_reg()) {
            if (SrcArg.reg_type() >= RegType::kGp8Lo && SrcArg.reg_type() <= RegType::kGp64) {
                const auto SavedValue = ptr(rsp, ContextOffset + RegisterContextOffsets[gpq(SrcArg.reg_id())]);
                TheAssembler.mov(r11, SavedValue);
                TheAssembler.mov(ptr(rsp, DestArg.stack_offset()), r11);
            }
            else {
                const auto SavedValue = ptr(rsp, ContextOffset + RegisterContextOffsets[xmm(SrcArg.reg_id())]);
                TheAssembler.movq(xmm8, SavedValue);
                TheAssembler.movq(ptr(rsp, DestArg.stack_offset()), xmm8);
            }
        }
        else if (DestArg.is_reg() && SrcArg.is_reg()) {
            if (SrcArg.reg_type() >= RegType::kGp8Lo && SrcArg.reg_type() <= RegType::kGp64) {
                TheAssembler.mov(gpq(DestArg.reg_id()), ptr(rsp, ContextOffset + RegisterContextOffsets[gpq(SrcArg.reg_id())]));
            }
            else {
                TheAssembler.movq(xmm(DestArg.reg_id()), ptr(rsp, ContextOffset + RegisterContextOffsets[xmm(SrcArg.reg_id())]));
            }
        }
    }

    TheAssembler.mov(gpq(Dest.GetArguments()[0].reg_id()), BindParam);
    TheAssembler.call(ToFn);

    if (!Dest.GetReturnValues().empty()) {
        // save any return values that are sitting in registers in the existing context structure before calling Pop
        for (auto& RetArg : Dest.GetReturnValues()) {
            if (RetArg.is_reg()) {
                if (RetArg.reg_type() >= RegType::kGp8Lo && RetArg.reg_type() <= RegType::kGp64) {
                    auto Register = gpq(RetArg.reg_id());
                    TheAssembler.mov(ptr(rsp, ContextOffset + RegisterContextOffsets[Register]), Register);
                }
                else {
                    auto Register = xmm(RetArg.reg_id());
                    TheAssembler.movdqu(ptr(rsp, ContextOffset + RegisterContextOffsets[Register]), Register);
                }
            }
        }
    }

    BasePtrOffset -= ArgStackSize;
    ContextOffset -= ArgStackSize - (32 + CalcCallPadding());
    const int NewOffset = ArgStackSize - (32 + CalcCallPadding());
    NewOffset > 0 ? TheAssembler.add(rsp, NewOffset) : NewOffset < 0 ? TheAssembler.sub(rsp, -NewOffset) : static_cast<Error>(0);
    TheAssembler.call(&RegisterContextStack::Pop);

    // restore return values to registers
    if (!Dest.GetReturnValues().empty()) {
        for (auto& RetArg : Dest.GetReturnValues()) {
            if (RetArg.is_reg()) {
                if (RetArg.reg_type() >= RegType::kGp8Lo && RetArg.reg_type() <= RegType::kGp64) {
                    auto Register = gpq(RetArg.reg_id());
                    TheAssembler.mov(Register, ptr(rsp, ContextOffset + RegisterContextOffsets[Register]));
                }
                else {
                    auto Register = xmm(RetArg.reg_id());
                    TheAssembler.movdqu(Register, ptr(rsp, ContextOffset + RegisterContextOffsets[Register]));
                }
            }
            else if (RetArg.is_stack()) {
                throw std::runtime_error("Thunks don't support returning by stack!");
            }
        }
    }

    TheAssembler.add(rsp, 32 + CalcCallPadding() + BasePtrOffset);
    TheAssembler.ret();

    if (TheAssembler.finalize() != Error::kOk) return nullptr;
    void* Temp{};
    if (GetJitRuntime().add(&Temp, &Code) != Error::kOk) return nullptr;
    return FThunkPtr { Temp };
}

FuncSignature ShiftSignature(const FuncSignature& InSignature) {
    auto InvokeSig = InSignature;
    InvokeSig.add_arg(asmjit::TypeId::kUInt64); // doesn't matter what type we add, it'll get overwritten
    for (int ArgIndex = static_cast<int>(InvokeSig.arg_count()) - 2; ArgIndex >= 0; ArgIndex--)
    {
        InvokeSig.set_arg(ArgIndex + 1, InvokeSig.args()[ArgIndex]);
    }
    InvokeSig.set_arg(0, asmjit::TypeId::kUIntPtr);
    return InvokeSig;
}