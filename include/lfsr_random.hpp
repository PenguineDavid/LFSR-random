#pragma once
#ifndef LFSR_RANDOM_HPP
#define LFSR_RANDOM_HPP

// ============================================================================
// LFSR-random - A pesudo rng library.
// Copyright (C) 2026 David S
//
// This library is free software; you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 3 of the License, or (at your
// option) any later version.
//
// This library is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
// for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this library. If not, see <https://www.gnu.org/licenses/lgpl-3.0.html#license-text>.
// ============================================================================

/**
 * lfsr_random.hpp  --  Single-header LFSR-based PRNG library
 *
 * Provides fast, zero-dependency pseudo-random number generation via two
 * complementary backends:
 *
 *   lfsr_engine_32  -- 32-bit Fibonacci LFSR, period 2^32 - 1.
 *                      Fast for small integer and float work.
 *   lfsr_engine_128 -- xoshiro256** (256-bit state, 64-bit output).
 *                      Passes all known statistical tests; period 2^256 - 1.
 *                      Used for all 64-bit, double, and distribution work.
 *                      The class retains the "128" name for API compatibility.
 *
 * Each backend is held in a thread_local instance, so all public API
 * calls are thread-safe without any mutex overhead.
 *
 * Quick start:
 *   int   n = lfsr::Int.uniform(0, 100);
 *   float f = lfsr::Float.uniform(0.f, 1.f);
 *   bool  b = lfsr::Bool.uniform();
 *   lfsr::shuffle(vec.begin(), vec.end());
 *
 * Seeding:
 *   lfsr::seed_32(42);
 *   lfsr::seed_128(hi, lo);
 *   // Engines are auto-seeded from RDTSC + stack address on first use.
 *
 * Requirements: C++17 or later.
 */

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// Portable high-resolution seed source (RDTSC on x86, steady_clock elsewhere)
// ---------------------------------------------------------------------------
#if defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)
inline uint64_t lfsr_rdtsc() noexcept
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
}
#else
#include <chrono>
inline uint64_t lfsr_rdtsc() noexcept
{
    return static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
}
#endif

// ---------------------------------------------------------------------------
// lfsr_engine_32  --  32-bit Fibonacci LFSR
//
// Taps: bits 31, 21, 1, 0  (primitive polynomial x^32 + x^11 + x^2 + x + 1,
// mapped to shift positions 0-based from MSB: 0,10,30,31 -- equivalent).
// Period: 2^32 - 1  (all non-zero 32-bit states).
// ---------------------------------------------------------------------------
class lfsr_engine_32
{
    uint32_t m_state;

    static constexpr uint32_t DEFAULT_SEED = 0x9E3779B9u;

public:
    using result_type = uint32_t;

    explicit lfsr_engine_32(uint32_t seed = DEFAULT_SEED) noexcept
        : m_state(seed ? seed : DEFAULT_SEED) {}

    void seed(uint32_t s) noexcept { m_state = s ? s : DEFAULT_SEED; }

    [[nodiscard]] uint32_t operator()() noexcept
    {
        // Fibonacci (external) LFSR: feedback bit is XOR of tap positions.
        uint32_t bit = ((m_state >> 31) ^ (m_state >> 21) ^
                        (m_state >> 1) ^ (m_state >> 0)) &
                       1u;
        m_state = (m_state << 1) | bit;
        return m_state;
    }

    [[nodiscard]] static constexpr uint64_t period() noexcept
    {
        return (1ULL << 32) - 1;
    }

    [[nodiscard]] uint32_t state() const noexcept { return m_state; }

    // Satisfy UniformRandomBitGenerator concept (for std::shuffle etc.)
    [[nodiscard]] static constexpr uint32_t min() noexcept { return 1u; }
    [[nodiscard]] static constexpr uint32_t max() noexcept { return 0xFFFFFFFFu; }
};

// ---------------------------------------------------------------------------
// lfsr_engine_128  --  xoshiro256** PRNG (256-bit state, 64-bit output)
//
// Replaces the earlier 128-bit Fibonacci LFSR backend.  xoshiro256** has a
// period of 2^256 - 1, passes all known statistical tests, and is faster
// than a Fibonacci LFSR for 64-bit output because it avoids the long shift
// chain.  The public interface and seeding API are identical to the old class.
//
// Reference: Blackman & Vigna, "Scrambled Linear Pseudorandom Number
// Generators", ACM Trans. Math. Softw. 47(4), 2021.
// ---------------------------------------------------------------------------
class lfsr_engine_128
{
    uint64_t m_s[4];

