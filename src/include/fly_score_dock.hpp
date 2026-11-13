#pragma once

#include <QWidget>
#include <QString>
#include <QList>
#include <QKeySequence>
#include <QToolButton>

#include "fly_score_state.hpp"
#include "fly_score_event_log.hpp"
#include "fly_score_match.hpp"

#include <memory>

class QPushButton;
class QSpinBox;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QVBoxLayout;
class QLabel;
class QShortcut;
class QJsonObject;
class FlyScoreWebSocketServer;
class FlyObsHotkeys;
class QListWidget;

struct FlyCustomFieldUi {
	QWidget *row = nullptr;
	QCheckBox *visibleCheck = nullptr;
	QLabel *labelLbl = nullptr;
	QSpinBox *homeSpin = nullptr;
	QSpinBox *awaySpin = nullptr;
	QToolButton *minusHome = nullptr;
	QToolButton *plusHome = nullptr;
	QToolButton *minusAway = nullptr;
	QToolButton *plusAway = nullptr;
};

struct FlySingleStatUi {
	QWidget *row = nullptr;
	QCheckBox *visibleCheck = nullptr;
	QLabel *labelLbl = nullptr;
	QSpinBox *valueSpin = nullptr;
	QToolButton *minusBtn = nullptr;
	QToolButton *plusBtn = nullptr;
};

struct FlyTimerUi {
	QWidget *row = nullptr;
	QLabel *labelLbl = nullptr;
	QLineEdit *timeEdit = nullptr;
	QPushButton *startStop = nullptr;
	QPushButton *reset = nullptr;
	QCheckBox *visibleCheck = nullptr;
};

struct FlyHotkeyBinding;

class FlyScoreDock : public QWidget {
	Q_OBJECT
public:
	explicit FlyScoreDock(QWidget *parent = nullptr);
	~FlyScoreDock() override;
	bool init();

	void handleFrontendEvent(int event);

public slots:
	void bumpCustomFieldHome(int index, int delta);
	void bumpCustomFieldAway(int index, int delta);
	void toggleCustomFieldVisible(int index);
	void bumpSingleStat(int index, int delta);
	void toggleSingleStatVisible(int index);
	void toggleTimerRunning(int index);
	void toggleSwap();
	void toggleScoreboardVisible();
	void openHotkeysDialog();
	void ensureResourcesDefaults();

public:
	// Match events (fly_score_dock_match.cpp). Plain methods, not slots:
	// they are invoked from lambdas (buttons, OBS hotkeys, WebSocket).
	void triggerAlert(FlyAlertKind kind, bool home);
	void hideAlert();
	void undoLastAlert();
	void cycleSelectedPlayer(bool home, int delta);
	void addShootoutKick(bool home, bool goal);
	void undoShootoutKick();
	void toggleShootoutVisible();
	void toggleSponsorsVisible();

private slots:
	void onClearTeamsAndReset();

	void onOpenCustomFieldsDialog();
	void onOpenTimersDialog();
	void onOpenTeamsDialog();

	void onSelectTemplateFolder();

private:
	void applyDockTheme();
	void loadState();
	void saveState();
	void refreshUiFromState(bool onlyTimeIfRunning = false);
	void clearAllCustomFieldRows();
	void loadCustomFieldControlsFromState();
	void syncCustomFieldControlsToState();
	void clearAllSingleStatRows();
	void loadSingleStatControlsFromState();
	void syncSingleStatControlsToState();
	void clearAllTimerRows();
	void loadTimerControlsFromState();
	QList<FlyHotkeyBinding> buildDefaultHotkeyBindings() const;
	QList<FlyHotkeyBinding> buildMergedHotkeyBindings() const;
	void applyHotkeyBindings(const QList<FlyHotkeyBinding> &bindings);
	void clearAllShortcuts();
	void refreshActiveTemplateLabel();
	QString selectedTemplateName() const;
	QString selectedTemplatePath() const;
	void activateTemplateFolder(const QString &path);
	void broadcastCurrentState();
	void updateWebSocketStatus();
	void handleRemoteCommand(const QJsonObject &command);
	void startEventLog(const QString &firstEvent = QStringLiteral("Stream Start"));
	void stopEventLog(const QString &lastEvent = QStringLiteral("Stream End"));
	void appendEvent(const QString &event);
	void logStateChanges(const FlyState &before, const FlyState &after);
	void refreshEventLogUi();
	QString eventLogsDirectory() const;

