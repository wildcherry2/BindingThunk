/** @file Context.hpp
 *  @brief Argument and register context containers used by argument and register thunk modes.
 */

#pragma once
#include <bit>
#include <cstring>
#include <cstdint>
#include <type_traits>
#include <asmjit/x86.h>
#include <unordered_map>
#include "Common.hpp"
#include "Hashes.hpp"

namespace BindingThunk {
	/** @brief Captured register state for register-based thunk modes. */
	struct THUNK_API RegisterContext {
	    uint64_t rflags, rax, rcx, rdx, r8, r9, r10, r11, r12, r13, r14, r15, rdi, rsi, rbx; ///< Captured general-purpose registers and flags.
	    Xmm xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11, xmm12, xmm13, xmm14, xmm15; ///< Captured XMM registers.
	};

	#define mro(reg) { asmjit::x86::reg, offsetof(RegisterContext, reg) }
	/** @brief Maps an AsmJit register operand to its byte offset within @ref RegisterContext. */
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

	/** @brief Thread-local stack used to pair register binding thunks with restore thunks. */
	struct THUNK_API RegisterContextStack {
	    /** @brief Pushes a captured register context for the current thread.
	     *  @param Context Context captured by a binding thunk.
	     */
	    static void Push(RegisterContext* Context);
	    /** @brief Pops the most recent captured register context for the current thread. */
	    static void Pop();
	    /** @brief Returns the most recent captured register context for the current thread. */
	    static RegisterContext* Top();
	};

	#ifdef THUNK_ENABLE_TEST_HOOKS
	/** @brief Fatal handler signature used by tests to intercept register-context stack failures. */
	using FRegisterContextStackFatalHandler = void(*)(const char* Message);
	/** @brief Installs a fatal handler for register-context stack failures while tests are enabled. */
	THUNK_API void SetRegisterContextStackFatalHandler(FRegisterContextStackFatalHandler Handler);
	/** @brief Restores the default fatal handler for register-context stack failures. */
	THUNK_API void ResetRegisterContextStackFatalHandler();
	#endif

	/** @brief Compact, variable-size container used by argument thunk modes.
	 *
	 *  The fixed header stores flags, a single return value slot, and the number of packed
	 *  arguments. Each packed argument then occupies one 64-bit slot in the trailing array.
	 *
	 *  It's possible to extend this class to add on more functionality, just ensure that you don't
	 *  add virtual functions or member variables, since the allocation for this object is done
	 *  completely in ASM.
	 */
	class THUNK_API ArgumentContext {
	public:
	    /** @brief Bit flags stored in the header portion of the context. */
	    enum : uint64_t {
	        HasReturnValueFlag = 1, ///< Indicates that the original function has a return value.
	        HasRegisterContextFlag = 2, ///< Indicates that a @ref RegisterContext is appended after the packed arguments.
	    };

	    /** @brief Reads a packed argument as type @p T.
	     *  @tparam T Source-language argument type to reconstruct from the stored ABI-lowered slot.
	     *  @param Index Zero-based packed argument index.
	     *  @details If the platform ABI lowered a by-value argument to an implicit reference, this method
	     *  dereferences the stored pointer and returns a copy of the original value. True reference
	     *  parameters should still be accessed explicitly through a pointer-like type.
	     *  @return The requested value or @ref EThunkErrorCode::ArgumentContextOutOfBoundsArgumentIndex if @p Index is invalid.
	     */
	    template<typename T>
	    std::expected<T, EThunkErrorCode> GetArgumentAs(const uint64_t Index) const noexcept {
	        static_assert(!std::is_reference_v<T>, "ArgumentContext::GetArgumentAs does not support reference types directly. Use a pointer-like type and dereference it explicitly.");
	        if (Index >= _ArgsCount) {
	            return std::unexpected(EThunkErrorCode::ArgumentContextOutOfBoundsArgumentIndex);
	        }
	        using LoweredType = AsmJitCompatArgV<T>;
	        if constexpr (!std::is_reference_v<T> && std::is_reference_v<LoweredType>) {
	            using PointedType = std::remove_reference_t<LoweredType>;
	            return *ReadPackedValue<PointedType*>(_Data[Index]);
	        }
	        else {
	            return ReadPackedValue<T>(_Data[Index]);
	        }
	    }

		/** @brief Gets the raw 8-byte value at the argument index.
		 * @param Index Zero-based packed argument index.
		 * @details This is an 'escape hatch' from the type system. This could be a pointer, reference, reference to caller-allocated copy,
		 * value, or part of a larger structure (Linux); the invoker of this function is responsible for using it properly.
		 * @return The requested value or @ref EThunkErrorCode::ArgumentContextOutOfBoundsArgumentIndex if @p Index is invalid.
		 */
		std::expected<uint64_t, EThunkErrorCode> GetArgumentRaw(const uint64_t Index) const noexcept {
	    	if (Index >= _ArgsCount) {
	    		return std::unexpected(EThunkErrorCode::ArgumentContextOutOfBoundsArgumentIndex);
	    	}

	    	return _Data[Index];
	    }

