/** @file BindingThunk.hpp
 *  @brief APIs for generating thunks that bind a context pointer to a runtime call target.
 */

#pragma once
#include "BindingThunk/Common.hpp"
#include "BindingThunk/Context.hpp"

namespace BindingThunk {
	namespace Internal {
		/** @brief Expert-only overload that accepts a raw @c asmjit::FuncSignature directly.
		 *  @param ToFn Target function whose first parameter receives @p BindParam.
		 *  @param BindParam Pointer bound into the first target argument. It must remain valid for the thunk lifetime.
		 *  @param SourceSignature Signature exposed by the generated thunk. It must match the unbound ABI seen by callers.
		 *  @param Type Binding mode flags.
		 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
		 *  @return Owning thunk pointer on success, or a detailed error on failure.
		 */
		THUNK_API FThunkResult GenerateBindingThunk(void* ToFn, void* BindParam, FuncSignature SourceSignature, EBindingThunkType Type = EBindingThunkType::Default, bool bLogAssembly = false);
	}

	/** @brief Generates a binding thunk from an explicit ABI signature.
	 *  @param ToFn Target function whose first parameter receives @p BindParam.
	 *  @param BindParam Pointer bound into the first target argument. It must remain valid for the thunk lifetime.
	 *  @param SourceSignature ABI signature exposed by the generated thunk.
	 *  @param Type Binding mode flags.
	 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
	 *  @return Owning thunk pointer on success, or a detailed error on failure.
	 */
	THUNK_API FThunkResult GenerateBindingThunk(void* ToFn, void* BindParam, const ABISignature& SourceSignature, EBindingThunkType Type = EBindingThunkType::Default, bool bLogAssembly = false);

	/** @brief Typed wrapper for binding thunk generation. Templated types can be inferred.
	 *  @tparam BindingType Compile-time binding mode flags.
	 *  @tparam BindParamType Type of the bound parameter pointer.
	 *  @tparam InReturnType Return type exposed by the generated thunk.
	 *  @tparam InArgs Unbound argument types exposed by the generated thunk.
	 *  @param ToFn Target function whose first parameter receives @p BindParam.
	 *  @param BindParam Pointer bound into the first target argument.
	 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
	 *  @return Owning thunk pointer on success, or a detailed error on failure.
	 */
	template<EBindingThunkType BindingType = EBindingThunkType::Default, typename BindParamType, typename InReturnType, typename... InArgs>
	THUNK_API FThunkResult GenerateBindingThunk(InReturnType(*ToFn)(BindParamType*, InArgs...), BindParamType* BindParam, const bool bLogAssembly = false) {
	    static_assert(IsValidBindingThunkType(BindingType), "GenerateBindingThunk typed overload was instantiated with an invalid binding thunk type.");
	    static_assert(!ContainsArgumentContextV<InReturnType, InArgs...>, "ArgumentContext callbacks require the explicit typed overload that takes the real thunk return type and arguments as template parameters.");
	    static_assert(!HasBindingThunkTypeFlag(BindingType, EBindingThunkType::Argument), "Argument binding thunks require the explicit typed overload for void(BindParamType*, ArgumentContext&) callbacks.");

	    auto SourceSignature = ABISignature::BuildABISignature<InReturnType, InArgs...>();
	    if (!SourceSignature) {
	        return std::unexpected(SourceSignature.error());
	    }

	    return GenerateBindingThunk(
	        reinterpret_cast<void*>(ToFn),
	        const_cast<void*>(reinterpret_cast<const void*>(BindParam)),
	        SourceSignature.value(),
	        BindingType,
	        bLogAssembly
	    );
	}