    static constexpr uint64_t DEFAULT_S0 = 0x9E3779B97F4A7C15ULL;
    static constexpr uint64_t DEFAULT_S1 = 0xBF58476D1CE4E5B9ULL;
    static constexpr uint64_t DEFAULT_S2 = 0x94D049BB133111EBULL;
    static constexpr uint64_t DEFAULT_S3 = 0x6C62272E07BB0142ULL;

    static uint64_t rotl(uint64_t x, int k) noexcept
    {
        return (x << k) | (x >> (64 - k));
    }

    // splitmix64 to expand a single seed into full state.
    static uint64_t splitmix64(uint64_t &x) noexcept
    {
        x += 0x9E3779B97F4A7C15ULL;
        uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

public:
    using result_type = uint64_t;

    explicit lfsr_engine_128(uint64_t seed_hi = DEFAULT_S0,
                             uint64_t seed_lo = DEFAULT_S1) noexcept
    {
        seed(seed_hi ? seed_hi : DEFAULT_S0,
             seed_lo ? seed_lo : DEFAULT_S1);
    }

    void seed(uint64_t hi, uint64_t lo) noexcept
    {
        uint64_t x = hi ? hi : DEFAULT_S0;
        m_s[0] = splitmix64(x);
        x = lo ? lo : DEFAULT_S1;
        m_s[1] = splitmix64(x);
        x = m_s[0] ^ m_s[1];
        m_s[2] = splitmix64(x);
        x = m_s[1] ^ m_s[2];
        m_s[3] = splitmix64(x);
        // Guarantee no all-zero state.
        if (!m_s[0] && !m_s[1] && !m_s[2] && !m_s[3])
            m_s[0] = DEFAULT_S0;
    }

    [[nodiscard]] uint64_t operator()() noexcept
    {
        const uint64_t result = rotl(m_s[1] * 5, 7) * 9;
        const uint64_t t = m_s[1] << 17;
        m_s[2] ^= m_s[0];
        m_s[3] ^= m_s[1];
        m_s[1] ^= m_s[2];
        m_s[0] ^= m_s[3];
        m_s[2] ^= t;
        m_s[3] = rotl(m_s[3], 45);
        return result;
    }

    [[nodiscard]] static constexpr uint64_t min() noexcept { return 0; }
    [[nodiscard]] static constexpr uint64_t max() noexcept
    {
        return std::numeric_limits<uint64_t>::max();
    }

    // Expose enough state for diagnostics; hi/lo are the first two words.
    [[nodiscard]] uint64_t state_hi() const noexcept { return m_s[0]; }
    [[nodiscard]] uint64_t state_lo() const noexcept { return m_s[1]; }
};

// ---------------------------------------------------------------------------
// Implementation namespace
// ---------------------------------------------------------------------------
namespace lfsr_impl
{

