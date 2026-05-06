# lfsr_random

Single-header C++17 PRNG library. Drop one file in, include it, done. No dependencies beyond the standard library.

## Installation

Copy `lfsr_random.hpp` into your project. The recommended layout is:

```
your_project/
    include/
        lfsr_random.hpp
    src/
        main.cpp
```

Then include it with:

```cpp
#include "lfsr_random.hpp"
```

If you use the recommended layout, pass `-I include` to the compiler so the path resolves:

```bash
g++ -std=c++17 -O2 -I include src/main.cpp -o myapp
```

**Requirements:** C++17 or later. No third-party dependencies.

---

## Backends

Two engines run under the hood. You never need to pick one manually -- the API routes to the right one based on the type you ask for.

| Engine | Class | State | Period | Used for |
|---|---|---|---|---|
| Fibonacci LFSR | `lfsr_engine_32` | 32-bit | 2^32 - 1 | `Int`, `Uint`, `Float`, `Char`, `Bool` |
| xoshiro256** | `lfsr_engine_128` | 256-bit | 2^256 - 1 | `Int64`, `Uint64`, `Double`, all distributions |

Both engines are held in `thread_local` storage. All calls are thread-safe with no mutex overhead. Each thread gets its own independently auto-seeded engine on first use.

---

## Quick Start

```cpp
#include "lfsr_random.hpp"

int   n = lfsr::Int.uniform(0, 100);        // [0, 100] inclusive
float f = lfsr::Float.uniform(0.f, 1.f);    // [0, 1)
bool  b = lfsr::Bool.uniform();             // 50/50 coin flip
char  c = lfsr::Char.uniform('a', 'z');     // random lowercase letter

std::vector<int> v = {1, 2, 3, 4, 5};
lfsr::shuffle(v.begin(), v.end());

auto it = lfsr::sample(v.begin(), v.end()); // pointer to a random element
```

---

## API Reference

### lfsr::Int

Backed by the 32-bit LFSR engine.

```cpp
int lfsr::Int.uniform(int min, int max)
```

Returns a uniformly distributed `int` in `[min, max]` (both endpoints inclusive). If `min >= max`, returns `min`.

```cpp
int lfsr::Int.sysTimeSeed()
```

Returns the current RDTSC counter cast to a positive `int`. Useful as a seed value.

```cpp
// Examples
int d6    = lfsr::Int.uniform(1, 6);
int index = lfsr::Int.uniform(0, vec.size() - 1);
```

---

### lfsr::Uint

Backed by the 32-bit LFSR engine.

```cpp
unsigned int lfsr::Uint.uniform(unsigned int min, unsigned int max)
unsigned int lfsr::Uint.sysTimeSeed()
```

Same semantics as `Int` but for `unsigned int`.

---

### lfsr::Int64

Backed by the xoshiro256** engine.

```cpp
int64_t lfsr::Int64.uniform(int64_t min, int64_t max)
```

Uniform `int64_t` in `[min, max]`. Handles the full signed 64-bit range including ranges that span the sign boundary.

```cpp
// Examples
int64_t big = lfsr::Int64.uniform(INT64_MIN, INT64_MAX);
int64_t ts  = lfsr::Int64.uniform(0, 1'000'000'000LL);
```

---

### lfsr::Uint64

Backed by the xoshiro256** engine.

```cpp
uint64_t lfsr::Uint64.uniform(uint64_t min, uint64_t max)
uint64_t lfsr::Uint64.sysTimeSeed()
```

```cpp
// Examples
uint64_t id  = lfsr::Uint64.uniform(0ULL, UINT64_MAX);
uint64_t off = lfsr::Uint64.uniform(0ULL, file_size - 1);
```

---

### lfsr::Float

Backed by the 32-bit LFSR engine. 24-bit mantissa precision.

```cpp
float lfsr::Float.uniform(float min, float max)
```

Returns a uniform `float` in `[min, max)`. The upper bound is exclusive.

```cpp
float t     = lfsr::Float.uniform(0.f, 1.f);
float angle = lfsr::Float.uniform(0.f, 6.2831853f);
```

---

### lfsr::Double

Backed by the xoshiro256** engine. 53-bit mantissa precision.

```cpp
double lfsr::Double.uniform(double min, double max)
```

Uniform `double` in `[min, max)`.

```cpp
double lfsr::Double.normal(double mean = 0.0, double stddev = 1.0)
```

Normal (Gaussian) distribution. Implemented with a Box-Muller transform. The spare sample is cached per-thread so every two calls to the underlying engine produce two output samples.

```cpp
double lfsr::Double.exponential(double lambda = 1.0)
```

Exponential distribution with rate parameter `lambda`. Mean of the distribution is `1 / lambda`. `lambda` must be greater than zero.

```cpp
// Examples
double u    = lfsr::Double.uniform(0.0, 1.0);
double roll = lfsr::Double.normal(100.0, 15.0); // IQ-style distribution
double wait = lfsr::Double.exponential(2.0);    // mean = 0.5
```

---

### lfsr::Bool

Backed by the 32-bit LFSR engine.

```cpp
bool lfsr::Bool.uniform()
```

Unbiased coin flip.

```cpp
bool lfsr::Bool.bernoulli(double p)
```

