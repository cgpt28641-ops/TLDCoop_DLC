#pragma once
// TLDCoop_DLC: which optional content (DLC) this game has installed, as TLD itself sees it:
// TLD.OptionalContent.OptionalContentManager (TLD.OptionalContent.dll) and its InstalledContent. The ids are TLD's
// own (the same ones its save slots record as m_InstalledOptionalContent), so no DLC is named here: Tales from the
// Far Territory, Iron Road and later packs are all read the same way. net/content.h compares two players' lists.
// Main thread only.

#include <string>

namespace optional_content {

// net/content.h's text for what's installed (e.g. "a,b"; "" for none), or net::kContentUnknown ("?") if the game's
// content manager isn't there or ready, or its list can't be read (coop.log says why).
std::string installed();

// What installed() last returned, for messages ("" before the first call).
const std::string& last();

}  // namespace optional_content
