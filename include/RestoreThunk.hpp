#pragma once
#include "Common.hpp"
#include "BindingThunk.hpp"

namespace BindingThunk {

// Restore thunks are only valid for non-default binding modes.
// For Argument types, a restore thunk can unwrap the structure and call a function with the same signature that excludes the binding argument.
//      The signature of an Argument restore thunk is void(ArgumentContext&).
//      Calling the function with the restore thunk sets the ArgumentContext's return value.
// For ArgumentRegister types, a restore can unwrap the structure and call a function with the same signature that excludes the binding argument, but restore
//  the state of all other registers right before the call.
// For Register types, a restore thunk can call a function with the same signature that excludes the binding argument, but restore the state of all other
//  registers right before the call.
THUNK_API FThunkResult GenerateRestoreThunk(void* CallTo, FuncSignature Signature, EBindingThunkType BindingType, bool bLogAssembly = false);

template<typename InReturnType, typename... InArgs>
THUNK_API FThunkResult GenerateRestoreThunk(InReturnType(*CallTo)(InArgs...), EBindingThunkType BindingType, const bool bLogAssembly = false) {
    return GenerateRestoreThunk(reinterpret_cast<void*>(CallTo),
        FuncSignature::build<AsmJitCompatRetV<InReturnType>, AsmJitCompatArgV<InArgs>...>(),
        BindingType,
        bLogAssembly);
}

}
