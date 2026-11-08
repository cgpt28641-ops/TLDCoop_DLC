#pragma once
// TLDCoop_DLC (Karol 08.11 "przechodzimy do IRON ROAD", item 10: the train): Iron Road's steam train
// (TLD.SteamTrain.SteamTrain) in co-op. One player drives: the first one into the cab. Their game runs the engine and
// sends the train ~15 times a second (Msg::Train: spline, place on it, direction, speed, pressure, gear, brakes, fuel,
// water); every other game in that scene stops its own engine and controls for it and moves its train to match.
// Anyone inside also sends where they stand in the cab, so their avatar rides along instead of trailing behind.
// Leaving the cab hands the train back (a last frame, reliable). With nobody driving, the host sends its train every
// 3 s and a friend's that is more than a metre off is put there. Main thread only.

#include <string>

struct Il2CppObject;

namespace coop_train {

bool init();
void on_frame(const std::string& scene, bool in_game);

// Where a player's avatar stands while they ride the train in our scene (their spot in the cab, on our train). False
// when they're not on it (or the train isn't here).
// F1 `train_flip`: the train's direction on its track the other way (a train an older build turned round).
bool console(const std::string& line);
bool riding(unsigned char player);
float ride_speed(unsigned char player);  // their walking speed in the cab (m/s)  // they're on the train in our scene (their speed is the train's)
bool ride_spot(unsigned char player, float& x, float& y, float& z, float& yaw);

// The train's locker (SteamTrain.m_Container) in our scene, and its id for world_containers ("train|GUID": it moves,
// so not by name and place). nullptr / "" when it isn't the train's.
Il2CppObject* container();
std::string container_key(Il2CppObject* c);

std::string describe();  // for the log and the dev command `train`

}  // namespace coop_train
