#include "RestoreThunk.hpp"
#include "Context.hpp"

FThunkPtr GenerateRestoreThunk(void* CallTo, FuncSignature Signature) {
    using namespace asmjit;
    using namespace asmjit::x86;

    CodeHolder Code{};
    Code.set_logger(GetLogger());
    Code.set_error_handler(GetErrorHandler());
    Code.init(GetJitRuntime().environment(), GetJitRuntime().cpu_features());
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
    SumSpace += SumSpace % 16 == 8 ? 0 : 8;
    TheAssembler.sub(rsp, SumSpace);
    auto GpScratchReg = GetPlatformGpScratchReg();
    /*
     * Roughly:
     * struct Locals {
     *      ShadowArgSpace  // offset 0
     *      IntArgSpace     // offset sizeof ShadowArgSpace
     *      FltArgSpace     // offset sizeof ShadowArgSpace + sizeof IntArgSpace
     *      NVIntRegSpace   // offset sizeof ShadowArgSpace + sizeof IntArgSpace + sizeof FltArgSpace
     *      NVFltRegSpace   // offset sizeof ShadowArgSpace + sizeof IntArgSpace + sizeof FltArgSpace + NVIntRegSpace
     * }
     */
    const auto IntArgPtr = ptr(rsp, ShadowArgSpace);                  // treat as an array of uint64_t
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
    //TheAssembler.offset()
    TheAssembler.call(&RegisterContextStack::Top); // shadow space already allocated

    // restore context, except for arg registers
    // start with pushing nv-registers, since we'll have to restore them after the call
    {
        int Index = 0;
        for (auto& Register : GetPlatformNonVolatileGpRegs()) {
            TheAssembler.mov(NVIntRegPtr.clone_adjusted(8 * Index++), Register);
        }
        Index = 0;
        for (auto& Register : GetPlatformNonVolatileVecRegs()) {
            TheAssembler.movdqu(NVFltRegPtr.clone_adjusted(16 * Index++), Register);
        }
    }

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
    {
        int Index = 0;
        for (auto& Register : GetPlatformNonVolatileGpRegs()) {
            TheAssembler.mov(Register, NVIntRegPtr.clone_adjusted(8 * Index++));
        }
        Index = 0;
        for (auto& Register : GetPlatformNonVolatileVecRegs()) {
            TheAssembler.movdqu(Register, NVFltRegPtr.clone_adjusted(16 * Index++));
        }
    }

    // now we're done, deallocate and return
    TheAssembler.add(rsp, SumSpace);
    TheAssembler.ret();

    if (TheAssembler.finalize() != Error::kOk) return nullptr;
    void* Temp{};
    if (GetJitRuntime().add(&Temp, &Code) != Error::kOk) return nullptr;
    return FThunkPtr { Temp };
}