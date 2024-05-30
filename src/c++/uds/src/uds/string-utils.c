// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "string-utils.h"

#if !defined(__KERNEL__) || defined(TEST_INTERNAL)
#include "errors.h"
#include "logger.h"
#include "memory-alloc.h"

int vdo_alloc_sprintf(const char *what, char **strp, const char *fmt, ...)
{
	va_list args;
	int result;
	int count;

	if (strp == NULL)
		return UDS_INVALID_ARGUMENT;

	va_start(args, fmt);
	count = vsnprintf(NULL, 0, fmt, args) + 1;
	va_end(args);
	result = vdo_allocate(count, char, what, strp);
	if (result == VDO_SUCCESS) {
		va_start(args, fmt);
		vsnprintf(*strp, count, fmt, args);
		va_end(args);
	}

	if ((result != VDO_SUCCESS) && (what != NULL))
		vdo_log_error("cannot allocate %s", what);

	return result;
}

#endif /* (not __KERNEL) or TEST_INTERNAL */
#ifdef TEST_INTERNAL
int vdo_fixed_sprintf(char *buf, size_t buf_size, const char *fmt, ...)
{
	va_list args;
	int n;

	if (buf == NULL)
		return UDS_INVALID_ARGUMENT;

	va_start(args, fmt);
	n = vsnprintf(buf, buf_size, fmt, args);
	va_end(args);

	if (n < 0) {
		return vdo_log_error_strerror(UDS_UNKNOWN_ERROR, "%s: vsnprintf failed",
					      __func__);
	}

	if ((size_t) n >= buf_size) {
		return vdo_log_error_strerror(UDS_INVALID_ARGUMENT,
					      "%s: string too long", __func__);
	}
        
	return UDS_SUCCESS;
}

#endif /* TEST_INTERNAL */
char *vdo_append_to_buffer(char *buffer, char *buf_end, const char *fmt, ...)
{
	va_list args;
	size_t n;

	va_start(args, fmt);
	n = vsnprintf(buffer, buf_end - buffer, fmt, args);
	if (n >= (size_t) (buf_end - buffer))
		buffer = buf_end;
	else
		buffer += n;
	va_end(args);

	return buffer;
}