    // Thread-local engine accessors.  Seeded once per thread from RDTSC + stack.
    inline lfsr_engine_32 &global32() noexcept
    {
        static thread_local lfsr_engine_32 eng = []() noexcept -> lfsr_engine_32
        {
            uint64_t tsc = lfsr_rdtsc();
            volatile int dummy = 0;
            uint32_t s = static_cast<uint32_t>(tsc ^ (tsc >> 32)) ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&dummy));
            return lfsr_engine_32(s ? s : 0x9E3779B9u);
        }();
        return eng;
    }

    inline lfsr_engine_128 &global128() noexcept
    {
        static thread_local lfsr_engine_128 eng = []() noexcept -> lfsr_engine_128
        {
            uint64_t tsc = lfsr_rdtsc();
            volatile int dummy = 0;
            uintptr_t addr = reinterpret_cast<uintptr_t>(&dummy);

            uint64_t hi = tsc ^ (tsc << 17) ^ (tsc >> 13) ^ static_cast<uint64_t>(addr);
            uint64_t lo = (tsc * 0x9E3779B97F4A7C15ULL) ^ (tsc >> 31) ^ (static_cast<uint64_t>(addr) << 32 | addr);

            return lfsr_engine_128(hi ? hi : 0x9E3779B97F4A7C15ULL,
                                   lo ? lo : 0xBF58476D1CE4E5B9ULL);
        }();
        return eng;
    }

    // ---------------------------------------------------------------------------
    // Rejection-sampled uniform integer helpers (no modulo bias).
    // ---------------------------------------------------------------------------

    // 32-bit rejection sampling (handles int and unsigned int).
    inline uint32_t unbiased32(uint32_t range) noexcept
    {
        // range must be > 0.  Returns value in [0, range).
        auto &eng = global32();
        if (range == 0)
            return 0;
        uint32_t threshold = (0u - range) % range; // == (2^32 - range) % range
        uint32_t raw;
        do
        {
            raw = eng();
        } while (raw < threshold);
        return raw % range;
    }

    // 64-bit rejection sampling.
    inline uint64_t unbiased64(uint64_t range) noexcept
    {
        auto &eng = global128();
        if (range == 0)
            return 0;
        uint64_t threshold = (0ULL - range) % range;
        uint64_t raw;
        do
        {
            raw = eng();
        } while (raw < threshold);
        return raw % range;
    }

    // ---------------------------------------------------------------------------
    // Public-facing uniform implementations
    // ---------------------------------------------------------------------------

    [[nodiscard]] inline int uniform_int(int min, int max) noexcept
    {
        if (min >= max)
            return min;
        uint32_t range = static_cast<uint32_t>(max) - static_cast<uint32_t>(min) + 1u;
        return min + static_cast<int>(unbiased32(range));
    }

    [[nodiscard]] inline unsigned int uniform_uint(unsigned int min, unsigned int max) noexcept
    {
        if (min >= max)
            return min;
        uint32_t range = static_cast<uint32_t>(max - min) + 1u;
        return min + static_cast<unsigned int>(unbiased32(range));
    }

    [[nodiscard]] inline int64_t uniform_int64(int64_t min, int64_t max) noexcept
    {
        if (min >= max)
            return min;
        uint64_t range = static_cast<uint64_t>(max) - static_cast<uint64_t>(min) + 1ULL;
        return static_cast<int64_t>(static_cast<uint64_t>(min) + unbiased64(range));
    }

    [[nodiscard]] inline uint64_t uniform_uint64(uint64_t min, uint64_t max) noexcept
    {
        if (min >= max)
            return min;
        uint64_t range = max - min + 1ULL;
        return min + unbiased64(range);
    }

    [[nodiscard]] inline float uniform_float(float min, float max) noexcept
    {
        if (min >= max)
            return min;
        // Map 24 bits into [0, 1) using full mantissa precision for float.
        uint32_t raw = global32()() >> 8; // top 24 bits
        float t = static_cast<float>(raw) * (1.0f / 16777216.0f);
        return min + t * (max - min);
    }

    [[nodiscard]] inline double uniform_double(double min, double max) noexcept
    {
        if (min >= max)
            return min;
        // Map 53 bits into [0, 1) using full mantissa precision for double.
        uint64_t raw = global128()() >> 11; // top 53 bits
        double t = static_cast<double>(raw) * (1.0 / 9007199254740992.0);
        return min + t * (max - min);
    }

    [[nodiscard]] inline char uniform_char(char min, char max) noexcept
    {
        if (min >= max)
            return min;
        return static_cast<char>(uniform_int(static_cast<int>(min),
                                             static_cast<int>(max)));
    }

    [[nodiscard]] inline bool uniform_bool() noexcept
    {
        return (global32()() & 1u) != 0;
    }

    // ---------------------------------------------------------------------------
    // Bernoulli  --  true with probability p in [0, 1]
    // ---------------------------------------------------------------------------
    [[nodiscard]] inline bool bernoulli(double p) noexcept
    {
        if (p <= 0.0)
            return false;
        if (p >= 1.0)
            return true;
        return uniform_double(0.0, 1.0) < p;
    }

    // ---------------------------------------------------------------------------
    // Box-Muller normal distribution (mean, stddev)
    // ---------------------------------------------------------------------------
    [[nodiscard]] inline double normal(double mean, double stddev) noexcept
    {
        // Box-Muller transform.  Caches the spare sample.
        static thread_local bool has_spare = false;
        static thread_local double spare = 0.0;

        if (has_spare)
        {
            has_spare = false;
            return mean + stddev * spare;
        }

        double u, v, s;
        do
        {
            u = uniform_double(-1.0, 1.0);
            v = uniform_double(-1.0, 1.0);
            s = u * u + v * v;
        } while (s >= 1.0 || s == 0.0);

        double mul = std::sqrt(-2.0 * std::log(s) / s);
        spare = v * mul;
        has_spare = true;
        return mean + stddev * (u * mul);
    }

    // ---------------------------------------------------------------------------
    // Exponential distribution  --  mean = 1/lambda
    // ---------------------------------------------------------------------------
    [[nodiscard]] inline double exponential(double lambda) noexcept
    {
        assert(lambda > 0.0);
        double u;
        do
        {
            u = uniform_double(0.0, 1.0);
        } while (u == 0.0);
        return -std::log(u) / lambda;
    }

    // ---------------------------------------------------------------------------
    // Weighted random index  --  weights need not be normalised
    // ---------------------------------------------------------------------------
    [[nodiscard]] inline std::size_t weighted_index(const std::vector<double> &weights)
    {
        if (weights.empty())
            throw std::invalid_argument("lfsr::weighted_index: empty weight vector");

        double total = 0.0;
        for (double w : weights)
        {
            assert(w >= 0.0 && "lfsr::weighted_index: negative weight");
            total += w;
        }
        assert(total > 0.0 && "lfsr::weighted_index: all weights are zero");

        double r = uniform_double(0.0, total);
        double cumulative = 0.0;
        for (std::size_t i = 0; i < weights.size(); ++i)
        {
            cumulative += weights[i];
            if (r < cumulative)
                return i;
        }
        return weights.size() - 1; // rounding fallback
    }

    // ---------------------------------------------------------------------------
    // Seeding helpers
    // ---------------------------------------------------------------------------
    inline void seed32(uint32_t s) noexcept { global32().seed(s); }
    inline void seed128(uint64_t hi, uint64_t lo) noexcept { global128().seed(hi, lo); }

} // namespace lfsr_impl

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * lfsr  --  Namespace-style class providing all distributions as nested types
 *           with static methods, plus instance objects for clean call syntax.
 *
 * Thread safety: every call is independent per-thread (thread_local engines).
 * There are no shared mutable globals.
 */
