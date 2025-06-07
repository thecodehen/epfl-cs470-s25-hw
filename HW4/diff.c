#include <stdint.h>
#include <stdio.h>
#include <x86intrin.h>

int variable_to_flush = 100;

int main() {
  _mm_clflush(&variable_to_flush);

  for (volatile int i = 0; i < 1000; i++); // wait for clflush to commit

  _mm_mfence(); // memory fence to ensure all previous operations are complete

  volatile unsigned int junk = 0;
  uint64_t t0 = __rdtscp(&junk);
  variable_to_flush++;
  uint64_t delta = __rdtscp(&junk) - t0;

  printf("Time taken to access variable_to_flush: %lu cycles\n", delta);

  t0 = __rdtscp(&junk);
  variable_to_flush++;
  delta = __rdtscp(&junk) - t0;

  printf("Time taken to access variable_to_flush again: %lu cycles\n", delta);
  return 0;
}