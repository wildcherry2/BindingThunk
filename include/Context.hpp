#pragma once
#include <bit>
#include <cstdint>
#include <asmjit/x86.h>
#include <unordered_map>
#include "Common.hpp"
#include "Hashes.hpp"

namespace RC::Thunk {

struct THUNK_API RegisterContext {
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

struct THUNK_API RegisterContextStack {
    static void Push(RegisterContext* Context);
    static void Pop();
    static RegisterContext* Top();
};

#ifdef THUNK_ENABLE_TEST_HOOKS
using FRegisterContextStackFatalHandler = void(*)(const char* Message);
THUNK_API void SetRegisterContextStackFatalHandler(FRegisterContextStackFatalHandler Handler);
THUNK_API void ResetRegisterContextStackFatalHandler();
#endif

class THUNK_API ArgumentContext {
public:
    enum : uint64_t {
        HasReturnValueFlag = 1,
        HasRegisterContextFlag = 2,
    };

    template<typename T>
    std::expected<T, EThunkErrorCode> GetArgumentAs(const uint64_t Index) const noexcept {
        if (Index >= _ArgsCount) {
            return std::unexpected(EThunkErrorCode::ArgumentContextOutOfBoundsArgumentIndex);
        }
        return std::bit_cast<T>(_Data[Index]);
    }

    [[nodiscard]] bool HasReturnValue() const noexcept { return _Flags & HasReturnValueFlag; }
    [[nodiscard]] bool HasRegisterContext() const noexcept { return _Flags & HasRegisterContextFlag; }
    // ReSharper disable once CppDFAConstantFunctionResult
    [[nodiscard]] uint64_t GetArgumentsCount() const noexcept { return _ArgsCount; }
    void SetReturnValue(const uint64_t Value) noexcept { _ReturnValue = Value; }

    template<typename T>
    void SetReturnValue(const T value) noexcept {
        static_assert(sizeof(T) <= sizeof(uint64_t), "Only types convertible to uint64_t supported!"); // todo use proper type transform later
        _ReturnValue = std::bit_cast<uint64_t>(value);
    }

    inline static constexpr uint64_t FlagsOffset = 0;
    inline static constexpr uint64_t ReturnValueOffset = 8;
    inline static constexpr uint64_t ArgsCountOffset = 16;
    inline static constexpr uint64_t ArgsOffset = 24;
    inline static constexpr uint64_t ArgumentContextNonVariableSize = 24;
    inline static constexpr uint64_t ArgumentSize = sizeof(uint64_t);
private:
    uint64_t _Flags{};
    uint64_t _ReturnValue{};
    uint64_t _ArgsCount{}; //todo add bounds checking
    uint64_t _Data[];
};

}