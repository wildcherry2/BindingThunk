#include "BindingThunk.hpp"
#include "Context.hpp"
#include <vector>

namespace BindingThunk {

static int32_t GetArgumentContextOffset(const size_t Index) {
    return static_cast<int32_t>(ArgumentContext::ArgsOffset + (ArgumentContext::ArgumentSize * Index));
}

static void StoreImmediateU64(asmjit::x86::Assembler& TheAssembler, const asmjit::x86::Mem& Destination, const uint64_t Value, const asmjit::x86::Gp& GpScratchReg) {
    TheAssembler.mov(GpScratchReg, Value);
    TheAssembler.mov(Destination, GpScratchReg);
}

static void SaveRegisterContext(asmjit::x86::Assembler& TheAssembler, const asmjit::x86::Mem& ContextPtr, const asmjit::x86::Gp& GpScratchReg) {
    for (const auto& [Register, Offset] : RegisterContextOffsets) {
        if (Register.is_gp()) {
            TheAssembler.mov(ContextPtr.clone_adjusted(Offset), Register.as<asmjit::x86::Gp>());
        }
        else if (Register.is_vec()) {
            TheAssembler.movdqu(ContextPtr.clone_adjusted(Offset), Register.as<asmjit::x86::Vec>());
        }
    }
    TheAssembler.pushfq();
    TheAssembler.pop(GpScratchReg);
    TheAssembler.mov(ContextPtr.clone_adjusted(offsetof(RegisterContext, rflags)), GpScratchReg);
}

THUNK_API FuncSignature ShiftSignature(const FuncSignature& InSignature);
THUNK_API FThunkResult GenerateSimpleShift(void *ToFn, void *BindParam, FuncArgInfo& Src, FuncArgInfo& Dest, bool bLogAssembly);
static FThunkResult GenerateComplexShift(void *ToFn, void *BindParam, FuncArgInfo& Src, FuncArgInfo& Dest, bool bLogAssembly);
THUNK_API FThunkResult GenerateShiftWithRegisterContext(void *ToFn, void *BindParam, FuncArgInfo& Src, FuncArgInfo& Dest, bool bLogAssembly);

FThunkResult GenerateBindingThunk(void *ToFn, void *BindParam, FuncSignature SourceSignature, EBindingThunkType Type, const bool bLogAssembly) {
    auto InvokeSignature = ShiftSignature(SourceSignature);
    FuncArgInfo SrcSig{SourceSignature};
    FuncArgInfo DestSig{InvokeSignature};
    switch (Type) {
        case EBindingThunkType::Default: {
            auto StackAllocSize = DestSig.Detail().arg_stack_size() - DestSig.Detail().red_zone_size() - DestSig.Detail().spill_zone_size();
            return StackAllocSize > 0 ? GenerateComplexShift(ToFn, BindParam, SrcSig, DestSig, bLogAssembly) : GenerateSimpleShift(ToFn, BindParam, SrcSig, DestSig, bLogAssembly);
        }
        case EBindingThunkType::Register: {
            return GenerateShiftWithRegisterContext(ToFn, BindParam, SrcSig, DestSig, bLogAssembly);
        }
        case EBindingThunkType::Argument:
        case EBindingThunkType::ArgumentAndRegister:
            return std::unexpected(MakeThunkError(EThunkErrorCode::InvalidBindingType, "Argument binding thunks require a callback that takes ArgumentContext&."));
        default: {
            return std::unexpected(MakeThunkError(EThunkErrorCode::InvalidBindingType, "Invalid binding thunk type."));
        }
    }
}

THUNK_API FThunkResult GenerateSimpleShift(void* ToFn, void* BindParam, FuncArgInfo& Src, FuncArgInfo& Dest, const bool bLogAssembly) {
    if (Src.Signature().arg_count() >= Dest.Signature().arg_count()) {
        return std::unexpected(MakeThunkError(EThunkErrorCode::InvalidSignature, "GenerateSimpleShift source arg count is >= destination arg count."));
    }

    asmjit::CodeHolder Code{};
    InitializeCodeHolder(Code, bLogAssembly);
    Assembler TheAssembler { &Code };

    for (int32_t ArgIndex = Src.GetArguments().size() - 1; ArgIndex >= 0; --ArgIndex) {
        const auto& SrcValue = Src.GetArguments()[ArgIndex];
        const auto& DestValue = Dest.GetArguments()[ArgIndex + 1];
        if (SrcValue.type_id() != DestValue.type_id()) {
            return std::unexpected(MakeThunkError(EThunkErrorCode::InvalidSignature, "Malformed destination value at index " + std::to_string(ArgIndex) + " in GenerateSimpleShift."));
        }
        if (SrcValue.reg_type() >= asmjit::RegType::kGp8Lo && SrcValue.reg_type() <= asmjit::RegType::kGp64) {
            TheAssembler.mov(asmjit::x86::gpq(DestValue.reg_id()), asmjit::x86::gpq(SrcValue.reg_id()));
        }
        else {
            TheAssembler.movaps(asmjit::x86::xmm(DestValue.reg_id()), asmjit::x86::xmm(SrcValue.reg_id()));
        }
    }

    TheAssembler.mov(asmjit::x86::gpq(Dest.GetArguments()[0].reg_id()), BindParam);
    TheAssembler.jmp(ToFn);
    if (TheAssembler.finalize() != asmjit::Error::kOk) return std::unexpected(MakeThunkError(EThunkErrorCode::AssemblerFinalizeFailed, "Failed to finalize simple binding thunk assembler."));
    return AddThunkToRuntime(Code, "Failed to add simple binding thunk to the JIT runtime.");
}

FThunkResult GenerateComplexShift(void *ToFn, void* BindParam, FuncArgInfo& Src, FuncArgInfo& Dest, const bool bLogAssembly) {
    asmjit::CodeHolder Code{};
    InitializeCodeHolder(Code, bLogAssembly);
    asmjit::x86::Compiler TheCompiler { &Code };
#if defined(_WIN64)
    const auto EndLabel = TheCompiler.new_label();
#endif

    asmjit::InvokeNode* Invoker{};
    auto* ThisFunc = TheCompiler.add_func(Src.Signature());
    TheCompiler.invoke(asmjit::Out(Invoker), ToFn, Dest.Signature());

    // ReSharper disable once CppDFAConstantConditions
    if (!Invoker) return std::unexpected(MakeThunkError(EThunkErrorCode::InvokeCreationFailed, "Failed to create invoke node for complex binding thunk."));

    // ReSharper disable once CppDFAUnreachableCode
    Invoker->set_arg(0, BindParam);

    for (uint32_t InArgIndex = 0; InArgIndex < Src.Signature().arg_count(); ++InArgIndex) {
        if (const auto Id = Src.Signature().arg(InArgIndex); asmjit::TypeUtils::is_int(Id)) {
            auto VReg = TheCompiler.new_gp64();
            ThisFunc->set_arg(InArgIndex, VReg);
            Invoker->set_arg(InArgIndex + 1, VReg);
        }
        else if (asmjit::TypeUtils::is_float(Id)) {
            auto VReg = TheCompiler.new_xmm();
            ThisFunc->set_arg(InArgIndex, VReg);
            Invoker->set_arg(InArgIndex + 1, VReg);
        }
        else {
            return std::unexpected(MakeThunkError(EThunkErrorCode::UnsupportedType, "Complex binding thunk encountered an unsupported argument type."));
        }
    }

    if (Src.Signature().has_ret()) {
        if (const auto Id = Src.Signature().ret(); asmjit::TypeUtils::is_int(Id)) {
            auto VReg = TheCompiler.new_gp64();
            Invoker->set_ret(0, VReg);
            TheCompiler.ret(VReg);
        }
        else if (asmjit::TypeUtils::is_float(Id)) {
            auto VReg = TheCompiler.new_xmm();
            Invoker->set_ret(0, VReg);
            TheCompiler.ret(VReg);
        }
        else {
            return std::unexpected(MakeThunkError(EThunkErrorCode::UnsupportedType, "Complex binding thunk encountered an unsupported return type."));
        }
    }
    else TheCompiler.ret();
    TheCompiler.end_func();
#if defined(_WIN64)
    TheCompiler.bind(EndLabel);
#endif

    if (TheCompiler.finalize() != asmjit::Error::kOk) return std::unexpected(MakeThunkError(EThunkErrorCode::AssemblerFinalizeFailed, "Failed to finalize complex binding thunk compiler output."));

#if defined(_WIN64)
    const FThunkWindowsRuntimeInfo* WindowsRuntimeInfoPtr = nullptr;
    FThunkWindowsRuntimeInfo WindowsRuntimeInfo{};
    if (const auto WindowsUnwindInfo = BuildWindowsUnwindInfoForFuncFrame(ThisFunc->frame()); !WindowsUnwindInfo.empty()) {
        asmjit::x86::Assembler UnwindAssembler { &Code };
        const auto UnwindInfoLabel = UnwindAssembler.new_label();
        if (UnwindAssembler.set_offset(Code.text_section()->buffer_size()) != asmjit::Error::kOk) {
            return std::unexpected(MakeThunkError(EThunkErrorCode::AssemblerFinalizeFailed, "Failed to position complex binding thunk unwind assembler."));
        }
        FManualThunkFrameState UnwindState{};
        UnwindState.WindowsUnwindInfo = WindowsUnwindInfo;
        EmitManualThunkWindowsUnwindInfo(
            UnwindAssembler,
            UnwindState,
            UnwindInfoLabel
        );
        WindowsRuntimeInfo = FThunkWindowsRuntimeInfo { ThisFunc->label(), EndLabel, UnwindInfoLabel };
        WindowsRuntimeInfoPtr = &WindowsRuntimeInfo;
    }
#endif

#if defined(_WIN64)
    return AddThunkToRuntime(Code, "Failed to add complex binding thunk to the JIT runtime.", WindowsRuntimeInfoPtr);
#else
    return AddThunkToRuntime(Code, "Failed to add complex binding thunk to the JIT runtime.");
#endif
}

THUNK_API FThunkResult GenerateShiftWithRegisterContext(void* ToFn, void* BindParam, FuncArgInfo& Src, FuncArgInfo& Dest, const bool bLogAssembly) {
    using namespace asmjit;
    using namespace asmjit::x86;

    CodeHolder Code{};
    InitializeCodeHolder(Code, bLogAssembly);
    Assembler TheAssembler { &Code };
#if defined(_WIN64)
    const auto BeginLabel = TheAssembler.new_label();
    const auto EndLabel = TheAssembler.new_label();
    const auto UnwindInfoLabel = TheAssembler.new_label();
#endif

    const auto ShadowArgSpace = Dest.Detail().arg_stack_size();
    constexpr auto RegisterCtxSpace = sizeof(RegisterContext);
#if defined(_WIN64)
    TheAssembler.bind(BeginLabel);
#endif
    const auto FrameState = EmitManualThunkProlog(TheAssembler, FManualThunkFramePlan {
        .RawStackAllocation = static_cast<uint32_t>(ShadowArgSpace + RegisterCtxSpace),
    });
    auto GpScratchReg = GetPlatformGpScratchReg();
    auto XmmScratchReg = GetPlatformXmmScratchReg();
    /*
     * Roughly:
     * struct Locals {
     *      ShadowArgSpace              // offset 0
     *      RegisterCtxSpace            // offset sizeof ShadowArgSpace
     * }
     */
    const auto ContextPtr = ptr(rsp, static_cast<int32_t>(ShadowArgSpace));
    const auto RspInitial = ptr(rsp, static_cast<int32_t>(FrameState.EntryRspOffset()));

    SaveRegisterContext(TheAssembler, ContextPtr, GpScratchReg);

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
        }
    }

    EmitManualThunkEpilog(TheAssembler, FrameState);
    TheAssembler.ret();
