/** @file Context.cpp
 *  @brief Implements thread-local register-context stack utilities used by register thunk modes.
 */

#include "Context.hpp"

#include <exception>
#include <iostream>

namespace BindingThunk {

/** @brief Per-thread storage for captured register contexts. */
thread_local RegisterContext* RegisterContextArray[256];
/** @brief Top index into @ref RegisterContextArray, or @c -1 when the stack is empty. */
thread_local int RegisterContextStackIndex = -1;
/** @brief Fatal handler signature used internally by the register-context stack. */
using RegisterContextStackFatalHandler = void(*)(const char* Message);

/** @brief Default fatal handler for register-context stack misuse outside tests. */
static void DefaultRegisterContextStackFatalHandler(const char* Message) {
    std::cerr << Message << std::endl;
    std::terminate();
}

/** @brief Active fatal handler for register-context stack misuse. */
static RegisterContextStackFatalHandler GRegisterContextStackFatalHandler = &DefaultRegisterContextStackFatalHandler;

/** @brief Reports a register-context stack fatal condition and terminates execution. */
[[noreturn]] static void HandleRegisterContextStackFatal(const char* Message) {
    GRegisterContextStackFatalHandler(Message);
    std::terminate();
}

/** @copydoc RegisterContextStack::Push */
void RegisterContextStack::Push(RegisterContext* Context) {
    if (RegisterContextStackIndex == 255) {
        HandleRegisterContextStackFatal("RegisterContextStack: Stack overflow");
    }
    RegisterContextArray[++RegisterContextStackIndex] = Context;
}

/** @copydoc RegisterContextStack::Pop */
void RegisterContextStack::Pop() {
    if (RegisterContextStackIndex == -1) {
        HandleRegisterContextStackFatal("RegisterContextStack: Stack underflow");
    }
    --RegisterContextStackIndex;
}

/** @copydoc RegisterContextStack::Top */
RegisterContext* RegisterContextStack::Top() {
    if (RegisterContextStackIndex == -1) {
        HandleRegisterContextStackFatal("RegisterContextStack: Stack empty");
    }
    return RegisterContextArray[RegisterContextStackIndex];
}

#ifdef THUNK_ENABLE_TEST_HOOKS
/** @copydoc SetRegisterContextStackFatalHandler */
void SetRegisterContextStackFatalHandler(FRegisterContextStackFatalHandler Handler) {
    GRegisterContextStackFatalHandler = Handler ? Handler : &DefaultRegisterContextStackFatalHandler;
}

/** @copydoc ResetRegisterContextStackFatalHandler */
void ResetRegisterContextStackFatalHandler() {
    GRegisterContextStackFatalHandler = &DefaultRegisterContextStackFatalHandler;
}
#endif

}
