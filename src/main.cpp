#include <iostream>
#include <ostream>
#include <format>

#include "BindingThunk.hpp"
#include "RestoreThunk.hpp"

static void testsimplefn(void*,double,int){}
static double testcomplexfn(void* bound, double p0, int p1, float p2, float p3, int64_t p4, int64_t p5, double p6, double p7) {
    std::cout << std::format("bound:{}\np0:{}\np1:{}\np2:{}\np3:{}\np4:{}\np5:{}\np6:{}\np7:{}\n",
        bound, p0, p1, p2, p3, p4, p5, p6, p7) << std::endl;

    return p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7 + reinterpret_cast<uint64_t>(bound);
};

int main() {
    void* binder = (void*)0x42;
    try {
        auto bind = GenerateBindingThunk(testcomplexfn, binder, EBindingThunkType::WithRegisterContext);

        //auto bind = GenerateRestoreThunk(testcomplexfn);
        //testcomplexfn((void*)0x42, 1.0, 2, 3.0, false, 123, 456, 5.5, 7.2);
        auto ret = reinterpret_cast<double(*)(double, int, float, float, int64_t, int64_t, double, double)>(bind.get())(1.0, 2, 3.0, 4.0, 5, 6, 7.0, 8.0);
        std::cout << ret << std::endl;
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }

    return 0;
}

//todo explicitly block thunks that don't tail call and return a value > 64 bits or a value < 64 bits that isn't pod
//todo check if return values that are on the stack are calculated by asmjit