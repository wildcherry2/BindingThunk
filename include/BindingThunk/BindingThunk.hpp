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

	/** @brief Typed wrapper for binding thunk generation.
	 *  @tparam BindingType Compile-time binding mode flags.
	 *  @tparam BindParamType Type of the bound parameter pointer.
	 *  @tparam InReturnType Return type exposed by the generated thunk.
	 *  @tparam InArgs Unbound argument types exposed by the generated thunk.
	 *  @param ToFn Target function whose first parameter receives @p BindParam.
	 *  @param BindParam Pointer bound into the first target argument.
	 *  @details When the unbound signature is exactly @c void(ArgumentContext&), the generated thunk
	 *           automatically enables @ref EBindingThunkType::Argument before forwarding to the raw API.
	 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
	 *  @return Owning thunk pointer on success, or a detailed error on failure.
	 */
	template<EBindingThunkType BindingType = EBindingThunkType::Default, typename BindParamType, typename InReturnType, typename... InArgs>
	THUNK_API FThunkResult GenerateBindingThunk(InReturnType(*ToFn)(BindParamType*, InArgs...), BindParamType* BindParam, const bool bLogAssembly = false) {
	    static_assert(IsValidBindingThunkType(BindingType), "GenerateBindingThunk typed overload was instantiated with an invalid binding thunk type.");
	    constexpr auto bContainsArgumentContext = Detail::ContainsArgumentContextV<InReturnType, InArgs...>;
	    constexpr auto bValidArgumentContextSignature = Detail::IsValidArgumentContextSignatureV<InReturnType, InArgs...>;
	    static_assert(!bContainsArgumentContext || bValidArgumentContextSignature, "ArgumentContext may only appear as the exact unbound parameter type ArgumentContext& of a void callback.");
	    static_assert(!HasBindingThunkTypeFlag(BindingType, EBindingThunkType::Argument) || bValidArgumentContextSignature, "Argument binding thunks require a callback signature of void(BindParamType*, ArgumentContext&).");

	    auto SourceSignature = ABISignature::BuildABISignature<InReturnType, InArgs...>();
	    if (!SourceSignature) {
	        return std::unexpected(SourceSignature.error());
	    }

	    constexpr auto FinalBindingType = bValidArgumentContextSignature ? (BindingType | EBindingThunkType::Argument) : BindingType;

	    return GenerateBindingThunk(
	        reinterpret_cast<void*>(ToFn),
	        reinterpret_cast<void*>(BindParam),
	        SourceSignature.value(),
	        FinalBindingType,
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
		return GenerateBindingThunk<BindingType>(&Traits::template StaticInvoker<MemberFunction>, This, bLogAssembly);
	}
}