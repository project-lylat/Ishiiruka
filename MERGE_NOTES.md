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

See the commit messages and the report. Built Release x64 with MSBuild
(VS 2019, v142) including the Rust library. Online play, reporting and
the user API refresh were not run against real servers.