	// Match events (fly_score_dock_match.cpp)
	QWidget *buildAlertsTab(QWidget *parent);
	QWidget *buildShootoutTab(QWidget *parent);
	QWidget *buildSponsorsTab(QWidget *parent);
	void refreshMatchUi();
	void refreshAlertStatus();
	void refreshShootoutStatus();
	void refreshSponsorsList();
	void registerObsHotkeys();
	bool handleMatchCommand(const QString &action, const QJsonObject &command);
	void fireAlert(FlyAlertKind kind, bool home, const FlyPlayer *player);
	void logMatchEvent(const QString &line);
	void startShootout();
	void endShootout();
	void recordShootoutKick(bool home, bool goal, const QString &player);
	void addSponsorsFromFiles();
	void removeSelectedSponsor();
	void moveSelectedSponsor(int delta);

private:
	QString dataDir_;
	QString activeTemplateName_;
	FlyState st_;
	QCheckBox *swapSides_ = nullptr;
	QCheckBox *showScoreboard_ = nullptr;
	QPushButton *teamsBtn_ = nullptr;
	QPushButton *editFieldsBtn_ = nullptr;
	QPushButton *editTimersBtn_ = nullptr;
	QVBoxLayout *customFieldsLayout_ = nullptr;
	QList<FlyCustomFieldUi> customFields_;
	QVBoxLayout *singleStatsLayout_ = nullptr;
	QList<FlySingleStatUi> singleStats_;
	QVBoxLayout *timersLayout_ = nullptr;
	QList<FlyTimerUi> timers_;
	QLabel *activeTemplateLabel_ = nullptr;
	QLabel *webSocketStatus_ = nullptr;
	FlyScoreWebSocketServer *webSocketServer_ = nullptr;
	QList<FlyHotkeyBinding> hotkeyBindings_;
	QList<QShortcut *> shortcuts_;
	FlyScoreEventLog eventLog_;
	FlyState lastLoggedState_;
	QLabel *eventLogStatus_ = nullptr;
	QComboBox *eventTimestampMode_ = nullptr;
	QLineEdit *eventText_ = nullptr;
	QPushButton *eventLogToggle_ = nullptr;
	QPushButton *eventAdd_ = nullptr;
	bool frontendEventCallbackConnected_ = false;

	struct LastAlertChange {
		bool valid = false;
		int fieldIndex = -1;
		bool home = true;
	};
	LastAlertChange lastAlertChange_;
	QComboBox *alertPlayerHome_ = nullptr;
	QComboBox *alertPlayerAway_ = nullptr;
	QSpinBox *alertSeconds_ = nullptr;
	QLabel *alertStatus_ = nullptr;
	QComboBox *shootoutFirst_ = nullptr;
	QSpinBox *shootoutRounds_ = nullptr;
	QComboBox *shootoutKickerHome_ = nullptr;
	QComboBox *shootoutKickerAway_ = nullptr;
	QCheckBox *shootoutVisible_ = nullptr;
	QLabel *shootoutStatus_ = nullptr;
	QCheckBox *sponsorsVisible_ = nullptr;
	QComboBox *sponsorsMode_ = nullptr;
	QSpinBox *sponsorsSeconds_ = nullptr;
	QListWidget *sponsorsList_ = nullptr;
	std::unique_ptr<FlyObsHotkeys> obsHotkeys_;
};

void fly_create_dock();
void fly_destroy_dock();
FlyScoreDock *fly_get_dock();
