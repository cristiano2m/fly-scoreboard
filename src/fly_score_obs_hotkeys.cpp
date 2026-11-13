#include "config.hpp"

#define LOG_TAG "[" PLUGIN_NAME "][obs-hotkeys]"
#include "fly_score_log.hpp"

#include "fly_score_obs_hotkeys.hpp"

#include <obs-frontend-api.h>

#include <QMetaObject>

namespace {
std::string saveKey(const std::string &name)
{
	return "fly_hk_" + name;
}
} // namespace

FlyObsHotkeys::FlyObsHotkeys(QObject *context) : context_(context) {}

FlyObsHotkeys::~FlyObsHotkeys()
{
	unregisterAll();
}

void FlyObsHotkeys::add(const char *name, const QString &description, Action action)
{
	if (!name || !*name || !action)
		return;

	auto entry = std::make_unique<Entry>();
	entry->name = name;
	entry->description = description.toStdString();
	entry->action = std::move(action);
	entry->context = context_;
	entry->id = obs_hotkey_register_frontend(entry->name.c_str(), entry->description.c_str(),
						 &FlyObsHotkeys::onHotkey, entry.get());
	if (entry->id == OBS_INVALID_HOTKEY_ID) {
		LOGW("Failed to register hotkey %s", name);
		return;
	}
	entries_.push_back(std::move(entry));

	if (!saveCallbackConnected_) {
		obs_frontend_add_save_callback(&FlyObsHotkeys::onFrontendSave, this);
		saveCallbackConnected_ = true;
	}
}

void FlyObsHotkeys::unregisterAll()
{
	if (saveCallbackConnected_) {
		obs_frontend_remove_save_callback(&FlyObsHotkeys::onFrontendSave, this);
		saveCallbackConnected_ = false;
	}
	// obs_hotkey_unregister takes the hotkey lock, so no callback is running for
	// an entry once this returns.
	for (const auto &entry : entries_) {
		if (entry->id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(entry->id);
	}
	entries_.clear();
}

void FlyObsHotkeys::onHotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (!pressed || !data)
		return;

	auto *entry = static_cast<Entry *>(data);
	QObject *context = entry->context.data();
	if (!context)
		return;

	// Hotkeys fire on OBS's hotkey thread; run the action on the UI thread.
	Action action = entry->action;
	QMetaObject::invokeMethod(context, [action]() { action(); }, Qt::QueuedConnection);
}

void FlyObsHotkeys::onFrontendSave(obs_data_t *saveData, bool saving, void *privateData)
{
	auto *self = static_cast<FlyObsHotkeys *>(privateData);
	if (!self || !saveData)
		return;

	if (saving)
		self->save(saveData);
	else
		self->load(saveData);
}

void FlyObsHotkeys::save(obs_data_t *saveData) const
{
	for (const auto &entry : entries_) {
		obs_data_array_t *arr = obs_hotkey_save(entry->id);
		if (!arr)
			continue;
		obs_data_set_array(saveData, saveKey(entry->name).c_str(), arr);
		obs_data_array_release(arr);
	}
}

void FlyObsHotkeys::load(obs_data_t *saveData)
{
	for (const auto &entry : entries_) {
		// A collection without saved bindings must clear the previous collection's keys.
		obs_data_array_t *arr = obs_data_get_array(saveData, saveKey(entry->name).c_str());
		if (!arr)
			arr = obs_data_array_create();
		obs_hotkey_load(entry->id, arr);
		obs_data_array_release(arr);
	}
}
