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
            Invoker->set_ret(0, VReg);
            TheCompiler.ret(VReg);
        }
        else {
            auto VReg = TheCompiler.new_xmm();
            Invoker->set_ret(0, VReg);
            TheCompiler.ret(VReg);
        }
    }
    else TheCompiler.ret();
    TheCompiler.end_func();

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

    const auto ShadowArgSpace = Dest.Detail().arg_stack_size();
    constexpr auto RegisterCtxSpace = sizeof(RegisterContext);
    auto SumSpace = ShadowArgSpace + RegisterCtxSpace;
    const auto Pad = (SumSpace % 16 == 8) ? 0 : 8;
    SumSpace += Pad;
    TheAssembler.sub(rsp, SumSpace);
    auto GpScratchReg = GetPlatformGpScratchReg();
    auto XmmScratchReg = GetPlatformXmmScratchReg();
    /*
     * Roughly:
     * struct Locals {
     *      ShadowArgSpace + Padding    // offset 0
     *      RegisterCtxSpace            // offset sizeof ShadowArgSpace
     * }
     */
    const auto ContextPtr = ptr(rsp, ShadowArgSpace + Pad);
    const auto RspInitial = ContextPtr.clone_adjusted(RegisterCtxSpace + 8);

#define GpMov(reg) TheAssembler.mov(ContextPtr.clone_adjusted(offsetof(RegisterContext, reg)), reg)
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
#define VecMov(reg) TheAssembler.movdqu(ContextPtr.clone_adjusted(offsetof(RegisterContext, reg)), reg)
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
    TheAssembler.pop(GpScratchReg);
    TheAssembler.mov(ContextPtr.clone_adjusted(offsetof(RegisterContext, rflags)), GpScratchReg);

    TheAssembler.lea(gpq(FuncArgInfo{FuncSignature::build<void, RegisterContext*>()}.Detail().arg(0).reg_id()), ContextPtr);
    TheAssembler.call(&RegisterContextStack::Push);

    for (int SrcArgIndex = static_cast<int>(Src.GetArguments().size()) - 1; SrcArgIndex >= 0; --SrcArgIndex) {
        auto& SrcArg = Src.GetArguments()[SrcArgIndex];
        auto& DestArg = Dest.GetArguments()[SrcArgIndex + 1];
        if (DestArg.is_stack() && SrcArg.is_stack()) {
            TheAssembler.mov(GpScratchReg, RspInitial.clone_adjusted(SrcArg.stack_offset()));
            TheAssembler.mov(ptr(rsp, DestArg.stack_offset()), GpScratchReg);
        }
        else if (DestArg.is_stack() && SrcArg.is_reg()) {
            if (SrcArg.reg_type() >= RegType::kGp8Lo && SrcArg.reg_type() <= RegType::kGp64) {
                TheAssembler.mov(GpScratchReg, ContextPtr.clone_adjusted(RegisterContextOffsets[gpq(SrcArg.reg_id())]));
                TheAssembler.mov(ptr(rsp, DestArg.stack_offset()), GpScratchReg);
            }
            else {
                TheAssembler.movq(XmmScratchReg, ContextPtr.clone_adjusted(RegisterContextOffsets[xmm(SrcArg.reg_id())]));
                TheAssembler.movq(ptr(rsp, DestArg.stack_offset()), XmmScratchReg);
            }
        }
        else if (DestArg.is_reg() && SrcArg.is_reg()) {
            if (SrcArg.reg_type() >= RegType::kGp8Lo && SrcArg.reg_type() <= RegType::kGp64) {
                TheAssembler.mov(gpq(DestArg.reg_id()), ContextPtr.clone_adjusted(RegisterContextOffsets[gpq(SrcArg.reg_id())]));
            }
            else {
                TheAssembler.movdqu(xmm(DestArg.reg_id()), ContextPtr.clone_adjusted(RegisterContextOffsets[xmm(SrcArg.reg_id())]));
            }
        }
    }

    TheAssembler.mov(gpq(Dest.GetArguments()[0].reg_id()), BindParam);
    TheAssembler.call(ToFn);

    if (!Dest.GetReturnValues().empty()) {
        for (auto& RetArg : Dest.GetReturnValues()) {
            if (RetArg.is_reg()) {
                if (RetArg.reg_type() >= RegType::kGp8Lo && RetArg.reg_type() <= RegType::kGp64) {
                    auto Register = gpq(RetArg.reg_id());
                    TheAssembler.mov(ContextPtr.clone_adjusted(RegisterContextOffsets[Register]), Register);
                }
                else {
                    auto Register = xmm(RetArg.reg_id());
                    TheAssembler.movdqu(ContextPtr.clone_adjusted(RegisterContextOffsets[Register]), Register);
                }
            }
            else if (RetArg.is_stack()) {
                throw std::runtime_error("Thunks don't support returning by stack!");
            }
        }
    }

    TheAssembler.call(&RegisterContextStack::Pop);

    // restore return values to registers
    if (!Dest.GetReturnValues().empty()) {
        for (auto& RetArg : Dest.GetReturnValues()) {
            if (RetArg.is_reg()) {
                if (RetArg.reg_type() >= RegType::kGp8Lo && RetArg.reg_type() <= RegType::kGp64) {
                    auto Register = gpq(RetArg.reg_id());
                    TheAssembler.mov(Register, ContextPtr.clone_adjusted(RegisterContextOffsets[Register]));
                }
                else {
                    auto Register = xmm(RetArg.reg_id());
                    TheAssembler.movdqu(Register, ContextPtr.clone_adjusted(RegisterContextOffsets[Register]));
                }
            }
            else if (RetArg.is_stack()) {
                throw std::runtime_error("Thunks don't support returning by stack!");
            }
        }
    }

    TheAssembler.add(rsp, SumSpace);
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