/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef VDO_STRING_UTILS_H
#define VDO_STRING_UTILS_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <linux/compiler.h>
#include <linux/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#endif

/* Utilities related to string manipulation */

static inline const char *vdo_bool_to_string(bool value)
{
	return value ? "true" : "false";
}

#if !defined(__KERNEL__) || defined(TEST_INTERNAL)
/*
 * Allocate memory to contain a formatted string. The caller is responsible for
 * freeing the allocated memory.
 */
int __must_check vdo_alloc_sprintf(const char *what, char **strp, const char *fmt, ...)
	__printf(3, 4);

#endif /* (! __KERNEL) or TEST_INTERNAL */
#ifdef TEST_INTERNAL
/* Format a string into a fixed-size buffer, similar to snprintf. */
int __must_check vdo_fixed_sprintf(char *buf, size_t buf_size, const char *fmt, ...)
	__printf(3, 4);

#endif /* TEST_INTERNAL */
/* Append a formatted string to the end of a buffer. */
char *vdo_append_to_buffer(char *buffer, char *buf_end, const char *fmt, ...)
	__printf(3, 4);

#endif /* VDO_STRING_UTILS_H */
