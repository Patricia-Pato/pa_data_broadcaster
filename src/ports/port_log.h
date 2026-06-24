#ifndef PORT_LOG_H
#define PORT_LOG_H

/*
 * Platform-agnostic logging interface.
 *
 * Each platform provides its own implementation by defining
 * PA_LOG_PLATFORM_HEADER before including this file, or by
 * providing a port_log_platform.h in the include path.
 *
 * If no platform header is found, logs are silently discarded.
 */

#ifdef PA_LOG_PLATFORM_HEADER
#include PA_LOG_PLATFORM_HEADER
#endif

#ifndef PA_LOG_ERR
#define PA_LOG_ERR(...)
#endif

#ifndef PA_LOG_WRN
#define PA_LOG_WRN(...)
#endif

#ifndef PA_LOG_INF
#define PA_LOG_INF(...)
#endif

#ifndef PA_LOG_DBG
#define PA_LOG_DBG(...)
#endif

/* Module registration (no-op by default) */
#ifndef PA_LOG_MODULE_REGISTER
#define PA_LOG_MODULE_REGISTER(name)
#endif

#endif /* PORT_LOG_H */
