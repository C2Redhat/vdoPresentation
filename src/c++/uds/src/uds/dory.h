/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Red Hat
 */

#ifndef DORY_H
#define DORY_H

#ifdef TEST_INTERNAL
#include <linux/atomic.h>

/*
 * The Dory mechanism is used for tests that want to simulate a device that becomes read-only, i.e.
 * it will get an -EROFS on any attempt to write to it. This is a cheaper technique than actually
 * controlling the power to the device, or doing a dirty reboot of the CPU.
 */

extern atomic_t dory_forgetful;

/*
 * Get the setting of the flag that determines whether UDS will write data (or not).
 *
 * Return: true to if writing is disabled, or false do normal I/O.
 */
static inline bool get_dory_forgetful(void)
{
	return atomic_read(&dory_forgetful);
}

/*
 * Change the setting of the flag that determines whether UDS will write data (or not).
 *
 * @setting: True to disable writing, or False do normal I/O.
 */
static inline void set_dory_forgetful(bool setting)
{
	atomic_set(&dory_forgetful, setting);
}
#endif /* TEST_INTERNAL */

#endif /* DORY_H */
