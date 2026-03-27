/** @file Types.hpp
 *  @brief Compile-time helpers that coerce C++ function types into AsmJit-compatible signatures.
 */

#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace BindingThunk {
	class ArgumentContext;

	namespace Detail {
	    template<typename T>
	    inline constexpr bool IsArgumentContextTypeV = std::same_as<std::remove_cvref_t<T>, ArgumentContext>;

	    template<typename... Ts>
	    inline constexpr bool ContainsArgumentContextV = (false || ... || IsArgumentContextTypeV<Ts>);

	    template<typename ReturnType, typename... Args>
	    inline constexpr bool IsValidArgumentContextSignatureV = false;

	    template<typename ReturnType>
	    inline constexpr bool IsValidArgumentContextSignatureV<ReturnType, ArgumentContext&> = std::is_void_v<ReturnType>;

	    /** @brief True when @p T is a scalar or reference type accepted by the thunk helpers. */
	    template<typename T>
	    concept ScalarOrReference = std::is_scalar_v<T> || std::is_reference_v<T>;

	    /** @brief True when @p T is a class or union type. */
	    template<typename T>
	    concept ClassOrUnion = std::is_class_v<T> || std::is_union_v<T>;

	    /** @brief Returns whether @p T has a power-of-two size within the supplied bit range. */
	    template<typename T, size_t MinimumBits = 1, size_t MaximumBits = 64>
	    inline constexpr auto IsSizeOfTPowerOfTwo() -> bool {
	        constexpr auto Size = sizeof(T) * 8;
	        return (Size >= MinimumBits) && (Size <= MaximumBits) && ((Size & (Size - 1)) == 0);
	    }

	    /** @brief Sentinel type used to trigger static assertions in unsupported template branches. */
	    template<typename T>
	    struct AlwaysFalse : std::false_type {};

	#if defined(_WIN32)
	    /** @brief True when Windows x64 passes a class or union directly by value. */
	    template<typename T>
	    concept ClassOrUnionValueActuallyPassedByValue = ClassOrUnion<T> && IsSizeOfTPowerOfTwo<T, 8>();

	    /** @brief True when Windows x64 lowers a class or union argument to an implicit reference. */
	    template<typename T>
	    concept ClassOrUnionValueActuallyPassedByReference = !ClassOrUnionValueActuallyPassedByValue<T>;

	    /** @brief True when Windows x64 returns @p T directly in registers or scalar storage. */
	    template<typename T>
	    concept ReturnedByValue = std::is_reference_v<T>
	                                || (std::is_scalar_v<T> && sizeof(T) <= sizeof(uint64_t))
	                                || (ClassOrUnion<T> && std::is_standard_layout_v<T> && std::is_trivial_v<T> && std::is_aggregate_v<T> && !std::is_polymorphic_v<T> && IsSizeOfTPowerOfTwo<T, 1>());

	    /** @brief True when Windows x64 requires @p T to be returned indirectly. */
	    template<typename T>
	    concept ReturnedByReference = !ReturnedByValue<T>;

	    /** @brief Maps an argument type to the form AsmJit should see if the user did not specialize the trait.
	     *
	     *  Unsupported non-floating forms fall back to a 64-bit integer register class because thunk codegen only
	     *  needs to know whether the ABI uses a general-purpose or floating-point register bank for the value.
	     */
	    template<typename T>
	    struct AsmJitCompatArgNotSpecialized {
	        using Type = std::conditional_t<ScalarOrReference<T> || ClassOrUnionValueActuallyPassedByValue<T>, T,
	                        std::conditional_t<ClassOrUnionValueActuallyPassedByReference<T>, T&, uint64_t>>;
	    };

	    /** @brief Maps a return type to the form AsmJit should see if the user did not specialize the trait. */
	    template<typename T>
	    struct AsmJitCompatRetNotSpecialized {
	        using Type = std::conditional_t<std::is_void_v<T> || ReturnedByValue<T>, T, AlwaysFalse<T>>;
	        static_assert(!std::is_same_v<Type, AlwaysFalse<T>>, "User-specialized AsmJitCompatRet is not supported! Make sure it's coerced into a pointer, reference, or scalar!");
	    };
	#else
	    /** @brief Disabled on non-Windows platforms because template-based ABI deduction is not implemented there. */
	    template<typename T>
	    concept ClassOrUnionValueActuallyPassedByValue = false;

	    /** @brief Disabled on non-Windows platforms because template-based ABI deduction is not implemented there. */
	    template<typename T>
	    concept ClassOrUnionValueActuallyPassedByReference = false;

	    /** @brief Disabled on non-Windows platforms because template-based ABI deduction is not implemented there. */
	    template<typename T>
	    concept ReturnedByValue = false;

	    /** @brief True when the unsupported platform path would need indirect return handling. */
	    template<typename T>
	    concept ReturnedByReference = !ReturnedByValue<T>;

	    /** @brief Placeholder mapping used to force an explanatory static assertion on unsupported platforms. */
	    template<typename T>
	    struct AsmJitCompatArgNotSpecialized {
	        using Type = AlwaysFalse<T>;
	        static_assert(!std::is_same_v<Type, AlwaysFalse<T>>, "AsmJit-based template signature deduction is only supported on Windows. Build ABISignature manually on this platform.");
	    };

	    /** @brief Placeholder mapping used to force an explanatory static assertion on unsupported platforms. */
	    template<typename T>
	    struct AsmJitCompatRetNotSpecialized {
	        using Type = AlwaysFalse<T>;
	        static_assert(!std::is_same_v<Type, AlwaysFalse<T>>, "AsmJit-based template signature deduction is only supported on Windows. Build ABISignature manually on this platform.");
	    };
	#endif
	}

	/** @brief True when @p T is a member function type. */
	template<typename T>
	concept MemberFunction = std::is_member_function_pointer_v<T>;

	/** @brief True when @p MF value is a member function type. */
	template<auto MF>
	concept MemberFunctionValue = std::is_member_function_pointer_v<decltype(MF)>;

	template<MemberFunction T>
	struct MemberFunctionHelper;

	#define THUNK_DECLARE_MEMBER_FUNCTION_HELPER(CV_QUALS, CLASS_TYPE, REF_QUALS, OBJECT_EXPR, NOEXCEPT_QUALS) \
	template<typename C, typename R, typename... As> \
	struct MemberFunctionHelper<R(C::*)(As...) CV_QUALS REF_QUALS NOEXCEPT_QUALS> { \
		using ReturnType = R; \
		using ClassType = CLASS_TYPE; \
		using MemberFunctionType = R (C::*)(As...) CV_QUALS REF_QUALS NOEXCEPT_QUALS; \
		static constexpr bool ContainsArgumentContext = Detail::ContainsArgumentContextV<R, As...>; \
		static constexpr bool IsArgumentContextCallback = Detail::IsValidArgumentContextSignatureV<R, As...>; \
		\
		template<auto MFunction> requires (MemberFunctionValue<MFunction> && std::same_as<decltype(MFunction), MemberFunctionType>) \
		static ReturnType StaticInvoker(ClassType* this_, As... args) NOEXCEPT_QUALS { \
			return ((OBJECT_EXPR).*MFunction)(std::forward<As>(args)...); \
		} \
	};

	#define THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(CV_QUALS, CLASS_TYPE, REF_QUALS, OBJECT_EXPR) \
		THUNK_DECLARE_MEMBER_FUNCTION_HELPER(CV_QUALS, CLASS_TYPE, REF_QUALS, OBJECT_EXPR, ) \
		THUNK_DECLARE_MEMBER_FUNCTION_HELPER(CV_QUALS, CLASS_TYPE, REF_QUALS, OBJECT_EXPR, noexcept)

	/** @brief Helper that gives member function traits and a StaticInvoker that's templated to a member function value. */
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(, C, , *this_)
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(const, const C, , *this_)
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(volatile, volatile C, , *this_)
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(const volatile, const volatile C, , *this_)
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(, C, &, *this_)
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(const, const C, &, *this_)
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(volatile, volatile C, &, *this_)
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(const volatile, const volatile C, &, *this_)
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(, C, &&, std::move(*this_))
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(const, const C, &&, std::move(*this_))
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(volatile, volatile C, &&, std::move(*this_))
	THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS(const volatile, const volatile C, &&, std::move(*this_))

	#undef THUNK_DECLARE_MEMBER_FUNCTION_HELPER_VARIANTS
	#undef THUNK_DECLARE_MEMBER_FUNCTION_HELPER

	#if defined(_WIN32)
	/** @brief Maps a C++ argument type to the form expected by AsmJit signature generation and the ABI. */
	template<typename T>
	struct AsmJitCompatArg {
	    using Type = std::conditional_t<Detail::ScalarOrReference<T> || Detail::ClassOrUnionValueActuallyPassedByValue<T>, T,
	                    std::conditional_t<Detail::ClassOrUnionValueActuallyPassedByReference<T>, T&, uint64_t>>;
	};

	/** @brief Maps a C++ return type to the form expected by AsmJit signature generation and the ABI. */
	template<typename T>
	struct AsmJitCompatRet {
	    using Type = std::conditional_t<Detail::ReturnedByValue<T>, T, Detail::AlwaysFalse<T>>;
	    static_assert(!std::is_same_v<Type, Detail::AlwaysFalse<T>>, "T is not a supported type for return type deduction! Manually make ABISignature instead or specialize if you know it's returned by value!");
	};

	/** @brief Specialization that preserves @c void returns. */
	template<>
	struct AsmJitCompatRet<void> {
	    using Type = void;
	};
	#else
	/** @brief Unsupported-platform placeholder for argument type deduction. */
	template<typename T>
	struct AsmJitCompatArg {
	    using Type = Detail::AlwaysFalse<T>;
	    static_assert(!std::is_same_v<Type, Detail::AlwaysFalse<T>>, "AsmJit-based template signature deduction is only supported on Windows. Build ABISignature manually on this platform.");
	};

	/** @brief Unsupported-platform placeholder for return type deduction. */
	template<typename T>
	struct AsmJitCompatRet {
	    using Type = Detail::AlwaysFalse<T>;
	    static_assert(!std::is_same_v<Type, Detail::AlwaysFalse<T>>, "AsmJit-based template signature deduction is only supported on Windows. Build ABISignature manually on this platform.");
	};
	#endif

	/** @brief Convenience alias for the AsmJit-compatible form of an argument type. */
	template<typename T>
	using AsmJitCompatArgV = Detail::AsmJitCompatArgNotSpecialized<typename AsmJitCompatArg<T>::Type>::Type;

	/** @brief Convenience alias for the AsmJit-compatible form of a return type. */
	template<typename T>
	using AsmJitCompatRetV = Detail::AsmJitCompatRetNotSpecialized<typename AsmJitCompatRet<T>::Type>::Type;
}
