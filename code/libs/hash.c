#include <stdlib.h>

/* 2^31 + 2^29 - 2^25 + 2^22 - 2^19 - 2^16 + 1 */
#define GOLDEN_RATIO_PRIME_32       0x9e370001UL

/* *
 * hash32 - generate a hash value in the range [0, 2^@bits - 1]
 * @val:    the input value
 * @bits:   the number of bits in a return value
 *
 * High bits are more random, so we use them.
 * */
/*
 * 作用：把 32 位输入换算为 bits 位的哈希值。
 * 实现：用固定常数作乘法，取 32 位乘积的高 bits 位；PID 查找取其中 10 位作桶下标。
 */
uint32_t
hash32(uint32_t val, unsigned int bits) {
    // 乘法结果截断为 32 位，再取高 bits 位；PID 查找取 10 位作桶下标。
    uint32_t hash = val * GOLDEN_RATIO_PRIME_32;
    return (hash >> (32 - bits));
}

