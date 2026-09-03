#pragma once
#include <QString>
#include <cstdint>

enum class CollisionPolicy {
    Overwrite,
    AppendCounter
};

struct ProcessingConfig {
    QString inputDir;
    QString outputDir;
    QString fileMask;
    bool deleteSource = false;
    CollisionPolicy collisionPolicy = CollisionPolicy::Overwrite;
    bool periodic = false;
    int intervalMs = 5000;
    uint64_t xorKey = 0;
};