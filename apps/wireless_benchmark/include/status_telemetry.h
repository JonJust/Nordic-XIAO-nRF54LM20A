#ifndef STATUS_TELEMETRY_H_
#define STATUS_TELEMETRY_H_

#include <stdbool.h>

/* Debugger switch for comparing RF performance with reporting enabled/disabled.
 * An already queued DMA line may finish after this is cleared. No reset needed.
 */
extern volatile bool g_status_telemetry_enabled;

/* Initialize the SAMD11 bridge UART for asynchronous, transmit-only status. */
int status_telemetry_init(void);

/* Report state changes, finished runs, and one-second counters from main only. */
void status_telemetry_process(void);

#endif /* STATUS_TELEMETRY_H_ */
