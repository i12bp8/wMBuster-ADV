// Tiny formula evaluator for driver `calculate` expressions.
// Supports: arithmetic/bitwise/comparison/logical operators, parentheses,
// sqrt/pow/min/max/floor/ceil/abs/round, field references (resolver callback),
// unit literals (1m3, 1counter, 1month, 24h, ...) and calendar-correct
// date arithmetic on "YYYY-MM-DD" literals.
// GPL-3.0
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace wmb {

// Value kinds that affect date arithmetic; numerically everything is a double.
enum class FUnit : uint8_t { None, Month, Hour, Day, Date };

struct FVal {
    double v;
    FUnit  unit;     // FUnit::Date => v is days since the unix epoch
    bool   ok;
};

// Resolve an identifier (field key like "total_m3", or context vars
// "storage_counter"/"tariff_counter"/"subunit_counter") to a value.
// Return true when found.
typedef bool (*FResolver)(const char* name, double* out, void* ctx);

// Evaluate expr (NUL-terminated). Returns FVal with ok=false on error.
FVal formula_eval(const char* expr, FResolver resolver, void* ctx);

// Days since unix epoch for a civil date; and back.
long     f_days_from_civil(int y, unsigned m, unsigned d);
void     f_civil_from_days(long z, int* y, unsigned* m, unsigned* d);

} // namespace wmb
