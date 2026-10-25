#ifndef FSM_TEST_IRQ_H_
#define FSM_TEST_IRQ_H_

/* Host tests execute one event at a time; production supplies real IRQ masking. */
static inline unsigned int irq_lock(void)
{
	return 0U;
}

/* No asynchronous host thread can mutate the tested objects. */
static inline void irq_unlock(unsigned int key)
{
	(void)key;
}

#define IRQ_CONNECT(irq, priority, handler, argument, flags) do { } while (0)

#endif
