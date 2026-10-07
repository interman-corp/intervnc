# WindowsビューアーからMacへの共有機能の調査

調査対象: `ecbab046`。ソースコードの確認結果であり、WindowsとMacの実機間での動作確認は未実施。

利用環境（ユーザー申告）: macOS Tahoe 26.7.1の標準「画面共有」。
SSH/SFTPが利用可能で、ファイル転送は双方向が必要。

## クリップボード

双方向のプレーンテキスト共有は元から実装済み。
今回、macOS標準画面共有向けにWindows限定のSSH経由の共有方式も追加した。
既存のRFB方式はSSH共有が無効なときに使用する。

- `vncviewer/parameters.cxx`: `AcceptClipboard`、`SendClipboard` はともに既定値が `true`。
- `vncviewer/Viewport.cxx`: FLTKのクリップボード変更通知から送信し、受信したテキストを `Fl::copy()` でローカルに格納する。
- `common/rfb/CConnection.cxx`: 拡張クリップボードではUTF-8、非対応サーバーには従来のClientCutTextを使用する。
- `common/rfb/CMsgWriter.cxx`、`CMsgReader.cxx`: 従来方式の文字コードはLatin-1。日本語の共有には対応サーバーとの拡張方式が必要。

### 再現確認手順

1. 接続先のmacOSバージョン、VNCサーバー名・バージョン、ビューアーのバージョンを記録する。
2. オプションの「Accept clipboard from server」「Send clipboard to server」を有効にし、`ViewOnly` を無効にする。表示のみモードではクリップボード共有も抑制される。
3. 接続完了後にWindowsのメモ帳で `Windows-to-Mac-123` をコピーする。ビューアーのリモート画面をクリックしてフォーカスを移し、Mac側テキストエディターの「編集」→「ペースト」で確認する。
4. ビューアーにフォーカスがある状態でMac側の `Mac-to-Windows-456` を「編集」→「コピー」し、Windowsのメモ帳で貼り付ける。
5. 英数字が双方向で成功した後、日本語・改行入りテキストで再確認する。画像やファイルのコピーは本実装の対象外。

Mac側では通常Commandキーを使うため、Ctrl+C/Vの操作差を除外するにはメニューで確認する。
Windowsでコピー後、ビューアーにフォーカスが戻るまで送信が保留される。
ビューアーにフォーカスがない間のサーバー側変更通知・データは取り込まれない。
接続前から保持していたテキストではなく、接続後に新しくコピーして試す。

必要に応じて `-Log "*:stderr:100"` を指定して起動し、
`Sending clipboard data`、`Got clipboard data`、`Got server clipboard capabilities`、
フォーカスに関するログを確認する。送信ログだけではMac側への反映成功を証明できない。
設定だけでなくサーバーのプロトコル対応も必要であり、現時点ではテスト実施者の操作ミスと断定できない。

### macOS標準サーバーの互換性

