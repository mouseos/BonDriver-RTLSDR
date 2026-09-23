#pragma once

#include <windows.h>

class IBonDriver {
public:
    virtual const BOOL OpenTuner() = 0;
    virtual void CloseTuner() = 0;
    virtual const BOOL SetChannel(BYTE channel) = 0;
    virtual const float GetSignalLevel() = 0;
    virtual const DWORD WaitTsStream(DWORD timeout = 0) = 0;
    virtual const DWORD GetReadyCount() = 0;
    virtual const BOOL GetTsStream(BYTE* dst, DWORD* size, DWORD* remain) = 0;
    virtual const BOOL GetTsStream(BYTE** dst, DWORD* size, DWORD* remain) = 0;
    virtual void PurgeTsStream() = 0;
    virtual void Release() = 0;
};

class IBonDriver2 : public IBonDriver {
public:
    virtual LPCTSTR GetTunerName() = 0;
    virtual const BOOL IsTunerOpening() = 0;
    virtual LPCTSTR EnumTuningSpace(DWORD space) = 0;
    virtual LPCTSTR EnumChannelName(DWORD space, DWORD channel) = 0;
    virtual const BOOL SetChannel(DWORD space, DWORD channel) = 0;
    virtual const DWORD GetCurSpace() = 0;
    virtual const DWORD GetCurChannel() = 0;
};

struct BonStruct {
    void* context;
    const void* end;
    BOOL (*open)(void*);
    void (*close)(void*);
    BOOL (*set_channel_legacy)(void*, BYTE);
    float (*signal_level)(void*);
    DWORD (*wait_ts)(void*, DWORD);
    DWORD (*ready_count)(void*);
    BOOL (*get_ts_copy)(void*, BYTE*, DWORD*, DWORD*);
    BOOL (*get_ts_pointer)(void*, BYTE**, DWORD*, DWORD*);
    void (*purge)(void*);
    void (*release)(void*);
    LPCTSTR (*tuner_name)(void*);
    BOOL (*is_open)(void*);
    LPCTSTR (*tuning_space)(void*, DWORD);
    LPCTSTR (*channel_name)(void*, DWORD, DWORD);
    BOOL (*set_channel)(void*, DWORD, DWORD);
    DWORD (*current_space)(void*);
    DWORD (*current_channel)(void*);
};
