# Merge notes: upstream/slippi into lylat

Branch `lylat-upstream-sync`, based on `origin/lylat` (efa8ab9e1). Merged
`upstream/slippi` (project-slippi/Ishiiruka, 60f7b6349, v3.6.4 plus the
desync recovery fix) in one step: 222 upstream commits, 56 Lylat commits,
merge base 3f86dc42b.

## The big upstream change

Upstream moved the user account, game reporting, direct codes, rank info,
jukebox and match status reporting into the Rust library
`Externals/SlippiRustExtensions` (a submodule, built by `cargo` from
Core.vcxproj). The Rust user manager reads `<User>/Slippi/user.json` and
talks only to Slippi servers; its file name and URLs are hard coded.

Lylat needs its own account file (`lylat.json`, with the linked Slippi
account under `slippi`), the Lylat user API and the Lylat reporting URL.
We cannot change the Rust submodule without forking it, so:

- **SlippiUser stays a C++ class.** It keeps the Lylat logic (read
  `lylat.json`, refresh from `<origin>/users/slp/<uid>`, the `slippi` block,
  `HasSlippiInfo`, the `TMP#1234` placeholder, the Lylat update message) and
  takes upstream's new interface: the constructor takes the Rust device
  pointer, plus `GetUserChatMessages`, `GetDefaultChatMessages` and the new
  `UserInfo` fields (chat messages, bot flag, ranked fields). Default chat
  messages come from Rust. User chat messages come from `chatMessages` in
  `lylat.json` or the Lylat user API when it sends 16 of them, else the
  defaults. `userInfo` is now guarded by a mutex (the watcher thread writes
  it while the game reads it).
- **Game reports are split by route.** `SlippiMatchmaking::IsMexMode` (the
  fork's rule) decides. Games routed to Lylat go through the fork's C++
  `SlippiGameReporter` to `SlippiCustomMatchmakingReportingURL` with the
  Lylat uid/playKey. Games routed to Slippi go through upstream's Rust
  reporter with the linked Slippi uid/playKey (before the merge the fork
  sent the Lylat uid to Slippi's old report URL). The C++ reporter was
  re-added to CMake and Core.vcxproj; it now takes the player uids in each
  report instead of a session list.
- **Lylat base URL is a setting.** `[Core] LylatServerOrigin` (default
  `https://lylat.gg`) replaces the hard coded `lylat.gg` login and user API
  URLs, so a local Lylat server can be used for testing.

## Conflicts

| File | Resolution |
| --- | --- |
| `.github/workflows/pr-build.yml` | Kept Lylat's PR branch (`lylat`) and `paths-ignore`, and the `Lylat Dolphin.app` copy. Took upstream's runners, Rust toolchain/cargo test steps, artifact names and the macOS signing steps (they are guarded by secrets). Lylat's earlier removal of the Playback jobs is kept. |
| `Source/Core/Common/FileUtil.cpp` | Took upstream's `GetSlippiUserConfigFolder()` (used by Rust). Kept Lylat's account path as `GetSlippiUserJSONPath()` (exe dir `lylat.json` on Windows, `<User>/lylat.json` on Linux, App Support `Slippi/lylat.json` on macOS), declared again in FileUtil.h. Kept `F_USERJSON_IDX` = `lylat.json` and took the new `F_VERSION_IDX`. |
| `Core/Slippi/SlippiUser.h/.cpp` | Rewritten as described above. |
| `Core/Slippi/SlippiGameReporter.h/.cpp` (deleted upstream) | Kept, for Lylat-routed games only. |
| `Core/Slippi/SlippiMatchmaking.h/.cpp` | Constructor is now `(rs_exi_device_ptr, user, isMex)`; `FindMatch(settings, isMex)` kept; upstream's `MatchmakeResult` and rank getters taken. The fork's duplicate Force LAN IP block was dropped (upstream already handles it before the fallback). Lylat's create-ticket body (Slippi or Lylat credentials, UTF-8 connect code, `game` block) kept. |
| `Core/HW/EXI_DeviceSlippi.cpp` | Constructor and connection cleanup pass `isMex`. `handleReportGame` uses upstream's query struct and routes the report as above. The C++ reporter session restarts next to the Rust one. |
| `DolphinWX/Frame.h`, `FrameTools.cpp`, `SlippiAuthWebView/*` | Upstream deleted the macOS sign-in webview. Followed upstream; the login now opens the QR sign-in dialog (separate commit), with the browser page as fallback on every platform. |
| `DolphinWX/Dolphin.vcxproj` | Took upstream (Rust DLL copy, link libs). Lylat's only change there (`/NODEFAULTLIB:MSVCRTD` for DebugFast) is in upstream too. |

## Other changes made during the merge

- `SlippiMatchmaking::startMatchmaking`: upstream's "known desync ISO"
  check (Rust MD5 list) now runs only for searches routed to Slippi, so
  Lylat-routed mods (Akaneia, Beyond Melee, Midnight Melee, 1.03) are not
  blocked by Slippi's list.

## Not kept / changed behaviour

- The executable is now `Slippi Dolphin.exe` (upstream's `TargetName`).
  The Lylat launcher, if it expects `Dolphin.exe`, needs checking.
- Rust-side features that need a logged-in Rust user (rank display,
  match status reports such as "abandoned" and "poor_performance", ranked
  match result fetch) only work if a Slippi `user.json` also exists in
  `User/Slippi`. With only `lylat.json` they do nothing.
- Match status reports for Lylat-routed games still go to the Rust side
  (and so to Slippi) when a Slippi `user.json` exists. Not filtered.

## Verification

Release x64 build, run from `tmp-ishiirukauild.bat` under VS 2019 vcvars64:
`msbuild Source\Dolphin.sln /t:Dolphin /p:Configuration=Release /p:Platform=x64 /m:1 /p:CL_MPCount=2`,
with `RUSTUP_TOOLCHAIN=1.88.0-x86_64-pc-windows-msvc` set. On this machine,
`rust-toolchain.toml` alone picks the GNU host toolchain, which has no MSVC
`core`. The build passed at commit 5805cd600 with 0 errors and exit code 0,
and needed no code fixes. It produced `Binaryd\Slippi Dolphin.exe` and
`slippi_rust_extensions.dll`.

Not run: a game, online play, reporting, or the user API refresh against
real servers.

## QR sign-in (separate commits)

`Core/Slippi/LylatDeviceLogin.*` (the device-code client) and
`DolphinWX/SlippiQrLoginDialog.*` (the dialog). `SlippiUser::OpenLogInPage`
calls a hook that `CFrame` sets to open the dialog. Without the hook, it opens
the browser page. qrcodegen v1.8.0 is in `Externals/qrcodegen` (MIT,
listed in `Externals/licenses.md`).

Checked against a local Python mock of `/api/device/code` and
`/api/device/token`, with `[Core] LylatServerOrigin = http://127.0.0.1:3002`
in the ignored `Binary/x64/User/Config/Dolphin.ini`. The test aid
`LYLAT_QR_LOGIN_AT_START=1` opened the dialog with no game running.

| Check | Result |
| --- | --- |
| Approve run | One code, polls every 2 s, 200 after about 8 s. `Binary/x64/lylat.json` was written with the mock account, and the dialog showed "Logged in". |
| Deny run | "Sign-in denied", with Retry and Manual login. |

Not checked:
- A real Lylat server, or a phone scan. The QR image was not decoded by a tool.
- The 404 fallback to the browser.
- Expired, Error and Retry, and the in-game login hook with a running game.
- The CMake (Linux/macOS) build.
