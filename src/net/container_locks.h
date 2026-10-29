#pragma once
// TLDCoop_DLC: one player at a time in a container. Two players with the same container open crashed the host: one's
// take loaded new contents into the other's game (Container.DestroyAllGear + Deserialize) while that game's container
// panel still showed the items just destroyed.
//   The host keeps who is in which container (this table). A friend about to search or open one asks the host first
//   (ContainerLock Take) and opens it once the host says it's theirs (ContainerLock State, owner = them); the host
//   decides for itself at once. Someone else's container isn't opened at all: the player is told who is in it. The one
//   in it says Take again every kLockRefreshMs while still busy with it (searching, the panel, looking at what the
//   search found) and Release once done; the host frees a container whose owner stopped saying so (kLockStaleMs), or
//   left, or went to another scene. Every change goes to all friends (State), so each game knows what's taken.
// No IL2CPP.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace net {

constexpr uint8_t kNoOwner = 0xFF;
constexpr double kLockRefreshMs = 1000;  // the owner says Take again this often while busy
constexpr double kLockStaleMs = 4000;    // the host frees a container whose owner hasn't for this long

// A container across scenes: "scene|id".
inline std::string lock_key(const std::string& scene, const std::string& id) { return scene + "|" + id; }

class LockTable {
public:
    struct Entry {
        uint8_t owner = kNoOwner;
        double refreshed_ms = 0;
    };

    // Who has it (kNoOwner: nobody).
    uint8_t owner(const std::string& key) const {
        const auto it = entries_.find(key);
        return it == entries_.end() ? kNoOwner : it->second.owner;
    }

    // Host: `player` wants it. Theirs if free or theirs already (refreshed). Returns the owner after the call.
    uint8_t take(const std::string& key, uint8_t player, double now_ms) {
        Entry& e = entries_[key];
        if (e.owner == kNoOwner || e.owner == player) {
            e.owner = player;
            e.refreshed_ms = now_ms;
        }
        return e.owner;
    }

    // Host: `player` is done with it. True if it was theirs (and is free now).
    bool release(const std::string& key, uint8_t player) {
        const auto it = entries_.find(key);
        if (it == entries_.end() || it->second.owner != player) return false;
        entries_.erase(it);
        return true;
    }

    // Client: what the host says (kNoOwner frees it).
    void set(const std::string& key, uint8_t owner, double now_ms) {
        if (owner == kNoOwner) {
            entries_.erase(key);
            return;
        }
        Entry& e = entries_[key];
        e.owner = owner;
        e.refreshed_ms = now_ms;
    }

    // Host: the keys freed because their owner went quiet (other than `keep`, the host itself, who isn't timed out).
    std::vector<std::string> expire(double now_ms, double stale_ms, uint8_t keep) {
        std::vector<std::string> freed;
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->second.owner != keep && now_ms - it->second.refreshed_ms > stale_ms) {
                freed.push_back(it->first);
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
        return freed;
    }

    // Host: everything `player` holds goes (they left). The keys freed.
    std::vector<std::string> drop_player(uint8_t player) {
        std::vector<std::string> freed;
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it->second.owner == player) {
                freed.push_back(it->first);
                it = entries_.erase(it);
            } else {
                ++it;
            }
        }
        return freed;
    }

    const std::unordered_map<std::string, Entry>& entries() const { return entries_; }
    void clear() { entries_.clear(); }

private:
    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace net
