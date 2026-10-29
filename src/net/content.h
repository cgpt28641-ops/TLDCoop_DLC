#pragma once
// Optional content (DLC): which of TLD's optional content packs each player has installed, as the game itself
// reports it (TLD.OptionalContent.OptionalContentManager.InstalledContent; see optional_content.h). The ids are TLD's
// own, so this needs no list of DLCs: Tales from the Far Territory, Iron Road and whatever comes next all work the
// same way.
//   The world is the host's, so a friend needs every pack the host has installed: the host's regions, items and
// buildings may come from any of them. A friend with more packs than the host is fine (the host's world never uses
// them). The host checks this when a friend says hello (Hello carries the friend's list) and turns them away with
// RejectReason::MissingContent, naming what's missing. coop.ini's dlc_check=0 turns the check off.
//   On the wire a list is one text: the ids sorted, without duplicates, joined by commas. kContentUnknown means the
// game couldn't tell (its content manager wasn't ready, or the mod couldn't read it). No IL2CPP.

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace net {

constexpr const char* kContentUnknown = "?";
constexpr size_t kContentLen = 256;  // Hello's content text (fits dozens of ids)

inline std::string content_trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && static_cast<unsigned char>(s[a]) <= ' ') ++a;
    while (b > a && static_cast<unsigned char>(s[b - 1]) <= ' ') --b;
    return std::string(s.substr(a, b - a));
}

// "b, a,,a " -> {"a", "b"}. kContentUnknown -> {} (use content_known() to tell it from "nothing installed").
inline std::vector<std::string> content_split(std::string_view text) {
    std::vector<std::string> ids;
    if (content_trim(text) == kContentUnknown) return ids;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t comma = text.find(',', start);
        const size_t end = comma == std::string_view::npos ? text.size() : comma;
        std::string id = content_trim(text.substr(start, end - start));
        if (!id.empty() && id.find(',') == std::string::npos) ids.push_back(std::move(id));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

// The wire text for a list: sorted, no duplicates, comma-separated ("" for none).
inline std::string content_join(std::vector<std::string> ids) {
    for (std::string& id : ids) id = content_trim(id);
    ids.erase(std::remove_if(ids.begin(), ids.end(),
                             [](const std::string& id) { return id.empty() || id.find(',') != std::string::npos; }),
              ids.end());
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::string out;
    for (const std::string& id : ids) {
        if (!out.empty()) out += ',';
        out += id;
    }
    return out;
}

inline bool content_known(std::string_view text) { return content_trim(text) != kContentUnknown; }

// The ids in `host` that `client` lacks (sorted). Empty when the client has them all, or when either side is unknown
// (nothing to compare: the caller decides what unknown means).
inline std::vector<std::string> content_missing(std::string_view host, std::string_view client) {
    std::vector<std::string> missing;
    if (!content_known(host) || !content_known(client)) return missing;
    const std::vector<std::string> have = content_split(client);
    for (std::string& id : content_split(host))
        if (!std::binary_search(have.begin(), have.end(), id)) missing.push_back(std::move(id));
    return missing;
}

// For a Reject's detail (kDetailLen): the missing ids, cut short with "..." if they don't fit in `max` bytes.
inline std::string content_detail(const std::vector<std::string>& missing, size_t max) {
    std::string out = content_join(missing);
    if (out.size() <= max) return out;
    if (max <= 3) return out.substr(0, max);
    return out.substr(0, max - 3) + "...";
}

}  // namespace net
