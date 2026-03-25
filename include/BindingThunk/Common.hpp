/** @file Common.hpp
 *  @brief Shared types, platform helpers, logging hooks, and JIT runtime utilities for thunk generation.
 */

#pragma once
#include <array>
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
#include <utility>
#include "BindingThunk/Types.hpp"

namespace BindingThunk {
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

	/** @brief Custom deleter that releases generated thunks back to the shared JIT runtime. */
	struct THUNK_API FThunkDeleter
	{
	    /** @brief Releases a thunk and any platform-specific metadata associated with it.
	     *  @param Thunk Pointer previously returned by the thunk runtime.
	     */
	    void operator()(void* Thunk) const noexcept;
	};

	/** @brief Owning pointer type for generated thunk entry points. */
	using FThunkPtr = std::unique_ptr<void, FThunkDeleter>;
	/** @brief Logging callback used by the assembly logger and error handler hooks. */
	using LogFn = std::function<void(std::wstring_view)>;

	/** @brief Selects how a binding thunk captures and restores call state. */
	enum class EBindingThunkType : uint8_t
	{
	    Default = 0, ///< Generates a direct binding thunk that only injects the bound parameter.
	    Argument = 1 << 0, ///< Packs unbound arguments into an @ref ArgumentContext for callback-style dispatch.
	    Register = 1 << 1, ///< Captures non-argument registers in a side-channel @ref RegisterContextStack for a matching restore thunk.
	};

	/** @brief Bitwise OR for @ref EBindingThunkType flags. */
	inline constexpr auto operator|(const EBindingThunkType Left, const EBindingThunkType Right) noexcept -> EBindingThunkType {
	    return static_cast<EBindingThunkType>(std::to_underlying(Left) | std::to_underlying(Right));
	}

	/** @brief Bitwise AND for @ref EBindingThunkType flags. */
	inline constexpr auto operator&(const EBindingThunkType Left, const EBindingThunkType Right) noexcept -> EBindingThunkType {
	    return static_cast<EBindingThunkType>(std::to_underlying(Left) & std::to_underlying(Right));
	}

	/** @brief Bitwise NOT for @ref EBindingThunkType flags. */
	inline constexpr auto operator~(const EBindingThunkType Value) noexcept -> EBindingThunkType {
	    return static_cast<EBindingThunkType>(~std::to_underlying(Value));
	}

	/** @brief Bitwise OR-assignment for @ref EBindingThunkType flags. */
	inline constexpr auto operator|=(EBindingThunkType& Left, const EBindingThunkType Right) noexcept -> EBindingThunkType& {
	    Left = Left | Right;
	    return Left;
	}

	/** @brief Returns the known non-default binding-thunk flags. */
	inline constexpr auto GetKnownBindingThunkTypeFlags() noexcept -> EBindingThunkType {
	    return EBindingThunkType::Argument | EBindingThunkType::Register;
	}

	/** @brief Returns whether @p Value contains @p Flag. */
	inline constexpr auto HasBindingThunkTypeFlag(const EBindingThunkType Value, const EBindingThunkType Flag) noexcept -> bool {
	    return (Value & Flag) == Flag;
	}

	/** @brief Returns whether @p Value contains only flags known to the library. */
	inline constexpr auto IsValidBindingThunkType(const EBindingThunkType Value) noexcept -> bool {
	    return (std::to_underlying(Value) & ~std::to_underlying(GetKnownBindingThunkTypeFlags())) == 0;
	}

