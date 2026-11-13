#pragma once

#include <QPointer>
#include <QObject>
#include <QString>

#include <obs.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// Global OBS hotkeys (Settings -> Hotkeys). Unlike the dock's QShortcut
// bindings they also work while OBS is not the focused window.
// Bindings are stored with the scene collection through the frontend save callback.
class FlyObsHotkeys {
public:
	using Action = std::function<void()>;

	explicit FlyObsHotkeys(QObject *context);
	~FlyObsHotkeys();

	FlyObsHotkeys(const FlyObsHotkeys &) = delete;
	FlyObsHotkeys &operator=(const FlyObsHotkeys &) = delete;

	// name must be unique and stable: it is the key used to persist the binding.
	void add(const char *name, const QString &description, Action action);
	void unregisterAll();

private:
	struct Entry {
		std::string name;
		std::string description;
		Action action;
		QPointer<QObject> context;
		obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	};

	static void onHotkey(void *data, obs_hotkey_id id, obs_hotkey_t *key, bool pressed);
	static void onFrontendSave(obs_data_t *saveData, bool saving, void *privateData);

	void save(obs_data_t *saveData) const;
	void load(obs_data_t *saveData);

	QPointer<QObject> context_;
	std::vector<std::unique_ptr<Entry>> entries_;
	bool saveCallbackConnected_ = false;
};
