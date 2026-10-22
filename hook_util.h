#pragma once

// Thin wrapper over the inline-hook library. Refuses null targets and refuses to hook the same
// address twice (identical-code-folding could make two IL2CPP methods share one native address).
// With check_patched, it also refuses targets whose first instructions already look like another
// module's hook (a branch or `br xN` right at the entry), to avoid fighting Kuro over a method.
bool install_hook(const char* label, void* target, void* replacement, void** original,
                  bool check_patched = false);
