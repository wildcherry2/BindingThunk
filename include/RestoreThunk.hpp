#pragma once
#include "Common.hpp"

FThunkPtr GenerateRestoreThunk(void* CallTo, FuncSignature Signature);

template<typename InReturnType, typename... InArgs>
FThunkPtr GenerateRestoreThunk(InReturnType(*CallTo)(InArgs...)) {
    return GenerateRestoreThunk(reinterpret_cast<void*>(CallTo),
        FuncSignature::build<AsmJitCompatibleArg<InReturnType>, AsmJitCompatibleArg<InArgs>...>());
}

FThunkPtr GenerateRestoreThunkForArgumentContext(void* CallTo, FuncSignature DestinationSignature, bool bSafe = false);

template<typename InReturnType, typename... InArgs>
FThunkPtr GenerateRestoreThunkForArgumentContext(InReturnType(*CallTo)(InArgs...), bool bSafe = false) {
    return GenerateRestoreThunkForArgumentContext(reinterpret_cast<void*>(CallTo),
        FuncSignature::build<AsmJitCompatibleArg<InReturnType>, AsmJitCompatibleArg<InArgs>...>());
}