	/** @brief Enumerates failure modes reported while generating or executing thunks. */
	enum class EThunkErrorCode {
	    InvalidBindingType, ///< The requested thunk mode is incompatible with the supplied function shape.
	    InvalidSignature, ///< The generated or inferred calling convention data is inconsistent.
	    UnsupportedType, ///< A parameter or return type cannot be marshalled by the current implementation.
	    UnsupportedReturnStorage, ///< The platform ABI requires a return-storage mechanism that is not implemented.
	    RegisterContextStackOverflow, ///< The per-thread register context stack exceeded its maximum depth.
	    RegisterContextStackUnderflow, ///< A register restore attempted to pop an empty context stack.
	    InvokeCreationFailed, ///< AsmJit failed to create an invoke node for a compiler-generated thunk.
	    AssemblerFinalizeFailed, ///< AsmJit failed to finalize the generated machine code.
	    JitAddFailed, ///< The finalized code could not be added to the JIT runtime.
	    WindowsUnwindRegistrationFailed, ///< Windows unwind metadata could not be generated or registered.
	    ArgumentContextOutOfBoundsArgumentIndex, ///< An @ref ArgumentContext lookup addressed an argument outside the stored range.
	    ABISignatureArgumentIndexOutOfBounds, ///< @ref ABISignature::SetArgumentSlot index was out of bounds.
	    ABISignatureInvalidEnumValue, ///< @ref ABISignature setter was given invalid enum (ArgumentType or ReturnType) value.
	    ABISignatureMissingReturnValue, ///< @ref ABISignature has unset return type when Finalize() is called.
	    ABISignatureMissingArgumentValue, ///< @ref ABISignature has unset argument type(s) when Finalize() is called (a gap of unknown arguments between set arguments).
	};

	/** @brief Describes an error returned from thunk generation. */
	struct THUNK_API FThunkError {
	    EThunkErrorCode Code{}; ///< Machine-readable error classification.
	    std::wstring Message{}; ///< Human-readable description of the failure.
	};

	/** @brief Result type returned by thunk creation APIs. */
	using FThunkResult = std::expected<FThunkPtr, FThunkError>;

	/** @brief Public builder for ABI-level function signatures used by thunk generation.
	 *
	 *  `ABISignature` is a small, validation-oriented wrapper around @c asmjit::FuncSignature.
	 *  It lets callers describe a function in terms of ABI slots instead of raw AsmJit value
	 *  objects, which is often enough for thunk generation on supported platforms.
	 *
	 *  Argument slots are ABI-level slots, not necessarily source-language parameters. This
	 *  distinction matters for calling conventions that can split one C++ parameter across
	 *  multiple ABI arguments or that materialize hidden return-storage pointers. In those
	 *  cases you should populate the slots manually instead of relying on template deduction.
	 *
	 *  Typical usage is either:
	 *  - Manually populate the return slot and each argument slot, then call @ref Finalize.
	 *  - Use @ref BuildABISignature to derive the slot layout from a C++ function type when
	 *    the ABI mapping is representable by the supported `AsmJitCompat*` traits.
	 */
	struct THUNK_API ABISignature {
	    /** @brief ABI classification for a single argument slot. */
	    enum class ArgumentType {
	        Unknown, ///< Slot has not been assigned yet.
	        Integral, ///< Slot is passed through the platform's integer/pointer ABI path.
	        Floating, ///< Slot is passed through the platform's floating-point/vector ABI path.

	        Minimum = Unknown,
	        Maximum = Floating
	    };

	    /** @brief ABI classification for the function return slot. */
	    enum class ReturnType {
	        Unknown, ///< Return type has not been assigned yet.
	        Void, ///< Function does not return a value.
	        Integral, ///< Return value uses the platform's integer/pointer ABI path.
	        Floating, ///< Return value uses the platform's floating-point/vector ABI path.

	        Minimum = Unknown,
	        Maximum = Floating
	    };

	    /** @brief Assigns the ABI class of one argument slot.
	     *  @param Index Zero-based ABI argument slot index.
	     *  @param Type ABI class to assign to that slot.
	     *  @return An error when @p Index or @p Type is invalid; otherwise @c std::nullopt.
	     */
	    std::optional<FThunkError> SetArgumentSlot(uint32_t Index, ArgumentType Type) noexcept;
	    /** @brief Assigns the ABI class of the function return slot.
	     *  @param Type ABI class to assign to the return value.
	     *  @return An error when @p Type is invalid; otherwise @c std::nullopt.
	     */
	    std::optional<FThunkError> SetReturnSlot(ReturnType Type) noexcept;
	    /** @brief Converts the validated slot description into an @c asmjit::FuncSignature.
	     *  @return Finalized signature on success, or an error if required slots are still unset.
	     */
	    std::expected<FuncSignature, FThunkError> Finalize() const noexcept;

