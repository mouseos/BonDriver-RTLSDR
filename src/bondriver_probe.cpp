#include "bon_abi.h"

#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::wcerr << L"usage: bondriver_probe path-to-DLL\n";
        return 2;
    }
    HMODULE module = LoadLibraryW(argv[1]);
    if (!module) {
        std::wcerr << L"LoadLibraryW failed: " << GetLastError() << L'\n';
        return 1;
    }
    using Create = const BonStruct* (*)();
    auto create = reinterpret_cast<Create>(GetProcAddress(module, "CreateBonStruct"));
    if (!create) {
        std::wcerr << L"CreateBonStruct export missing\n";
        return 1;
    }
    const BonStruct* bon = create();
    void* context = bon->context;
    if (!bon->open(context)) {
        std::wcerr << L"OpenTuner failed\n";
        bon->release(context);
        return 1;
    }
    if (!bon->set_channel(context, 0, 0)) {
        std::wcerr << L"SetChannel failed\n";
        bon->release(context);
        return 1;
    }
    if (bon->wait_ts(context, 1000) != WAIT_OBJECT_0) {
        std::wcerr << L"WaitTsStream failed\n";
        bon->release(context);
        return 1;
    }
    BYTE* ts = nullptr;
    DWORD size = 0;
    DWORD remain = 0;
    bool okay = bon->get_ts_pointer(context, &ts, &size, &remain) &&
                ts && size && size % 188 == 0;
    for (DWORD pos = 0; okay && pos < size; pos += 188) okay = ts[pos] == 0x47;
    std::cout << "size=" << size << " packets=" << size / 188
              << " sync_ok=" << okay << '\n';
    bon->release(context);
    FreeLibrary(module);
    return okay ? 0 : 1;
}
