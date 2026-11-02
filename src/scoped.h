#pragma once
// TLDCoop_DLC: flags and counters set around a call into the game, put back however it leaves (an IL2CPP exception
// is a C++ exception on Windows and runs destructors), so a game exception can't leave one stuck on.

template <typename T>
class ScopedValue {
public:
    ScopedValue(T& target, T value) : target_(target), old_(target) { target_ = value; }
    ~ScopedValue() { target_ = old_; }
    ScopedValue(const ScopedValue&) = delete;
    ScopedValue& operator=(const ScopedValue&) = delete;

private:
    T& target_;
    T old_;
};

class ScopedCount {
public:
    explicit ScopedCount(int& count, bool on = true) : count_(count), on_(on) {
        if (on_) ++count_;
    }
    ~ScopedCount() {
        if (on_) --count_;
    }
    ScopedCount(const ScopedCount&) = delete;
    ScopedCount& operator=(const ScopedCount&) = delete;

private:
    int& count_;
    bool on_;
};
