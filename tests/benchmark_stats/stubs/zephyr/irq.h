#ifndef TEST_ZEPHYR_IRQ_H_
#define TEST_ZEPHYR_IRQ_H_

#include <assert.h>

/* Test single-CPU critical-section pairing without depending on Zephyr. */
static inline unsigned int irq_lock(void)
{
	return 1U;
}

/* Every production early-return path must release the key it acquired. */
static inline void irq_unlock(unsigned int key)
{
	assert(key == 1U);
}

#endif
