#pragma once

// Each installer resolves its target by name, verifies the signature, and hooks it.
// Returns false (after logging "NOT FOUND ...") if anything does not match - nothing is hooked then.
#include <string>
namespace features {
bool install_attack_speed(bool dry_run);   // AbilityEnhancement.get_NormalAtkSpeedUpRate
bool install_move_speed(bool dry_run);     // AbilityEnhancement.get_MoveSpeedUpRate
bool install_attack_range(bool dry_run);   // Battle.Attack.get_AttackRange
bool install_skill_no_cd(bool dry_run);    // Characters.CoolTime.Update{HeavyAttack,ExtraActionAttack}CT
bool install_god_mode(bool dry_run);       // CharaBase.get_IsInvincible (player side only)
bool install_damage_rate(bool dry_run);   // AbilityBelongAttributeMaster.CalculateDamageUpRate (2 overloads)
bool install_skip_story(bool dry_run);     // CharacterTheaterController.Skip (the story dialog Skip button)
bool install_event_points(bool dry_run);   // read-only: QuestResultModel event points -> kuro_companion_points.txt
void event_points_set_paths(const std::string& internal_file, const std::string& external_file);
bool install_unity_speed(bool dry_run);
bool install_game_speed(bool dry_run);     // in-battle speed: scales only timeScale requests made by GameTimeManager    // UnityEngine.Time.set_timeScale (+ get_deltaTime re-apply)
}
