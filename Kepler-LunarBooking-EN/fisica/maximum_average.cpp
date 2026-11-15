#include "maximum_average.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <vector>

using namespace std;

optional<AverageRange> findMaximumAverageRange(
    const vector<DataPoint>& data,
    int targetCount,
    int margin,
    const set<pair<size_t, size_t>>& excludedRanges) {
    vector<DataPoint> sortedData = data;
    sort(sortedData.begin(), sortedData.end(),
              [](const DataPoint& left, const DataPoint& right) {
                  return left.timestamp < right.timestamp;
              });

    const size_t dataCount = sortedData.size();
    if (dataCount == 0) {
        return nullopt;
    }

    const size_t minimumLength = static_cast<size_t>(
        max(1, targetCount - margin));
    const size_t maximumLength = min(
        dataCount, static_cast<size_t>(max(0, targetCount + margin)));

    vector<double> prefixSums(dataCount + 1, 0.0);
    for (size_t index = 0; index < dataCount; ++index) {
        prefixSums[index + 1] = prefixSums[index] + sortedData[index].value;
    }

    double bestAverage = -numeric_limits<double>::infinity();
    optional<AverageRange> bestRange;

    for (size_t startIndex = 0;
         startIndex + minimumLength <= dataCount;
         ++startIndex) {
        for (size_t length = minimumLength;
             length <= maximumLength;
             ++length) {
            const size_t endExclusive = startIndex + length;
            if (endExclusive > dataCount) {
                break;
            }

            const size_t endIndex = endExclusive - 1;
            if (excludedRanges.count({startIndex, endIndex}) != 0) {
                continue;
            }

            const double average =
                (prefixSums[endExclusive] - prefixSums[startIndex]) / length;
            if (average > bestAverage) {
                bestAverage = average;
                bestRange = AverageRange{
                    sortedData[startIndex].timestamp,
                    sortedData[endIndex].timestamp,
                    length,
                    average,
                    startIndex,
                    endIndex};
            }
        }
    }

    return bestRange;
}
