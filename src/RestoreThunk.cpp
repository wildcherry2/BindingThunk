#include "RestoreThunk.hpp"
#include "Context.hpp"

static FThunkError MakeThunkError(const EThunkErrorCode Code, std::string Message) {
    return FThunkError { Code, std::move(Message) };
}

static FThunkResult GenerateRestoreThunkForRegisterContext(void* CallTo, FuncSignature Signature, bool bLogAssembly);
static FThunkResult GenerateRestoreThunkForArgumentContext(void* CallTo, FuncSignature DestinationSignature, bool bSafe, bool bLogAssembly);

static int32_t GetArgumentContextOffset(const size_t Index) {
    return static_cast<int32_t>(ArgumentContext::ArgumentSize * Index);
}

static void SaveNonVolatileRegisters(asmjit::x86::Assembler& TheAssembler, const asmjit::x86::Mem& GpPtr, const asmjit::x86::Mem& VecPtr) {
    auto Offset = 0;
    for (auto& PlatformNonVolatileGpReg : GetPlatformNonVolatileGpRegs()) {
        TheAssembler.mov(GpPtr.clone_adjusted(Offset), PlatformNonVolatileGpReg);
        Offset += 8;
    }
    Offset = 0;
    for (auto& PlatformNonVolatileVecReg : GetPlatformNonVolatileVecRegs()) {
        TheAssembler.movdqu(VecPtr.clone_adjusted(Offset), PlatformNonVolatileVecReg);
        Offset += 16;
    }
}

static void RestoreNonVolatileRegisters(asmjit::x86::Assembler& TheAssembler, const asmjit::x86::Mem& GpPtr, const asmjit::x86::Mem& VecPtr) {
    auto Offset = 0;
    for (auto& PlatformNonVolatileGpReg : GetPlatformNonVolatileGpRegs()) {
        TheAssembler.mov(PlatformNonVolatileGpReg, GpPtr.clone_adjusted(Offset));
        Offset += 8;
    }
    Offset = 0;
    for (auto& PlatformNonVolatileVecReg : GetPlatformNonVolatileVecRegs()) {
        TheAssembler.movdqu(PlatformNonVolatileVecReg, VecPtr.clone_adjusted(Offset));
        Offset += 16;
    }
}

