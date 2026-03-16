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
#include "Types.hpp"

namespace RC::Thunk {

#if defined(_WIN32)
#  if defined(THUNK_SHARED)
#    if defined(THUNK_EXPORTS)
#      define THUNK_API __declspec(dllexport)
#    else
#      define THUNK_API __declspec(dllimport)
#    endif
#  else
#    define THUNK_API
#  endif
#elif defined(__GNUC__) && defined(THUNK_SHARED)
#  define THUNK_API __attribute__((visibility("default")))
#else
#  define THUNK_API
#endif

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

struct THUNK_API FThunkDeleter
{
    void operator()(void* Thunk) const noexcept;
};

using FThunkPtr = std::unique_ptr<void, FThunkDeleter>;
using LogFn = std::function<void(std::wstring_view)>;

enum class EBindingThunkType
{
    Default, // Generates a simple binding thunk.
    Argument, // Generates a binding thunk that compacts unbound arguments into an ArgumentContext. Generate a RestoreThunk to unwrap the ArgumentContext to call a function with an identical SourceSignature.
    Register, // Generates a binding thunk that saves all non-argument registers to a side channel stack, and restores all non-argument registers to call a function in a corresponding RestoreThunk.
    ArgumentAndRegister, // Combination of Argument and Register options. Compacts unbound arguments into an ArgumentContext and saves non-argument registers.
};

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

struct THUNK_API FThunkError {
    EThunkErrorCode Code{};
    std::wstring Message{};
};

using FThunkResult = std::expected<FThunkPtr, FThunkError>;

class THUNK_API FuncArgInfo
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

THUNK_API const std::vector<Gp>&  GetPlatformNonVolatileGpRegs();
THUNK_API const std::vector<Vec>& GetPlatformNonVolatileVecRegs();
THUNK_API size_t GetPlatformStackSpaceForNonVolatileRegs();
THUNK_API Gp GetPlatformGpScratchReg(); // gets a volatile register that isn't used as an argument on the current platform
THUNK_API Vec GetPlatformXmmScratchReg(); // gets a volatile register that isn't used as an argument on the current platform

// borrowed from SafetyHook
union THUNK_API Xmm {
    uint8_t u8[16];
    uint16_t u16[8];
    uint32_t u32[4];
    uint64_t u64[2];
    float f32[4];
    double f64[2];
};

THUNK_API auto GetJitRuntime() -> JitRuntime&;
THUNK_API std::wstring WideFromUtf8(std::string_view Message);
THUNK_API inline FThunkError MakeThunkError(const EThunkErrorCode Code, const std::string_view Message) {
    return FThunkError { Code, WideFromUtf8(Message) };
}
THUNK_API void InitializeCodeHolder(CodeHolder& Code, bool bLogAssembly = false);
THUNK_API auto GetAsmJitErrorHandler() -> ErrorHandler*;
THUNK_API auto GetAsmJitLogger() -> Logger*;
THUNK_API auto GetLogFunction() -> LogFn;
THUNK_API auto GetErrorLogFunction() -> LogFn;
THUNK_API auto SetLogFunction(LogFn fn) -> void;
THUNK_API auto SetErrorLogFunction(LogFn fn) -> void;

struct THUNK_API FManualThunkFramePlan {
    std::vector<Gp> PushedGpRegs{};
    std::vector<Vec> SavedVecRegs{};
    uint32_t RawStackAllocation{};
    uint32_t SavedVecOffset{};
};

struct THUNK_API FManualThunkFrameState {
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
struct THUNK_API FThunkWindowsRuntimeInfo {
    asmjit::Label BeginLabel{};
    asmjit::Label EndLabel{};
    asmjit::Label UnwindInfoLabel{};
};
#endif

THUNK_API FManualThunkFrameState EmitManualThunkProlog(Assembler& TheAssembler, FManualThunkFramePlan Plan);
THUNK_API void EmitManualThunkEpilog(Assembler& TheAssembler, const FManualThunkFrameState& FrameState);
THUNK_API void EmitManualThunkWindowsUnwindInfo(Assembler& TheAssembler, const FManualThunkFrameState& FrameState, asmjit::Label UnwindInfoLabel);
#if defined(_WIN64)
THUNK_API std::vector<std::byte> BuildWindowsUnwindInfoForFuncFrame(const asmjit::FuncFrame& Frame);
THUNK_API FThunkResult AddThunkToRuntime(CodeHolder& Code, const char* JitAddErrorMessage, const FThunkWindowsRuntimeInfo* WindowsRuntimeInfo);
#endif
THUNK_API FThunkResult AddThunkToRuntime(CodeHolder& Code, const char* JitAddErrorMessage);

}