Returns `true` with probability `p`. `p = 0.0` always returns `false`, `p = 1.0` always returns `true`.

```cpp
if (lfsr::Bool.bernoulli(0.05)) trigger_rare_event();
```

---

### lfsr::Char

Backed by the 32-bit LFSR engine.

```cpp
char lfsr::Char.uniform(char min, char max)
```

Uniform character in `[min, max]`.

```cpp
char lower = lfsr::Char.uniform('a', 'z');
char digit = lfsr::Char.uniform('0', '9');
```

---

### lfsr::shuffle

```cpp
template <typename RandomIt>
void lfsr::shuffle(RandomIt first, RandomIt last)
```

Fisher-Yates in-place shuffle over `[first, last)`. Works on any `RandomAccessIterator` -- `std::vector`, raw arrays, anything with pointer arithmetic. Empty and single-element ranges are no-ops.

```cpp
std::vector<int> deck(52);
std::iota(deck.begin(), deck.end(), 0);
lfsr::shuffle(deck.begin(), deck.end());

int arr[] = {1, 2, 3, 4, 5};
lfsr::shuffle(std::begin(arr), std::end(arr));
```

---

### lfsr::sample

```cpp
template <typename RandomIt>
RandomIt lfsr::sample(RandomIt first, RandomIt last)
```

Returns an iterator to a uniformly random element in `[first, last)`. Returns `last` if the range is empty, so always check before dereferencing.

```cpp
auto& winner = *lfsr::sample(players.begin(), players.end());

auto it = lfsr::sample(v.begin(), v.end());
if (it != v.end()) use(*it);
```

---

### lfsr::weighted_pick

```cpp
template <typename T>
T lfsr::weighted_pick(const std::vector<std::pair<T, double>>& table)
```

Picks a value from a table of `{value, weight}` pairs. Weights are relative -- they do not need to sum to 1. All weights must be non-negative and at least one must be greater than zero. Throws `std::invalid_argument` if the table is empty.

```cpp
using P = std::pair<std::string, double>;
auto loot = lfsr::weighted_pick<std::string>({
    P{"common sword",  70.0},
    P{"rare shield",   25.0},
    P{"epic axe",       4.5},
    P{"legendary bow",  0.5},
});
```

---

### lfsr::weighted_index

```cpp
std::size_t lfsr::weighted_index(const std::vector<double>& weights)
```

Same as `weighted_pick` but returns the index into the weight vector rather than a value. Use this when your items are stored separately from the weights.

```cpp
std::vector<double> spawn_weights = {10.0, 5.0, 1.0};
std::size_t enemy_type = lfsr::weighted_index(spawn_weights);
```

---

## Seeding

Engines are auto-seeded from RDTSC (or `std::chrono::steady_clock` on non-x86) mixed with a stack address on first use. You only need to seed manually if you want reproducible output.

```cpp
// Seed the 32-bit engine (affects Int, Uint, Float, Char, Bool)
lfsr::seed_32(42u);

// Seed the 128-bit engine (affects Int64, Uint64, Double, distributions)
lfsr::seed_128(0xDEADBEEFu, 0xCAFEBABEu);
```

Seeding is per-thread. Calling `seed_32` on thread A does not affect thread B's engine.

For reproducible tests:

```cpp
int main() {
    lfsr::seed_32(12345u);
    lfsr::seed_128(12345u, 67890u);
    // all subsequent calls are deterministic on this thread
}
```

---

## Raw Engine Access

If you need to drive a custom distribution or inspect engine state directly:

```cpp
lfsr_engine_32&  e32  = lfsr::engine32();
lfsr_engine_128& e128 = lfsr::engine128();

uint32_t raw32 = e32();
uint64_t raw64 = e128();

// Both satisfy UniformRandomBitGenerator so they work with std:: algorithms
std::shuffle(v.begin(), v.end(), lfsr::engine32());
std::generate(v.begin(), v.end(), lfsr::engine32());
```

You can also construct and seed engine instances directly without touching the global thread-local state:

```cpp
lfsr_engine_32  local32(0xABCDu);
lfsr_engine_128 local128(0x1234u, 0x5678u);

for (int i = 0; i < 100; ++i)
    process(local32());
```

---

## Thread Safety

Every thread has its own engine instances via `thread_local`. There are no shared mutable globals and no locks. Calls from different threads never interfere.

```cpp
// Safe -- each thread uses its own engine
std::vector<std::thread> workers;
for (int i = 0; i < 8; ++i)
    workers.emplace_back([]() {
        int v = lfsr::Int.uniform(0, 100); // independent from all other threads
    });
```

Note that `seed_32` and `seed_128` only affect the calling thread's engine.

---

## Compile and Test

The test suite is in `tests/test_lfsr_random.cpp`. From the project root:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -pthread -I include tests/test_lfsr_random.cpp -o test_lfsr
./test_lfsr
```

Expected output ends with `52 / 52 tests passed.`

---

## Notes on Uniformity

All integer samplers use rejection sampling to eliminate modulo bias. The 32-bit sampler uses the Lemire fast-range method with a rejection loop; the 64-bit sampler mirrors the same approach. Floating-point outputs use the top 24 bits (float) or top 53 bits (double) of the raw engine output, mapping directly to the mantissa width of each type.
