#pragma once
#include "Common.hpp"
#include "BindingThunk.hpp"

namespace BindingThunk {

// Restore thunks are only valid for non-default binding modes.
THUNK_API FThunkResult GenerateRestoreThunk(void* CallTo, FuncSignature Signature, EBindingThunkType BindingType, bool bLogAssembly = false);

template<typename InReturnType, typename... InArgs>
THUNK_API FThunkResult GenerateRestoreThunk(InReturnType(*CallTo)(InArgs...), EBindingThunkType BindingType, const bool bLogAssembly = false) {
    return GenerateRestoreThunk(reinterpret_cast<void*>(CallTo),
        FuncSignature::build<AsmJitCompatRetV<InReturnType>, AsmJitCompatArgV<InArgs>...>(),
        BindingType,
        bLogAssembly);
}

}
