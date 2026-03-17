/** @file BindingThunk.hpp
 *  @brief APIs for generating thunks that bind a context pointer to a runtime call target.
 */

#pragma once
#include "Common.hpp"
#include "Context.hpp"

namespace BindingThunk {

/** @brief Generates a binding thunk for a plain callback target.
 *  @param ToFn Target function whose first parameter receives @p BindParam.
 *  @param BindParam Pointer bound into the first target argument. It must remain valid for the thunk lifetime.
 *  @param SourceSignature Signature exposed by the generated thunk. It must match @p ToFn without the bound parameter.
 *  @param Type Binding mode. This overload accepts @ref EBindingThunkType::Default and @ref EBindingThunkType::Register.
 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
 *  @return Owning thunk pointer on success, or a detailed error on failure.
 */
THUNK_API FThunkResult GenerateBindingThunk(void* ToFn, void* BindParam, FuncSignature SourceSignature, EBindingThunkType Type = EBindingThunkType::Default, bool bLogAssembly = false);

/** @brief Typed wrapper for @ref GenerateBindingThunk(void*, void*, FuncSignature, EBindingThunkType, bool).
 *  @tparam BindParamType Type of the bound context pointer.
 *  @tparam InReturnType Return type exposed by the generated thunk and target callback.
 *  @tparam InArgs Unbound argument types exposed by the generated thunk.
 *  @param ToFn Target function whose first parameter receives @p BindParam.
 *  @param BindParam Pointer bound into the first target argument.
 *  @param BindingType Binding mode. This overload accepts @ref EBindingThunkType::Default and @ref EBindingThunkType::Register.
 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
 *  @return Owning thunk pointer on success, or a detailed error on failure.
 */
template<typename BindParamType, typename InReturnType, typename... InArgs>
THUNK_API FThunkResult GenerateBindingThunk(InReturnType(*ToFn)(BindParamType*, InArgs...), BindParamType* BindParam, const EBindingThunkType BindingType = EBindingThunkType::Default, const bool bLogAssembly = false) {
    return GenerateBindingThunk(reinterpret_cast<void*>(ToFn),
        reinterpret_cast<void*>(BindParam),
        FuncSignature::build<AsmJitCompatRetV<InReturnType>, AsmJitCompatArgV<InArgs>...>(),
        BindingType,
        bLogAssembly);
}

/** @brief Generates a binding thunk that forwards unbound arguments through @ref ArgumentContext.
 *  @param ToFn Target callback that receives the bound parameter and an argument context.
 *  @param BindParam Pointer bound into the first target argument. It must remain valid for the thunk lifetime.
 *  @param SourceSignature Signature exposed by the generated thunk.
 *  @param Type Binding mode. This overload accepts @ref EBindingThunkType::Argument and @ref EBindingThunkType::ArgumentAndRegister.
 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
 *  @return Owning thunk pointer on success, or a detailed error on failure.
 */
THUNK_API FThunkResult GenerateBindingThunk(void(*ToFn)(void*, ArgumentContext&), void* BindParam, FuncSignature SourceSignature, EBindingThunkType Type = EBindingThunkType::Argument, bool bLogAssembly = false);

/** @brief Typed wrapper for @ref GenerateBindingThunk(void(*)(void*, ArgumentContext&), void*, FuncSignature, EBindingThunkType, bool).
 *  @tparam BindParamType Type of the bound context pointer.
 *  @tparam InReturnType Return type represented by the generated thunk signature.
 *  @tparam InArgs Unbound argument types packed into the generated @ref ArgumentContext.
 *  @param ToFn Target callback that receives the bound parameter and an argument context.
 *  @param BindParam Pointer bound into the first target argument.
 *  @param Type Binding mode. This overload accepts @ref EBindingThunkType::Argument and @ref EBindingThunkType::ArgumentAndRegister.
 *  @param bLogAssembly When true, emits generated assembly through the configured logger.
 *  @return Owning thunk pointer on success, or a detailed error on failure.
 */
template<typename BindParamType, typename InReturnType, typename... InArgs>
THUNK_API FThunkResult GenerateBindingThunk(void(*ToFn)(BindParamType*, ArgumentContext&), BindParamType* BindParam, EBindingThunkType Type = EBindingThunkType::Argument, const bool bLogAssembly = false) {
    return GenerateBindingThunk(reinterpret_cast<void(*)(void*, ArgumentContext &)>(ToFn), BindParam, FuncSignature::build<AsmJitCompatRetV<InReturnType>, AsmJitCompatArgV<InArgs>...>(), Type, bLogAssembly);
}

}
