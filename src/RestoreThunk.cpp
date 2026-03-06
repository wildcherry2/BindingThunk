#include "RestoreThunk.hpp"
#include "Context.hpp"

struct RestoreThunkLocals {
    uint64_t IncomingIntArgs[4];
    uint64_t NonVolatileIntRegs[4];
    Xmm IncomingFloatArgs[10];
    Xmm NonVolatileFloatRegs[7];
};

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
    // we'll need space for all nonvolatile registers and argument registers in addition to the shadow/arg space.
    const auto LocalsOffset = ArgInfo.Detail().arg_stack_size() + (sizeof(RestoreThunkLocals) + ArgInfo.Detail().arg_stack_size() % 16 == 8 ? 0 : 8);
    TheAssembler.sub(rsp, sizeof(RestoreThunkLocals) + LocalsOffset);
    const auto InitialRspOffset = sizeof(RestoreThunkLocals) + LocalsOffset;

    // we need to call RegisterContextStack::Top, so we need to save the registers with arguments first
    {
        int IntIndex = 0, FloatIndex = 0;
        for (auto& Arg: ArgInfo.GetArgumentIntegralRegisters()) {
            TheAssembler.mov(ptr(rsp, offsetof(RestoreThunkLocals, IncomingIntArgs) + (8 * IntIndex++) + LocalsOffset), Arg);
        }
        for (auto& Arg: ArgInfo.GetArgumentFloatingRegisters()) {
            TheAssembler.movdqu(ptr(rsp, offsetof(RestoreThunkLocals, IncomingIntArgs) + (16 * FloatIndex++) + LocalsOffset), Arg);
        }
    }

    TheAssembler.call(&RegisterContextStack::Top); // shadow space already allocated

    // restore context, except for arg registers
    // start with pushing nv-registers, since we'll have to restore them after the call
    {
        int Index = 0;
        for (auto& Register : GetNonVolatileGpRegs()) {
            TheAssembler.mov(ptr(rsp, offsetof(RestoreThunkLocals, NonVolatileIntRegs) + (8 * Index++) + LocalsOffset), Register);
        }
        Index = 0;
        for (auto& Register : GetNonVolatileVecRegs()) {
            TheAssembler.movdqu(ptr(rsp, offsetof(RestoreThunkLocals, NonVolatileFloatRegs) + (16 * Index++) + LocalsOffset), Register);
        }
    }

    // move arguments on the stack to new stack slot for the call (already allocated)
    for (auto& Arg: ArgInfo.GetArguments()) {
        if (Arg.is_stack()) {
            TheAssembler.mov(r11, ptr(rsp, InitialRspOffset + Arg.stack_offset()));
            TheAssembler.mov(ptr(rsp, Arg.stack_offset()), r11);
        }
    }

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
            TheAssembler.mov(Arg, ptr(rsp, offsetof(RestoreThunkLocals, IncomingIntArgs) + (8 * IntIndex++) + LocalsOffset));
        }
        for (auto& Arg: ArgInfo.GetArgumentFloatingRegisters()) {
            TheAssembler.movdqu(Arg, ptr(rsp, offsetof(RestoreThunkLocals, IncomingIntArgs) + (16 * FloatIndex++) + LocalsOffset));
        }
    }

    // now args are setup and context is restored, make the call
    TheAssembler.call(CallTo);

    // restore nv-registers
    {
        int Index = 0;
        for (auto& Register : GetNonVolatileGpRegs()) {
            TheAssembler.mov(Register, ptr(rsp, offsetof(RestoreThunkLocals, NonVolatileIntRegs) + (8 * Index++) + LocalsOffset));
        }
        Index = 0;
        for (auto& Register : GetNonVolatileVecRegs()) {
            TheAssembler.movdqu(Register, ptr(rsp, offsetof(RestoreThunkLocals, NonVolatileFloatRegs) + (16 * Index++) + LocalsOffset));
        }
    }

    // now we're done, deallocate and return
    TheAssembler.add(rsp, InitialRspOffset);
    TheAssembler.ret();

    if (TheAssembler.finalize() != Error::kOk) return nullptr;
    void* Temp{};
    if (GetJitRuntime().add(&Temp, &Code) != Error::kOk) return nullptr;
    return FThunkPtr { Temp };
}