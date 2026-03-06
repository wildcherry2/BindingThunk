#pragma once
#include <cstdint>
#include <asmjit/asmjit.h>
#include <asmjit/x86.h>
#include <unordered_map>
#include <unordered_set>
#include "Common.hpp"
#include "Hashes.hpp"

struct RegisterContext {
    uint64_t rflags, rax, rcx, rdx, r8, r9, r10, r11, r12, r13, r14, r15, rdi, rsi, rbx;
    Xmm xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15;
};

#define mro(reg) { asmjit::x86::reg, offsetof(RegisterContext, reg) }
inline std::unordered_map<asmjit::Operand, size_t> RegisterContextOffsets = {
    mro(rax),
    mro(rcx),
    mro(rdx),
    mro(r8),
    mro(r9),
    mro(r10),
    mro(r11),
    mro(r12),
    mro(r13),
    mro(r14),
    mro(r15),
    mro(rdi),
    mro(rsi),
    mro(rbx),
    mro(xmm0),
    mro(xmm1),
    mro(xmm2),
    mro(xmm3),
    mro(xmm4),
    mro(xmm5),
    mro(xmm6),
    mro(xmm7),
    mro(xmm8),
    mro(xmm9),
    mro(xmm10),
    mro(xmm11),
    mro(xmm12),
    mro(xmm13),
    mro(xmm14),
    mro(xmm15),
};
#undef mro

struct RegisterContextStack {
    static void Push(RegisterContext* Context);
    static void Pop();
    static RegisterContext* Top();
};