// SPDX-License-Identifier: MIT

#include "DiskUsageView.h"

#include "DockerClient.h"
#include "Json.h"
#include "Messages.h"
#include "Settings.h"
#include "Util.h"

#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <GridLayout.h>
#include <LayoutBuilder.h>
#include <RadioButton.h>
#include <StringView.h>
#include <Window.h>

namespace {

enum {
	kInfo = 'duin',
	kCleanUp = 'ducu',
	kCleanUpDone = 'ducd',
	kOptionChanged = 'duop',
};

const char* kInfoLabels[] = {
	"Version:", "API version:", "Operating system:", "Architecture:",
	"CPUs:", "Memory:", "Storage driver:", "Root directory:", "Containers:",
};

const char* kUsageRows[] = { "Images", "Containers", "Local volumes",
	"Build cache" };
const char* kUsageColumns[] = { "Total", "Active", "Size", "Reclaimable" };

BStringView* Value()
{
	BStringView* view = new BStringView(NULL, "–");
	view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	return view;
}

BStringView* Header(const char* text, alignment align = B_ALIGN_RIGHT)
{
	BStringView* view = new BStringView(NULL, text);
	view->SetFont(be_bold_font);
	view->SetAlignment(align);
	return view;
}

} // namespace


DiskUsageView::DiskUsageView()
	:
	BView("disk usage", B_WILL_DRAW),
	fEngineRunning(false),
	fLoading(false),
	fCleaning(false)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BBox* engineBox = new BBox("engine");
	engineBox->SetLabel("Docker Engine");
	BGridLayout* infoGrid;
	BLayoutBuilder::Group<>(engineBox, B_VERTICAL)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.GetLayout(&infoGrid)
		.End()
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);
	for (int i = 0; i < 9; i++) {
		fInfo[i] = Value();
		// Two columns of label and value.
		int column = i < 5 ? 0 : 2;
		int row = i < 5 ? i : i - 5;
		BStringView* label = new BStringView(NULL, kInfoLabels[i]);
		label->SetAlignment(B_ALIGN_RIGHT);
		infoGrid->AddView(label, column, row);
		infoGrid->AddView(fInfo[i], column + 1, row);
	}

	BBox* usageBox = new BBox("usage");
	usageBox->SetLabel("Disk usage");
	BGridLayout* usageGrid;
	fTotal = new BStringView("total", "");
	Util::MakeElastic(fTotal);
	BLayoutBuilder::Group<>(usageBox, B_VERTICAL, B_USE_SMALL_SPACING)
		.AddGrid(B_USE_BIG_SPACING, B_USE_SMALL_SPACING)
			.GetLayout(&usageGrid)
		.End()
		.Add(fTotal)
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);
	for (int column = 0; column < 4; column++)
		usageGrid->AddView(Header(kUsageColumns[column]), column + 1, 0);
	for (int row = 0; row < 4; row++) {
		usageGrid->AddView(Header(kUsageRows[row], B_ALIGN_LEFT), 0, row + 1);
		for (int column = 0; column < 4; column++) {
			fUsage[row][column] = Value();
			fUsage[row][column]->SetAlignment(B_ALIGN_RIGHT);
			usageGrid->AddView(fUsage[row][column], column + 1, row + 1);
		}
	}

	BBox* cleanUpBox = new BBox("cleanUp");
	cleanUpBox->SetLabel("Clean up");
	fContainers = new BCheckBox("containers", "Stopped containers",
		new BMessage(kOptionChanged));
	fContainers->SetValue(B_CONTROL_ON);
	fNetworks = new BCheckBox("networks", "Unused networks",
		new BMessage(kOptionChanged));
	fNetworks->SetValue(B_CONTROL_ON);
	fImages = new BCheckBox("images", "Images:",
		new BMessage(kOptionChanged));
	fImages->SetValue(B_CONTROL_ON);
	fDanglingImages = new BRadioButton("dangling",
		"only untagged (dangling) ones", new BMessage(kOptionChanged));
	fDanglingImages->SetValue(B_CONTROL_ON);
	fAllImages = new BRadioButton("all", "all that no container uses",
		new BMessage(kOptionChanged));
	fVolumes = new BCheckBox("volumes", "Unused volumes (their data is lost)",
		new BMessage(kOptionChanged));
	fBuildCache = new BCheckBox("buildCache", "Build cache",
		new BMessage(kOptionChanged));
	fBuildCache->SetValue(B_CONTROL_ON);
	fCleanUpButton = new BButton("cleanUp", "Clean up", new BMessage(kCleanUp));
	fCleanUpStatus = new BStringView("cleanUpStatus", "");
	Util::MakeElastic(fCleanUpStatus);

	BLayoutBuilder::Group<>(cleanUpBox, B_VERTICAL, B_USE_SMALL_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_BIG_SPACING)
			.AddGroup(B_VERTICAL, 0)
				.Add(fContainers)
				.Add(fNetworks)
				.Add(fBuildCache)
			.End()
			.AddGroup(B_VERTICAL, 0)
				.Add(fImages)
				.AddGroup(B_VERTICAL, 0)
					.Add(fDanglingImages)
					.Add(fAllImages)
					.SetInsets(B_USE_BIG_SPACING, 0, 0, 0)
				.End()
				.Add(fVolumes)
			.End()
			.AddGlue()
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(fCleanUpStatus)
			.Add(fCleanUpButton)
		.End()
		.SetInsets(B_USE_DEFAULT_SPACING, B_USE_BIG_SPACING,
			B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.Add(engineBox)
		.Add(usageBox)
		.Add(cleanUpBox)
		.AddGlue()
		.SetInsets(B_USE_DEFAULT_SPACING);
	_UpdateButtons();
}


