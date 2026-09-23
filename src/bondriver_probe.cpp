#include "bon_abi.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 5) {
        std::wcerr << L"usage: bondriver_probe path-to-DLL [physical-channel] [seconds] [output.ts]\n";
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
    const DWORD channel = argc >= 3 ? std::wcstoul(argv[2], nullptr, 10) - 13 : 0;
    if (!bon2->SetChannel(0, channel)) {
        std::wcerr << L"SetChannel failed\n";
        bon->Release();
        return 1;
    }
    if (bon->WaitTsStream(15'000) != WAIT_OBJECT_0) {
        std::wcerr << L"WaitTsStream failed; signal="
                   << bon->GetSignalLevel() << L" ready="
                   << bon->GetReadyCount() << L'\n';
        bon->Release();
        return 1;
    }
    const DWORD seconds = argc >= 4 ? std::wcstoul(argv[3], nullptr, 10) : 0;
    std::ofstream output;
    if (argc == 5) {
        output.open(std::filesystem::path(argv[4]), std::ios::binary);
        if (!output) { bon->Release(); FreeLibrary(module); return 1; }
    }
    const auto deadline = GetTickCount64() + seconds * 1000;
    std::size_t packets = 0;
    std::set<std::string> unique;
    bool okay = true;
    do {
        BYTE* ts = nullptr;
        DWORD size = 0, remain = 0;
        okay = bon->GetTsStream(&ts, &size, &remain) && size % 188 == 0;
        if (okay && size && output)
            output.write(reinterpret_cast<const char*>(ts), size);
        for (DWORD pos = 0; okay && pos < size; pos += 188) {
            okay = ts && ts[pos] == 0x47;
            if (okay) {
                ++packets;
                const unsigned pid = ((ts[pos + 1] & 0x1f) << 8) | ts[pos + 2];
                if (pid != 0x1fff) unique.emplace(reinterpret_cast<const char*>(ts + pos), 188);
            }
        }
        if (seconds && GetTickCount64() < deadline) bon->WaitTsStream(1000);
    } while (okay && seconds && GetTickCount64() < deadline);
    std::cout << "packets=" << packets << " unique_non_null=" << unique.size()
              << " sync_ok=" << okay << '\n';
    bon->Release();
    FreeLibrary(module);
    return okay ? 0 : 1;
}