#if defined(_WIN64)
    TheAssembler.bind(EndLabel);
    EmitManualThunkWindowsUnwindInfo(TheAssembler, FrameState, UnwindInfoLabel);
#endif

    if (TheAssembler.finalize() != Error::kOk) return std::unexpected(MakeThunkError(EThunkErrorCode::AssemblerFinalizeFailed, "Failed to finalize register binding thunk assembler."));
#if defined(_WIN64)
    const FThunkWindowsRuntimeInfo WindowsRuntimeInfo { BeginLabel, EndLabel, UnwindInfoLabel };
    return AddThunkToRuntime(Code, "Failed to add register binding thunk to the JIT runtime.", &WindowsRuntimeInfo);
#else
    return AddThunkToRuntime(Code, "Failed to add register binding thunk to the JIT runtime.");
#endif
}

FThunkResult GenerateBindingThunk(void(*ToFn)(void*, ArgumentContext&), void *BindParam, FuncSignature SourceSignature, EBindingThunkType Type, const bool bLogAssembly) {
    using namespace asmjit;
    using namespace asmjit::x86;

    CodeHolder Code{};
    InitializeCodeHolder(Code, bLogAssembly);
    Assembler TheAssembler { &Code };
#if defined(_WIN64)
    const auto BeginLabel = TheAssembler.new_label();
    const auto EndLabel = TheAssembler.new_label();
    const auto UnwindInfoLabel = TheAssembler.new_label();
#endif
    auto DestInfo = FuncArgInfo{FuncSignature::build<void, void*, ArgumentContext&>()};
    auto SrcInfo = FuncArgInfo{SourceSignature};

    const auto ShadowArgSpace = DestInfo.Detail().arg_stack_size();
    if (Type != EBindingThunkType::Argument && Type != EBindingThunkType::ArgumentAndRegister) {
        return std::unexpected(MakeThunkError(EThunkErrorCode::InvalidBindingType, "ArgumentContext binding callbacks require Argument or ArgumentAndRegister binding types."));
    }

    auto CtxSpace = ArgumentContext::ArgumentContextNonVariableSize + (ArgumentContext::ArgumentSize * SrcInfo.GetArguments().size());
    if (Type == EBindingThunkType::ArgumentAndRegister) CtxSpace += sizeof(RegisterContext);
#if defined(_WIN64)
    TheAssembler.bind(BeginLabel);
#endif
    const auto FrameState = EmitManualThunkProlog(TheAssembler, FManualThunkFramePlan {
        .RawStackAllocation = static_cast<uint32_t>(ShadowArgSpace + CtxSpace),
    });
    auto GpScratchReg = GetPlatformGpScratchReg();
    auto XmmScratchReg = GetPlatformXmmScratchReg();
    const auto ArgContextPtr = ptr(rsp, static_cast<int32_t>(ShadowArgSpace));
    const auto RegContextPtr = ArgContextPtr.clone_adjusted(ArgumentContext::ArgumentContextNonVariableSize + (ArgumentContext::ArgumentSize * SrcInfo.GetArguments().size()));
    const auto RspInitial = ptr(rsp, static_cast<int32_t>(FrameState.EntryRspOffset()));

    // start by saving registers at the tail end of the data part of ArgumentContext, if needed
    if (Type == EBindingThunkType::ArgumentAndRegister) {
        SaveRegisterContext(TheAssembler, RegContextPtr, GpScratchReg);
    }

    StoreImmediateU64(TheAssembler, ArgContextPtr.clone_adjusted(ArgumentContext::FlagsOffset), 0, GpScratchReg);
    StoreImmediateU64(TheAssembler, ArgContextPtr.clone_adjusted(ArgumentContext::ReturnValueOffset), 0, GpScratchReg);
    StoreImmediateU64(TheAssembler, ArgContextPtr.clone_adjusted(ArgumentContext::ArgsCountOffset), SrcInfo.GetArguments().size(), GpScratchReg);

    // push arguments into structure
    for (auto Index = 0; auto& FuncValue : SrcInfo.GetArguments()) {
        const auto ArgumentOffset = GetArgumentContextOffset(Index++);
        if (FuncValue.is_reg()) {
            if (TypeUtils::is_int(FuncValue.type_id())) {
                TheAssembler.mov(ArgContextPtr.clone_adjusted(ArgumentOffset), gpq(FuncValue.reg_id()));
            }
            else if (TypeUtils::is_float(FuncValue.type_id())) {
                TheAssembler.movq(ArgContextPtr.clone_adjusted(ArgumentOffset), xmm(FuncValue.reg_id()));
            }
            else {
                return std::unexpected(MakeThunkError(EThunkErrorCode::UnsupportedType, "Argument binding thunk encountered an unsupported argument register type."));
            }
        }
        else {
            if (TypeUtils::is_int(FuncValue.type_id())) {
                TheAssembler.mov(GpScratchReg, RspInitial.clone_adjusted(FuncValue.stack_offset()));
                TheAssembler.mov(ArgContextPtr.clone_adjusted(ArgumentOffset), GpScratchReg);
            }
            else if (TypeUtils::is_float(FuncValue.type_id())) {
                TheAssembler.movq(XmmScratchReg, RspInitial.clone_adjusted(FuncValue.stack_offset()));
                TheAssembler.movq(ArgContextPtr.clone_adjusted(ArgumentOffset), XmmScratchReg);
            }
            else {
                return std::unexpected(MakeThunkError(EThunkErrorCode::UnsupportedType, "Argument binding thunk encountered an unsupported stack argument type."));
            }
        }
    }

    // set flags if needed
    auto Flag = (SrcInfo.GetReturnValues().empty() ? 0 : ArgumentContext::HasReturnValueFlag) |
        (Type == EBindingThunkType::ArgumentAndRegister ? ArgumentContext::HasRegisterContextFlag : 0);
    StoreImmediateU64(TheAssembler, ArgContextPtr.clone_adjusted(ArgumentContext::FlagsOffset), Flag, GpScratchReg);

    // call function
    TheAssembler.mov(gpq(DestInfo.Detail().arg(0).reg_id()), BindParam);
    TheAssembler.lea(gpq(DestInfo.Detail().arg(1).reg_id()), ArgContextPtr);
    TheAssembler.call(ToFn);

    // if there's a return value, make sure it's in the right register. we don't support return types that take multiple registers or other weirdness
    // todo test if asmjit figures out return type calling conventions
    if (Flag & 1) {
        auto& Val = SrcInfo.GetReturnValues()[0];
        if (TypeUtils::is_int(Val.type_id())) {
            TheAssembler.mov(gpq(Val.reg_id()), ArgContextPtr.clone_adjusted(ArgumentContext::ReturnValueOffset));
        }
        else if (TypeUtils::is_float(Val.type_id())) {
            TheAssembler.movq(xmm(Val.reg_id()), ArgContextPtr.clone_adjusted(ArgumentContext::ReturnValueOffset));
        }
        else {
            return std::unexpected(MakeThunkError(EThunkErrorCode::UnsupportedType, "Argument binding thunk encountered an unsupported return type."));
        }
    }

    EmitManualThunkEpilog(TheAssembler, FrameState);
    TheAssembler.ret();
#if defined(_WIN64)
    TheAssembler.bind(EndLabel);
    EmitManualThunkWindowsUnwindInfo(TheAssembler, FrameState, UnwindInfoLabel);
#endif

    if (TheAssembler.finalize() != Error::kOk) return std::unexpected(MakeThunkError(EThunkErrorCode::AssemblerFinalizeFailed, "Failed to finalize argument binding thunk assembler."));
#if defined(_WIN64)
    const FThunkWindowsRuntimeInfo WindowsRuntimeInfo { BeginLabel, EndLabel, UnwindInfoLabel };
    return AddThunkToRuntime(Code, "Failed to add argument binding thunk to the JIT runtime.", &WindowsRuntimeInfo);
#else
    return AddThunkToRuntime(Code, "Failed to add argument binding thunk to the JIT runtime.");
#endif
}

THUNK_API FuncSignature ShiftSignature(const FuncSignature& InSignature) {
    auto InvokeSig = InSignature;
    InvokeSig.add_arg(asmjit::TypeId::kUInt64); // doesn't matter what type we add, it'll get overwritten
    for (int ArgIndex = static_cast<int>(InvokeSig.arg_count()) - 2; ArgIndex >= 0; ArgIndex--)
    {
        InvokeSig.set_arg(ArgIndex + 1, InvokeSig.args()[ArgIndex]);
    }
    InvokeSig.set_arg(0, asmjit::TypeId::kUIntPtr);
    return InvokeSig;
}

}
