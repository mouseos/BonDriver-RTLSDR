# BonDriver for RTL-SDR One-seg

Windows x64 BonDriver2 DLL for RTL-SDR / LT-DT306 live ISDB-T one-seg reception in TVTest. The driver starts `rtl_oneseg_helper.exe` for the selected channel. That process loads a 64-bit `librtlsdr` DLL, captures unsigned 8-bit I/Q, and calls the portable [RTL-SDR one-seg core](https://github.com/mouseos/rtl-sdr-oneseg-core) to produce 188-byte MPEG-TS packets. The BonDriver receives TS over a Windows pipe. It has no GNU Radio runtime dependency.

The current demodulator supports Mode 3, guard interval 1/8, QPSK, code rate 2/3 and time interleave I=4. Other transmission modes are not yet supported. It continuously acquires USB I/Q, decodes overlapping 2.06-second windows on another thread, and aligns shared TS packets to avoid duplication. The shared core now corrects up to eight byte errors per RS packet; uncorrectable packets are discarded. Service/channel scan may require more time than a hardware tuner; TVTest video continuity with this corrected build is not yet established.

The x64 MSVC BonDriver ABI was confirmed in TVTest 0.10.0. A TVTest manual viewing test of the earlier syndrome-only build showed severe video corruption and a high drop counter. Two separate 20-second physical channel 25 probe runs showed 359 PID continuity breaks without RS correction and zero with RS correction. The user then reported stable TVTest reception, but the in-process `librtlsdr` build crashed during channel scanning. A debugger located the access violation inside `librtlsdr` during repeated retuning. The USB and demodulation work now runs in a separate helper process. The isolated build completed a 40-channel, five-seconds-per-channel probe without crashing; channels 14 and 25 yielded TS in that run. A separate ten-second channel 25 capture yielded 1,867 sync-correct packets and zero PID continuity breaks. Manual TVTest scanning with this isolated build remains to be checked.

`GetSignalLevel()` reports a non-calibrated dB-like quality estimate derived from cyclic-prefix correlation after TS lock. It is not a measured RF C/N or signal power.

## Build

Clone this repository next to `rtl-sdr-oneseg-core`, or pass its path explicitly. **Build with MSVC x64.** TVTest performs an MSVC `dynamic_cast` on the BonDriver object, so a MinGW C++ DLL can crash TVTest even when a simple exported-function probe succeeds.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DONESEG_CORE_DIR=D:\path\to\rtl-sdr-oneseg-core
cmake --build build
```

Use a 64-bit compiler for 64-bit TVTest. Copy **both** `BonDriver_RTLSDR_OneSeg.dll` and `rtl_oneseg_helper.exe` next to `TVTest.exe`. Copy `BonDriver_RTLSDR_OneSeg.ini.example` to `BonDriver_RTLSDR_OneSeg.ini` and set `RtlSdrLibrary` to the full Windows path of the installed 64-bit librtlsdr DLL. The LT-DT306 must use a compatible libusb driver and be connected to an antenna. The default `Mode=Live` uses the isolated helper; each channel change stops the old helper and starts a new one.

The default tuner gain is 5.8 dB (`GainTenthsDb=58`). On the tested FC0013, a 19.7 dB setting decoded only one strong physical channel, while 5.8 dB decoded five channels from the same antenna. This value is an empirical starting point; reception varies with the RF path.

Start TVTest with `/d BonDriver_RTLSDR_OneSeg.dll`. The DLL reports one tuning space with UHF physical channels 13 through 52. For debugging a recorded capture, explicitly set `Mode=Replay`, `ViterbiFile`, and `PhysicalChannel` in the INI; live mode is the default and never falls back silently to replay.

BonDriver ABI was checked against the [TvtPlay BonDriver_Pipe source headers](https://github.com/xtne6f/TvtPlay/tree/work/BonDriver_Pipe_src). TVTest invocation follows [TVTest's documentation](https://github.com/DBCTRADO/TVTest/blob/develop/doc/TVTest.txt).