	/** @brief Typed wrapper for ArgumentContext callbacks with an explicit caller-facing signature. Most templated types can't be inferred.
	 *  @tparam BindingType Compile-time binding mode flags. @ref EBindingThunkType::Argument is always enabled.
	 *  @tparam InReturnType Return type exposed by the generated thunk. Can't be inferred.
	 *  @tparam InArgs Unbound argument types exposed by the generated thunk. Can't be inferred.
	 *  @tparam BindParamType Type of the bound parameter pointer. Inferred through @p ToFn.
	 *  @param ToFn Target function whose first parameter receives @p BindParam and whose second parameter receives the marshalled @ref ArgumentContext.
	 *  @param BindParam Pointer bound into the first target argument.
	 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
	 *  @return Owning thunk pointer on success, or a detailed error on failure.
	 */
	template<EBindingThunkType BindingType = EBindingThunkType::Default, typename InReturnType, typename... InArgs, typename BindParamType>
	THUNK_API FThunkResult GenerateBindingThunk(void(*ToFn)(BindParamType*, ArgumentContext&), BindParamType* BindParam, const bool bLogAssembly = false) {
	    static_assert(IsValidBindingThunkType(BindingType), "GenerateBindingThunk ArgumentContext overload was instantiated with an invalid binding thunk type.");
	    static_assert(!ContainsArgumentContextV<InReturnType, InArgs...>, "The explicit ArgumentContext overload requires the real caller-facing return type and arguments.");

	    auto SourceSignature = ABISignature::BuildABISignature<InReturnType, InArgs...>();
	    if (!SourceSignature) {
	        return std::unexpected(SourceSignature.error());
	    }

	    return GenerateBindingThunk(
	        reinterpret_cast<void*>(ToFn),
	        const_cast<void*>(reinterpret_cast<const void*>(BindParam)),
	        SourceSignature.value(),
	        BindingType | EBindingThunkType::Argument,
	        bLogAssembly
	    );
	}

	/** @brief Typed wrapper for safely binding member functions.
	 * @tparam MemberFunction The value of the member function to bind to. Can be virtual.
	 * @tparam BindingType Compile-time binding mode flags.
	 * @tparam Traits Helper struct type to deduce a static invoker and 'this' type from @ref MemberFunction.
	 * @param This The 'this' pointer to bind to the member function.
	 * @param bLogAssembly When true, emits generated assembly through the configured logger.
	 * @return Owning thunk pointer on success, or a detailed error on failure.
	 */
	template<auto MemberFunction, EBindingThunkType BindingType = EBindingThunkType::Default, typename Traits = MemberFunctionHelper<decltype(MemberFunction)>>
		requires MemberFunctionValue<MemberFunction>
	THUNK_API FThunkResult GenerateBindingThunk(typename Traits::ClassType* This, const bool bLogAssembly = false) {
		static_assert(!Traits::ContainsArgumentContext, "ArgumentContext member callbacks require the explicit member-function overload that takes the real thunk return type and arguments as template parameters.");
		static_assert(!HasBindingThunkTypeFlag(BindingType, EBindingThunkType::Argument), "Argument binding thunks require the explicit member-function overload for void(ArgumentContext&) callbacks.");
		return GenerateBindingThunk<BindingType>(&Traits::template StaticInvoker<MemberFunction>, This, bLogAssembly);
	}

	/** @brief Typed wrapper for safely binding member functions for ArgumentContext types with a specified return type and arguments.
	 * @tparam MemberFunction The value of the member function to bind to. Can be virtual. It must be a member function with signature @c void(ArgumentContext&).
	 * @tparam BindingType Compile-time binding mode flags.
	 * @tparam InReturnType The return type exposed by the generated thunk.
	 * @tparam InArgs The arguments exposed by the generated thunk.
	 * @param This The 'this' pointer to bind to the member function.
	 * @param bLogAssembly When true, emits generated assembly through the configured logger.
	 * @return Owning thunk pointer on success, or a detailed error on failure.
	 */
	template<auto MemberFunction, EBindingThunkType BindingType, typename InReturnType, typename... InArgs>
		requires MemberFunctionValue<MemberFunction>
	THUNK_API FThunkResult GenerateBindingThunk(typename MemberFunctionHelper<decltype(MemberFunction)>::ClassType* This, const bool bLogAssembly = false) {
		using Traits = MemberFunctionHelper<decltype(MemberFunction)>;
		static_assert(Traits::IsArgumentContextCallback, "The explicit member-function overload requires a callback signature of void(ArgumentContext&).");
		return GenerateBindingThunk<BindingType, InReturnType, InArgs...>(
			&MemberFunctionHelper<decltype(MemberFunction)>::template StaticInvoker<MemberFunction>, This, bLogAssembly);
	}
}
