#pragma once
// TLDCoop_DLC: our own entries in TLD's string table (Localization.Get): a key's English and Polish text, the Polish
// one when the game runs in Polish. automat_items' Localization.Get hook asks here first. Main thread only.

#include <string>

struct Il2CppString;

namespace custom_text {

void add(const char* key, const char* english, const char* polish);
// Ours for this key (a managed string kept alive), or nullptr: TLD's own.
Il2CppString* find(Il2CppString* key);
// The text for a key in the game's language, as UTF-8 ("" if not ours).
std::string text(const char* key);

}  // namespace custom_text
