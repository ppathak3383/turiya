/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - first component on Genode (Phase 1 gate).
 * It holds no capabilities beyond its own environment: it can only log.
 * That is the point: in Turiya every component starts with nothing and is given only what it needs.
 */
#include <base/log.h>
#include <base/component.h>

void Component::construct(Genode::Env &)
{
	Genode::log("Turiya: Sakshi is awake. I hold no capabilities except this log.");
}
