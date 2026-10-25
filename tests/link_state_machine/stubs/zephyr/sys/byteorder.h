#ifndef FSM_TEST_BYTEORDER_H_
#define FSM_TEST_BYTEORDER_H_

#include <stdint.h>

/* Decode bytes explicitly so production protocol checks run unchanged. */
static inline uint32_t sys_get_le32(const uint8_t *bytes)
{
	return bytes[0] | ((uint32_t)bytes[1] << 8U) |
		((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

/* Decode the little-endian contract's 16-bit period field. */
static inline uint16_t sys_get_le16(const uint8_t *bytes)
{
	return bytes[0] | ((uint16_t)bytes[1] << 8U);
}

/* Encode production packet IDs, sequence numbers, and contract counts. */
static inline void sys_put_le32(uint32_t value, uint8_t *bytes)
{
	for (unsigned int index = 0U; index < 4U; index++) {
		bytes[index] = value >> (index * 8U);
	}
}

/* Encode production report coordinates and period fields. */
static inline void sys_put_le16(uint16_t value, uint8_t *bytes)
{
	bytes[0] = value;
	bytes[1] = value >> 8U;
}

#endif
