/** @file Tests.hpp
 *  @brief Test-only helpers exposed when the unit-test target is enabled.
 */

#pragma once

#include "BindingThunk.hpp"
#include "RestoreThunk.hpp"

namespace BindingThunk {

/** @brief Reinterprets a thunk pointer as a callable function pointer for tests.
 *  @tparam FnType Target function pointer type.
 *  @param Thunk Thunk owning pointer returned by the runtime.
 *  @return Reinterpreted callable entry point.
 */
template<typename FnType>
FnType ThunkCast(const FThunkPtr& Thunk) {
    return reinterpret_cast<FnType>(Thunk.get());
}

#ifdef THUNK_ENABLE_TEST_HOOKS
/** @brief Test hook that exposes the shifted invocation signature used for plain binding thunks. */
THUNK_API FuncSignature ShiftSignature(const FuncSignature& InSignature);
/** @brief Test hook that exposes the simple register-shift code path directly. */
THUNK_API FThunkResult GenerateSimpleShift(void* ToFn, void* BindParam, FuncArgInfo& Src, FuncArgInfo& Dest, bool bLogAssembly);
/** @brief Test hook that exposes the register-context binding code path directly. */
THUNK_API FThunkResult GenerateShiftWithRegisterContext(void* ToFn, void* BindParam, FuncArgInfo& Src, FuncArgInfo& Dest, bool bLogAssembly);
#endif

}