class lfsr
{
public:
    lfsr() = delete;

    // -----------------------------------------------------------------------
    // 32-bit backed distributions
    // -----------------------------------------------------------------------

    struct Int
    {
        /** Uniform integer in [min, max] (inclusive both ends). */
        [[nodiscard]] static int uniform(int min, int max) noexcept
        {
            return lfsr_impl::uniform_int(min, max);
        }

        /** Current RDTSC value cast to int, useful for manual seeding. */
        [[nodiscard]] static int sysTimeSeed() noexcept
        {
            return static_cast<int>(lfsr_rdtsc() & 0x7FFFFFFFu);
        }
    };

    struct Uint
    {
        [[nodiscard]] static unsigned int uniform(unsigned int min, unsigned int max) noexcept
        {
            return lfsr_impl::uniform_uint(min, max);
        }

        [[nodiscard]] static unsigned int sysTimeSeed() noexcept
        {
            return static_cast<unsigned int>(lfsr_rdtsc());
        }
    };

    struct Float
    {
        /** Uniform float in [min, max).  24-bit mantissa precision. */
        [[nodiscard]] static float uniform(float min, float max) noexcept
        {
            return lfsr_impl::uniform_float(min, max);
        }
    };

    struct Char
    {
        [[nodiscard]] static char uniform(char min, char max) noexcept
        {
            return lfsr_impl::uniform_char(min, max);
        }
    };

    struct Bool
    {
        /** Unbiased coin flip. */
        [[nodiscard]] static bool uniform() noexcept
        {
            return lfsr_impl::uniform_bool();
        }

        /** True with probability p in [0.0, 1.0]. */
        [[nodiscard]] static bool bernoulli(double p) noexcept
        {
            return lfsr_impl::bernoulli(p);
        }
    };

    // -----------------------------------------------------------------------
    // 128-bit backed distributions
    // -----------------------------------------------------------------------

    struct Int64
    {
        [[nodiscard]] static int64_t uniform(int64_t min, int64_t max) noexcept
        {
            return lfsr_impl::uniform_int64(min, max);
        }
    };

    struct Uint64
    {
        [[nodiscard]] static uint64_t uniform(uint64_t min, uint64_t max) noexcept
        {
            return lfsr_impl::uniform_uint64(min, max);
        }

        [[nodiscard]] static uint64_t sysTimeSeed() noexcept
        {
            return lfsr_rdtsc();
        }
    };

