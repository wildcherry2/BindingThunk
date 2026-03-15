#include "Context.hpp"

#include <exception>
#include <iostream>

namespace RC::Thunk {

thread_local RegisterContext* RegisterContextArray[256];
thread_local int RegisterContextStackIndex = -1;
using RegisterContextStackFatalHandler = void(*)(const char* Message);

static void DefaultRegisterContextStackFatalHandler(const char* Message) {
    std::cerr << Message << std::endl;
    std::terminate();
}

static RegisterContextStackFatalHandler GRegisterContextStackFatalHandler = &DefaultRegisterContextStackFatalHandler;

[[noreturn]] static void HandleRegisterContextStackFatal(const char* Message) {
    GRegisterContextStackFatalHandler(Message);
    std::terminate();
}

void RegisterContextStack::Push(RegisterContext* Context) {
    if (RegisterContextStackIndex == 255) {
        HandleRegisterContextStackFatal("RegisterContextStack: Stack overflow");
    }
    RegisterContextArray[++RegisterContextStackIndex] = Context;
}

void RegisterContextStack::Pop() {
    if (RegisterContextStackIndex == -1) {
        HandleRegisterContextStackFatal("RegisterContextStack: Stack underflow");
    }
    --RegisterContextStackIndex;
}

RegisterContext* RegisterContextStack::Top() {
    if (RegisterContextStackIndex == -1) {
        HandleRegisterContextStackFatal("RegisterContextStack: Stack empty");
    }
    return RegisterContextArray[RegisterContextStackIndex];
}

#ifdef THUNK_ENABLE_TEST_HOOKS
void SetRegisterContextStackFatalHandler(FRegisterContextStackFatalHandler Handler) {
    GRegisterContextStackFatalHandler = Handler ? Handler : &DefaultRegisterContextStackFatalHandler;
}

void ResetRegisterContextStackFatalHandler() {
    GRegisterContextStackFatalHandler = &DefaultRegisterContextStackFatalHandler;
}
#endif

}
