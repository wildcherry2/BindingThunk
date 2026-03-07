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

const std::vector<asmjit::FuncValue>& FuncArgInfo::GetArguments() noexcept {
    if (_ArgVals) return *_ArgVals;
    _ArgVals = std::vector<asmjit::FuncValue>{};
    for (uint32_t ArgPackIndex = 0; ArgPackIndex < asmjit::Globals::kMaxFuncArgs; ++ArgPackIndex) {
        const auto& Pack = Detail().arg_packs()[ArgPackIndex];
        if (Pack.count() == 0) break;
        for (uint32_t PackIndex = 0; PackIndex < Pack.count(); ++PackIndex) {
            const auto& SrcArg = Pack[PackIndex];
            if (SrcArg.is_assigned()) {
                _ArgVals->push_back(SrcArg);
            }
        }
    }
    return *_ArgVals;
}

const std::vector<asmjit::FuncValue>& FuncArgInfo::GetReturnValues() noexcept {
    if (_RetVals) return *_RetVals;
    _RetVals = std::vector<asmjit::FuncValue>{};
    for (uint32_t PackIndex = 0; PackIndex < Detail().ret_pack().count(); ++PackIndex) {
        const auto& DestArg = Detail().ret_pack()[PackIndex];
        if (DestArg.is_assigned()) {
            _RetVals->push_back(DestArg);
        }
    }
    return *_RetVals;
}

asmjit::RegMask FuncArgInfo::GpRegMask() const noexcept { return _GpRegMask; }
asmjit::RegMask FuncArgInfo::VecRegMask() const noexcept { return _VecRegMask; }

bool FuncArgInfo::IsArgumentRegister(const Gp& Reg) const noexcept {
    return (1 << Reg.id()) & _GpRegMask;
}

bool FuncArgInfo::IsArgumentRegister(const Vec& Reg) const noexcept {
    return (1 << Reg.id()) & _VecRegMask;
}

bool FuncArgInfo::IsArgumentRegister(const asmjit::Operand& Reg) const noexcept {
    if (Reg.is_gp()) return IsArgumentRegister(Reg.as<Gp>());
    if (Reg.is_vec()) return IsArgumentRegister(Reg.as<Vec>());
    return false;
}

const std::vector<Gp> & FuncArgInfo::GetArgumentIntegralRegisters() noexcept {
    if (_IntArgRegs) return *_IntArgRegs;
    _IntArgRegs = std::vector<Gp>{};
    for (int I = 0; I < 32; ++I) {
        if ((1 << I) & _GpRegMask) _IntArgRegs->push_back(asmjit::x86::gpq(I));
    }
    return *_IntArgRegs;
}

const std::vector<Vec> & FuncArgInfo::GetArgumentFloatingRegisters() noexcept {
    if (_VecArgRegs) return *_VecArgRegs;
    _VecArgRegs = std::vector<Vec>{};
    for (int I = 0; I < 32; ++I) {
        if ((1 << I) & _VecRegMask) _VecArgRegs->push_back(asmjit::x86::xmm(I));
    }
    return *_VecArgRegs;
}

const asmjit::FuncSignature& FuncArgInfo::Signature() const noexcept { return _Signature; }
const asmjit::FuncDetail& FuncArgInfo::Detail() const noexcept { return _Detail; }

inline const asmjit::CallConv& GetCallingConvention() {
    static asmjit::CallConv CallConv = [] {
        asmjit::CallConv Convention{};
        Convention.init(asmjit::CallConvId::kCDecl, GetJitRuntime().environment());
        return Convention;
    }();

    return CallConv;
}

const std::vector<Gp>& GetPlatformNonVolatileGpRegs() {
    static std::vector<Gp> Regs = [] {
        const auto& Conv = GetCallingConvention();
        std::vector<Gp> Regs {};
        auto Preserved = Conv.preserved_regs(asmjit::RegGroup::kGp);
        for (int I = 0; I < 32; ++I) {
            if ((1 << I) & Preserved) Regs.push_back(asmjit::x86::gpq(I));
        }
        return Regs;
    }();

    return Regs;
}

const std::vector<Vec>& GetPlatformNonVolatileVecRegs() {
    static std::vector<Vec> Regs = [] {
        const auto& Conv = GetCallingConvention();
        std::vector<Vec> Regs {};
        auto Preserved = Conv.preserved_regs(asmjit::RegGroup::kVec);
        for (int I = 0; I < 32; ++I) {
            if ((1 << I) & Preserved) Regs.push_back(asmjit::x86::xmm(I));
        }
        return Regs;
    }();

    return Regs;
}

Gp GetPlatformGpScratchReg() {
    static Gp Reg = [] {
        const auto& Conv = GetCallingConvention();
        auto RegsMask = ~(Conv.preserved_regs(asmjit::RegGroup::kGp) | Conv.passed_regs(asmjit::RegGroup::kGp));
        for (int I = 0; I < 32; ++I) {
            if (((1 << I) & RegsMask) && I != Gp::kIdAx) return asmjit::x86::gpq(I);
        }
        throw std::runtime_error("Failed to find Gp scratch register for platform!");
    }();

    return Reg;
}

Vec GetPlatformXmmScratchReg() {
    static Vec Reg = [] {
        const auto& Conv = GetCallingConvention();
        auto RegsMask = ~(Conv.preserved_regs(asmjit::RegGroup::kVec) | Conv.passed_regs(asmjit::RegGroup::kVec));
        for (int I = 0; I < 32; ++I) {
            if ((1 << I) & RegsMask) return asmjit::x86::xmm(I);
        }
        throw std::runtime_error("Failed to find Vec scratch register for platform!");
    }();

    return Reg;
}