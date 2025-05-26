#include "kernel5.h"

#define PARTITION_FACTOR 8
float kernel5(float bound, float a[ARRAY_SIZE], float b[ARRAY_SIZE])
{
    float sums[ARRAY_SIZE];
#pragma HLS ARRAY_PARTITION variable=a type=cyclic factor=PARTITION_FACTOR
#pragma HLS ARRAY_PARTITION variable=b type=cyclic factor=PARTITION_FACTOR
#pragma HLS ARRAY_PARTITION variable=sums type=cyclic factor=PARTITION_FACTOR
    for (int i=0; i < ARRAY_SIZE; ++i) {
#pragma HLS PIPELINE II=1
#pragma HLS UNROLL factor=PARTITION_FACTOR
        sums[i] = a[i] + b[i];
    }
    float sum = 0;

    int j = 0;
    bool done = false;
    for (int i = 0; i < ARRAY_SIZE; i++) {
#pragma HLS PIPELINE
#pragma HLS UNROLL factor=PARTITION_FACTOR
        if (!done && sums[i] < bound) {
            ++j;
        } else {
            done = true;
        }
    }
    return sums[j];
}
