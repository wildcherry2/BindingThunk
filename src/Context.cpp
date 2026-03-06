#include "Context.hpp"

#include <stdexcept>

thread_local RegisterContext* RegisterContextArray[256];
thread_local int RegisterContextStackIndex = -1;

void RegisterContextStack::Push(RegisterContext* Context) {
    if (RegisterContextStackIndex == 255) throw std::runtime_error("RegisterContextStack: Stack overflow");
    RegisterContextArray[++RegisterContextStackIndex] = Context;
}

void RegisterContextStack::Pop() {
    if (RegisterContextStackIndex == -1) throw std::runtime_error("RegisterContextStack: Stack underflow");
    --RegisterContextStackIndex;
}

RegisterContext* RegisterContextStack::Top() {
    if (RegisterContextStackIndex == -1) return nullptr;
    return RegisterContextArray[RegisterContextStackIndex];
}
