#pragma once
#include <asmjit/x86.h>
#include <type_traits>
#include <memory>
#include <vector>
#include <optional>

using asmjit::JitRuntime;
using asmjit::ErrorHandler;
using asmjit::Logger;
using asmjit::RegMask;
using asmjit::x86::Gp;
using asmjit::x86::Vec;
using asmjit::FuncSignature;
using asmjit::FuncDetail;
using asmjit::x86::Assembler;

template<typename T>
    struct AsmJitCompatibleArgStr
{
    using Type = std::conditional_t<std::is_class_v<T> || std::is_aggregate_v<T>,
        std::conditional_t<sizeof(T) <= 8, uint64_t, void*>,
        T>;
};

template<>
struct AsmJitCompatibleArgStr<void>
{
    using Type = void;
};

template<typename T>
using AsmJitCompatibleArg = AsmJitCompatibleArgStr<T>::Type;

struct FThunkDeleter
{
    void operator()(void* Thunk) const noexcept;
};

using FThunkPtr = std::unique_ptr<void, FThunkDeleter>;

class FuncArgInfo
{
public:
    FuncArgInfo() = delete;
    explicit FuncArgInfo(const FuncSignature& Signature);

    [[nodiscard]] const std::vector<asmjit::FuncValue>& GetArguments();
    [[nodiscard]] const std::vector<asmjit::FuncValue>& GetReturnValues();
    [[nodiscard]] RegMask GpRegMask() const;
    [[nodiscard]] RegMask VecRegMask() const;
    [[nodiscard]] const FuncSignature& Signature() const;
    [[nodiscard]] const FuncDetail& Detail() const;

private:
    RegMask _GpRegMask{};
    RegMask _VecRegMask{};
    FuncSignature _Signature{};
    FuncDetail _Detail{};
    std::optional<std::vector<asmjit::FuncValue>> _ArgRegs{};
    std::optional<std::vector<asmjit::FuncValue>> _RetRegs{};
};

// borrowed from SafetyHook
union Xmm {
    uint8_t u8[16];
    uint16_t u16[8];
    uint32_t u32[4];
    uint64_t u64[2];
    float f32[4];
    double f64[2];
};

auto GetJitRuntime() -> JitRuntime&;
auto GetErrorHandler() -> ErrorHandler*;
auto GetLogger() -> Logger*;