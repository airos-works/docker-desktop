// SPDX-License-Identifier: MIT

#ifndef DOCKER_MESSAGES_H
#define DOCKER_MESSAGES_H

#include <SupportDefs.h>

enum {
	// Lists and the main window
	M_REFRESH = 'rfrs',
	M_REFRESH_ALL = 'rfal',
	M_LIST_RESULT = 'dlis',
	M_ACTION_DONE = 'adon',
	M_SELECTION_CHANGED = 'slch',
	M_ROW_INVOKED = 'rinv',
	M_SEARCH_CHANGED = 'srch',
	M_SET_STATUS = 'stat',
	M_LIST_UPDATED = 'lsup',
	M_SHOW_TAB = 'stab',
	M_ENGINE_EVENT = 'evnt',
	M_EVENTS_ENDED = 'evnd',

	// The Engine
	M_ENGINE_POLL = 'epol',
	M_ENGINE_STATUS = 'ests',
	M_ENGINE_START = 'esta',
	M_ENGINE_STOP = 'esto',
	M_ENGINE_RESTART = 'erst',
	M_ENGINE_START_AT_BOOT = 'ebot',
	M_ENGINE_INFO = 'einf',
	M_SHOW_ENGINE_LOG = 'elog',
	M_SOCKET_PATH_CHANGED = 'spch',

	// Containers
	M_CONTAINER_START = 'csta',
	M_CONTAINER_STOP = 'csto',
	M_CONTAINER_RESTART = 'crst',
	M_CONTAINER_PAUSE = 'cpau',
	M_CONTAINER_UNPAUSE = 'cunp',
	M_CONTAINER_KILL = 'ckil',
	M_CONTAINER_REMOVE = 'crmv',
	M_CONTAINER_RENAME = 'cren',
	M_CONTAINER_DETAILS = 'cdet',
	M_CONTAINER_LOGS = 'clog',
	M_CONTAINER_INSPECT = 'cins',
	M_CONTAINER_TERMINAL = 'cter',
	M_CONTAINER_BROWSER = 'cbro',
	M_ONLY_RUNNING = 'conl',
	M_RUN_CONTAINER = 'crun',
	M_COMPOSE_UP = 'cmpu',

	// Images
	M_IMAGE_PULL = 'ipul',
	M_IMAGE_REPULL = 'irpl',
	M_IMAGE_RUN = 'irun',
	M_IMAGE_REMOVE = 'irmv',
	M_IMAGE_FORCE_REMOVE = 'ifrm',
	M_IMAGE_INSPECT = 'iins',
	M_IMAGE_HISTORY = 'ihis',
	M_IMAGE_TAG = 'itag',
	M_IMAGE_PUSH = 'ipus',
	M_IMAGE_BUILD = 'ibld',
	M_IMAGE_LOAD = 'ilod',
	M_IMAGE_SAVE = 'isav',
	M_HUB_SEARCH = 'hubs',

	// Volumes and networks
	M_VOLUME_CREATE = 'vcre',
	M_VOLUME_REMOVE = 'vrmv',
	M_VOLUME_INSPECT = 'vins',
	M_VOLUME_OPEN = 'vopn',
	M_NETWORK_CREATE = 'ncre',
	M_NETWORK_REMOVE = 'nrmv',
	M_NETWORK_INSPECT = 'nins',
	M_PRUNE = 'prun',

	// Common row actions
	M_COPY_ID = 'cpid',
	M_COPY_NAME = 'cpnm',

	// Windows
	M_SHOW_SETTINGS = 'sset',
	M_SHOW_CLEANUP = 'sclu',
	M_HUB_SIGN_IN = 'hsin',
	M_HUB_SIGN_OUT = 'hsou',
	M_TOGGLE_DESKBAR = 'tdsk',
	M_OPEN_URL = 'ourl',
	M_PROMPT_DONE = 'prdn',
	M_WINDOW_CLOSED = 'wcls',
};

#endif
