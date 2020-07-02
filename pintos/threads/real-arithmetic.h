#ifndef THREADS_REAL_ARITHMETIC_H
#define THREADS_REAL_ARITHMETIC_H

#include <stdint.h>
#include <stdbool.h>

int int_to_fp (int n);
int fp_to_int (int x, bool round_to_nearest);

int fp_mul (int x, int y);
int fp_div (int x, int y);

#endif /* threads/real-arithmetic.h */