void DiskUsageView::AttachedToWindow()
{
	BView::AttachedToWindow();
	BControl* controls[] = { fContainers, fNetworks, fImages, fDanglingImages,
		fAllImages, fVolumes, fBuildCache, fCleanUpButton };
	for (BControl* control : controls)
		control->SetTarget(this);
}


void DiskUsageView::SetEngineRunning(bool running)
{
	if (running == fEngineRunning)
		return;
	fEngineRunning = running;
	if (running)
		Refresh();
	_UpdateButtons();
}


void DiskUsageView::_UpdateButtons()
{
	bool images = fImages->Value() == B_CONTROL_ON;
	fDanglingImages->SetEnabled(images);
	fAllImages->SetEnabled(images);
	bool any = fContainers->Value() == B_CONTROL_ON
		|| fNetworks->Value() == B_CONTROL_ON || images
		|| fVolumes->Value() == B_CONTROL_ON
		|| fBuildCache->Value() == B_CONTROL_ON;
	fCleanUpButton->SetEnabled(fEngineRunning && !fCleaning && any);
}


void DiskUsageView::Refresh()
{
	if (!fEngineRunning || fLoading)
		return;
	fLoading = true;
	BString socket = Settings::Default().SocketPath();
	Util::RunTask(BMessenger(this), kInfo, [socket](BMessage& reply) {
		DockerClient client(socket.String());
		BString json;
		if (client.Get("/info", &json) == B_OK)
			reply.AddString("info", json);
		if (client.Get("/version", &json) == B_OK)
			reply.AddString("version", json);
		// One type at a time: an Engine that fails to measure one (a
		// broken build cache record, say) still reports the others.
		static const char* kTypes[] = { "image", "container", "volume",
			"build-cache" };
		for (const char* type : kTypes) {
			BString path("/system/df?type="), error;
			path << type;
			if (client.Get(path.String(), &json, &error) == B_OK)
				reply.AddString(type, json);
			else
				reply.AddString(BString(type) << ":error", error);
		}
	});
}


