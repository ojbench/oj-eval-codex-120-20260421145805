// TimingWheel implementation for ACMOJ 2560
// Implements TaskNode, TimingWheel, and Timer using three-level wheels: seconds, minutes, hours

#pragma once

#include <vector>
// Do not include any other headers to satisfy constraints

// Ensure Task is a complete type when this header is included by OJ main
#include "Task.hpp"

class TaskNode {
    friend class TimingWheel;
    friend class Timer;
public:
    TaskNode() : task(nullptr), next(nullptr), prev(nullptr), time(0), wheel_level(0), canceled(false), slot_idx(0) {}

private:
    Task* task;              // Associated task
    TaskNode* next;          // Next in slot list
    TaskNode* prev;          // Previous in slot list
    int time;                // Remaining ticks within its wheel interval
    int wheel_level;         // 0: seconds, 1: minutes, 2: hours
    bool canceled;           // Mark as canceled
    size_t slot_idx;         // current slot index within its wheel
};

class TimingWheel {
    friend class Timer;
public:
    TimingWheel(size_t size, size_t interval)
        : size(size), interval(interval), current_slot(0) {
        // Create circular doubly linked list heads for each slot
        slots.resize(size, nullptr);
    }

    ~TimingWheel() {
        // Nodes are managed by Timer; slots contain non-owning pointers
    }

private:
    const size_t size;       // number of slots
    const size_t interval;   // tick span represented by a single slot advance
    size_t current_slot;     // current index
    std::vector<TaskNode*> slots; // head pointers of slot lists
};

class Timer {
public:
    Timer()
        : sec(60, 1), min(60, 60), hour(24, 3600) {}

    ~Timer() {
        // No ownership of Task; TaskNode allocated dynamically here must be freed
        // We free all remaining TaskNodes from all wheels
        cleanupWheel(sec);
        cleanupWheel(min);
        cleanupWheel(hour);
    }

    TaskNode* addTask(Task* task);
    void cancelTask(TaskNode *p);
    std::vector<Task*> tick();

private:
    TimingWheel sec;   // 1-second slots, 60 slots
    TimingWheel min;   // 1-minute slots, 60 slots
    TimingWheel hour;  // 1-hour slots, 24 slots

    void insertNode(TimingWheel &wheel, TaskNode* node, size_t slot_idx) {
        TaskNode* head = wheel.slots[slot_idx];
        if (!head) {
            wheel.slots[slot_idx] = node;
            node->next = node->prev = nullptr;
        } else {
            // insert at front
            node->next = head;
            node->prev = nullptr;
            head->prev = node;
            wheel.slots[slot_idx] = node;
        }
        node->slot_idx = slot_idx;
    }

    void removeNode(TimingWheel &wheel, size_t slot_idx, TaskNode* node) {
        if (node->prev) node->prev->next = node->next;
        else wheel.slots[slot_idx] = node->next;
        if (node->next) node->next->prev = node->prev;
        node->next = node->prev = nullptr;
    }

    void cleanupWheel(TimingWheel &wheel) {
        for (size_t i = 0; i < wheel.size; ++i) {
            TaskNode* cur = wheel.slots[i];
            while (cur) {
                TaskNode* nxt = cur->next;
                delete cur;
                cur = nxt;
            }
            wheel.slots[i] = nullptr;
        }
    }

    void cascade(TimingWheel &from, TimingWheel &to, int to_level, bool from_is_hour) {
        // Move all tasks from current slot of 'from' down to 'to'
        size_t idx = from.current_slot;
        TaskNode* cur = from.slots[idx];
        from.slots[idx] = nullptr;
        while (cur) {
            TaskNode* nxt = cur->next;
            cur->next = cur->prev = nullptr;
            // recompute placement in lower wheel based on remaining time
            int t = cur->time; // remaining seconds within next lower wheel range
            size_t slot;
            if (from_is_hour) {
                // t encodes minutes*60 + seconds
                size_t minutes = (size_t)t / 60;
                size_t secs = (size_t)t % 60;
                slot = (to.current_slot + (minutes % to.size)) % to.size; // 'to' is minute wheel
                cur->time = (int)secs; // remainder seconds for next cascade
            } else {
                // from is minute wheel; t is seconds remainder
                slot = (to.current_slot + (size_t)t) % to.size; // 'to' is second wheel
                cur->time = 0;
            }
            cur->wheel_level = to_level;
            insertNode(to, cur, slot);
            cur = nxt;
        }
    }

    void scheduleNode(TaskNode* node, size_t delay) {
        // Place node into appropriate wheel and slot based on delay in seconds
        size_t seconds = delay;
        size_t hours = seconds / 3600;
        seconds %= 3600;
        size_t minutes = seconds / 60;
        size_t secs = seconds % 60;

        if (hours > 0) {
            node->wheel_level = 2;
            // time remaining within next lower wheel once cascaded (minutes*60 + secs)
            node->time = (int)(minutes * 60 + secs);
            size_t slot = (hour.current_slot + (hours % hour.size)) % hour.size;
            insertNode(hour, node, slot);
        } else if (minutes > 0) {
            node->wheel_level = 1;
            node->time = (int)secs;
            size_t slot = (min.current_slot + (minutes % min.size)) % min.size;
            insertNode(min, node, slot);
        } else {
            node->wheel_level = 0;
            node->time = (int)secs;
            size_t slot = (sec.current_slot + (secs % sec.size)) % sec.size;
            insertNode(sec, node, slot);
        }
    }
};

// Implementation details

inline TaskNode* Timer::addTask(Task* task) {
    TaskNode* node = new TaskNode();
    node->task = task;
    // first schedule using task->getFirstInterval()
    // We cannot include headers beyond vector, but Task is forward-declared and methods exist in OJ.
    size_t delay = task->getFirstInterval();
    scheduleNode(node, delay);
    return node;
}

inline void Timer::cancelTask(TaskNode *p) {
    if (!p) return;
    p->canceled = true;
}

inline std::vector<Task*> Timer::tick() {
    std::vector<Task*> ready;

    // advance seconds wheel
    size_t prev_sec_slot = sec.current_slot;
    sec.current_slot = (sec.current_slot + 1) % sec.size;

    // Execute tasks in current seconds slot
    size_t sidx = sec.current_slot;
    TaskNode* cur = sec.slots[sidx];
    sec.slots[sidx] = nullptr;
    while (cur) {
        TaskNode* nxt = cur->next;
        cur->next = cur->prev = nullptr;
        if (!cur->canceled) {
            ready.push_back(cur->task);
            // reschedule periodic tasks
            size_t period = cur->task->getPeriod();
            if (period > 0) {
                scheduleNode(cur, period);
            } else {
                delete cur;
            }
        } else {
            delete cur;
        }
        cur = nxt;
    }

    // Cascade only on wraps
    bool wrapped_min = false;
    if (sec.current_slot == 0 && prev_sec_slot != 0) {
        size_t prev_min_slot = min.current_slot;
        min.current_slot = (min.current_slot + 1) % min.size;
        cascade(min, sec, 0, false);
        wrapped_min = (min.current_slot == 0 && prev_min_slot != 0);
    }

    if (wrapped_min) {
        size_t prev_hour_slot = hour.current_slot;
        hour.current_slot = (hour.current_slot + 1) % hour.size;
        (void)prev_hour_slot;
        cascade(hour, min, 1, true);
    }

    return ready;
}
