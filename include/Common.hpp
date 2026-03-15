#pragma once
#include <asmjit/x86.h>
#include <expected>
#include <type_traits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstddef>
#include <functional>

namespace RC::Thunk {

using asmjit::JitRuntime;
using asmjit::CodeHolder;
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
using LogFn = std::function<void(std::wstring_view)>;

enum class EThunkErrorCode {
    InvalidBindingType,
    InvalidSignature,
    UnsupportedType,
    UnsupportedReturnStorage,
    RegisterContextStackOverflow,
    RegisterContextStackUnderflow,
    InvokeCreationFailed,
    AssemblerFinalizeFailed,
    JitAddFailed,
    WindowsUnwindRegistrationFailed,
    ArgumentContextOutOfBoundsArgumentIndex,
};

struct FThunkError {
    EThunkErrorCode Code{};
    std::wstring Message{};
};

using FThunkResult = std::expected<FThunkPtr, FThunkError>;

class FuncArgInfo
{
public:
    FuncArgInfo() = delete;
    explicit FuncArgInfo(const FuncSignature& Signature);

    // returns FuncValues in the order in which they're used to construct the arguments in the signature
    [[nodiscard]] const std::vector<asmjit::FuncValue>& GetArguments() noexcept;
    [[nodiscard]] const std::vector<asmjit::FuncValue>& GetReturnValues() noexcept;
    [[nodiscard]] RegMask GpRegMask() const noexcept;
    [[nodiscard]] RegMask VecRegMask() const noexcept;

    // returns Gp registers used in the arguments. not guaranteed to be in order.
    [[nodiscard]] const std::vector<Gp>& GetArgumentIntegralRegisters() noexcept;

    // returns Vec registers used in the arguments. not guaranteed to be in order.
    [[nodiscard]] const std::vector<Vec>& GetArgumentFloatingRegisters() noexcept;

    [[nodiscard]] const FuncSignature& Signature() const noexcept;
    [[nodiscard]] const FuncDetail& Detail() const noexcept;

private:
    RegMask _GpRegMask{};
    RegMask _VecRegMask{};
    FuncSignature _Signature{};
    FuncDetail _Detail{};
    std::optional<std::vector<asmjit::FuncValue>> _ArgVals{};
    std::optional<std::vector<asmjit::FuncValue>> _RetVals{};
    std::optional<std::vector<Gp>> _IntArgRegs{};
    std::optional<std::vector<Vec>> _VecArgRegs{};
};

const std::vector<Gp>&  GetPlatformNonVolatileGpRegs();
const std::vector<Vec>& GetPlatformNonVolatileVecRegs();
size_t GetPlatformStackSpaceForNonVolatileRegs();
Gp GetPlatformGpScratchReg(); // gets a volatile register that isn't used as an argument on the current platform
Vec GetPlatformXmmScratchReg(); // gets a volatile register that isn't used as an argument on the current platform

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
std::wstring WideFromUtf8(std::string_view Message);
inline FThunkError MakeThunkError(const EThunkErrorCode Code, const std::string_view Message) {
    return FThunkError { Code, WideFromUtf8(Message) };
}
void InitializeCodeHolder(CodeHolder& Code, bool bLogAssembly = false);
auto GetAsmJitErrorHandler() -> ErrorHandler*;
auto GetAsmJitLogger() -> Logger*;
auto GetLogFunction() -> LogFn;
auto GetErrorLogFunction() -> LogFn;
auto SetLogFunction(LogFn fn) -> void;
auto SetErrorLogFunction(LogFn fn) -> void;

struct FManualThunkFramePlan {
    std::vector<Gp> PushedGpRegs{};
    std::vector<Vec> SavedVecRegs{};
    uint32_t RawStackAllocation{};
    uint32_t SavedVecOffset{};
};

struct FManualThunkFrameState {
    FManualThunkFramePlan Plan{};
    uint32_t StackAllocation{};
    uint32_t PushBytes{};
    std::vector<std::byte> WindowsUnwindInfo{};

    [[nodiscard]] uint32_t EntryRspOffset() const noexcept {
        return StackAllocation + PushBytes + sizeof(uint64_t);
    }

    [[nodiscard]] bool HasWindowsUnwindInfo() const noexcept {
        return !WindowsUnwindInfo.empty();
    }
};

#if defined(_WIN64)
struct FThunkWindowsRuntimeInfo {
    asmjit::Label BeginLabel{};
    asmjit::Label EndLabel{};
    asmjit::Label UnwindInfoLabel{};
};
#endif

FManualThunkFrameState EmitManualThunkProlog(Assembler& TheAssembler, FManualThunkFramePlan Plan);
void EmitManualThunkEpilog(Assembler& TheAssembler, const FManualThunkFrameState& FrameState);
void EmitManualThunkWindowsUnwindInfo(Assembler& TheAssembler, const FManualThunkFrameState& FrameState, asmjit::Label UnwindInfoLabel);
#if defined(_WIN64)
std::vector<std::byte> BuildWindowsUnwindInfoForFuncFrame(const asmjit::FuncFrame& Frame);
FThunkResult AddThunkToRuntime(CodeHolder& Code, const char* JitAddErrorMessage, const FThunkWindowsRuntimeInfo* WindowsRuntimeInfo);
#endif
FThunkResult AddThunkToRuntime(CodeHolder& Code, const char* JitAddErrorMessage);

}
