#ifndef PORT_OS_H
#define PORT_OS_H

#include <stdint.h>
#include <stddef.h>

/*
 * Platform-agnostic OS primitives.
 *
 * Provides abstractions for:
 * - Deferred work (ISR-safe callback scheduling)
 * - Critical sections (interrupt disable/enable)
 * - Sleep
 */

/* ===== Deferred Work ===== */

typedef void (*pa_work_handler_t)(void *user_data);

struct pa_port_work {
	int (*init)(void *ctx, pa_work_handler_t handler, void *user_data);
	int (*submit)(void *ctx);
	void *ctx;
};

/* ===== Critical Section ===== */

struct pa_port_critical {
	uint32_t (*enter)(void *ctx);
	void (*exit)(void *ctx, uint32_t key);
	void *ctx;
};

/* ===== Sleep ===== */

struct pa_port_sleep {
	void (*ms)(void *ctx, uint32_t milliseconds);
	void *ctx;
};

#endif /* PORT_OS_H */