void DiskUsageView::_InfoReceived(BMessage* message)
{
	fLoading = false;
	djson::Value* info = djson::Parse(message->GetString("info", "{}"));
	djson::Value* version = djson::Parse(message->GetString("version", "{}"));

	BString text;
	fInfo[0]->SetText(djson::GetString(version, "Version", "–"));
	text.SetToFormat("%s (minimum %s)", djson::GetString(version, "ApiVersion",
		"?"), djson::GetString(version, "MinAPIVersion", "?"));
	fInfo[1]->SetText(text.String());
	BString os = djson::GetString(info, "OperatingSystem");
	if (os.Length() == 0)
		os = djson::GetString(version, "Os", "–");
	int32 note = os.FindFirst(" (");
	if (note > 0)
		os.Truncate(note);
	fInfo[2]->SetText(os.String());
	fInfo[3]->SetText(djson::GetString(info, "Architecture",
		djson::GetString(version, "Arch", "–")));
	text.SetToFormat("%.0f", djson::GetNumber(info, "NCPU"));
	fInfo[4]->SetText(text.String());
	double memory = djson::GetNumber(info, "MemTotal");
	fInfo[5]->SetText(memory > 0 ? Util::FormatSize(memory).String()
		: "Not reported");
	fInfo[6]->SetText(djson::GetString(info, "Driver", "–"));
	fInfo[7]->SetText(djson::GetString(info, "DockerRootDir", "–"));
	text.SetToFormat("%.0f running, %.0f paused, %.0f stopped",
		djson::GetNumber(info, "ContainersRunning"),
		djson::GetNumber(info, "ContainersPaused"),
		djson::GetNumber(info, "ContainersStopped"));
	fInfo[8]->SetText(text.String());
	djson::FreeValue(info);
	djson::FreeValue(version);

	double total = 0, reclaimable = 0;
	BString unavailable;
	// The usage of one type, or NULL (with its row marked) if the Engine
	// could not tell.
	auto usage = [&](int row, const char* type) -> djson::Value* {
		djson::Value* value = djson::Parse(message->GetString(type, ""));
		if (value != NULL)
			return value;
		for (int column = 0; column < 4; column++) {
			fUsage[row][column]->SetText("–");
			fUsage[row][column]->SetToolTip(message->GetString(
				(BString(type) << ":error").String(), NULL));
		}
		if (unavailable.Length() > 0)
			unavailable << ", ";
		unavailable << BString(kUsageRows[row]).ToLower();
		return NULL;
	};
	auto setRow = [&](int row, int32 count, int32 active, double size,
			double free) {
		BString value;
		value.SetToFormat("%" B_PRId32, count);
		fUsage[row][0]->SetText(value.String());
		value.SetToFormat("%" B_PRId32, active);
		fUsage[row][1]->SetText(value.String());
		fUsage[row][2]->SetText(Util::FormatSize(size).String());
		for (int column = 0; column < 4; column++)
			fUsage[row][column]->SetToolTip((const char*)NULL);
		value = Util::FormatSize(free);
		if (size > 0)
			value << BString().SetToFormat(" (%.0f%%)", free * 100 / size);
		fUsage[row][3]->SetText(value.String());
		total += size;
		reclaimable += free;
	};

	// Images: what is shared between images counts once.
	djson::Value* df = usage(0, "image");
	const djson::Value* images = djson::Get(df, "Images");
	int32 activeImages = 0;
	double imagesFree = 0;
	for (int32 i = 0; i < djson::Length(images); i++) {
		const djson::Value* image = djson::At(images, i);
		if (djson::GetNumber(image, "Containers") > 0)
			activeImages++;
		else {
			imagesFree += djson::GetNumber(image, "Size")
				- djson::GetNumber(image, "SharedSize");
		}
	}
	double layers = djson::GetNumber(df, "LayersSize");
	if (imagesFree > layers)
		imagesFree = layers;
	if (df != NULL)
		setRow(0, djson::Length(images), activeImages, layers, imagesFree);
	djson::FreeValue(df);

	df = usage(1, "container");
	const djson::Value* containers = djson::Get(df, "Containers");
	int32 activeContainers = 0;
	double containersSize = 0, containersFree = 0;
	for (int32 i = 0; i < djson::Length(containers); i++) {
		const djson::Value* container = djson::At(containers, i);
		double size = djson::GetNumber(container, "SizeRw");
		containersSize += size;
		if (BString(djson::GetString(container, "State")) == "running")
			activeContainers++;
		else
			containersFree += size;
	}
	if (df != NULL) {
		setRow(1, djson::Length(containers), activeContainers, containersSize,
			containersFree);
	}
	djson::FreeValue(df);

	df = usage(2, "volume");
	const djson::Value* volumes = djson::Get(df, "Volumes");
	int32 activeVolumes = 0;
	double volumesSize = 0, volumesFree = 0;
	for (int32 i = 0; i < djson::Length(volumes); i++) {
		const djson::Value* data = djson::Get(djson::At(volumes, i),
			"UsageData");
		double size = djson::GetNumber(data, "Size");
		if (size < 0)
			size = 0;
		volumesSize += size;
		if (djson::GetNumber(data, "RefCount") > 0)
			activeVolumes++;
		else
			volumesFree += size;
	}
	if (df != NULL)
		setRow(2, djson::Length(volumes), activeVolumes, volumesSize,
			volumesFree);
	djson::FreeValue(df);

	df = usage(3, "build-cache");
	const djson::Value* cache = djson::Get(df, "BuildCache");
	int32 activeCache = 0;
	double cacheSize = 0, cacheFree = 0;
	for (int32 i = 0; i < djson::Length(cache); i++) {
		const djson::Value* record = djson::At(cache, i);
		double size = djson::GetNumber(record, "Size");
		cacheSize += size;
		if (djson::GetBool(record, "InUse"))
			activeCache++;
		else if (!djson::GetBool(record, "Shared"))
			cacheFree += size;
	}
	if (df != NULL)
		setRow(3, djson::Length(cache), activeCache, cacheSize, cacheFree);
	djson::FreeValue(df);

	BString summary;
	summary.SetToFormat("Docker uses %s; up to %s can be reclaimed.",
		Util::FormatSize(total).String(),
		Util::FormatSize(reclaimable).String());
	if (unavailable.Length() > 0) {
		summary << " The Engine could not measure " << unavailable
			<< " (see the tooltip).";
	}
	fTotal->SetText(summary.String());
}


