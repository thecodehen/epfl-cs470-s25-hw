#include "kernel5.h"

#define PARTITION_FACTOR 8
float kernel5(float bound, float a[ARRAY_SIZE], float b[ARRAY_SIZE])
{
    bool done = false;
#pragma HLS ARRAY_PARTITION variable=a type=cyclic factor=PARTITION_FACTOR
#pragma HLS ARRAY_PARTITION variable=b type=cyclic factor=PARTITION_FACTOR
    float sum;
    for (int i=0; i < ARRAY_SIZE; ++i) {
#pragma HLS PIPELINE
#pragma HLS UNROLL factor=PARTITION_FACTOR
        if (done || (sum = a[i] + b[i]) >= bound) {
            done = true;
        }
    }
    return sum;
}
