#include "arm_nnsupportfunctions.h"
#include <stdint.h>

int32_t qopcert_exact_requantize(int32_t value, int32_t multiplier, int32_t shift)
{
  return arm_nn_requantize(value, multiplier, shift);
}

int32_t qopcert_shadow_requantize(int32_t value, int32_t multiplier, int32_t shift)
{
  const uint32_t left = shift > 0 ? (uint32_t)shift : 0U;
  const uint32_t right = shift < 0 ? (uint32_t)(-shift) : 0U;
  const int32_t shifted = (int32_t)((uint32_t)value << left);
  int32_t high;
  __asm volatile("smmulr %0, %1, %2" : "=r"(high) : "r"(shifted), "r"(multiplier));
  return (int32_t)((high * 2) >> right);
}

uint32_t qopcert_infective_commit4(int8_t *output, uint32_t syndrome, uint32_t epoch)
{
  if (syndrome != 0U) {
    output[0] = 0; output[1] = 0; output[2] = 0; output[3] = 0;
    return ~epoch;
  }
  return epoch;
}