void DiskUsageView::_CleanUp()
{
	bool containers = fContainers->Value() == B_CONTROL_ON;
	bool networks = fNetworks->Value() == B_CONTROL_ON;
	bool images = fImages->Value() == B_CONTROL_ON;
	bool allImages = fAllImages->Value() == B_CONTROL_ON;
	bool volumes = fVolumes->Value() == B_CONTROL_ON;
	bool buildCache = fBuildCache->Value() == B_CONTROL_ON;

	BString text("This deletes:\n");
	if (containers)
		text << "\n• all stopped containers";
	if (networks)
		text << "\n• all networks no container uses";
	if (images) {
		text << (allImages ? "\n• all images no container uses"
			: "\n• all untagged images");
	}
	if (volumes)
		text << "\n• all volumes no container uses, with their data";
	if (buildCache)
		text << "\n• the build cache";
	text << "\n\nThis cannot be undone.";
	if (!Util::Confirm(text.String(), "Clean up"))
		return;

	fCleaning = true;
	_UpdateButtons();
	fCleanUpStatus->SetText("Cleaning up…");
	BString socket = Settings::Default().SocketPath();
	Util::RunTask(BMessenger(this), kCleanUpDone,
		[=](BMessage& reply) {
			DockerClient client(socket.String());
			double reclaimed = 0;
			int32 deleted = 0;
			auto prune = [&](const char* path, const char* listField) {
				BString json, error;
				if (client.Call("POST", path, NULL, &json, &error) != B_OK) {
					reply.AddString("error", error);
					return;
				}
				djson::Value* root = djson::Parse(json.String());
				reclaimed += djson::GetNumber(root, "SpaceReclaimed");
				deleted += djson::Length(djson::Get(root, listField));
				djson::FreeValue(root);
			};
			if (containers)
				prune("/containers/prune", "ContainersDeleted");
			if (networks)
				prune("/networks/prune", "NetworksDeleted");
			if (images) {
				prune(allImages
					? "/images/prune?filters=%7B%22dangling%22%3A%5B%22false"
						"%22%5D%7D"
					: "/images/prune", "ImagesDeleted");
			}
			if (volumes) {
				prune("/volumes/prune?filters=%7B%22all%22%3A%5B%22true%22"
					"%5D%7D", "VolumesDeleted");
			}
			if (buildCache)
				prune("/build/prune", "CachesDeleted");
			reply.AddDouble("reclaimed", reclaimed);
			reply.AddInt32("deleted", deleted);
		});
}


void DiskUsageView::_CleanUpDone(BMessage* message)
{
	fCleaning = false;
	_UpdateButtons();
	BString text;
	text.SetToFormat("Deleted %" B_PRId32 " objects; reclaimed %s.",
		message->GetInt32("deleted", 0),
		Util::FormatSize(message->GetDouble("reclaimed", 0)).String());
	fCleanUpStatus->SetText(text.String());

	BString errors, error;
	for (int32 i = 0; message->FindString("error", i, &error) == B_OK; i++) {
		if (errors.Length() > 0)
			errors << "\n\n";
		errors << error;
	}
	if (errors.Length() > 0)
		Util::ShowError(errors.String());

	// The lists in the other tabs change as well.
	if (Window() != NULL)
		Window()->PostMessage(M_REFRESH_ALL);
}


void DiskUsageView::MessageReceived(BMessage* message)
{
	switch (message->what) {
	case kInfo:
		_InfoReceived(message);
		break;
	case kCleanUp:
		_CleanUp();
		break;
	case kCleanUpDone:
		_CleanUpDone(message);
		break;
	case kOptionChanged:
		_UpdateButtons();
		break;
	case M_REFRESH:
		Refresh();
		break;
	default:
		BView::MessageReceived(message);
		break;
	}
}
