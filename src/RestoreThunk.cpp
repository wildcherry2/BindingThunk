#include "RestoreThunk.hpp"
#include "Context.hpp"

struct RestoreThunkLocals {
    uint64_t IncomingIntArgs[4];
    Xmm IncomingFloatArgs[4];
    uint64_t NonVolatileIntRegs[7];
    Xmm NonVolatileFloatRegs[10];
};

// windows specific notes - assumes integral return value register is rax and windows x64 non-volatile register list
FThunkPtr GenerateRestoreThunk(void* CallTo, FuncSignature Signature) {
    using namespace asmjit;
    using namespace asmjit::x86;

    CodeHolder Code{};
    Code.set_logger(GetLogger());
    Code.set_error_handler(GetErrorHandler());
    Code.init(GetJitRuntime().environment(), GetJitRuntime().cpu_features());
    Assembler TheAssembler { &Code };
    FuncArgInfo ArgInfo { Signature };

    // allocate locals
    TheAssembler.sub(rsp, sizeof(RestoreThunkLocals));

    // we need to call RegisterContextStack::Top, so we need to save the incoming args first


    //uint32_t StackSize = 8;
    // we need to call RegisterContextStack::Top, so we need to save the incoming args first
    // std::vector<Vec> SavedXmms{};
    // for (auto& Arg: ArgInfo.GetArguments()) {
    //     if (!Arg.is_reg()) continue;
    //     if (Arg.reg_type() >= RegType::kGp8Lo && Arg.reg_type() <= RegType::kGp64) {
    //         TheAssembler.push(gpq(Arg.reg_id()));
    //         StackSize += 8;
    //     }
    //     else SavedXmms.push_back(xmm(Arg.reg_id()));
    // }
    // if (!SavedXmms.empty()) {
    //     TheAssembler.sub(rsp, SavedXmms.size() * 16);
    //     uint32_t Offset = 0;
    //     for (auto& Arg: SavedXmms) {
    //         TheAssembler.movdqu(ptr(rsp, Offset), Arg);
    //         Offset += 16;
    //         StackSize += 16;
    //     }
    // }
    //
    // // call RegisterContextStack::Top
    // TheAssembler.sub(rsp, 32 + (StackSize % 16 == 8 ? 8 : 0));
    // TheAssembler.call(&RegisterContextStack::Top);
    // TheAssembler.add(rsp, 32 + (StackSize % 16 == 8 ? 8 : 0));
    //
    // // restore context, except for arg registers
    // // start with pushing nv-registers, since we'll have to restore them after the call
    // TheAssembler.push(r12);
    // TheAssembler.push(r13);
    // TheAssembler.push(r14);
    // TheAssembler.push(r15);
    // TheAssembler.push(rdi);
    // TheAssembler.push(rsi);
    // TheAssembler.push(rbx);
    // TheAssembler.sub(rsp, 160);
    // TheAssembler.movdqu(ptr(rsp), xmm6);
    // TheAssembler.movdqu(ptr(rsp, 16), xmm7);
    // TheAssembler.movdqu(ptr(rsp, 32), xmm8);
    // TheAssembler.movdqu(ptr(rsp, 48), xmm9);
    // TheAssembler.movdqu(ptr(rsp, 64), xmm10);
    // TheAssembler.movdqu(ptr(rsp, 80), xmm11);
    // TheAssembler.movdqu(ptr(rsp, 96), xmm12);
    // TheAssembler.movdqu(ptr(rsp, 112), xmm13);
    // TheAssembler.movdqu(ptr(rsp, 128), xmm14);
    // TheAssembler.movdqu(ptr(rsp, 144), xmm15);
    // StackSize += 216;
    //
    // // for each stack arg, move them for the new call
    // StackSize += ArgInfo.Detail().arg_stack_size();
    // const auto CallAlignment = StackSize % 16 == 0 ? 0 : 8;
    // StackSize += CallAlignment;
    // TheAssembler.sub(rsp, ArgInfo.Detail().arg_stack_size() + CallAlignment); // includes shadow space, arg space, and alignment adjustment
    // for (auto& Arg: ArgInfo.GetArguments()) {
    //     if (Arg.is_stack()) {
    //         TheAssembler.mov(r11, ptr(rsp, StackSize + Arg.stack_offset()));
    //         TheAssembler.mov(ptr(rsp, Arg.stack_offset()), r11);
    //     }
    // }
    //
    // // iterate through the RegisterContext using the RegisterContextOffsets map, restoring them if they're not args
    // const auto GpMask = ArgInfo.GpRegMask();
    // const auto VecMask = ArgInfo.VecRegMask();
    // for (auto& [Register, Offset] : RegisterContextOffsets) {
    //     if (Register.is_gp()) {
    //         if ((1 << Register.id()) & GpMask) continue; // register is a integral arg
    //         auto AsGp = Register.as<Gp>();
    //         if (AsGp == rax) continue; // we don't want to overwrite rax until we're done with it
    //         TheAssembler.mov(AsGp, ptr(rax, Offset)); // register is not an arg, restore it
    //     }
    //     else if (Register.is_vec()) {
    //         if ((1 << Register.id()) & VecMask) continue; // register is a floating arg
    //         TheAssembler.movdqu(Register.as<Vec>(), ptr(rax, Offset)); // register is not an arg, restore it
    //     }
    // }
    // TheAssembler.mov(rax, ptr(rax, RegisterContextOffsets[rax]));
    //
    // // restore args
    // const auto ArgsOffset = StackSize - CallAlignment - ArgInfo.Detail().arg_stack_size() - 216;

    return nullptr;
}
