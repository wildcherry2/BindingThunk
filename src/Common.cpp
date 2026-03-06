#include "Common.hpp"

#include <ranges>
#include <string_view>
#include <iostream>
#include <format>

auto GetJitRuntime()-> JitRuntime&
{
    static JitRuntime JitRuntime{};
    return JitRuntime;
}

class UE4SSJitErrorHandler : public ErrorHandler
{
    public: ~UE4SSJitErrorHandler() noexcept override = default;

    void handle_error(asmjit::Error err, const char *message, asmjit::BaseEmitter *origin) override {
        std::cerr << std::format("AsmJit error {}: {}", static_cast<uint32_t>(err), message) << std::endl;
    }
};

class UE4SSJitLogger : public asmjit::Logger
{
    public: ~UE4SSJitLogger() noexcept override = default;

    asmjit::Error _log(const char* data, size_t size) noexcept override
    {
        std::string_view AsStringView{ data, size };
        std::cout << AsStringView;
        std::cout.flush();
        return asmjit::kErrorOk;
    }
};

auto GetErrorHandler()-> ErrorHandler* {
    static UE4SSJitErrorHandler JitErrorHandler{};
    return &JitErrorHandler;
}

auto GetLogger()-> Logger* {
    static UE4SSJitLogger JitLogger{};
    return &JitLogger;
}

void FThunkDeleter::operator()(void* Thunk) const noexcept
{
    return (void)GetJitRuntime().release(Thunk);
}

FuncArgInfo::FuncArgInfo(const FuncSignature& Signature)
{
    _Detail.init(Signature, GetJitRuntime().environment());
    _GpRegMask = _Detail.used_regs(asmjit::RegGroup::kGp);
    _VecRegMask = _Detail.used_regs(asmjit::RegGroup::kVec);
    _Signature = Signature;
}

const std::vector<asmjit::FuncValue>& FuncArgInfo::GetArguments() {
    if (_ArgRegs) return *_ArgRegs;
    _ArgRegs = std::vector<asmjit::FuncValue>{};
    for (uint32_t ArgPackIndex = 0; ArgPackIndex < asmjit::Globals::kMaxFuncArgs; ++ArgPackIndex) {
        const auto& Pack = Detail().arg_packs()[ArgPackIndex];
        if (Pack.count() == 0) break;
        for (uint32_t PackIndex = 0; PackIndex < Pack.count(); ++PackIndex) {
            const auto& SrcArg = Pack[PackIndex];
            if (SrcArg.is_assigned()) {
                _ArgRegs->push_back(SrcArg);
            }
        }
    }
    return *_ArgRegs;
}

const std::vector<asmjit::FuncValue>& FuncArgInfo::GetReturnValues() {
    if (_RetRegs) return *_RetRegs;
    _RetRegs = std::vector<asmjit::FuncValue>{};
    for (uint32_t PackIndex = 0; PackIndex < Detail().ret_pack().count(); ++PackIndex) {
        const auto& DestArg = Detail().ret_pack()[PackIndex];
        if (DestArg.is_assigned()) {
            _RetRegs->push_back(DestArg);
        }
    }

    return *_RetRegs;
}

asmjit::RegMask FuncArgInfo::GpRegMask() const { return _GpRegMask; }
asmjit::RegMask FuncArgInfo::VecRegMask() const { return _VecRegMask; }
const asmjit::FuncSignature& FuncArgInfo::Signature() const{ return _Signature; }
const asmjit::FuncDetail& FuncArgInfo::Detail() const { return _Detail; }