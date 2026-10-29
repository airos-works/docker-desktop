// SPDX-License-Identifier: MIT
// Pulling an image, with its progress.
//
// Registries rarely have images for haiku/amd64, the air/OS Engine's own
// platform; when a pull finds no match for it, it is retried for the
// Linux platform of the Engine's architecture (which the air/OS Engine
// runs), as `docker pull --platform linux/amd64` would.

#ifndef DOCKER_PULL_H
#define DOCKER_PULL_H

#include "DockerClient.h"

#include <String.h>

#include <functional>
#include <map>

namespace Pull {

// What a pull is doing: per layer, its status and bytes done/total.
struct Progress {
	struct Layer {
		BString status;
		double current = 0;
		double total = 0;
	};
	std::map<BString, Layer> layers;
	BString status;	// the last overall status line
	BString platform; // the platform pulled, when it was not the default

	// Done and total bytes of the layers that report sizes.
	void Totals(double* current, double* total) const;
	BString Describe() const;
};

// "nginx:1.25" -> "nginx", "1.25"; "nginx" -> "nginx", "latest";
// "registry:5000/app" -> "registry:5000/app", "latest"; digests stay whole.
void SplitReference(const char* reference, BString& name, BString& tag);

// Pulls reference; onProgress (on the calling thread) after each update.
status_t Run(DockerClient& client, const char* reference,
	const char* platform, const CancelerRef& canceler,
	const std::function<void(const Progress& progress)>& onProgress,
	BString& error);

} // namespace Pull

#endif