	    /** @brief Returns whether the original function had a return value slot. */
	    [[nodiscard]] bool HasReturnValue() const noexcept { return _Flags & HasReturnValueFlag; }
	    /** @brief Returns whether a register context follows the packed argument array. */
	    [[nodiscard]] bool HasRegisterContext() const noexcept { return _Flags & HasRegisterContextFlag; }
	    /** @brief Returns the number of packed arguments stored in the context. */
	    [[nodiscard]] uint64_t GetArgumentsCount() const noexcept { return _ArgsCount; }
	    /** @brief Reads the stored return value as type @p T.
	     *  @tparam T Return type to reconstruct from the raw 64-bit return slot. Defaults to @c uint64_t.
	     *  @details Reference return types are not supported directly. Use a pointer-like type and
	     *  dereference it explicitly if the original function returned an address. This accessor only
	     *  supports source-language types that the platform ABI returns directly by value.
	     *  @return The return value reconstructed from the packed slot.
	     */
	    template<typename T = uint64_t>
	    [[nodiscard]] T GetReturnValue() const noexcept {
	        static_assert(!std::is_reference_v<T>, "ArgumentContext::GetReturnValue does not support reference types directly. Use a pointer-like type and dereference it explicitly.");
	        static_assert(Detail::ReturnedByValue<T>, "ArgumentContext::GetReturnValue only supports source-language types that are returned directly by value. Use a pointer-like type for indirect return forms.");
	        return ReadPackedValue<T>(_ReturnValue);
	    }
	    /** @brief Stores a raw 64-bit return value into the context. */
	    void SetReturnValue(const uint64_t Value) noexcept { _ReturnValue = Value; }

	    /** @brief Stores a typed return value into the raw 64-bit return slot.
	     *  @tparam T Value type to write.
	     *  @param value Value to store.
	     */
	    template<typename T>
	    void SetReturnValue(const T value) noexcept {
	        static_assert(sizeof(T) <= sizeof(uint64_t), "ArgumentContext only supports return types up to 64 bits.");
	        if constexpr (sizeof(T) == sizeof(uint8_t)) {
	            _ReturnValue = std::bit_cast<uint8_t>(value);
	        }
	        else if constexpr (sizeof(T) == sizeof(uint16_t)) {
	            _ReturnValue = std::bit_cast<uint16_t>(value);
	        }
	        else if constexpr (sizeof(T) == sizeof(uint32_t)) {
	            _ReturnValue = std::bit_cast<uint32_t>(value);
	        }
	        else if constexpr (sizeof(T) == sizeof(uint64_t)) {
	            _ReturnValue = std::bit_cast<uint64_t>(value);
	        }
	        else {
	            static_assert(sizeof(T) < sizeof(uint64_t), "ArgumentContext only uses memcpy fallback for return values smaller than 64 bits.");
	            static_assert(std::is_trivially_copyable_v<T>, "ArgumentContext only supports trivially copyable return types.");
	            uint64_t Raw {};
	            std::memcpy(&Raw, &value, sizeof(T));
	            _ReturnValue = Raw;
	        }
	    }

	    inline static constexpr uint64_t FlagsOffset = 0; ///< Byte offset of the flags field from the context base.
	    inline static constexpr uint64_t ReturnValueOffset = 8; ///< Byte offset of the return slot from the context base.
	    inline static constexpr uint64_t ArgsCountOffset = 16; ///< Byte offset of the argument-count field from the context base.
	    inline static constexpr uint64_t ArgsOffset = 24; ///< Byte offset of the packed argument array from the context base.
	    inline static constexpr uint64_t ArgumentContextNonVariableSize = 24; ///< Size of the fixed header before the trailing packed argument array.
	    inline static constexpr uint64_t ArgumentSize = sizeof(uint64_t); ///< Size in bytes of each packed argument slot.
	private:
	    template<typename T>
	    static T ReadPackedValue(const uint64_t Raw) noexcept {
	        static_assert(sizeof(T) <= sizeof(uint64_t), "ArgumentContext only supports packed values up to 64 bits.");
	        if constexpr (sizeof(T) == sizeof(uint8_t)) {
	            return std::bit_cast<T>(static_cast<uint8_t>(Raw));
	        }
	        else if constexpr (sizeof(T) == sizeof(uint16_t)) {
	            return std::bit_cast<T>(static_cast<uint16_t>(Raw));
	        }
	        else if constexpr (sizeof(T) == sizeof(uint32_t)) {
	            return std::bit_cast<T>(static_cast<uint32_t>(Raw));
	        }
	        else if constexpr (sizeof(T) == sizeof(uint64_t)) {
	            return std::bit_cast<T>(Raw);
	        }
	        else {
	            static_assert(sizeof(T) < sizeof(uint64_t), "ArgumentContext only uses memcpy fallback for packed values smaller than 64 bits.");
	            static_assert(std::is_trivially_copyable_v<T>, "ArgumentContext only supports trivially copyable packed values.");
	            T Value {};
	            std::memcpy(&Value, &Raw, sizeof(T));
	            return Value;
	        }
	    }

	    uint64_t _Flags{}; ///< Context flags described by @ref HasReturnValueFlag and @ref HasRegisterContextFlag.
	    uint64_t _ReturnValue{}; ///< Raw return value storage used by argument restore thunks.
	    uint64_t _ArgsCount{}; ///< Number of valid entries in @ref _Data.
	    uint64_t _Data[]; ///< Trailing packed argument array.
	};
}
