/** @file RestoreThunk.cpp
 *  @brief Implements restore thunks that rebuild call state from argument or register captures.
 */

#include "RestoreThunk.hpp"
#include "Context.hpp"

namespace BindingThunk {

/** @brief Emits a restore thunk that rebuilds arguments from the thread-local register stack. */
static FThunkResult GenerateRestoreThunkForRegisterContext(void* CallTo, FuncSignature Signature, bool bLogAssembly);
/** @brief Emits a restore thunk that rebuilds arguments from an @ref ArgumentContext. */
static FThunkResult GenerateRestoreThunkForArgumentContext(void* CallTo, FuncSignature DestinationSignature, bool bSafe, bool bLogAssembly);

/** @brief Returns the byte offset of a packed argument within the variable part of an @ref ArgumentContext. */
static int32_t GetArgumentContextOffset(const size_t Index) {
    return static_cast<int32_t>(ArgumentContext::ArgumentSize * Index);
}

/** @copydoc GenerateRestoreThunk(void*, const ABISignature&, EBindingThunkType, bool) */
FThunkResult GenerateRestoreThunk(void* CallTo, const ABISignature& Signature, EBindingThunkType BindingType, const bool bLogAssembly) {
    auto FinalizedSignature = Signature.Finalize();
    if (!FinalizedSignature) {
        return std::unexpected(FinalizedSignature.error());
    }

    return Internal::GenerateRestoreThunk(CallTo, FinalizedSignature.value(), BindingType, bLogAssembly);
}

/** @copydoc Internal::GenerateRestoreThunk(void*, FuncSignature, EBindingThunkType, bool) */
FThunkResult Internal::GenerateRestoreThunk(void* CallTo, FuncSignature Signature, EBindingThunkType BindingType, const bool bLogAssembly) {
    switch (BindingType) {
        case EBindingThunkType::Default:
            return std::unexpected(MakeThunkError(EThunkErrorCode::InvalidBindingType, "Default binding thunks do not require a restore thunk."));
        case EBindingThunkType::Argument:
            return GenerateRestoreThunkForArgumentContext(CallTo, Signature, false, bLogAssembly);
        case EBindingThunkType::Register:
            return GenerateRestoreThunkForRegisterContext(CallTo, Signature, bLogAssembly);
        case EBindingThunkType::ArgumentAndRegister:
            return GenerateRestoreThunkForArgumentContext(CallTo, Signature, true, bLogAssembly);
        default:
            return std::unexpected(MakeThunkError(EThunkErrorCode::InvalidBindingType, "Invalid binding thunk type."));
    }
}

/** @brief Emits a restore thunk for @ref EBindingThunkType::Register. */
FThunkResult GenerateRestoreThunkForRegisterContext(void* CallTo, FuncSignature Signature, const bool bLogAssembly) {
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
    FuncArgInfo ArgInfo { Signature };
    auto SavedNonVolatileGpRegs = GetPlatformNonVolatileGpRegs();
    auto SavedNonVolatileVecRegs = GetPlatformNonVolatileVecRegs();

    // The frame needs outgoing shadow/stack argument space plus local spill slots for preserved state
    // while the original register context is reconstructed into the live machine state.
    const auto ShadowArgSpace = ArgInfo.Detail().arg_stack_size();
    const auto NVFltRegSpace = static_cast<uint32_t>(SavedNonVolatileVecRegs.size() * sizeof(Xmm));
    const auto IntArgSpace = ArgInfo.GetArgumentIntegralRegisters().size() * 8;
    const auto FltArgSpace = ArgInfo.GetArgumentFloatingRegisters().size() * 16;
    const auto SavedVecOffset = static_cast<uint32_t>((ShadowArgSpace + IntArgSpace + FltArgSpace + 15) & ~uint32_t(15));
#if defined(_WIN64)
    TheAssembler.bind(BeginLabel);
#endif
    // This frame temporarily clobbers all platform nonvolatiles while reconstructing the captured register context,
    // so the prolog/epilog must save and restore the complete preserved set.
    const auto FrameState = EmitManualThunkProlog(TheAssembler, FManualThunkFramePlan {
        .PushedGpRegs = std::move(SavedNonVolatileGpRegs),
        .SavedVecRegs = std::move(SavedNonVolatileVecRegs),
        .RawStackAllocation = static_cast<uint32_t>(SavedVecOffset + NVFltRegSpace),
        .SavedVecOffset = SavedVecOffset,
    });
    auto GpScratchReg = GetPlatformGpScratchReg();
    /*
     * Stack layout after the manual prolog:
     * struct Locals {
     *      ShadowArgSpace            // outgoing call shadow/stack argument space
     *      IntArgSpace               // scratch slots for live GP argument registers
     *      FltArgSpace               // scratch slots for live XMM argument registers
     *      NVFltRegSpace             // saved non-volatile XMM registers from the manual frame
     * }
     */
    const auto IntArgPtr = ptr(rsp, static_cast<int32_t>(ShadowArgSpace)); // treat as an array of uint64_t
    const auto FltArgPtr = IntArgPtr.clone_adjusted(IntArgSpace);          // treat as an array of Xmm
    const auto RspInitial = ptr(rsp, static_cast<int32_t>(FrameState.EntryRspOffset()));


    // RegisterContextStack::Top is an ordinary call, so preserve the live argument registers before invoking it.
    {
        int IntIndex = 0, FloatIndex = 0;
        for (auto& Arg: ArgInfo.GetArgumentIntegralRegisters()) {
            TheAssembler.mov(IntArgPtr.clone_adjusted(8 * IntIndex++), Arg);
        }
        for (auto& Arg: ArgInfo.GetArgumentFloatingRegisters()) {
            TheAssembler.movdqu(FltArgPtr.clone_adjusted(16 * FloatIndex++), Arg);
        }
    }

    TheAssembler.call(&RegisterContextStack::Top); // shadow space already allocated

    // Stack arguments can be copied directly into the outgoing call area because it is already allocated.
    for (auto& Arg: ArgInfo.GetArguments()) {
        if (Arg.is_stack()) {
            TheAssembler.mov(GpScratchReg, RspInitial.clone_adjusted(Arg.stack_offset()));
            TheAssembler.mov(ptr(rsp, Arg.stack_offset()), GpScratchReg);
        }
    }

    // restore rflags
    TheAssembler.mov(GpScratchReg, ptr(rax, offsetof(RegisterContext, rflags)));
    TheAssembler.push(GpScratchReg);
    TheAssembler.popfq();

    // Restore every captured register except active argument registers, which are rebuilt from dedicated scratch slots.
    const auto GpMask = ArgInfo.GpRegMask();
    const auto VecMask = ArgInfo.VecRegMask();
    for (auto& [Register, Offset] : RegisterContextOffsets) {
        if (Register.is_gp()) {
            if ((1 << Register.id()) & GpMask) continue; // register is a integral arg
            auto AsGp = Register.as<Gp>();
            if (AsGp == rax) continue; // we don't want to overwrite rax until we're done with it
            TheAssembler.mov(AsGp, ptr(rax, Offset)); // register is not an arg, restore it
        }
        else if (Register.is_vec()) {
            if ((1 << Register.id()) & VecMask) continue; // register is a floating arg
            TheAssembler.movdqu(Register.as<Vec>(), ptr(rax, Offset)); // register is not an arg, restore it
        }
    }
    TheAssembler.mov(rax, ptr(rax, RegisterContextOffsets[rax]));

    // Rehydrate the argument registers last so earlier context restoration does not clobber them.
    {
        int IntIndex = 0, FloatIndex = 0;
        for (auto& Arg: ArgInfo.GetArgumentIntegralRegisters()) {
            TheAssembler.mov(Arg, IntArgPtr.clone_adjusted(8 * IntIndex++));
        }
        for (auto& Arg: ArgInfo.GetArgumentFloatingRegisters()) {
            TheAssembler.movdqu(Arg, FltArgPtr.clone_adjusted(16 * FloatIndex++));
        }
    }

    // The destination now sees the same live argument and non-argument register state that the binding thunk captured.
    TheAssembler.call(CallTo);

    EmitManualThunkEpilog(TheAssembler, FrameState);
    TheAssembler.ret();
#if defined(_WIN64)
    TheAssembler.bind(EndLabel);
    EmitManualThunkWindowsUnwindInfo(TheAssembler, FrameState, UnwindInfoLabel);
#endif

    if (TheAssembler.finalize() != Error::kOk) return std::unexpected(MakeThunkError(EThunkErrorCode::AssemblerFinalizeFailed, "Failed to finalize register restore thunk assembler."));
#if defined(_WIN64)
    const FThunkWindowsRuntimeInfo WindowsRuntimeInfo { BeginLabel, EndLabel, UnwindInfoLabel };
    return AddThunkToRuntime(Code, "Failed to add register restore thunk to the JIT runtime.", &WindowsRuntimeInfo);
#else
    return AddThunkToRuntime(Code, "Failed to add register restore thunk to the JIT runtime.");
#endif
}

/** @brief Emits a restore thunk for @ref EBindingThunkType::Argument and @ref EBindingThunkType::ArgumentAndRegister. */
FThunkResult GenerateRestoreThunkForArgumentContext(void* CallTo, FuncSignature DestinationSignature, bool bSafe, const bool bLogAssembly) {
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

    auto SrcInfo = FuncArgInfo{FuncSignature::build<void, ArgumentContext&>()};
    auto DestInfo = FuncArgInfo{DestinationSignature};

    const auto ShadowArgSpace = DestInfo.Detail().arg_stack_size();
    constexpr uint32_t SavedContextPtrSize = sizeof(uint64_t);
    const auto SavedContextPtrOffset = static_cast<uint32_t>(ShadowArgSpace);
    const auto NonVolatileVecStackSpace = bSafe ? GetPlatformNonVolatileVecRegs().size() * sizeof(Xmm) : 0;
    const auto SavedVecOffset = static_cast<uint32_t>((SavedContextPtrOffset + SavedContextPtrSize + 15) & ~uint32_t(15));
#if defined(_WIN64)
    TheAssembler.bind(BeginLabel);
#endif
    const auto FrameState = EmitManualThunkProlog(TheAssembler, FManualThunkFramePlan {
        .PushedGpRegs = bSafe ? GetPlatformNonVolatileGpRegs() : std::vector<Gp> {},
        .SavedVecRegs = bSafe ? GetPlatformNonVolatileVecRegs() : std::vector<Vec> {},
        .RawStackAllocation = static_cast<uint32_t>(bSafe ? SavedVecOffset + NonVolatileVecStackSpace : SavedContextPtrOffset + SavedContextPtrSize),
        .SavedVecOffset = SavedVecOffset,
    });
    auto GpScratchReg = GetPlatformGpScratchReg();
    auto XmmScratchReg = GetPlatformXmmScratchReg();

    const auto ContextReg = SrcInfo.GetArgumentIntegralRegisters()[0];
    const auto ContextPtr = ptr(ContextReg);
    const auto ArgContextPtr = ContextPtr.clone_adjusted(ArgumentContext::ArgsOffset);
    const auto SavedContextPtr = ptr(rsp, static_cast<int32_t>(SavedContextPtrOffset));
    int32_t DeferredContextArgOffset = -1;

    // The context pointer lives in a volatile argument register, so preserve it before reconstructing the call.
    TheAssembler.mov(SavedContextPtr, ContextReg);

    // Rebuild the destination call frame from the packed context. If the destination also wants ContextReg,
    // defer that load until every other context read is complete.
    for (auto Index = 0; auto& FuncValue : DestInfo.GetArguments()) {
        const auto ArgumentOffset = GetArgumentContextOffset(Index++);
        if (FuncValue.is_reg()) {
            if (TypeUtils::is_int(FuncValue.type_id())) {
                auto reg = gpq(FuncValue.reg_id());
                if (reg == ContextReg) {
                    DeferredContextArgOffset = ArgumentOffset;
                    continue;
                }
                TheAssembler.mov(reg, ArgContextPtr.clone_adjusted(ArgumentOffset));
            }
            else if (TypeUtils::is_float(FuncValue.type_id())) {
                TheAssembler.movq(xmm(FuncValue.reg_id()), ArgContextPtr.clone_adjusted(ArgumentOffset));
            }
            else {
                return std::unexpected(MakeThunkError(EThunkErrorCode::UnsupportedType, "Argument restore thunk encountered an unsupported register argument type."));
            }
        }
        else {
            if (TypeUtils::is_int(FuncValue.type_id())) {
                TheAssembler.mov(GpScratchReg, ArgContextPtr.clone_adjusted(ArgumentOffset));
                TheAssembler.mov(ptr(rsp, FuncValue.stack_offset()), GpScratchReg);
            }
            else if (TypeUtils::is_float(FuncValue.type_id())) {
                TheAssembler.movq(XmmScratchReg, ArgContextPtr.clone_adjusted(ArgumentOffset));
                TheAssembler.movq(ptr(rsp, FuncValue.stack_offset()), XmmScratchReg);
            }
            else {
                return std::unexpected(MakeThunkError(EThunkErrorCode::UnsupportedType, "Argument restore thunk encountered an unsupported stack argument type."));
            }
        }
    }

    if (bSafe) {
        const auto RegPtr = ArgContextPtr.clone_adjusted(DestInfo.GetArguments().size() * ArgumentContext::ArgumentSize);
        TheAssembler.mov(GpScratchReg, RegPtr.clone_adjusted(offsetof(RegisterContext, rflags)));
        TheAssembler.push(GpScratchReg);
        TheAssembler.popfq();

        // Restore the captured non-argument register state after the argument array has been consumed.
        const auto GpMask = DestInfo.GpRegMask();
        const auto VecMask = DestInfo.VecRegMask();
        for (auto& [Register, Offset] : RegisterContextOffsets) {
            if (Register.is_gp()) {
                if ((1 << Register.id()) & GpMask) continue; // register is an integral arg, skip since we'll restore from the argument array
                auto AsGp = Register.as<Gp>();
                if (AsGp == ContextReg) continue; // we don't want to overwrite context reg until we're done with it
                TheAssembler.mov(AsGp, RegPtr.clone_adjusted(Offset)); // register is not an arg, restore it
            }
            else if (Register.is_vec()) {
                if ((1 << Register.id()) & VecMask) continue; // register is a floating arg, skip since we'll restore from the argument array
                TheAssembler.movdqu(Register.as<Vec>(), RegPtr.clone_adjusted(Offset)); // register is not an arg, restore it
            }
        }
    }

    if (DeferredContextArgOffset >= 0) {
        TheAssembler.mov(ContextReg, ArgContextPtr.clone_adjusted(DeferredContextArgOffset));
    }

    TheAssembler.call(CallTo);

    // Argument-mode restore thunks report results through the context instead of their own signature.
    if (!DestInfo.GetReturnValues().empty()) {
        auto& Val = DestInfo.GetReturnValues()[0];
        TheAssembler.mov(GpScratchReg, SavedContextPtr);
        if (TypeUtils::is_int(Val.type_id())) {
            TheAssembler.mov(ptr(GpScratchReg, ArgumentContext::ReturnValueOffset), gpq(Val.reg_id()));
        }
        else if (TypeUtils::is_float(Val.type_id())) {
            TheAssembler.movq(ptr(GpScratchReg, ArgumentContext::ReturnValueOffset), xmm(Val.reg_id()));
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

    if (TheAssembler.finalize() != Error::kOk) return std::unexpected(MakeThunkError(EThunkErrorCode::AssemblerFinalizeFailed, "Failed to finalize argument restore thunk assembler."));
#if defined(_WIN64)
    const FThunkWindowsRuntimeInfo WindowsRuntimeInfo { BeginLabel, EndLabel, UnwindInfoLabel };
    return AddThunkToRuntime(Code, "Failed to add argument restore thunk to the JIT runtime.", &WindowsRuntimeInfo);
#else
    return AddThunkToRuntime(Code, "Failed to add argument restore thunk to the JIT runtime.");
#endif
}

}
