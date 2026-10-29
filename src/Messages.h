// SPDX-License-Identifier: MIT

#ifndef DOCKER_MESSAGES_H
#define DOCKER_MESSAGES_H

#include <SupportDefs.h>

enum {
	M_REFRESH = 'rfrs',
	M_TAB_CHANGED = 'tbch',
	M_CONTAINER_SELECTED = 'csel',
	M_IMAGE_SELECTED = 'isel',
	M_CONTAINER_START = 'csta',
	M_CONTAINER_STOP = 'csto',
	M_CONTAINER_RESTART = 'crst',
	M_CONTAINER_REMOVE = 'crmv',
	M_CONTAINER_LOGS = 'clog',
	M_IMAGE_REMOVE = 'irmv',
	M_DOCKER_TASK_RESULT = 'dtas',
	M_DOCKER_LIST_RESULT = 'dlis',
	M_CONNECTION_SETTINGS = 'cset',
	M_SOCKET_PATH_CHANGED = 'spch',
	M_ENGINE_POLL = 'epol',
	M_ENGINE_STATUS = 'ests',
	M_ENGINE_START = 'esta',
	M_ENGINE_START_AT_BOOT = 'ebot',
};

#endif
