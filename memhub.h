#ifndef __MEMHUB_H
#define __MEMHUB_H

#include <stdint.h>
#include <libmemsvc.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * This library is a thin wrapper around libmemsvc, which adds semaphores to synchronize concurrent read/write operations from different processes.
 */
int memhub_open(memsvc_handle_t *handle);
int memhub_close(memsvc_handle_t *handle);

/* These functions return -1 on error and 0 on success.
 *
 * On error, the error message will be available via memsvc_get_last_error()
 */
int memhub_read(memsvc_handle_t handle, uint32_t addr, uint32_t words, uint32_t *data);
int memhub_write(memsvc_handle_t handle, uint32_t addr, uint32_t words, const uint32_t *data);
void die(int signo);

#ifdef __cplusplus
}
#endif

#endif
