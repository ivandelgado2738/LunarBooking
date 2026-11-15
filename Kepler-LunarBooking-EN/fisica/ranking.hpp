#ifndef RANKING_HPP
#define RANKING_HPP

#include "maximum_average.hpp"

#include <vector>

using namespace std;

vector<AverageRange> findTopAverageRanges(
    const vector<DataPoint>& data,
    int targetCount,
    int margin = 0,
    int numberOfRanges = 20);

#endif
