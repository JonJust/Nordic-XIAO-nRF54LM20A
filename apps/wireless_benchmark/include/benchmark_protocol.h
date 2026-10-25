#ifndef BENCHMARK_PROTOCOL_H
#define BENCHMARK_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/sys/byteorder.h>

#define BENCHMARK_PACKET_COUNT 1000000U
#define BENCHMARK_PERIOD_US 125U
#define BENCHMARK_DATA_SIZE 16U
#define BENCHMARK_CONTROL_SIZE 24U
#define BENCHMARK_PROTOCOL_VERSION 1U

enum benchmark_packet_kind {
	PACKET_HELLO = 1,
	PACKET_READY,
	PACKET_START,
	PACKET_ARMED,
	PACKET_FINISH,
	PACKET_QUERY,
	PACKET_SUMMARY,
	PACKET_MOUSE = 0x80,
};

/* This is an in-memory representation, not an over-the-air C struct. Explicit
 * little-endian encoding avoids padding, alignment, and compiler ABI surprises.
 */
struct benchmark_control {
	enum benchmark_packet_kind kind;
	uint32_t run_id;
	uint32_t received;
	uint32_t missing;
	bool completed;
};

/* Encode the fixed test contract as well as the control message's variable data. */
static inline void benchmark_control_encode(uint8_t *data,
					    const struct benchmark_control *message)
{
	data[0] = message->kind;
	data[1] = BENCHMARK_PROTOCOL_VERSION;
	sys_put_le32(message->run_id, &data[2]);
	sys_put_le32(BENCHMARK_PACKET_COUNT, &data[6]);
	sys_put_le16(BENCHMARK_PERIOD_US, &data[10]);
	data[12] = BENCHMARK_DATA_SIZE;
	data[13] = 4; /* 4 Mbit/s proprietary PHY, not Bluetooth. */
	data[14] = 1; /* Motion packets deliberately request no ACK. */
	sys_put_le32(message->received, &data[15]);
	sys_put_le32(message->missing, &data[19]);
	data[23] = message->completed;
}

/* Reject peers using a different contract before accepting a run or its result. */
static inline bool benchmark_control_decode(const uint8_t *data, size_t length,
					    struct benchmark_control *message)
{
	if (length != BENCHMARK_CONTROL_SIZE || data[0] < PACKET_HELLO ||
	    data[0] > PACKET_SUMMARY || data[1] != BENCHMARK_PROTOCOL_VERSION ||
	    sys_get_le32(&data[6]) != BENCHMARK_PACKET_COUNT ||
	    sys_get_le16(&data[10]) != BENCHMARK_PERIOD_US ||
	    data[12] != BENCHMARK_DATA_SIZE || data[13] != 4 || data[14] != 1 ||
	    data[23] > 1) {
		return false;
	}
	message->kind = data[0];
	message->run_id = sys_get_le32(&data[2]);
	message->received = sys_get_le32(&data[15]);
	message->missing = sys_get_le32(&data[19]);
	message->completed = data[23] != 0;
	return true;
}

/* Create a reproducible spoof mouse report with a 32-bit run and slot sequence.
 * Bytes 9..15 match the seven mouse data bytes in your HID descriptor. The ESB
 * radio appends CRC16 separately; no extra application CRC is needed here.
 */
static inline void benchmark_mouse_encode(uint8_t *data, uint32_t run_id,
					 uint32_t sequence)
{
	data[0] = PACKET_MOUSE;
	sys_put_le32(run_id, &data[1]);
	sys_put_le32(sequence, &data[5]);
	data[9] = (sequence / 8000U) & 0x1fU; /* Five fake buttons. */
	sys_put_le16((uint16_t)(int16_t)((int32_t)(sequence % 201U) - 100), &data[10]);
	sys_put_le16((uint16_t)(int16_t)(100 - (int32_t)(sequence % 201U)), &data[12]);
	data[14] = 0; /* Wheel. */
	data[15] = 0; /* Horizontal pan. */
}

#endif
