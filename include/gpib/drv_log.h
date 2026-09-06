/* Append-only text log for the driver and tools (\gpib.log on the device). */
#ifndef GPIB_DRV_LOG_H
#define GPIB_DRV_LOG_H

#include "ce/ce_types.h"

void log_set_path(LPCWSTR path);
void log_enable(BOOL on);
void log_printf(LPCWSTR fmt, ...);
void log_hex(LPCWSTR label, const void *data, UINT32 len);

#endif
