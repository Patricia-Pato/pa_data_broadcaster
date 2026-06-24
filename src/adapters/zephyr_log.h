#ifndef ZEPHYR_LOG_H
#define ZEPHYR_LOG_H

/*
 * Zephyr implementation of port_log.h macros.
 * Include this header BEFORE port_log.h, or define
 * PA_LOG_PLATFORM_HEADER="zephyr_log.h" at build time.
 */

#include <zephyr/logging/log.h>

#define PA_LOG_MODULE_REGISTER(name) LOG_MODULE_REGISTER(name, CONFIG_MAIN_LOG_LEVEL)
#define PA_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define PA_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define PA_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define PA_LOG_DBG(...) LOG_DBG(__VA_ARGS__)

#endif /* ZEPHYR_LOG_H */
