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
    for (int i = 0; i < ARRAY_SIZE; i++) {
#pragma HLS PIPELINE II=2
#pragma HLS DEPENDENCE variable=sum type=inter direction=WAW dependent=false
        sum = sums[i];
        if (sum >= bound) {
            break;
        }
    }
    return sum;
}