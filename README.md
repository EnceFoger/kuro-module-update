# Kuro Companion

Separate Zygisk module that runs **alongside** Kuro. It does not touch Kuro's files, hooks or menu.
Target package: `com.klab.bleach` (Bleach Brave Souls). Controlled by a text file (no menu, because
Kuro's ImGui menu cannot be extended without its source).

## Scope (compared with Kuro and libSHAI)

| libSHAI feature            | In Kuro?                                         | Here |
|----------------------------|--------------------------------------------------|------|
| God Mode                   | Yes (`CharaBase.get_IsInvincible`)               | skipped (duplicate) |
| Damage Multiplier          | Yes (`CalculateDamageUpRate`)                    | skipped (duplicate) |
| Speed Multiplier [Battle]  | Yes (`Time.set_timeScale`)                       | skipped (duplicate) |
| Attack Speed Multiplier    | No                                               | implemented, unverified on device |
| Move Speed Multiplier      | No                                               | implemented, unverified on device |
| Attack Range               | No                                               | implemented, unverified on device |
| Skill No Cooldown          | No                                               | implemented, experimental |
| Kill All Enemy             | No                                               | **NOT FEASIBLE** (see below) |

## Targets (found in this game version's global-metadata.dat, resolved by name at runtime)

- `Bleach.Games.QuestBattle.Characters.AbilityEnhancement::get_NormalAtkSpeedUpRate()` (must return `System.Single`)
- `Bleach.Games.QuestBattle.Characters.AbilityEnhancement::get_MoveSpeedUpRate()` (must return `System.Single`)
- `Bleach.Games.QuestBattle.Battle.Attack::get_AttackRange()` (`System.Single` or `System.Int32`)
- `Bleach.Games.QuestBattle.Characters.CoolTime::UpdateHeavyAttackCT(float)` and `UpdateExtraActionAttackCT(float)`

Every target is checked (class, method, argc, instance, return/param types). Any mismatch logs
`NOT FOUND ...` and that feature is simply not hooked. No offsets are hard-coded.
Hooks that Kuro already owns (`get_IsInvincible`, `CalculateDamageUpRate`,
`CalculateAttributeSpeedUpRate`, `set_timeScale`) are deliberately not touched.

## Known limits (be honest with yourself about these)

1. **IL2CPP export names.** The `libil2cpp_1.so` you supplied has its `il2cpp_*` export names blanked,
   and this module resolves the IL2CPP API by exported name. If the installed game's `libil2cpp.so`
   is the same, the log shows `NOT FOUND: libil2cpp export il2cpp_...` and **nothing is hooked**.
   It will not guess offsets. If that happens, send the logcat output.
2. **Never compiled or run on a device** by the author (no NDK available). Only the logic was reviewed.
3. **Semantics of the `*UpRate` getters are unconfirmed.** If a getter returns a bonus that is 0 by
   default, `mul` has no visible effect; try `attack_speed_mode=add`. Check logcat.
4. **Scope is unconfirmed.** `Attack` and `CoolTime` may also be used by enemies, so `attack_range`
   and `skill_no_cd` could affect them too.
5. `skill_no_cd` scales the frame delta (x1000) passed to the cooldown update; it does not write
   a zero cooldown. Off by default.
6. **Kill All Enemy: NOT FEASIBLE.** There is no single verified method; it would need an enemy
   list plus a damage/death call whose contract is unknown, and a UI trigger (impossible without
   Kuro's source). Not implemented, no placeholder.
7. Features that act in online modes (raids, co-op, PvP) can get accounts banned.

## Build

```
git clone https://github.com/jmpews/And64InlineHook external/And64InlineHook      # see external/README.txt
cp /path/to/zygisk.hpp include/zygisk.hpp                      # official header, API v4+
ANDROID_NDK=/path/to/ndk ./build.sh                            # -> kuro_companion.zip
```
Only CMake is provided (And64InlineHook builds with CMake); no Android.mk/Application.mk.

## Install and configure

1. Flash `kuro_companion.zip` in Magisk (Zygisk enabled). Kuro stays installed as is.
2. Start the game once. The module creates `/data/data/com.klab.bleach/files/kuro_companion.cfg`
   with everything off. Edit it as root (keep the file owner); changes apply within ~2 s.
3. Watch it work: `adb logcat -s KuroCompanion`. Expected lines: `feature ... : INSTALLED`
   or `NOT FOUND ...`.

## Build without a PC (GitHub Actions)

1. Create a GitHub repo and upload this whole folder (including the hidden `.github` folder).
2. Open the **Actions** tab -> "Build Kuro Companion" -> **Run workflow**.
3. When it finishes, download the `kuro_companion` artifact. Inside is `kuro_companion.zip`:
   this is the file to flash in Magisk.
The workflow downloads And64InlineHook and `zygisk.hpp` itself. If a step fails, the log says which one.

## Diagnosing crashes: `stage` and the log file

The module writes its log to `/data/data/com.klab.bleach/files/kuro_companion.log` (and to
`/storage/emulated/0/Android/data/com.klab.bleach/files/kuro_companion.log` if writable), besides logcat.
`stage` in `kuro_companion.cfg` controls how far it goes (default 1):
0 = idle, 1 = find libil2cpp + load API by name, 2 = also find the methods but install NO hooks,
3 = install hooks (needed for the features to work). The last line of the log shows where it stopped.

## New in this version

| Feature | Target | Notes |
|---|---|---|
| God Mode | `CharaBase.get_IsInvincible`, gated by `CharaBase.get_IsPlayableSide` | player-side characters only; off = original behaviour |
| Unity Speed | `UnityEngine.Time.set_timeScale` (+ `get_deltaTime` to re-apply slider changes) | scales the timeScale the game requests; pause stays 0 |
| Game Speed (battle only) | wraps `GameTimeManager.Update/FixedUpdate/ShiftOnFrame`; scales only `Time.set_timeScale` requests made from inside them | shares the `set_timeScale` hook with Unity Speed; both on = factors multiply |
| Damage Rate Up | `AbilityBelongAttributeMaster.CalculateDamageUpRate` (2- and 3-argument overloads) | return must be `Single`; all parameters verified integer/enum/reference, else NOT FOUND. Default mode `add` (works whether the base is 0 or 1.0). Same method Kuro hooks: skipped if already patched |
| Event Points (overlay data) | `QuestResultViewController.StartClearMode` + `QuestResultModel.get_LimitedEventPoint/TotalEventPoint/IsEventQuestUseEventPoint` | read-only; writes `kuro_companion_points.txt`; solo event quests only |
| Skip Story | `SkipButtonView.OnClicked` (+ `CharacterTheaterController.Update/Awake`, `SkipButtonView.Awake/Enable/Disable`) | presses the dialog's Skip button once per story dialog; see below |

God Mode and Unity Speed are also hooks that Kuro uses. If Kuro already patched the same method, this
module detects the patched entry point, logs `already looks patched ... skipped` and leaves it alone.
Do not enable the same feature in both places.

### Configurable limits
`max_attack_speed`, `max_move_speed`, `max_attack_range`, `max_unity_speed` (default 5.0). Valid range 1.0 .. 100.0;
missing or invalid values fall back to 5.0, and every value is clamped to its limit (minimum 0.1).
Example: `max_unity_speed=20.0` lets the Unity Speed slider reach 20.0x.

### Skip Story
`CharacterTheaterController.Skip` is a coroutine (returns `IEnumerator`) and cannot be called directly, so the module
invokes the Skip button's own handler, `SkipButtonView.OnClicked`, once per theater: after the dialog has run for about
1.2 s, only if it started and has not ended or been skipped, the game has not disabled the button, and the button object
is alive (`Object.op_Implicit`) and active+enabled (`Behaviour.get_isActiveAndEnabled`). Quests that consist only of a story
are then cleared by the game itself when the theater finishes. One controller can play several dialogs in a row, so the action is re-armed for every new dialog (when the previous one ended/was skipped, or `currentTheater` changes). If the button cannot be pressed, the log says why (`skip_story: waiting - ...`). Applies to every theater with an active Skip button.
Keys: `skip_story=0/1`, `damage_rate`, `damage_rate_mode=add|mul`, `max_damage_rate`.

### Event points (read-only)
With `event_points=1`, when the solo result screen reports CLEAR for a quest that uses event points, the module reads
`LimitedEventPoint` (gained) and `LimitedTotalEventPoint` (total) from the game's own `QuestResultModel` and writes
`kuro_companion_points.txt` (keys: seq, gained, total, session_points, session_quests, start, last_run, time; the file is reset at every game start) next to the log. The Kuro Control
overlay displays it. Nothing in the game is modified. Co-op results are not tracked.

### Game Speed (battle only)
Unity Speed scales every `Time.timeScale` request, menus included. Game Speed scales only the requests made while the
battle's own time controller (`GameTimeManager`) is running, so menus and loading keep normal speed. When the controller
stops ticking (battle over) the battle factor is dropped again. The log confirms it works with
`game_speed: confirmed - GameTimeManager sets Time.timeScale`; if the controller never sets timeScale itself the log warns
that Game Speed has no effect. Keys: `game_speed`, `max_game_speed`. It needs the `Time.set_timeScale` hook, so it is
unavailable when another module (e.g. Kuro) already owns that method.