[別のVNC実装によるmacOS実機調査](https://github.com/sp00nznet/vncfree/blob/main/docs/macos.md#clipboard-why-it-cannot-work-over-vnc)
では、標準のClientCutTextが無視され、ServerCutTextも送信されなかったと報告されている。
これは本リポジトリやTahoe 26.7.1での検証結果ではないが、標準画面共有との
互換性に起因して失敗する可能性を示している。既存の共有設定を有効にするだけで
解決するとは保証できない。VNC認証に成功することと、クリップボード通信に
対応することは別である。

### 追加したSSHクリップボード共有（Windowsのみ）

macOS標準のVNC通信とは別にSSHで接続し、Mac標準の `osascript` から
AppKitの `NSPasteboard` を読み書きする。Macへの追加アプリ・常駐プログラムの
インストールは不要。WinSCPの接続や認証情報は使わない。

#### 準備

1. Windowsのオプション機能 **OpenSSHクライアント** をインストールする。
2. Macのリモートログインを有効にし、画面共有で操作するMacのユーザーに
   SSH鍵認証を設定する。パスフレーズ付き鍵はWindowsの `ssh-agent` に登録する。
   鍵とエージェントの設定方法は
   [Microsoftの手順](https://learn.microsoft.com/en-us/windows-server/administration/openssh/openssh_keymanagement)
   を参照する。秘密鍵はWindows側で管理する。
3. WindowsのPowerShellで一度手動接続し、Macのホスト鍵を確認する。
   以下の `macuser` と `mac.local` は実際のユーザー名・ホスト名に置き換える。

   ```powershell
   ssh -p 22 macuser@mac.local /usr/bin/true
   ```

4. 次のコマンドが入力待ちなしで成功することを確認する。

   ```powershell
   ssh -o BatchMode=yes -o StrictHostKeyChecking=yes -p 22 macuser@mac.local /usr/bin/true
   ```

   WinSCPでパスワード接続できるだけでは、この準備を満たさない。
   OpenSSHの設定ファイル（`%USERPROFILE%\.ssh\config`）にある `IdentityFile` や
   ホスト別名を利用できる。GUIで指定するポートとユーザー名は明示的に優先される。

#### 操作

1. WindowsビューアーからMacへVNC接続する。
2. メニュー（既定Ctrl+Alt+M）の **Mac clipboard (SSH)...** を開く。
3. SSHホスト・ポート・Macの短いユーザー名を入力し **Enable / Retry** を押す。
4. `SSH clipboard active` と表示されたら **Close** で設定画面を閉じる。
5. Windowsで新しくテキストをコピーし、VNC画面をクリックしてMacでペーストする。
   Macでの操作は「編集」メニューを使うと、CtrlとCommandの違いを除外できる。
6. 逆方向はVNC画面でMacのテキストをコピーし、Windowsのメモ帳などに貼り付ける。
   同期にはポーリング約1秒とSSH接続処理の遅延があるため、反映前に貼り付けた場合は
   少し待って貼り付け直す。

共有は接続ごとに明示的に有効にする。開始時の既存クリップボードはどちらも
上書きせず、その後のコピーを対象とする。**Disable** でSSH共有を止め、既存の
RFB方式に戻す。VNC切断時はSSH処理と設定画面も破棄する。

#### 対応範囲と動作

- 日本語・絵文字・改行・空文字を含むプレーンテキスト。UTF-8換算1MiBまで。
  RTFヘッダーやPostScriptヘッダーに似た文字列も文字列のまま扱う。
  画像・書式・ファイルは対象外。NULを含むデータは対象外。
- SSHユーザーはMacで現在アクティブなデスクトップのユーザーである必要がある。
  `/dev/console` の所有者と一致しなければ停止する。別ユーザーの仮想画面共有
  セッションや、ログイン画面のクリップボードは対象外。
- `SendClipboard`、`AcceptClipboard` の設定を尊重し、表示のみモードでは停止する。
- Windowsからの送信は対象VNC画面にフォーカスが戻ったときに行う。
  Mac側はVNC操作中に取得し、Windowsへ切り替えた直後にも最後の取得を行う。
  バックグラウンドで常時クリップボードを取得し続ける機能ではない。
- ローカルの変更番号とMacの変更番号を使って送り返しを防止する。
  Mac側の取得中にWindowsでコピーされた新しい値は上書きしない。
  同時コピーではWindows側の新しい値を優先する。
- 複数接続の混線を避けるため、SSHクリップボードを有効にできるのは
  ビューアープロセス内で一つの接続だけ。別の接続で有効化すると前の接続では無効化する。
- SSH認証・ホスト鍵・通信エラー、容量超過時は同期を止め、設定画面に理由を表示する。
  設定修正後 **Enable / Retry** で再開する。自動でホスト鍵を承認しない。
  エラー中もRFB共有には自動フォールバックせず、明示的なDisableを待つ。
- テキストは標準入力・出力のパイプで受け渡す。コマンド文字列、一時ファイル、
  ログにクリップボード本文を保存しない。SSHは要求ごとに起動し、15秒でタイムアウトする。

#### SSHクリップボードの検証

- C++のプロトコルテスト4件と既存WinSCP連携テスト5件が成功。
- `python3 tests/unit/macclipboard-helper.py` の4テストが成功。
  通常のクリップボードには触れず、Macの専用ペーストボードでUnicode・空文字・
  RTF/EPS風文字列・1MiB・非テキスト・ファイル名を送らないこと・不正UTF-8・容量超過を検証した。
- Windows用コードはMinGW-w64でコンパイル確認。macOS版ビューアーもビルド確認。
- Windowsと対象のTahoe 26.7.1をSSHでつないだGUIの実機試験は未実施。
  初回ホスト鍵未登録／鍵変更、SSH鍵未登録、Macユーザー不一致、通信切断、
  双方向のコピー、取得中のローカルコピー、送受信設定切替、VNC切断を
  対象環境で受け入れ確認する。

## ドラッグ・アンド・ドロップでのファイル転送

元のビューアーにはドロップ受付、ファイル送受信処理ともに未実装だった。
`common/rfb/clipboardTypes.h` に `clipboardFiles` 定数はあるが、
ビューアーが対応を通知する形式は `clipboardUTF8` のみであり、ファイル転送対応を意味しない。

接続先が対応する転送方式の確認が必要。ドロップ受付だけを追加してもファイルは送信できない。
Appleの画面共有アプリ同士で利用できるドラッグ転送は、Windowsビューアーとの互換性を保証しない。

転送方式は、利用可能なSSH/SFTPを使う。
VNC接続とは別に、Macのリモートログインを許可されたユーザーで認証する。
SSHでログインするユーザーのアクセス権に従うため、画面共有中のユーザーとは
ホームディレクトリーやアクセスできるファイルが異なる場合がある。

SSH/SFTPを利用する場合は、VNCとは別の認証・ホスト鍵確認・転送先指定が必要。
Mac画面の任意の場所へのドロップと、指定フォルダーへのアップロードは異なる操作である。
Mac画面からWindowsへのドラッグでは、画面画像とポインターイベントだけから
対象ファイルのパスや内容を取得できないため、接続先側の協調機構も必要となる。

### 追加したWinSCP連携（Windowsのみ）

ユーザーの選択に従い、ビューアーのメニューからWinSCPの独立した転送画面を
開く方式を採用した。SFTPクライアントの内蔵や、MacのFinder画面に直接ドロップする
方式ではない。クリップボード共有は上記の独立したSSH機能を使用する。

1. Windowsに[WinSCP](https://winscp.net/eng/download.php)を別途インストールする。
   双方向のドラッグ転送にはCommander（左右2ペイン）表示が分かりやすい。
2. Macの「システム設定」→「一般」→「共有」でリモートログインを有効にし、
   使用するMacユーザーを許可する。
3. VNC接続後、ビューアーのメニュー（既定ではCtrl+Alt+M）から
   **File transfer (WinSCP)...** を選ぶ。
4. SSHホスト名、SSHポート（既定22）、Macのユーザー名を確認して **Open** を押す。
   ホスト名はVNC接続先を引き継ぐが、VNCのポート番号は引き継がない。
   トンネル・リバース接続では必要に応じて実際のSSH接続先に修正する。
   ユーザー名は空欄にしてWinSCP側で入力することもできる。
5. 初回接続ではWinSCPでMacのホスト鍵を確認し、SSHの認証情報を入力する。
   秘密鍵を使う場合はWinSCP側で設定する。
6. Windows→Macはローカル側からリモート側へ、Mac→Windowsは逆方向へ
   ファイルやフォルダーをドラッグする。保存先フォルダーを各ペインで選ぶ。
   上書き確認・進捗・キャンセル・エラー表示はWinSCPが担当する。

WinSCPのパスはApp Pathsレジストリーと標準インストール先から自動検出する。
見つからない場合やポータブル版の場合は **Browse...** で `WinSCP.exe` を選択する。
成功した実行ファイルのパスはビューアー実行中だけ記憶する。
VNCのパスワードをSSHに流用したり、パスワードをコマンドラインへ渡したりはしない。
WinSCPは別プロセスなので、転送中にVNC接続が終了しても独立して動作する。

Windowsエクスプローラーとの直接ドラッグもWinSCPが提供するが、Mac側から
エクスプローラーへのドロップにはWinSCPのシェル拡張または一時フォルダー方式の設定が
必要になる場合がある。左右ペイン間のドラッグにはシェル拡張は不要。

### 検証項目

自動テストでは、SFTP URL生成、IPv4/IPv6、ポート範囲、日本語ユーザー名、
URL・追加オプションの混入防止、Windows引数の引用を検証する。

Windowsと対象Macでの受け入れ確認:

- WinSCPインストール済み／未インストール／日本語・空白を含むポータブル版パス。
- SSHポート22と変更済みポート、認証失敗、初回ホスト鍵確認、ホスト鍵変更時の警告。
- 両方向で空ファイル、日本語名・空白入り名、複数ファイル、フォルダー、大容量ファイル。
- 上書き確認とキャンセル、通信切断時の表示、転送完了後のファイルサイズ・SHA-256一致。
- VNC画面操作の継続、VNC切断後の転送継続、クリップボードの既存挙動。

対象のWindowsとTahoe 26.7.1間での実転送は未検証。

実施済みの開発検証:

- 追加した自動テスト5件すべて成功（macOS上のCTest）。
- MinGW-w64で `FileTransferWin32.cxx`、`FileTransferCommand.cxx`、
  変更した `Viewport.cxx` のWindows用オブジェクト生成に成功（警告をエラー扱い）。
- macOS版ビューアーのビルド成功。Windows限定の追加による他プラットフォームへの
  ビルド影響がないことを確認。
- Windows版全体のリンクとGUI操作は未実施。

参考:

- [TigerVNCビューアー公式マニュアル](https://tigervnc.org/doc/vncviewer.html)
- [Appleの画面共有ガイド](https://support.apple.com/en-az/guide/mac-help/mh14066/mac)
- [WinSCPのコマンドライン](https://winscp.net/eng/docs/commandline)
- [WinSCPのアップロード操作](https://winscp.net/eng/docs/task_upload)
- [WinSCPのドラッグ用シェル拡張](https://winscp.net/eng/docs/dragext)
