#include "bon_abi.h"

#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 6) {
        std::wcerr << L"usage: bondriver_scan_probe DLL [dwell-ms] [passes] [first-physical] [count]\n";
        return 2;
    }
    const DWORD dwell = argc >= 3 ? std::wcstoul(argv[2], nullptr, 10) : 100;
    const DWORD passes = argc >= 4 ? std::wcstoul(argv[3], nullptr, 10) : 1;
    const DWORD first = argc >= 5 ? std::wcstoul(argv[4], nullptr, 10) : 13;
    const DWORD count = argc >= 6 ? std::wcstoul(argv[5], nullptr, 10) : 40;
    if (first < 13 || first > 52 || !count || first + count > 53) return 2;
    HMODULE module = LoadLibraryW(argv[1]);
    if (!module) return 1;
    using Create = IBonDriver* (*)();
    auto create = reinterpret_cast<Create>(GetProcAddress(module, "CreateBonDriver"));
    if (!create) return 1;
    IBonDriver* base = create();
    IBonDriver2* driver = dynamic_cast<IBonDriver2*>(base);
    if (!driver || !base->OpenTuner()) return 1;
    for (DWORD pass = 0; pass < passes; ++pass) {
        for (DWORD channel = first - 13; channel < first - 13 + count; ++channel) {
            if (!driver->EnumChannelName(0, channel) ||
                !driver->SetChannel(0, channel)) return 1;
            base->WaitTsStream(dwell);
            BYTE* ts = nullptr;
            DWORD size = 0, remain = 0;
            if (!base->GetTsStream(&ts, &size, &remain)) return 1;
            std::cout << "pass=" << pass << " channel=" << channel + 13
                      << " size=" << size << " signal="
                      << base->GetSignalLevel() << std::endl;
        }
    }
    base->Release();
    FreeLibrary(module);
    return 0;
}
