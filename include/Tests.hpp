#pragma once

#include "BindingThunk.hpp"
#include "RestoreThunk.hpp"

namespace RC::Thunk {

template<typename FnType>
FnType ThunkCast(const FThunkPtr& Thunk) {
    return reinterpret_cast<FnType>(Thunk.get());
}

#ifdef THUNK_ENABLE_TEST_HOOKS
THUNK_API FuncSignature ShiftSignature(const FuncSignature& InSignature);
THUNK_API FThunkResult GenerateSimpleShift(void* ToFn, void* BindParam, FuncArgInfo& Src, FuncArgInfo& Dest, bool bLogAssembly);
THUNK_API FThunkResult GenerateShiftWithRegisterContext(void* ToFn, void* BindParam, FuncArgInfo& Src, FuncArgInfo& Dest, bool bLogAssembly);
#endif

}
