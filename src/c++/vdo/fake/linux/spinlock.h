/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Unit test requirements from linux/spinlock.h.
 *
 * Copyright 2023 Red Hat
 *
 */

#ifndef LINUX_SPINLOCK_H
#define LINUX_SPINLOCK_H

#include <linux/list.h>

#include "thread-utils.h"

typedef struct mutex spinlock_t;

#define DEFINE_SPINLOCK(x) spinlock_t x = UDS_MUTEX_INITIALIZER

#define spin_lock_init(lock)                                 \
	VDO_ASSERT_LOG_ONLY(uds_init_mutex(lock) == UDS_SUCCESS, \
			    "spinlock init succeeds")
#define spin_lock(lock) uds_lock_mutex(lock)
#define spin_unlock(lock) uds_unlock_mutex(lock)
#define spin_lock_bh(lock) uds_lock_mutex(lock)
#define spin_unlock_bh(lock) uds_unlock_mutex(lock)

typedef struct mutex rwlock_t;

#define rwlock_init(lock) \
	VDO_ASSERT_LOG_ONLY(uds_init_mutex(lock) == UDS_SUCCESS, \
			    "rwlock init succeeds")
#define read_lock(lock) uds_lock_mutex(lock)
#define read_unlock(lock) uds_unlock_mutex(lock)
#define write_lock(lock) uds_lock_mutex(lock)
#define write_unlock(lock) uds_unlock_mutex(lock)

#endif // LINUX_SPINLOCK_H
