#ifndef FSM_TEST_KERNEL_H_
#define FSM_TEST_KERNEL_H_

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ARG_UNUSED(value) (void)(value)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define IS_ENABLED(value) (value)
#define K_NO_WAIT 0

struct k_msgq {
	uint8_t *data;
	size_t item_size;
	size_t capacity;
	size_t head;
	size_t count;
};

#define K_MSGQ_DEFINE(name, item_bytes, entries, alignment) \
	static uint8_t name##_storage[(item_bytes) * (entries)]; \
	static struct k_msgq name = { name##_storage, item_bytes, entries, 0, 0 }

/* A bounded FIFO preserves the production control-queue behavior. */
static inline int k_msgq_put(struct k_msgq *queue, const void *item, int timeout)
{
	size_t slot;

	(void)timeout;
	if (queue->count == queue->capacity) {
		return -ENOMSG;
	}
	slot = (queue->head + queue->count) % queue->capacity;
	memcpy(queue->data + slot * queue->item_size, item, queue->item_size);
	queue->count++;
	return 0;
}

/* Pop one control item without sleeping, matching K_NO_WAIT use in the FSM. */
static inline int k_msgq_get(struct k_msgq *queue, void *item, int timeout)
{
	(void)timeout;
	if (queue->count == 0U) {
		return -ENOMSG;
	}
	memcpy(item, queue->data + queue->head * queue->item_size, queue->item_size);
	queue->head = (queue->head + 1U) % queue->capacity;
	queue->count--;
	return 0;
}

uint64_t k_cycle_get_64(void);

/* The board's GRTC and this deterministic clock both tick once per microsecond. */
static inline uint64_t k_cyc_to_us_floor64(uint64_t cycles)
{
	return cycles;
}

#endif
