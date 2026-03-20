/** @file RestoreThunk.hpp
 *  @brief APIs for generating thunks that reconstruct original call state from binding-thunk captures.
 */

#pragma once
#include "Common.hpp"
#include "BindingThunk.hpp"

namespace BindingThunk {
	namespace Internal {
		/** @brief Expert-only overload that accepts a raw @c asmjit::FuncSignature directly.
		 *
		 *  Argument-mode restore thunks accept @c void(ArgumentContext&) and write the call result back
		 *  into the context. Register-mode restore thunks reconstruct the original register state before
		 *  calling the destination function.
		 *
		 *  @param CallTo Target function invoked after the captured call state is reconstructed.
		 *  @param Signature Signature of the original unbound destination function.
		 *  @param BindingType Binding mode being restored. @ref EBindingThunkType::Default is invalid.
		 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
		 *  @return Owning thunk pointer on success, or a detailed error on failure.
		 */
		THUNK_API FThunkResult GenerateRestoreThunk(void* CallTo, FuncSignature Signature, EBindingThunkType BindingType, bool bLogAssembly = false);
	}

	/** @brief Generates a restore thunk for a previously bound callback mode.
	 *
	 *  Argument-mode restore thunks accept @c void(ArgumentContext&) and write the call result back
	 *  into the context. Register-mode restore thunks reconstruct the original register state before
	 *  calling the destination function.
	 *
	 *  @param CallTo Target function invoked after the captured call state is reconstructed.
	 *  @param Signature Signature of the original unbound destination function.
	 *  @param BindingType Binding mode being restored. @ref EBindingThunkType::Default is invalid.
	 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
	 *  @return Owning thunk pointer on success, or a detailed error on failure.
	 */
	THUNK_API FThunkResult GenerateRestoreThunk(void* CallTo, const ABISignature& Signature, EBindingThunkType BindingType, bool bLogAssembly = false);

	/** @brief Typed wrapper for restore thunk generation using compile-time ABI deduction.
	 *  @tparam InReturnType Return type of the destination function.
	 *  @tparam InArgs Argument types of the destination function.
	 *  @param CallTo Destination function invoked after captured state is restored.
	 *  @param BindingType Binding mode being restored. @ref EBindingThunkType::Default is invalid.
	 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
	 *  @return Owning thunk pointer on success, or a detailed error on failure.
	 */
	template<typename InReturnType, typename... InArgs>
	THUNK_API FThunkResult GenerateRestoreThunk(InReturnType(*CallTo)(InArgs...), EBindingThunkType BindingType, const bool bLogAssembly = false) {
	    auto Signature = ABISignature::BuildABISignature<InReturnType, InArgs...>();
	    if (!Signature) {
	        return std::unexpected(Signature.error());
	    }

	    return GenerateRestoreThunk(reinterpret_cast<void*>(CallTo), Signature.value(), BindingType, bLogAssembly);
	}
}