    struct Double
    {
        /** Uniform double in [min, max).  53-bit mantissa precision. */
        [[nodiscard]] static double uniform(double min, double max) noexcept
        {
            return lfsr_impl::uniform_double(min, max);
        }

        /** Normal (Gaussian) distribution.  Box-Muller transform. */
        [[nodiscard]] static double normal(double mean = 0.0, double stddev = 1.0) noexcept
        {
            return lfsr_impl::normal(mean, stddev);
        }

        /** Exponential distribution with rate parameter lambda > 0. */
        [[nodiscard]] static double exponential(double lambda = 1.0) noexcept
        {
            return lfsr_impl::exponential(lambda);
        }
    };

    // -----------------------------------------------------------------------
    // Container utilities
    // -----------------------------------------------------------------------

    /**
     * Fisher-Yates shuffle over [first, last).
     * Works on any RandomAccessIterator.
     */
    template <typename RandomIt>
    static void shuffle(RandomIt first, RandomIt last) noexcept
    {
        using diff_t = typename std::iterator_traits<RandomIt>::difference_type;
        diff_t n = last - first;
        for (diff_t i = n - 1; i > 0; --i)
        {
            diff_t j = static_cast<diff_t>(
                lfsr_impl::unbiased64(static_cast<uint64_t>(i) + 1ULL));
            if (i != j)
                std::iter_swap(first + i, first + j);
        }
    }

    /**
     * Pick a uniformly random element from [first, last).
     * Returns last if the range is empty.
     */
    template <typename RandomIt>
    [[nodiscard]] static RandomIt sample(RandomIt first, RandomIt last) noexcept
    {
        if (first == last)
            return last;
        using diff_t = typename std::iterator_traits<RandomIt>::difference_type;
        diff_t n = last - first;
        diff_t i = static_cast<diff_t>(
            lfsr_impl::unbiased64(static_cast<uint64_t>(n)));
        return first + i;
    }

    /**
     * Weighted random pick from a vector of (value, weight) pairs.
     * Weights must be non-negative; at least one must be > 0.
     *
     * Example:
     *   auto v = lfsr::weighted_pick<std::string>({{"rare", 1.0}, {"common", 9.0}});
     */
    template <typename T>
    [[nodiscard]] static T weighted_pick(const std::vector<std::pair<T, double>> &table)
    {
        if (table.empty())
            throw std::invalid_argument("lfsr::weighted_pick: empty table");
        std::vector<double> weights;
        weights.reserve(table.size());
        for (const auto &[val, w] : table)
            weights.push_back(w);
        return table[lfsr_impl::weighted_index(weights)].first;
    }

    /**
     * Weighted random index from a flat weight vector.
     * Weights must be non-negative; at least one must be > 0.
     */
    [[nodiscard]] static std::size_t weighted_index(const std::vector<double> &weights)
    {
        return lfsr_impl::weighted_index(weights);
    }

    // -----------------------------------------------------------------------
    // Raw engine access  --  for custom distributions or engine inspection
    // -----------------------------------------------------------------------

    /** Direct access to the thread-local 32-bit engine. */
    [[nodiscard]] static lfsr_engine_32 &engine32() noexcept
    {
        return lfsr_impl::global32();
    }

    /** Direct access to the thread-local 128-bit engine. */
    [[nodiscard]] static lfsr_engine_128 &engine128() noexcept
    {
        return lfsr_impl::global128();
    }

    // -----------------------------------------------------------------------
    // Seeding
    // -----------------------------------------------------------------------

    /** Re-seed the 32-bit engine for this thread. */
    static void seed_32(uint32_t s) noexcept { lfsr_impl::seed32(s); }

    /** Re-seed the 128-bit engine for this thread. */
    static void seed_128(uint64_t hi, uint64_t lo) noexcept
    {
        lfsr_impl::seed128(hi, lo);
    }

    // -----------------------------------------------------------------------
    // Instances for clean call syntax  (e.g. lfsr::Int.uniform(0, 10))
    // The instance names use lowercase to avoid colliding with the type names.
    // -----------------------------------------------------------------------
    inline static const lfsr::Int Int{};
    inline static const lfsr::Uint Uint{};
    inline static const lfsr::Float Float{};
    inline static const lfsr::Char Char{};
    inline static const lfsr::Bool Bool{};
    inline static const lfsr::Int64 Int64{};
    inline static const lfsr::Uint64 Uint64{};
    inline static const lfsr::Double Double{};
};

#endif // LFSR_RANDOM_HPP
