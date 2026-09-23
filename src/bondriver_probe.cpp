#include "bon_abi.h"

#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3) {
        std::wcerr << L"usage: bondriver_probe path-to-DLL [physical-channel]\n";
        return 2;
    }
    HMODULE module = LoadLibraryW(argv[1]);
    if (!module) {
        std::wcerr << L"LoadLibraryW failed: " << GetLastError() << L'\n';
        return 1;
    }
    using Create = IBonDriver* (*)();
    auto create = reinterpret_cast<Create>(GetProcAddress(module, "CreateBonDriver"));
    if (!create) {
        std::wcerr << L"CreateBonDriver export missing\n";
        return 1;
    }
    IBonDriver* bon = create();
    IBonDriver2* bon2 = dynamic_cast<IBonDriver2*>(bon);
    if (!bon2 || !bon2->EnumTuningSpace(0) || !bon2->EnumChannelName(0, 0)) {
        std::wcerr << L"IBonDriver2 RTTI or channel enumeration failed\n";
        bon->Release();
        return 1;
    }
    if (!bon->OpenTuner()) {
        std::wcerr << L"OpenTuner failed\n";
        bon->Release();
        return 1;
    }
    const DWORD channel = argc == 3 ? std::wcstoul(argv[2], nullptr, 10) - 13 : 0;
    if (!bon2->SetChannel(0, channel)) {
        std::wcerr << L"SetChannel failed\n";
        bon->Release();
        return 1;
    }
    if (bon->WaitTsStream(15'000) != WAIT_OBJECT_0) {
        std::wcerr << L"WaitTsStream failed\n";
        bon->Release();
        return 1;
    }
    BYTE* ts = nullptr;
    DWORD size = 0;
    DWORD remain = 0;
    bool okay = bon->GetTsStream(&ts, &size, &remain) &&
                ts && size && size % 188 == 0;
    for (DWORD pos = 0; okay && pos < size; pos += 188) okay = ts[pos] == 0x47;
    std::cout << "size=" << size << " packets=" << size / 188
              << " sync_ok=" << okay << '\n';
    bon->Release();
    FreeLibrary(module);
    return okay ? 0 : 1;
}