	    /** @brief Assigns an argument slot using the public `AsmJitCompatArg` trait machinery.
	     *  @tparam T C++ type to map onto an ABI argument classification.
	     *  @param Index Zero-based ABI argument slot index.
	     *  @return An error when the slot assignment fails; otherwise @c std::nullopt.
	     */
	    template<typename T>
	    std::optional<FThunkError> SetArgumentSlot(const uint32_t Index) noexcept {
	        using CompatType = AsmJitCompatArgV<T>;
	        if constexpr(std::is_floating_point_v<CompatType>) {
	            return SetArgumentSlot(Index, ArgumentType::Floating);
	        }
	        else {
	            return SetArgumentSlot(Index, ArgumentType::Integral);
	        }
	    }

	    /** @brief Assigns the return slot using the public `AsmJitCompatRet` trait machinery.
	     *  @tparam T C++ return type to map onto an ABI return classification.
	     *  @return An error when the slot assignment fails; otherwise @c std::nullopt.
	     */
	    template<typename T>
	    std::optional<FThunkError> SetReturnSlot() noexcept {
	        using CompatType = AsmJitCompatRetV<T>;
	        if constexpr(std::is_void_v<CompatType>) {
	            return SetReturnSlot(ReturnType::Void);
	        }
	        else if constexpr(std::is_floating_point_v<CompatType>) {
	            return SetReturnSlot(ReturnType::Floating);
	        }
	        else {
	            return SetReturnSlot(ReturnType::Integral);
	        }
	    }

	    /** @brief Builds an `ABISignature` from a C++-level return type and argument list.
	     *
	     *  This helper uses the same public compatibility traits as the slot-based setters, so
	     *  it is only valid when each source-language type maps cleanly to the ABI model exposed
	     *  by this library. If a platform ABI splits parameters, inserts hidden parameters, or
	     *  otherwise cannot be represented by those traits, build the signature manually instead.
	     *
	     *  @tparam InReturnType C++ return type to expose from the generated thunk.
	     *  @tparam InArgs C++ argument types to expose from the generated thunk.
	     *  @return A populated `ABISignature`, or the first validation error encountered.
	     */
	    template<typename InReturnType, typename... InArgs>
	    static std::expected<ABISignature, FThunkError> BuildABISignature() noexcept {
	        ABISignature Signature{};
	        if (auto Error = Signature.template SetReturnSlot<InReturnType>()) {
	            return std::unexpected(*Error);
	        }

	        std::optional<FThunkError> ArgumentError{};
	        uint32_t ArgumentIndex = 0;
	        const auto AllArgumentsSet = (... && [&] {
	            if (auto Error = Signature.template SetArgumentSlot<InArgs>(ArgumentIndex++)) {
	                ArgumentError = *Error;
	                return false;
	            }
	            return true;
	        }());

	        if (!AllArgumentsSet) {
	            return std::unexpected(*ArgumentError);
	        }

	        return Signature;
	    }

	private:
	    std::array<ArgumentType, asmjit::Globals::kMaxFuncArgs> _Args{};
	    int _LargestSetArgIndex = -1;
	    ReturnType _ReturnType{ ReturnType::Unknown };
	};

	/** @brief Internal helper that normalizes argument and return placement metadata for a function signature.
	 *
	 *  This wrapper caches register masks, assigned and flattened @c asmjit::FuncValue entries, and the fully
	 *  initialized @c asmjit::FuncDetail so the code generators can reason about a signature without
	 *  repeating ABI queries.
	 */
	class THUNK_API FuncArgInfo
	{
	public:
	    FuncArgInfo() = delete;
	    /** @brief Builds cached ABI metadata for a signature.
	     *  @param Signature Signature to analyze using the current JIT runtime environment.
	     */
	    explicit FuncArgInfo(const FuncSignature& Signature);

