#pragma once

#include <array>
#include <string_view>

namespace fzy {

static bool hasMatch(std::string_view needle, std::string_view haystack) {
    if (needle.empty()) {
        return true;
    }

    size_t j = 0;
    for (size_t i = 0; i < haystack.size() && j < needle.size(); ++i) {
        if (needle[j] == haystack[i]) ++j;
    }

    return j == needle.size();
}

// Heuristic score: reward adjacency, word/segment starts, penalize gaps.
// Returns 0 if not a subsequence (so caller can skip).
static double score(std::string_view needle, std::string_view haystack) {
    if (needle.empty()) {
        return 0.0;
    }

    // greedy forward match, record positions
    std::array<int, 512> pos{};
    if (needle.size() > pos.size()) {
        return 0.0;
    }

    size_t j = 0;
    for (size_t i = 0; i < haystack.size() && j < needle.size(); ++i) {
        if (needle[j] == haystack[i]) {
            pos[j++] = (int)i;
        }
    }

    if (j != needle.size()) {
        return 0.0;
    }

    // score matched positions
    double score = 0.0;
    for (size_t k = 0; k < needle.size(); ++k) {
        const int i = pos[k];
        const char cur = haystack[i];
        const char prev = (i > 0) ? haystack[i - 1] : '\0';
        bool boundary = (i == 0) || prev == '/' || prev == '\\' || prev == '_' || prev == '-' || prev == ' ' || prev == '.';
        score += 1.0;

        if (boundary) {
            score += 2.0; // prefer segment starts ("foo/bar", "FooBar", ".cpp")
        }

        if (k > 0 && pos[k] == pos[k - 1] + 1) {
            score += 3.0; // adjacency bonus
        }
    }

    // penalize overall spread (tighter matches rank higher)
    const int span = pos[needle.size() - 1] - pos[0] + 1;
    score -= 0.15 * (span - (int)needle.size());

    return score;
}

}
