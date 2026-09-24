# BonDriver-RTLSDR

RTL-SDR（RTL2832U + FC0013）を使い、TVTestで地上デジタル放送のワンセグを視聴するためのWindows x64用BonDriverです。LT-DT306で動作を確認しています。アンテナとlibusb対応のUSBドライバーが必要です。GNU Radioや純正Windowsアプリは実行時に使いません。

USBから取得したI/Qを別プロセスの`rtl_oneseg_helper.exe`で復調し、[共通コア](https://github.com/mouseos/rtl-sdr-oneseg-core)から188バイトのMPEG-TSを出力します。USB処理を分離して、異常がTVTest本体に波及するのを抑えています。

## 対応環境

- Windows x64版TVTest（TVTest 0.10.0で確認）。32ビット版には対応しません。
- RTL2832U + FC0013のUSBチューナー。動作確認機はLT-DT306です。
- チューナーに対応するlibusb系ドライバーと受信用アンテナ。
- `VCRUNTIME140.dll`が見つからない場合は、Microsoft Visual C++再頒布可能パッケージのx64版が必要です。
- 放送条件はISDB-T Mode 3、ガードインターバル1/8、QPSK、符号化率2/3、時間インターリーブI=4の中央1セグに対応します。その他の条件では受信できません。

## ReleaseのZIPから使う

1. [Releases](https://github.com/mouseos/BonDriver-RTLSDR/releases)からWindows x64用ZIPを取得し、展開します。
2. 次の4ファイルを**同じフォルダー**に置きます。TVTestの構成に合わせ、`TVTest.exe`があるフォルダー、またはBonDriverを置くフォルダーを使ってください。`BonDriver_RTLSDR.dll`と`rtl_oneseg_helper.exe`は必ず同じフォルダーに置きます。

   ```text
   BonDriver_RTLSDR.dll
   rtl_oneseg_helper.exe
   rtlsdr.dll
   libusb-1.0.dll
   ```

3. ZIP内の`BonDriver_RTLSDR.ini.example`を同じフォルダーにコピーし、名前を`BonDriver_RTLSDR.ini`に変更します。`[Source]`の`Mode=Live`、`RtlSdrLibrary=rtlsdr.dll`を確認します。依存DLLを別の場所に置く場合は、`RtlSdrLibrary`を**Windows形式の絶対パス**にしてください。
4. チューナーとアンテナを接続し、TVTestで`BonDriver_RTLSDR.dll`を選択します。例: `TVTest.exe /d BonDriver_RTLSDR.dll`。
5. TVTestのチャンネルスキャンでは、**「信号レベルを無視する」**を有効にします。選局後、復調開始まで数秒かかるため、スキャンの待ち時間を短くしすぎないでください。スキャン後はワンセグのサービスを選びます。SDTにフルセグのサービス名が現れても、このドライバーが出すのは中央1セグのTSです。

`GetSignalLevel()`は復調後の同期相関から計算した便宜的な品質指標です。表示単位がdBでも、校正済みのRF C/Nや受信電力ではありません。選局直後は0になります。

## 設定と制約

- `Mode=Live`が通常の実機受信です。`Mode=Replay`は録画済み中間データの開発用再生で、`ViterbiFile`と`PhysicalChannel`の指定が必要です。通常の視聴では変更しません。
- 初期のチューナー利得は`GainTenthsDb=58`（5.8 dB）。物理ch13・15には7.1 dBとFC0013のIF利得レジスター`0x0f`を使い、その他はINIの利得と`0x0a`を使います。動作確認機の設定であり、受信条件によって調整が必要です。
- チューナーは放送中心より600 kHz低く同調し、RTL2832U内のIF補正を適用します。この処理に必要な機能を持たない通常版`rtlsdr.dll`では起動しません。Release ZIP内の修正版を使ってください。
- USB受信と復調は約2.06秒の重複窓で処理します。共有コアはRS誤り訂正と、硬判定で復号できないときの限定的な軟判定を行い、部分受信TSにPATが無いときは補完します。選局直後の待ち時間とCPU負荷はハードウェアに依存します。
- ユーザーのTVTest実視聴で全局の良好受信が報告されています。長時間の無ドロップ性能はまだ定量測定していません。

## ソースからビルドする

Visual StudioのMSVC x64、CMake、Ninjaを使用します。TVTestはMSVCのC++ RTTIでBonDriverを判定するため、BonDriver DLLをMinGWでビルドするとクラッシュすることがあります。共通コアのチェックアウトを`ONESEG_CORE_DIR`に指定します。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DONESEG_CORE_DIR=C:\src\rtl-sdr-oneseg-core
cmake --build build
```

加えて、[osmocom rtl-sdr](https://github.com/osmocom/rtl-sdr)のコミット`797f8143266d983c56d8f35d2d442527529dd8a5`に[FC0013/ISDB-T用パッチ](patches/rtl-sdr-fc0013-isdb.patch)を適用し、MSVC x64とlibusbで`rtlsdr.dll`をビルドします。`libusb-1.0.dll`も必要です。パッチはRTL2832U初期化、デジタルIF補正、FC0013 IF利得設定のエクスポートを追加します。

## ライセンス・参照

修正版rtl-sdrは上流のGPL-2.0に従います。配布ZIPにはrtl-sdrとlibusbのライセンス文書を同梱します。共通コアとこのBonDriverのソースは上記リポジトリとReleaseのソース一式を参照してください。

- [BonDriver ABIの参照実装](https://github.com/xtne6f/TvtPlay/tree/work/BonDriver_Pipe_src)
- [TVTestのドキュメント](https://github.com/DBCTRADO/TVTest/blob/develop/doc/TVTest.txt)
