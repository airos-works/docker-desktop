// SPDX-License-Identifier: MIT

#include "Pull.h"

#include "Json.h"
#include "Util.h"

namespace Pull {

void Progress::Totals(double* current, double* total) const
{
	*current = 0;
	*total = 0;
	for (const auto& entry : layers) {
		const Layer& layer = entry.second;
		if (layer.total <= 0) {
			continue;
		}
		*total += layer.total;
		if (layer.status == "Pull complete" || layer.status == "Download complete"
			|| layer.status == "Already exists" || layer.status == "Extracting")
			*current += layer.total;
		else
			*current += layer.current;
	}
}


BString Progress::Describe() const
{
	BString text;
	for (const auto& entry : layers) {
		const Layer& layer = entry.second;
		text << Util::ShortId(entry.first.String()) << ": " << layer.status;
		if (layer.total > 0 && layer.status != "Pull complete"
			&& layer.status != "Download complete") {
			text << " " << Util::FormatSize(layer.current) << " / "
				<< Util::FormatSize(layer.total);
		}
		text << "\n";
	}
	if (status.Length() > 0)
		text << status << "\n";
	return text;
}


void SplitReference(const char* reference, BString& name, BString& tag)
{
	name = reference;
	name.Trim();
	tag = "";
	if (name.FindFirst('@') >= 0)
		return;
	int32 colon = name.FindLast(':');
	int32 slash = name.FindLast('/');
	if (colon > slash) {
		name.CopyInto(tag, colon + 1, name.Length() - colon - 1);
		name.Truncate(colon);
	}
	if (tag.Length() == 0)
		tag = "latest";
}


static status_t PullOnce(DockerClient& client, const BString& name,
	const BString& tag, const char* platform, const CancelerRef& canceler,
	Progress& progress,
	const std::function<void(const Progress& progress)>& onProgress,
	BString& error)
{
	BString path("/images/create?fromImage=");
	path << DockerClient::Escape(name.String());
	if (tag.Length() > 0)
		path << "&tag=" << DockerClient::Escape(tag.String());
	if (platform != NULL && platform[0] != '\0')
		path << "&platform=" << DockerClient::Escape(platform);

	LineSplitter lines;
	BString streamError;
	status_t status = client.Stream("POST", path.String(), NULL, canceler,
		[&](const char* data, size_t length) {
			return lines.Feed(data, length, [&](const BString& line) {
				djson::Value* root = djson::Parse(line.String());
				if (root == NULL)
					return true;
				BString message = djson::GetString(root, "error");
				if (message.Length() > 0) {
					streamError = message;
					djson::FreeValue(root);
					return false;
				}
				BString id = djson::GetString(root, "id");
				BString text = djson::GetString(root, "status");
				const djson::Value* detail = djson::Get(root,
					"progressDetail");
				if (id.Length() > 0 && text.FindFirst("Pulling from") < 0) {
					Progress::Layer& layer = progress.layers[id];
					layer.status = text;
					double total = djson::GetNumber(detail, "total");
					if (total > 0) {
						layer.total = total;
						layer.current = djson::GetNumber(detail, "current");
					}
				} else if (text.Length() > 0)
					progress.status = text;
				djson::FreeValue(root);
				onProgress(progress);
				return !canceler->IsCancelled();
			});
		}, &error);

	if (canceler->IsCancelled()) {
		error = "Cancelled";
		return B_CANCELED;
	}
	if (status == B_OK && streamError.Length() > 0) {
		error = streamError;
		return B_ERROR;
	}
	return status;
}


status_t Run(DockerClient& client, const char* reference,
	const char* platform, const CancelerRef& canceler,
	const std::function<void(const Progress& progress)>& onProgress,
	BString& error)
{
	BString name, tag;
	SplitReference(reference, name, tag);
	Progress progress;
	status_t status = PullOnce(client, name, tag, platform, canceler,
		progress, onProgress, error);
	if (status == B_OK || (platform != NULL && platform[0] != '\0')
		|| error.FindFirst("no matching manifest") < 0)
		return status;

	// No image for the Engine's own platform: take the Linux one.
	BString json, ignored;
	BString os = "linux", arch = "amd64";
	if (client.Get("/version", &json, &ignored) == B_OK) {
		djson::Value* root = djson::Parse(json.String());
		if (root != NULL) {
			arch = djson::GetString(root, "Arch", "amd64");
			djson::FreeValue(root);
		}
	}
	BString linuxPlatform;
	linuxPlatform << os << "/" << arch;
	progress = Progress();
	progress.platform = linuxPlatform;
	progress.status.SetToFormat("No %s image for this Engine's platform; "
		"pulling %s", reference, linuxPlatform.String());
	onProgress(progress);
	error = "";
	return PullOnce(client, name, tag, linuxPlatform.String(), canceler,
		progress, onProgress, error);
}

} // namespace Pull
