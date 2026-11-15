#ifndef MAXIMUM_AVERAGE_HPP
#define MAXIMUM_AVERAGE_HPP

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace std;

struct DataPoint {
    string timestamp;
    double value;
};

struct AverageRange {
    string startDate;
    string endDate;
    size_t dataCount;
    double maximumAverage;
    size_t startIndex;
    size_t endIndex;
};

optional<AverageRange> findMaximumAverageRange(
    const vector<DataPoint>& data,
    int targetCount,
    int margin = 0,
    const set<pair<size_t, size_t>>& excludedRanges = {});

#endif