FThunkResult GenerateRestoreThunk(void* CallTo, FuncSignature Signature, EBindingThunkType BindingType, const bool bLogAssembly) {
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

FThunkResult GenerateRestoreThunkForRegisterContext(void* CallTo, FuncSignature Signature, const bool bLogAssembly) {
    using namespace asmjit;
    using namespace asmjit::x86;

    CodeHolder Code{};
    InitializeCodeHolder(Code, bLogAssembly);
    Assembler TheAssembler { &Code };
    FuncArgInfo ArgInfo { Signature };

    // allocate locals with shadow space for calls (call to 'CallTo' will always take the most space so we use ArgInfo.Detail().arg_stack_size())
    // we'll need space for all nonvolatile registers and argument registers in addition to the shadow/arg space + alignment.
    const auto ShadowArgSpace = ArgInfo.Detail().arg_stack_size();
    const auto NVIntRegSpace = GetPlatformNonVolatileGpRegs().size() * 8;
    const auto NVFltRegSpace = GetPlatformNonVolatileVecRegs().size() * 16;
    const auto IntArgSpace = ArgInfo.GetArgumentIntegralRegisters().size() * 8;
    const auto FltArgSpace = ArgInfo.GetArgumentFloatingRegisters().size() * 16;
    auto SumSpace = ShadowArgSpace + NVIntRegSpace + NVFltRegSpace + IntArgSpace + FltArgSpace;
    const auto Pad = (SumSpace % 16 == 8) ? 0 : 8;
    SumSpace += Pad;
    TheAssembler.sub(rsp, SumSpace);
    auto GpScratchReg = GetPlatformGpScratchReg();
    /*
     * Roughly:
     * struct Locals {
     *      ShadowArgSpace + Padding  // offset 0
     *      IntArgSpace               // offset sizeof ShadowArgSpace
     *      FltArgSpace               // offset sizeof ShadowArgSpace + sizeof IntArgSpace
     *      NVIntRegSpace             // offset sizeof ShadowArgSpace + sizeof IntArgSpace + sizeof FltArgSpace
     *      NVFltRegSpace             // offset sizeof ShadowArgSpace + sizeof IntArgSpace + sizeof FltArgSpace + NVIntRegSpace
     * }
     */
    const auto IntArgPtr = ptr(rsp, ShadowArgSpace + Pad);      // treat as an array of uint64_t
    const auto FltArgPtr = IntArgPtr.clone_adjusted(IntArgSpace);          // treat as an array of Xmm
    const auto NVIntRegPtr = FltArgPtr.clone_adjusted(FltArgSpace);        // treat as an array of uint64_t
    const auto NVFltRegPtr = NVIntRegPtr.clone_adjusted(NVIntRegSpace);    // treat as an array of Xmm
    const auto RspInitial = NVFltRegPtr.clone_adjusted(NVFltRegSpace + 8); // pointer to what rsp was at the beginning of the function + return address space


    // we need to call RegisterContextStack::Top, so we need to save the registers with arguments first
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

    // restore context, except for arg registers
    // start with pushing nv-registers, since we'll have to restore them after the call
    SaveNonVolatileRegisters(TheAssembler, NVIntRegPtr, NVFltRegPtr);

    // move arguments on the stack to new stack slot for the call (already allocated)
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

    // restore all saved registers that aren't args
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

    // restore arg registers
    {
        int IntIndex = 0, FloatIndex = 0;
        for (auto& Arg: ArgInfo.GetArgumentIntegralRegisters()) {
            TheAssembler.mov(Arg, IntArgPtr.clone_adjusted(8 * IntIndex++));
        }
        for (auto& Arg: ArgInfo.GetArgumentFloatingRegisters()) {
            TheAssembler.movdqu(Arg, FltArgPtr.clone_adjusted(16 * FloatIndex++));
        }
    }

    // now args are setup and context is restored, make the call
    TheAssembler.call(CallTo);

    // restore nv-registers
    RestoreNonVolatileRegisters(TheAssembler, NVIntRegPtr, NVFltRegPtr);

    // now we're done, deallocate and return
    TheAssembler.add(rsp, SumSpace);
    TheAssembler.ret();

    if (TheAssembler.finalize() != Error::kOk) return std::unexpected(MakeThunkError(EThunkErrorCode::AssemblerFinalizeFailed, "Failed to finalize register restore thunk assembler."));
    void* Temp{};
    if (GetJitRuntime().add(&Temp, &Code) != Error::kOk) return std::unexpected(MakeThunkError(EThunkErrorCode::JitAddFailed, "Failed to add register restore thunk to the JIT runtime."));
    return FThunkPtr { Temp };
}

FThunkResult GenerateRestoreThunkForArgumentContext(void* CallTo, FuncSignature DestinationSignature, bool bSafe, const bool bLogAssembly) {
    using namespace asmjit;
    using namespace asmjit::x86;

    CodeHolder Code{};
    InitializeCodeHolder(Code, bLogAssembly);
    Assembler TheAssembler { &Code };

    auto SrcInfo = FuncArgInfo{FuncSignature::build<void, ArgumentContext&>()};
    auto DestInfo = FuncArgInfo{DestinationSignature};

    const auto ShadowArgSpace = DestInfo.Detail().arg_stack_size();
    const auto NonVolatileStackSpace = bSafe ? GetPlatformStackSpaceForNonVolatileRegs() : 0;
    auto SumSpace = ShadowArgSpace + NonVolatileStackSpace;
    const auto Padding = (SumSpace % 16 == 8) ? 0 : 8;
    SumSpace += Padding;
    const auto NVPtr = ptr(rsp, ShadowArgSpace + Padding);
    TheAssembler.sub(rsp, SumSpace);
    auto GpScratchReg = GetPlatformGpScratchReg();
    auto XmmScratchReg = GetPlatformXmmScratchReg();

    // save nonvolatiles
    if (bSafe) {
        SaveNonVolatileRegisters(TheAssembler, NVPtr, NVPtr.clone_adjusted(GetPlatformNonVolatileGpRegs().size() * 8));
    }

    const auto ContextReg = SrcInfo.GetArgumentIntegralRegisters()[0];
    const auto ContextPtr = ptr(ContextReg);
    const auto ArgContextPtr = ContextPtr.clone_adjusted(ArgumentContext::ArgsOffset);
    int32_t DeferredContextArgOffset = -1;

    // move arguments from context to call position, excluding ContextReg
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

        // restore all saved registers that aren't args
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

    // make the call
    TheAssembler.call(CallTo);

    // restore nonvolatile registers if needed
    if (bSafe) {
        RestoreNonVolatileRegisters(TheAssembler, NVPtr, NVPtr.clone_adjusted(GetPlatformNonVolatileGpRegs().size() * 8));
    }

    TheAssembler.add(rsp, SumSpace);
    TheAssembler.ret();

    if (TheAssembler.finalize() != Error::kOk) return std::unexpected(MakeThunkError(EThunkErrorCode::AssemblerFinalizeFailed, "Failed to finalize argument restore thunk assembler."));
    void* Temp{};
    if (GetJitRuntime().add(&Temp, &Code) != Error::kOk) return std::unexpected(MakeThunkError(EThunkErrorCode::JitAddFailed, "Failed to add argument restore thunk to the JIT runtime."));
    return FThunkPtr { Temp };
}
