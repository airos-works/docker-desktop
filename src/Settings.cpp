// SPDX-License-Identifier: MIT

#include "Settings.h"

#include <Autolock.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

const char* kDefaultSocketPath = "/var/run/docker.sock";

static status_t SettingsFilePath(BPath* path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, path, true);
	if (status != B_OK)
		return status;
	return path->Append("DockerDesktop");
}


Settings& Settings::Default()
{
	static Settings settings;
	return settings;
}


Settings::Settings()
	:
	fLock("settings")
{
}


status_t Settings::Load()
{
	BAutolock lock(fLock);
	BPath path;
	status_t status = SettingsFilePath(&path);
	if (status != B_OK)
		return status;
	BFile file(path.Path(), B_READ_ONLY);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	BMessage data;
	status = data.Unflatten(&file);
	if (status == B_OK)
		fData = data;
	return status;
}


status_t Settings::Save()
{
	BAutolock lock(fLock);
	BPath path;
	status_t status = SettingsFilePath(&path);
	if (status != B_OK)
		return status;
	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status = file.InitCheck();
	if (status != B_OK)
		return status;
	return fData.Flatten(&file);
}


BString Settings::SocketPath()
{
	BAutolock lock(fLock);
	BString path = fData.GetString("socket", kDefaultSocketPath);
	if (path.Length() == 0)
		path = kDefaultSocketPath;
	return path;
}


void Settings::SetSocketPath(const char* path)
{
	BAutolock lock(fLock);
	fData.SetString("socket", path);
}


BRect Settings::WindowFrame()
{
	BAutolock lock(fLock);
	return fData.GetRect("frame", BRect());
}


void Settings::SetWindowFrame(BRect frame)
{
	BAutolock lock(fLock);
	fData.SetRect("frame", frame);
}


int32 Settings::CurrentTab()
{
	BAutolock lock(fLock);
	return fData.GetInt32("tab", 0);
}


void Settings::SetCurrentTab(int32 tab)
{
	BAutolock lock(fLock);
	fData.SetInt32("tab", tab);
}


bool Settings::OnlyRunning()
{
	BAutolock lock(fLock);
	return fData.GetBool("only running", false);
}


void Settings::SetOnlyRunning(bool only)
{
	BAutolock lock(fLock);
	fData.SetBool("only running", only);
}


bool Settings::StartEngineOnLaunch()
{
	BAutolock lock(fLock);
	return fData.GetBool("start engine on launch", false);
}


void Settings::SetStartEngineOnLaunch(bool start)
{
	BAutolock lock(fLock);
	fData.SetBool("start engine on launch", start);
}


bool Settings::ConfirmDeletes()
{
	BAutolock lock(fLock);
	return fData.GetBool("confirm deletes", true);
}


void Settings::SetConfirmDeletes(bool confirm)
{
	BAutolock lock(fLock);
	fData.SetBool("confirm deletes", confirm);
}


bool Settings::GetListState(const char* name, BMessage* state)
{
	BAutolock lock(fLock);
	BString field("list:");
	field << name;
	return fData.FindMessage(field.String(), state) == B_OK;
}


void Settings::SetListState(const char* name, const BMessage& state)
{
	BAutolock lock(fLock);
	BString field("list:");
	field << name;
	fData.RemoveName(field.String());
	fData.AddMessage(field.String(), &state);
}