	    /** @brief Returns assigned argument values in call order.
	     *  @return Cached argument descriptors for the analyzed signature.
	     */
	    [[nodiscard]] const std::vector<asmjit::FuncValue>& GetArguments() noexcept;
	    /** @brief Returns assigned return values in ABI order.
	     *  @return Cached return descriptors for the analyzed signature.
	     */
	    [[nodiscard]] const std::vector<asmjit::FuncValue>& GetReturnValues() noexcept;
	    /** @brief Returns the bit mask of general-purpose registers consumed by arguments. */
	    [[nodiscard]] RegMask GpRegMask() const noexcept;
	    /** @brief Returns the bit mask of vector registers consumed by arguments. */
	    [[nodiscard]] RegMask VecRegMask() const noexcept;

	    /** @brief Returns the general-purpose argument registers used by the signature.
	     *  @return Cached register list. The order matches the internal register-id scan, not source order.
	     */
	    [[nodiscard]] const std::vector<Gp>& GetArgumentIntegralRegisters() noexcept;

	    /** @brief Returns the vector argument registers used by the signature.
	     *  @return Cached register list. The order matches the internal register-id scan, not source order.
	     */
	    [[nodiscard]] const std::vector<Vec>& GetArgumentFloatingRegisters() noexcept;

	    /** @brief Returns the original function signature. */
	    [[nodiscard]] const FuncSignature& Signature() const noexcept;
	    /** @brief Returns the fully initialized AsmJit function detail for the signature. */
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

	/** @brief Returns the platform non-volatile general-purpose registers tracked by the thunk runtime. */
	THUNK_API const std::vector<Gp>&  GetPlatformNonVolatileGpRegs();
	/** @brief Returns the platform non-volatile vector registers tracked by the thunk runtime. */
	THUNK_API const std::vector<Vec>& GetPlatformNonVolatileVecRegs();
	/** @brief Returns the stack space required to spill all tracked non-volatile registers. */
	THUNK_API size_t GetPlatformStackSpaceForNonVolatileRegs();
	/** @brief Returns a volatile general-purpose scratch register not used for integer arguments or the primary integer return register. */
	THUNK_API Gp GetPlatformGpScratchReg();
	/** @brief Returns a volatile vector scratch register not used for vector arguments on the current platform. */
	THUNK_API Vec GetPlatformXmmScratchReg();

	/** @brief Packed representation of a 128-bit XMM register value.
	 *
	 *  Borrowed from SafetyHook so the thunk runtime can copy vector register state without
	 *  depending on a specific scalar interpretation.
	 */
	union THUNK_API Xmm {
	    uint8_t u8[16]; ///< Byte-wise access.
	    uint16_t u16[8]; ///< 16-bit lane access.
	    uint32_t u32[4]; ///< 32-bit lane access.
	    uint64_t u64[2]; ///< 64-bit lane access.
	    float f32[4]; ///< Single-precision interpretation.
	    double f64[2]; ///< Double-precision interpretation.
	};

	/** @brief Returns the shared AsmJit JIT runtime used for all generated thunks. */
	THUNK_API auto GetJitRuntime() -> JitRuntime&;
	/** @brief Converts UTF-8 text to UTF-16/UTF-32 wide characters for public error reporting. */
	THUNK_API std::wstring WideFromUtf8(std::string_view Message);
	/** @brief Creates an @ref FThunkError from a UTF-8 message.
	 *  @param Code Machine-readable error code.
	 *  @param Message UTF-8 error message.
	 *  @return Wide-character error object suitable for public APIs.
	 */
	THUNK_API inline FThunkError MakeThunkError(const EThunkErrorCode Code, const std::string_view Message) {
	    return FThunkError { Code, WideFromUtf8(Message) };
	}
	/** @brief Initializes an AsmJit code holder for the current runtime and optional assembly logging. */
	THUNK_API void InitializeCodeHolder(CodeHolder& Code, bool bLogAssembly = false);
	/** @brief Returns the shared AsmJit error handler used by generated code. */
	THUNK_API auto GetAsmJitErrorHandler() -> ErrorHandler*;
	/** @brief Returns the shared AsmJit logger used when assembly logging is enabled. */
	THUNK_API auto GetAsmJitLogger() -> Logger*;
	/** @brief Returns the current standard log callback. */
	THUNK_API auto GetLogFunction() -> LogFn;
	/** @brief Returns the current error log callback. */
	THUNK_API auto GetErrorLogFunction() -> LogFn;
	/** @brief Replaces the standard log callback. */
	THUNK_API auto SetLogFunction(LogFn fn) -> void;
	/** @brief Replaces the error log callback. */
	THUNK_API auto SetErrorLogFunction(LogFn fn) -> void;

