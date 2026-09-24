# BonDriver for RTL-SDR One-seg

Windows x64 BonDriver2 DLL for RTL-SDR / LT-DT306 live ISDB-T one-seg reception in TVTest. The driver starts `rtl_oneseg_helper.exe` for the selected channel. That process loads a 64-bit `librtlsdr` DLL, captures unsigned 8-bit I/Q, and calls the portable [RTL-SDR one-seg core](https://github.com/mouseos/rtl-sdr-oneseg-core) to produce 188-byte MPEG-TS packets. The BonDriver receives TS over a Windows pipe. It has no GNU Radio runtime dependency.

The current demodulator supports Mode 3, guard interval 1/8, QPSK, code rate 2/3 and time interleave I=4. Other transmission modes are not yet supported. It continuously acquires USB I/Q, decodes overlapping 2.06-second windows on another thread, and aligns shared TS packets to avoid duplication. The shared core corrects up to eight byte errors per RS packet; uncorrectable packets are discarded. A bounded soft-decision Viterbi fallback handles captures where hard-decision decoding fails. The helper uses the shared `PartialReceptionPat` adapter to add a PAT when the partial-reception stream carries PMT and SDT but no PAT, so TVTest can find the one-seg service. Service/channel scan may require more time than a hardware tuner.

The x64 MSVC BonDriver ABI was confirmed in TVTest 0.10.0. An earlier build had corruption, dropouts and a scan crash; USB and demodulation now run in an isolated helper. A later TVTest scan still missed physical channels 13, 15 and 21. The FC0013 comparison below recovered all seven tested physical channels. On the tested hardware, continuous helper captures of channels 13, 14 and 15 lasted 12 seconds each and channels 19, 21, 23 and 25 lasted 10 seconds each; all seven had zero TS continuity errors and zero repeated TS packets. This is a bounded capture result, not a long-term or TVTest playback guarantee.

`GetSignalLevel()` reports a non-calibrated dB-like quality estimate derived from cyclic-prefix correlation after TS lock. It is not a measured RF C/N or signal power. In TVTest channel-scan settings, enable **Ignore signal level** so a channel is judged by its TS service information: the quality estimate is initially zero until the first decode and can be below a fixed scan threshold even when valid TS is available.

## Build

Clone this repository next to `rtl-sdr-oneseg-core`, or pass its path explicitly. **Build with MSVC x64.** TVTest performs an MSVC `dynamic_cast` on the BonDriver object, so a MinGW C++ DLL can crash TVTest even when a simple exported-function probe succeeds.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DONESEG_CORE_DIR=D:\path\to\rtl-sdr-oneseg-core
cmake --build build
```

Build a 64-bit `rtl-sdr` DLL from osmocom revision `797f8143266d983c56d8f35d2d442527529dd8a5` with [the FC0013 ISDB-T patch](patches/rtl-sdr-fc0013-isdb.patch). From that checkout, apply the patch with `git apply <path-to-this-repository>\patches\rtl-sdr-fc0013-isdb.patch`, then build `rtlsdr.dll` with MSVC x64 and libusb. Place `libusb-1.0.dll` beside `rtlsdr.dll`. The patch changes the RTL2832U initialization, adds the vendor IF correction at page 1 registers 0x16-0x18, and exposes FC0013 register 0x13 tuning. The patched RTL-SDR source retains its upstream GPL license. The BonDriver requires these added exports and fails to open if an ordinary RTL-SDR DLL is selected.

Use a 64-bit compiler for 64-bit TVTest. Copy **both** `BonDriver_RTLSDR_OneSeg.dll` and `rtl_oneseg_helper.exe` next to `TVTest.exe`. Copy `BonDriver_RTLSDR_OneSeg.ini.example` to `BonDriver_RTLSDR_OneSeg.ini` and set `RtlSdrLibrary` to the full Windows path of the patched `rtlsdr.dll`. The LT-DT306 must use a compatible libusb driver and be connected to an antenna. The default `Mode=Live` uses the isolated helper; each channel change stops the old helper and starts a new one.

The default FC0013 tuner gain is 5.8 dB (`GainTenthsDb=58`). Physical channels 13 and 15 use 7.1 dB and FC0013 IF gain register `0x13=0x0f`; the other channels use the INI tuner gain and `0x13=0x0a`. The tuner is set 600 kHz below channel center, and the RTL2832U digital IF correction restores the center before I/Q decimation. These values were measured on one LT-DT306; reception may vary with RF conditions.

Start TVTest with `/d BonDriver_RTLSDR_OneSeg.dll`. The DLL reports one tuning space with UHF physical channels 13 through 52. For debugging a recorded capture, explicitly set `Mode=Replay`, `ViterbiFile`, and `PhysicalChannel` in the INI; live mode is the default and never falls back silently to replay.

BonDriver ABI was checked against the [TvtPlay BonDriver_Pipe source headers](https://github.com/xtne6f/TvtPlay/tree/work/BonDriver_Pipe_src). TVTest invocation follows [TVTest's documentation](https://github.com/DBCTRADO/TVTest/blob/develop/doc/TVTest.txt).
