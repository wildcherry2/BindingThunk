#pragma once
#include "Common.hpp"

FThunkPtr GenerateRestoreThunk(void* CallTo, FuncSignature Signature);

template<typename InReturnType, typename... InArgs>
FThunkPtr GenerateRestoreThunk(InReturnType(*CallTo)(InArgs...)) {
    return GenerateRestoreThunk(reinterpret_cast<void*>(CallTo),
        FuncSignature::build<AsmJitCompatibleArg<InReturnType>, AsmJitCompatibleArg<InArgs>...>());
}