	/** @brief Describes how a manually emitted thunk frame should save registers and allocate locals. */
	struct THUNK_API FManualThunkFramePlan {
	    std::vector<Gp> PushedGpRegs{}; ///< General-purpose registers to push in prolog order.
	    std::vector<Vec> SavedVecRegs{}; ///< Vector registers to spill to the stack frame.
	    uint32_t RawStackAllocation{}; ///< Requested local allocation before alignment adjustment.
	    uint32_t SavedVecOffset{}; ///< Byte offset from @c rsp to the first saved vector register slot.
	};

	/** @brief Captures the realized layout of a manually emitted thunk frame. */
	struct THUNK_API FManualThunkFrameState {
	    FManualThunkFramePlan Plan{}; ///< Original plan used to build the frame.
	    uint32_t StackAllocation{}; ///< Final aligned stack allocation performed by the prolog.
	    uint32_t PushBytes{}; ///< Bytes consumed by pushed general-purpose registers.
	    std::vector<std::byte> WindowsUnwindInfo{}; ///< Serialized Windows unwind info, if available.

	    /** @brief Returns the displacement from the current @c rsp to the caller entry @c rsp. */
	    [[nodiscard]] uint32_t EntryRspOffset() const noexcept {
	        return StackAllocation + PushBytes + sizeof(uint64_t);
	    }

	    /** @brief Returns whether Windows unwind bytes were generated for the frame. */
	    [[nodiscard]] bool HasWindowsUnwindInfo() const noexcept {
	        return !WindowsUnwindInfo.empty();
	    }
	};

	#if defined(_WIN64)
	/** @brief Labels required to register a thunk with the Windows unwind runtime. */
	struct THUNK_API FThunkWindowsRuntimeInfo {
	    asmjit::Label BeginLabel{}; ///< Entry label of the emitted function body.
	    asmjit::Label EndLabel{}; ///< End label of the emitted function body.
	    asmjit::Label UnwindInfoLabel{}; ///< Label where serialized unwind bytes are embedded.
	};
	#endif

	/** @brief Emits the prolog for a manually assembled thunk frame.
	 *  @param TheAssembler Assembler receiving the prolog instructions.
	 *  @param Plan Register-save and stack-allocation description.
	 *  @return Realized frame state, including alignment adjustments and unwind metadata.
	 */
	THUNK_API FManualThunkFrameState EmitManualThunkProlog(Assembler& TheAssembler, FManualThunkFramePlan Plan);
	/** @brief Emits the epilog that matches a frame previously created by @ref EmitManualThunkProlog. */
	THUNK_API void EmitManualThunkEpilog(Assembler& TheAssembler, const FManualThunkFrameState& FrameState);
	/** @brief Emits serialized Windows unwind metadata for a manual frame if it is available. */
	THUNK_API void EmitManualThunkWindowsUnwindInfo(Assembler& TheAssembler, const FManualThunkFrameState& FrameState, asmjit::Label UnwindInfoLabel);
	#if defined(_WIN64)
	/** @brief Attempts to derive Windows unwind metadata from an AsmJit compiler-generated frame. */
	THUNK_API std::vector<std::byte> BuildWindowsUnwindInfoForFuncFrame(const asmjit::FuncFrame& Frame);
	/** @brief Adds code to the runtime and optionally registers Windows unwind metadata for it. */
	THUNK_API FThunkResult AddThunkToRuntime(CodeHolder& Code, const char* JitAddErrorMessage, const FThunkWindowsRuntimeInfo* WindowsRuntimeInfo);
	#endif
	/** @brief Adds finalized code to the runtime without extra platform metadata registration. */
	THUNK_API FThunkResult AddThunkToRuntime(CodeHolder& Code, const char* JitAddErrorMessage);
}
