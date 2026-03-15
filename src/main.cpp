#include <iostream>
#include <ostream>
#include <format>

#include "BindingThunk.hpp"
#include "RestoreThunk.hpp"

using namespace RC::Thunk;

FThunkPtr complexrestore{};
static double testcomplexfn(void* bound, double p0, int p1, float p2, float p3, int64_t p4, int64_t p5, double p6, double p7) {
    std::cout << std::format("bound:{}\np0:{}\np1:{}\np2:{}\np3:{}\np4:{}\np5:{}\np6:{}\np7:{}\n\n",
        bound, p0, p1, p2, p3, p4, p5, p6, p7) << std::endl;

    //return p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7 + reinterpret_cast<uint64_t>(bound);
    return reinterpret_cast<double(*)(double, int, float, float, int64_t, int64_t, double, double)>(complexrestore.get())(p0, p1, p2, p3, p4, p5, p6, p7);
};
static double testcomplexfnoriginal(double p0, int p1, float p2, float p3, int64_t p4, int64_t p5, double p6, double p7) {
    std::cout << std::format("unbound p0:{}\np1:{}\np2:{}\np3:{}\np4:{}\np5:{}\np6:{}\np7:{}\n",
        p0, p1, p2, p3, p4, p5, p6, p7) << std::endl;

    return p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7;
}

int main() {
    void* binder = (void*)0x42;
    auto bindResult = GenerateBindingThunk(testcomplexfn, binder, EBindingThunkType::Register);
    if (!bindResult) {
        std::cerr << bindResult.error().Message << std::endl;
        return 1;
    }

    auto restoreResult = GenerateRestoreThunk(testcomplexfnoriginal, EBindingThunkType::Register);
    if (!restoreResult) {
        std::cerr << restoreResult.error().Message << std::endl;
        return 1;
    }

    auto bind = std::move(bindResult.value());
    complexrestore = std::move(restoreResult.value());
    auto ret = reinterpret_cast<double(*)(double, int, float, float, int64_t, int64_t, double, double)>(bind.get())(1.0, 2, 3.0, 4.0, 5, 6, 7.0, 8.0);
    std::cout << ret << std::endl;
    complexrestore.reset();

    return 0;
}

//todo explicitly block thunks that don't tail call and return a value > 64 bits or a value < 64 bits that isn't pod
//todo check if return values that are on the stack are calculated by asmjit
