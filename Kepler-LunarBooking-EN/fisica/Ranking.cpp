#include "ranking.hpp"

#include "maximum_average.hpp"

#include <optional>
#include <set>
#include <utility>
#include <vector>

using namespace std;

vector<AverageRange> findTopAverageRanges(
    const vector<DataPoint>& data,
    int targetCount,
    int margin,
    int numberOfRanges) {
    vector<AverageRange> bestRanges;
    set<pair<size_t, size_t>> usedRanges;

    for (int rank = 0; rank < numberOfRanges; ++rank) {
        const optional<AverageRange> bestRange =
            findMaximumAverageRange(data, targetCount, margin, usedRanges);
        if (!bestRange.has_value()) {
            break;
        }

        bestRanges.push_back(*bestRange);
        usedRanges.insert({bestRange->startIndex, bestRange->endIndex});
    }

    return bestRanges;
}
