#pragma once
#include "Common.hpp"
#include "BindingThunk.hpp"

namespace RC::Thunk {

// Restore thunks are only valid for non-default binding modes.
THUNK_API FThunkResult GenerateRestoreThunk(void* CallTo, FuncSignature Signature, EBindingThunkType BindingType, bool bLogAssembly = false);

template<typename InReturnType, typename... InArgs>
FThunkResult GenerateRestoreThunk(InReturnType(*CallTo)(InArgs...), EBindingThunkType BindingType, const bool bLogAssembly = false) {
    return GenerateRestoreThunk(reinterpret_cast<void*>(CallTo),
        FuncSignature::build<AsmJitCompatibleArg<InReturnType>, AsmJitCompatibleArg<InArgs>...>(),
        BindingType,
        bLogAssembly);
}

}
