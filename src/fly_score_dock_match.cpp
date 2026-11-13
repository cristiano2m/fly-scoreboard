// Match events for the Fly Score dock: goal/card alerts with player names,
// penalty shootout, sponsor logos and the global OBS hotkeys that drive them.
// Kept apart from fly_score_dock.cpp so that file stays close to upstream.

#include "config.hpp"

#define LOG_TAG "[" PLUGIN_NAME "][dock-match]"
#include "fly_score_log.hpp"

#include "fly_score_dock.hpp"
#include "fly_score_i18n.hpp"
#include "fly_score_logo_helpers.hpp"
#include "fly_score_match.hpp"
#include "fly_score_obs_hotkeys.hpp"
#include "fly_score_qt_helpers.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <vector>

namespace {

constexpr int kNoPlayer = -1;
constexpr int kMsPerSecond = 1000;

QString teamDisplayName(const FlyState &st, bool home)
{
	const QString title = fly_team_ref(st, home).title.trimmed();
	if (!title.isEmpty())
		return title;
	return home ? fly_i18n("Common.Home") : fly_i18n("Common.Guests");
}

QString alertTitle(FlyAlertKind kind)
{
	switch (kind) {
	case FlyAlertKind::Goal:
		return fly_i18n("Match.AlertGoal");
	case FlyAlertKind::Yellow:
		return fly_i18n("Match.AlertYellow");
	case FlyAlertKind::Red:
		return fly_i18n("Match.AlertRed");
	}
	return QString();
}

void fillPlayerCombo(QComboBox *combo, const QVector<FlyPlayer> &players)
{
	if (!combo)
		return;

	const int previous = combo->count() > 0 ? combo->currentData().toInt() : kNoPlayer;
	const QSignalBlocker blocker(combo);
	combo->clear();
	combo->addItem(fly_i18n("Match.NoPlayer"), kNoPlayer);
	for (int i = 0; i < players.size(); ++i)
		combo->addItem(fly_player_display(players[i]), i);

	const int index = combo->findData(previous);
	combo->setCurrentIndex(index >= 0 ? index : 0);
}

const FlyPlayer *selectedPlayer(const QComboBox *combo, const QVector<FlyPlayer> &players)
{
	if (!combo || combo->count() == 0)
		return nullptr;
	const int index = combo->currentData().toInt();
	return (index >= 0 && index < players.size()) ? &players[index] : nullptr;
}

void cycleCombo(QComboBox *combo, int delta)
{
	if (!combo || combo->count() == 0)
		return;
	const int count = combo->count();
	combo->setCurrentIndex(((combo->currentIndex() + delta) % count + count) % count);
}

void setGroupTitle(QWidget *child, const QString &title)
{
	if (!child)
		return;
	if (auto *group = qobject_cast<QGroupBox *>(child->parentWidget()))
		group->setTitle(title);
}

QPushButton *makeButton(const QString &text, QWidget *parent)
{
	auto *button = new QPushButton(text, parent);
	button->setCursor(Qt::PointingHandCursor);
	return button;
}

QString jsonText(const QJsonObject &o, const char *key)
{
	const QJsonValue v = o.value(QLatin1String(key));
	if (v.isDouble())
		return QString::number(v.toInt());
	return v.toString().trimmed();
}

int jsonNumber(const QJsonObject &o, const char *key, int fallback)
{
	const QJsonValue v = o.value(QLatin1String(key));
	if (v.isDouble())
		return v.toInt(fallback);
	bool ok = false;
	const int n = v.toString().toInt(&ok);
	return ok ? n : fallback;
}

// true/false when the command carries an explicit value, otherwise toggles.
bool jsonVisibility(const QJsonObject &o, bool current)
{
	const QJsonValue v = o.value(QStringLiteral("visible"));
	if (v.isBool())
		return v.toBool();
	if (v.isDouble())
		return v.toInt() != 0;
	return !current;
}

bool jsonIsHome(const QJsonObject &o)
{
	const QString team = jsonText(o, "team").toLower();
	return team != QLatin1String("away") && team != QLatin1String("guests") && team != QLatin1String("guest");
}

// Player from a WebSocket command: roster index ("player") or free text
// ("player_number" / "player_name").
bool playerFromCommand(const QJsonObject &o, const QVector<FlyPlayer> &roster, FlyPlayer &out)
{
	const int index = jsonNumber(o, "player", kNoPlayer);
	if (index >= 0 && index < roster.size()) {
		out = roster[index];
		return true;
	}
	out.number = jsonText(o, "player_number");
	out.name = jsonText(o, "player_name");
	return !out.number.isEmpty() || !out.name.isEmpty();
}

} // namespace

// -----------------------------------------------------------------------------
// Tabs
// -----------------------------------------------------------------------------

QWidget *FlyScoreDock::buildAlertsTab(QWidget *parent)
{
	auto *page = new QWidget(parent);
	auto *vbox = new QVBoxLayout(page);
	vbox->setContentsMargins(6, 6, 6, 6);
	vbox->setSpacing(6);

	auto buildTeam = [this, page](bool home) {
		auto *group = new QGroupBox(page);
		auto *grid = new QGridLayout(group);
		grid->setContentsMargins(8, 8, 8, 8);
		grid->setSpacing(6);

		auto *combo = new QComboBox(group);
		combo->setAccessibleName(fly_i18n("Match.Player"));
		auto *goal = makeButton(fly_i18n("Match.Goal"), group);
		auto *yellow = makeButton(fly_i18n("Match.Yellow"), group);
		auto *red = makeButton(fly_i18n("Match.Red"), group);

		grid->addWidget(new QLabel(fly_i18n("Match.Player"), group), 0, 0);
		grid->addWidget(combo, 0, 1, 1, 3);
		grid->addWidget(goal, 1, 1);
		grid->addWidget(yellow, 1, 2);
		grid->addWidget(red, 1, 3);
		grid->setColumnStretch(1, 1);
		grid->setColumnStretch(2, 1);
		grid->setColumnStretch(3, 1);

		connect(goal, &QPushButton::clicked, this, [this, home]() { triggerAlert(FlyAlertKind::Goal, home); });
		connect(yellow, &QPushButton::clicked, this,
			[this, home]() { triggerAlert(FlyAlertKind::Yellow, home); });
		connect(red, &QPushButton::clicked, this, [this, home]() { triggerAlert(FlyAlertKind::Red, home); });

		(home ? alertPlayerHome_ : alertPlayerAway_) = combo;
		return group;
	};

	vbox->addWidget(buildTeam(true));
	vbox->addWidget(buildTeam(false));

	auto *controls = new QHBoxLayout();
	alertSeconds_ = new QSpinBox(page);
	alertSeconds_->setRange(1, 120);
	alertSeconds_->setSuffix(QStringLiteral(" s"));
	alertSeconds_->setToolTip(fly_i18n("Match.AlertSeconds"));
	auto *hide = makeButton(fly_i18n("Match.HideAlert"), page);
	auto *undo = makeButton(fly_i18n("Match.UndoAlert"), page);
	undo->setToolTip(fly_i18n("Match.UndoAlertTooltip"));
	controls->addWidget(new QLabel(fly_i18n("Match.AlertSeconds"), page));
	controls->addWidget(alertSeconds_);
	controls->addStretch(1);
	controls->addWidget(hide);
	controls->addWidget(undo);
	vbox->addLayout(controls);

	alertStatus_ = new QLabel(page);
	alertStatus_->setWordWrap(true);
	vbox->addWidget(alertStatus_);

	auto *hint = new QLabel(fly_i18n("Match.AlertHint"), page);
	hint->setWordWrap(true);
	vbox->addWidget(hint);
	vbox->addStretch(1);

	connect(alertSeconds_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int seconds) {
		st_.alert_duration_ms = static_cast<long long>(seconds) * kMsPerSecond;
		saveState();
	});
	connect(hide, &QPushButton::clicked, this, [this]() { hideAlert(); });
	connect(undo, &QPushButton::clicked, this, [this]() { undoLastAlert(); });

	return page;
}

QWidget *FlyScoreDock::buildShootoutTab(QWidget *parent)
{
	auto *page = new QWidget(parent);
	auto *vbox = new QVBoxLayout(page);
	vbox->setContentsMargins(6, 6, 6, 6);
	vbox->setSpacing(6);

	auto *setup = new QGridLayout();
	shootoutFirst_ = new QComboBox(page);
	shootoutFirst_->addItem(fly_i18n("Common.Home"), QStringLiteral("home"));
	shootoutFirst_->addItem(fly_i18n("Common.Guests"), QStringLiteral("away"));
	shootoutRounds_ = new QSpinBox(page);
	shootoutRounds_->setRange(1, 20);
	shootoutRounds_->setValue(5);
	auto *start = makeButton(fly_i18n("Match.ShootoutStart"), page);
	auto *end = makeButton(fly_i18n("Match.ShootoutEnd"), page);
	setup->addWidget(new QLabel(fly_i18n("Match.ShootoutFirst"), page), 0, 0);
	setup->addWidget(shootoutFirst_, 0, 1);
	setup->addWidget(new QLabel(fly_i18n("Match.ShootoutRounds"), page), 1, 0);
	setup->addWidget(shootoutRounds_, 1, 1);
	setup->addWidget(start, 0, 2);
	setup->addWidget(end, 1, 2);
	setup->setColumnStretch(1, 1);
	vbox->addLayout(setup);

	auto buildTeam = [this, page](bool home) {
		auto *group = new QGroupBox(page);
		auto *grid = new QGridLayout(group);
		grid->setContentsMargins(8, 8, 8, 8);
		grid->setSpacing(6);

		auto *combo = new QComboBox(group);
		combo->setAccessibleName(fly_i18n("Match.Kicker"));
		auto *scored = makeButton(fly_i18n("Match.PenaltyScored"), group);
		auto *missed = makeButton(fly_i18n("Match.PenaltyMissed"), group);

		grid->addWidget(new QLabel(fly_i18n("Match.Kicker"), group), 0, 0);
		grid->addWidget(combo, 0, 1, 1, 2);
		grid->addWidget(scored, 1, 1);
		grid->addWidget(missed, 1, 2);
		grid->setColumnStretch(1, 1);
		grid->setColumnStretch(2, 1);

		connect(scored, &QPushButton::clicked, this, [this, home]() { addShootoutKick(home, true); });
		connect(missed, &QPushButton::clicked, this, [this, home]() { addShootoutKick(home, false); });

		(home ? shootoutKickerHome_ : shootoutKickerAway_) = combo;
		return group;
	};

	vbox->addWidget(buildTeam(true));
	vbox->addWidget(buildTeam(false));

	auto *controls = new QHBoxLayout();
	shootoutVisible_ = new QCheckBox(fly_i18n("Match.ShootoutShow"), page);
	auto *undo = makeButton(fly_i18n("Match.ShootoutUndo"), page);
	controls->addWidget(shootoutVisible_);
	controls->addStretch(1);
	controls->addWidget(undo);
	vbox->addLayout(controls);

	shootoutStatus_ = new QLabel(page);
	shootoutStatus_->setWordWrap(true);
	vbox->addWidget(shootoutStatus_);

	auto *hint = new QLabel(fly_i18n("Match.ShootoutHint"), page);
	hint->setWordWrap(true);
	vbox->addWidget(hint);
	vbox->addStretch(1);

	connect(start, &QPushButton::clicked, this, [this]() { startShootout(); });
	connect(end, &QPushButton::clicked, this, [this]() { endShootout(); });
	connect(undo, &QPushButton::clicked, this, [this]() { undoShootoutKick(); });
	connect(shootoutVisible_, &QCheckBox::toggled, this, [this](bool on) {
		st_.shootout.visible = on;
		saveState();
	});

	return page;
}

QWidget *FlyScoreDock::buildSponsorsTab(QWidget *parent)
{
	auto *page = new QWidget(parent);
	auto *vbox = new QVBoxLayout(page);
	vbox->setContentsMargins(6, 6, 6, 6);
	vbox->setSpacing(6);

	sponsorsVisible_ = new QCheckBox(fly_i18n("Match.SponsorsShow"), page);
	vbox->addWidget(sponsorsVisible_);

	auto *modeRow = new QHBoxLayout();
	sponsorsMode_ = new QComboBox(page);
	sponsorsMode_->addItem(fly_i18n("Match.SponsorsRotate"), QStringLiteral("rotate"));
	sponsorsMode_->addItem(fly_i18n("Match.SponsorsAll"), QStringLiteral("all"));
	sponsorsSeconds_ = new QSpinBox(page);
	sponsorsSeconds_->setRange(1, 600);
	sponsorsSeconds_->setSuffix(QStringLiteral(" s"));
	sponsorsSeconds_->setToolTip(fly_i18n("Match.SponsorsSeconds"));
	modeRow->addWidget(sponsorsMode_, 1);
	modeRow->addWidget(new QLabel(fly_i18n("Match.SponsorsSeconds"), page));
	modeRow->addWidget(sponsorsSeconds_);
	vbox->addLayout(modeRow);

	sponsorsList_ = new QListWidget(page);
	sponsorsList_->setIconSize(QSize(48, 48));
	sponsorsList_->setMinimumHeight(140);
	vbox->addWidget(sponsorsList_, 1);

	auto *buttons = new QHBoxLayout();
	auto *add = makeButton(fly_i18n("Match.SponsorsAdd"), page);
	auto *remove = makeButton(fly_i18n("Match.SponsorsRemove"), page);
	auto *up = makeButton(QStringLiteral("▲"), page);
	auto *down = makeButton(QStringLiteral("▼"), page);
	up->setToolTip(fly_i18n("Match.SponsorsUp"));
	down->setToolTip(fly_i18n("Match.SponsorsDown"));
	up->setAccessibleName(fly_i18n("Match.SponsorsUp"));
	down->setAccessibleName(fly_i18n("Match.SponsorsDown"));
	buttons->addWidget(add);
	buttons->addWidget(remove);
	buttons->addStretch(1);
	buttons->addWidget(up);
	buttons->addWidget(down);
	vbox->addLayout(buttons);

	auto *hint = new QLabel(fly_i18n("Match.SponsorsHint"), page);
	hint->setWordWrap(true);
	vbox->addWidget(hint);

	connect(sponsorsVisible_, &QCheckBox::toggled, this, [this](bool on) {
		st_.show_sponsors = on;
		saveState();
	});
	connect(sponsorsMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
		st_.sponsors_mode = sponsorsMode_->currentData().toString();
		saveState();
	});
	connect(sponsorsSeconds_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int seconds) {
		st_.sponsors_rotate_ms = static_cast<long long>(seconds) * kMsPerSecond;
		saveState();
	});
	connect(sponsorsList_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
		const int row = sponsorsList_->row(item);
		if (row < 0 || row >= st_.sponsors.size())
			return;
		st_.sponsors[row].name = item->text().trimmed();
		saveState();
	});
	connect(add, &QPushButton::clicked, this, [this]() { addSponsorsFromFiles(); });
	connect(remove, &QPushButton::clicked, this, [this]() { removeSelectedSponsor(); });
	connect(up, &QPushButton::clicked, this, [this]() { moveSelectedSponsor(-1); });
	connect(down, &QPushButton::clicked, this, [this]() { moveSelectedSponsor(+1); });

	return page;
}

// -----------------------------------------------------------------------------
// UI refresh
// -----------------------------------------------------------------------------

void FlyScoreDock::refreshMatchUi()
{
	fillPlayerCombo(alertPlayerHome_, st_.home.players);
	fillPlayerCombo(alertPlayerAway_, st_.away.players);
	fillPlayerCombo(shootoutKickerHome_, st_.home.players);
	fillPlayerCombo(shootoutKickerAway_, st_.away.players);

	setGroupTitle(alertPlayerHome_, teamDisplayName(st_, true));
	setGroupTitle(alertPlayerAway_, teamDisplayName(st_, false));
	setGroupTitle(shootoutKickerHome_, teamDisplayName(st_, true));
	setGroupTitle(shootoutKickerAway_, teamDisplayName(st_, false));

	if (alertSeconds_) {
		const QSignalBlocker blocker(alertSeconds_);
		alertSeconds_->setValue(static_cast<int>(st_.alert_duration_ms / kMsPerSecond));
	}
	if (shootoutVisible_) {
		const QSignalBlocker blocker(shootoutVisible_);
		shootoutVisible_->setChecked(st_.shootout.visible);
	}
	if (sponsorsVisible_) {
		const QSignalBlocker blocker(sponsorsVisible_);
		sponsorsVisible_->setChecked(st_.show_sponsors);
	}
	if (sponsorsMode_) {
		const QSignalBlocker blocker(sponsorsMode_);
		const int index = sponsorsMode_->findData(st_.sponsors_mode);
		sponsorsMode_->setCurrentIndex(index >= 0 ? index : 0);
	}
	if (sponsorsSeconds_) {
		const QSignalBlocker blocker(sponsorsSeconds_);
		sponsorsSeconds_->setValue(static_cast<int>(st_.sponsors_rotate_ms / kMsPerSecond));
	}

	refreshAlertStatus();
	refreshShootoutStatus();
	refreshSponsorsList();
}

void FlyScoreDock::refreshAlertStatus()
{
	if (!alertStatus_)
		return;

	const FlyAlert &a = st_.alert;
	if (a.type.isEmpty()) {
		alertStatus_->setText(fly_i18n("Match.NoAlertYet"));
		return;
	}

	FlyPlayer player;
	player.number = a.player_number;
	player.name = a.player_name;
	const QString who = fly_player_display(player);
	const QString team = teamDisplayName(st_, a.team != QLatin1String("away"));
	QString text = QStringLiteral("%1 - %2").arg(a.title, team);
	if (!who.isEmpty())
		text += QStringLiteral(" - ") + who;
	if (!a.minute.isEmpty())
		text += QStringLiteral(" (%1)").arg(a.minute);
	alertStatus_->setText(fly_i18n("Match.LastAlert").arg(text));
}

void FlyScoreDock::refreshShootoutStatus()
{
	if (!shootoutStatus_)
		return;

	const FlyShootout &so = st_.shootout;
	if (!so.active && so.sequence.isEmpty()) {
		shootoutStatus_->setText(fly_i18n("Match.ShootoutInactive"));
		return;
	}

	const QString next = fly_shootout_next_team(so) == QLatin1String("home") ? teamDisplayName(st_, true)
										  : teamDisplayName(st_, false);
	shootoutStatus_->setText(fly_i18n("Match.ShootoutStatus")
					 .arg(teamDisplayName(st_, true))
					 .arg(fly_shootout_goals(so.home))
					 .arg(so.home.size())
					 .arg(teamDisplayName(st_, false))
					 .arg(fly_shootout_goals(so.away))
					 .arg(so.away.size())
					 .arg(so.active ? next : fly_i18n("Match.ShootoutFinished")));
}

void FlyScoreDock::refreshSponsorsList()
{
	if (!sponsorsList_)
		return;

	const int previousRow = sponsorsList_->currentRow();
	const QSignalBlocker blocker(sponsorsList_);
	sponsorsList_->clear();
	const QDir root(dataDir_);
	for (const auto &sponsor : st_.sponsors) {
		auto *item = new QListWidgetItem(QIcon(root.filePath(sponsor.logo)),
						 sponsor.name.isEmpty() ? sponsor.logo : sponsor.name);
		item->setFlags(item->flags() | Qt::ItemIsEditable);
		item->setToolTip(sponsor.logo);
		sponsorsList_->addItem(item);
	}
	if (previousRow >= 0 && previousRow < sponsorsList_->count())
		sponsorsList_->setCurrentRow(previousRow);
}

// -----------------------------------------------------------------------------
// Alerts
// -----------------------------------------------------------------------------

void FlyScoreDock::logMatchEvent(const QString &line)
{
	// Only write while a log session runs (stream/recording or started by hand):
	// a goal must not silently start a new log file.
	if (eventLog_.isActive())
		eventLog_.appendEvent(line.simplified());
}

void FlyScoreDock::triggerAlert(FlyAlertKind kind, bool home)
{
	const auto &roster = fly_team_ref(st_, home).players;
	const FlyPlayer *player = selectedPlayer(home ? alertPlayerHome_ : alertPlayerAway_, roster);
	fireAlert(kind, home, player);
}

void FlyScoreDock::fireAlert(FlyAlertKind kind, bool home, const FlyPlayer *player)
{
	const qint64 now = fly_now_ms();

	FlyAlert alert;
	alert.type = fly_alert_kind_key(kind);
	alert.team = fly_team_key(home);
	alert.title = alertTitle(kind);
	if (player) {
		alert.player_number = player->number;
		alert.player_name = player->name;
	}
	alert.minute = fly_match_minute(st_, now);
	alert.started_ms = now;
	alert.duration_ms = st_.alert_duration_ms;
	alert.visible = true;
	st_.alert = alert;

	lastAlertChange_ = LastAlertChange{};
	const int fieldIndex = fly_alert_kind_field_index(kind);
	if (fieldIndex >= 0 && fieldIndex < st_.custom_fields.size()) {
		auto &field = st_.custom_fields[fieldIndex];
		int &value = home ? field.home : field.away;
		value = std::max(0, value) + 1;
		lastAlertChange_ = LastAlertChange{true, fieldIndex, home};
	}

	QString line = QStringLiteral("%1 - %2").arg(alert.title, teamDisplayName(st_, home));
	if (player)
		line += QStringLiteral(" - ") + fly_player_display(*player);
	if (!alert.minute.isEmpty())
		line += QStringLiteral(" (%1)").arg(alert.minute);
	logMatchEvent(line);

	saveState();
	refreshUiFromState(false);
}

void FlyScoreDock::hideAlert()
{
	if (!st_.alert.visible)
		return;
	st_.alert.visible = false;
	saveState();
	refreshAlertStatus();
}

void FlyScoreDock::undoLastAlert()
{
	st_.alert.visible = false;
	if (lastAlertChange_.valid && lastAlertChange_.fieldIndex < st_.custom_fields.size()) {
		auto &field = st_.custom_fields[lastAlertChange_.fieldIndex];
		int &value = lastAlertChange_.home ? field.home : field.away;
		value = std::max(0, value - 1);
		logMatchEvent(fly_i18n("Match.AlertUndone"));
	}
	lastAlertChange_ = LastAlertChange{};
	saveState();
	refreshUiFromState(false);
}

void FlyScoreDock::cycleSelectedPlayer(bool home, int delta)
{
	// During a shootout the kicker selector is the one in use.
	if (st_.shootout.active)
		cycleCombo(home ? shootoutKickerHome_ : shootoutKickerAway_, delta);
	else
		cycleCombo(home ? alertPlayerHome_ : alertPlayerAway_, delta);
}

// -----------------------------------------------------------------------------
// Penalty shootout
// -----------------------------------------------------------------------------

void FlyScoreDock::startShootout()
{
	if (st_.shootout.active && !st_.shootout.sequence.isEmpty()) {
		const auto rc = QMessageBox::question(this, fly_i18n("Match.Shootout"),
						      fly_i18n("Match.ShootoutRestartConfirm"));
		if (rc != QMessageBox::Yes)
			return;
	}

	const bool homeFirst = !shootoutFirst_ || shootoutFirst_->currentData().toString() != QLatin1String("away");
	const int rounds = shootoutRounds_ ? shootoutRounds_->value() : 5;
	fly_shootout_start(st_.shootout, homeFirst, rounds);
	logMatchEvent(fly_i18n("Match.ShootoutStarted"));
	saveState();
	refreshMatchUi();
}

void FlyScoreDock::endShootout()
{
	if (!st_.shootout.active)
		return;
	st_.shootout.active = false;
	logMatchEvent(fly_i18n("Match.ShootoutEnded")
			    .arg(fly_shootout_goals(st_.shootout.home))
			    .arg(fly_shootout_goals(st_.shootout.away)));
	saveState();
	refreshMatchUi();
}

void FlyScoreDock::addShootoutKick(bool home, bool goal)
{
	QComboBox *kicker = home ? shootoutKickerHome_ : shootoutKickerAway_;
	const FlyPlayer *player = selectedPlayer(kicker, fly_team_ref(st_, home).players);
	recordShootoutKick(home, goal, player ? fly_player_display(*player) : QString());

	// Pre-select the next kicker of that team.
	if (player)
		cycleCombo(kicker, +1);
}

void FlyScoreDock::recordShootoutKick(bool home, bool goal, const QString &player)
{
	if (!st_.shootout.active) {
		// A finished shootout keeps its result: ignore stray kicks until a new one starts.
		if (!st_.shootout.sequence.isEmpty())
			return;
		const bool homeFirst =
			!shootoutFirst_ || shootoutFirst_->currentData().toString() != QLatin1String("away");
		fly_shootout_start(st_.shootout, homeFirst, shootoutRounds_ ? shootoutRounds_->value() : 5);
	}

	fly_shootout_add_kick(st_.shootout, home, goal, player);

	QString line = fly_i18n(goal ? "Match.PenaltyScoredLog" : "Match.PenaltyMissedLog")
			       .arg(teamDisplayName(st_, home));
	if (!player.isEmpty())
		line += QStringLiteral(" - ") + player;
	line += QStringLiteral(" (%1-%2)")
			.arg(fly_shootout_goals(st_.shootout.home))
			.arg(fly_shootout_goals(st_.shootout.away));
	logMatchEvent(line);

	saveState();
	refreshShootoutStatus();
}

void FlyScoreDock::undoShootoutKick()
{
	if (!fly_shootout_undo(st_.shootout))
		return;
	st_.shootout.active = true;
	logMatchEvent(fly_i18n("Match.ShootoutUndone"));
	saveState();
	refreshShootoutStatus();
}

void FlyScoreDock::toggleShootoutVisible()
{
	st_.shootout.visible = !st_.shootout.visible;
	saveState();
	refreshMatchUi();
}

// -----------------------------------------------------------------------------
// Sponsors
// -----------------------------------------------------------------------------

void FlyScoreDock::toggleSponsorsVisible()
{
	st_.show_sponsors = !st_.show_sponsors;
	saveState();
	refreshMatchUi();
}

void FlyScoreDock::addSponsorsFromFiles()
{
	const QStringList files = QFileDialog::getOpenFileNames(this, fly_i18n("Match.SponsorsSelect"), {},
								fly_i18n("Common.ImageFilter"));
	if (files.isEmpty())
		return;

	int failed = 0;
	for (const QString &file : files) {
		// Unique prefix: fly_copy_logo_to_overlay deletes files sharing the prefix.
		const QString prefix =
			QStringLiteral("sponsor-%1").arg(QUuid::createUuid().toString(QUuid::Id128).left(8));
		const QString rel = fly_copy_logo_to_overlay(dataDir_, file, prefix);
		if (rel.isEmpty()) {
			++failed;
			continue;
		}
		FlySponsor sponsor;
		sponsor.name = QFileInfo(file).completeBaseName();
		sponsor.logo = rel;
		st_.sponsors.push_back(sponsor);
	}

	if (failed > 0)
		QMessageBox::warning(this, fly_i18n("Match.Sponsors"), fly_i18n("Teams.CopyLogoFailed"));

	saveState();
	refreshSponsorsList();
	if (sponsorsList_)
		sponsorsList_->setCurrentRow(sponsorsList_->count() - 1);
}

void FlyScoreDock::removeSelectedSponsor()
{
	const int row = sponsorsList_ ? sponsorsList_->currentRow() : -1;
	if (row < 0 || row >= st_.sponsors.size())
		return;

	fly_delete_logo_if_exists(dataDir_, st_.sponsors[row].logo);
	st_.sponsors.removeAt(row);
	saveState();
	refreshSponsorsList();
}

void FlyScoreDock::moveSelectedSponsor(int delta)
{
	const int row = sponsorsList_ ? sponsorsList_->currentRow() : -1;
	const int target = row + delta;
	if (row < 0 || row >= st_.sponsors.size() || target < 0 || target >= st_.sponsors.size())
		return;

	st_.sponsors.swapItemsAt(row, target);
	saveState();
	refreshSponsorsList();
	sponsorsList_->setCurrentRow(target);
}

// -----------------------------------------------------------------------------
// Global OBS hotkeys
// -----------------------------------------------------------------------------

void FlyScoreDock::registerObsHotkeys()
{
	obsHotkeys_ = std::make_unique<FlyObsHotkeys>(this);

	struct Binding {
		const char *name;
		const char *textKey;
		std::function<void()> action;
	};

	const std::vector<Binding> bindings = {
		{"FlyScore.GoalHome", "ObsHotkey.GoalHome", [this]() { triggerAlert(FlyAlertKind::Goal, true); }},
		{"FlyScore.GoalAway", "ObsHotkey.GoalAway", [this]() { triggerAlert(FlyAlertKind::Goal, false); }},
		{"FlyScore.YellowHome", "ObsHotkey.YellowHome",
		 [this]() { triggerAlert(FlyAlertKind::Yellow, true); }},
		{"FlyScore.YellowAway", "ObsHotkey.YellowAway",
		 [this]() { triggerAlert(FlyAlertKind::Yellow, false); }},
		{"FlyScore.RedHome", "ObsHotkey.RedHome", [this]() { triggerAlert(FlyAlertKind::Red, true); }},
		{"FlyScore.RedAway", "ObsHotkey.RedAway", [this]() { triggerAlert(FlyAlertKind::Red, false); }},
		{"FlyScore.AlertHide", "ObsHotkey.AlertHide", [this]() { hideAlert(); }},
		{"FlyScore.AlertUndo", "ObsHotkey.AlertUndo", [this]() { undoLastAlert(); }},
		{"FlyScore.NextPlayerHome", "ObsHotkey.NextPlayerHome", [this]() { cycleSelectedPlayer(true, +1); }},
		{"FlyScore.PrevPlayerHome", "ObsHotkey.PrevPlayerHome", [this]() { cycleSelectedPlayer(true, -1); }},
		{"FlyScore.NextPlayerAway", "ObsHotkey.NextPlayerAway", [this]() { cycleSelectedPlayer(false, +1); }},
		{"FlyScore.PrevPlayerAway", "ObsHotkey.PrevPlayerAway", [this]() { cycleSelectedPlayer(false, -1); }},
		{"FlyScore.PenaltyScoredHome", "ObsHotkey.PenaltyScoredHome",
		 [this]() { addShootoutKick(true, true); }},
		{"FlyScore.PenaltyMissedHome", "ObsHotkey.PenaltyMissedHome",
		 [this]() { addShootoutKick(true, false); }},
		{"FlyScore.PenaltyScoredAway", "ObsHotkey.PenaltyScoredAway",
		 [this]() { addShootoutKick(false, true); }},
		{"FlyScore.PenaltyMissedAway", "ObsHotkey.PenaltyMissedAway",
		 [this]() { addShootoutKick(false, false); }},
		{"FlyScore.PenaltyUndo", "ObsHotkey.PenaltyUndo", [this]() { undoShootoutKick(); }},
		{"FlyScore.PenaltyToggle", "ObsHotkey.PenaltyToggle", [this]() { toggleShootoutVisible(); }},
		{"FlyScore.SponsorsToggle", "ObsHotkey.SponsorsToggle", [this]() { toggleSponsorsVisible(); }},
		{"FlyScore.ScoreboardToggle", "ObsHotkey.ScoreboardToggle", [this]() { toggleScoreboardVisible(); }},
	};

	for (const auto &b : bindings)
		obsHotkeys_->add(b.name, fly_i18n(b.textKey), b.action);
}

// -----------------------------------------------------------------------------
// WebSocket commands
// -----------------------------------------------------------------------------

bool FlyScoreDock::handleMatchCommand(const QString &action, const QJsonObject &command)
{
	if (action == QLatin1String("alert") || action == QLatin1String("alert_goal") ||
	    action == QLatin1String("alert_yellow") || action == QLatin1String("alert_red")) {
		FlyAlertKind kind = FlyAlertKind::Goal;
		const QString kindKey = action == QLatin1String("alert") ? jsonText(command, "kind")
									  : action.mid(QStringLiteral("alert_").size());
		if (!fly_alert_kind_from_key(kindKey, kind))
			return true;
		const bool home = jsonIsHome(command);
		FlyPlayer player;
		const bool hasPlayer = playerFromCommand(command, fly_team_ref(st_, home).players, player);
		fireAlert(kind, home, hasPlayer ? &player : nullptr);
		return true;
	}
	if (action == QLatin1String("alert_hide")) {
		hideAlert();
		return true;
	}
	if (action == QLatin1String("alert_undo")) {
		undoLastAlert();
		return true;
	}
	if (action == QLatin1String("set_players")) {
		const bool home = jsonIsHome(command);
		QVector<FlyPlayer> players;
		for (const QJsonValue v : command.value(QStringLiteral("players")).toArray()) {
			const QJsonObject o = v.toObject();
			FlyPlayer p;
			p.number = jsonText(o, "number");
			p.name = jsonText(o, "name");
			if (!p.number.isEmpty() || !p.name.isEmpty())
				players.push_back(p);
		}
		fly_team_ref(st_, home).players = players;
		saveState();
		refreshMatchUi();
		return true;
	}
	if (action == QLatin1String("shootout_start")) {
		const bool homeFirst = jsonText(command, "first_team").toLower() != QLatin1String("away");
		fly_shootout_start(st_.shootout, homeFirst, jsonNumber(command, "rounds", 5));
		logMatchEvent(fly_i18n("Match.ShootoutStarted"));
		saveState();
		refreshMatchUi();
		return true;
	}
	if (action == QLatin1String("shootout_kick")) {
		const bool home = jsonIsHome(command);
		const QString result = jsonText(command, "result").toLower();
		if (result != QLatin1String("goal") && result != QLatin1String("miss"))
			return true;
		FlyPlayer player;
		const bool hasPlayer = playerFromCommand(command, fly_team_ref(st_, home).players, player);
		recordShootoutKick(home, result == QLatin1String("goal"),
				   hasPlayer ? fly_player_display(player) : QString());
		return true;
	}
	if (action == QLatin1String("shootout_undo")) {
		undoShootoutKick();
		return true;
	}
	if (action == QLatin1String("shootout_end")) {
		endShootout();
		return true;
	}
	if (action == QLatin1String("shootout_visibility")) {
		st_.shootout.visible = jsonVisibility(command, st_.shootout.visible);
		saveState();
		refreshMatchUi();
		return true;
	}
	if (action == QLatin1String("sponsors_visibility")) {
		st_.show_sponsors = jsonVisibility(command, st_.show_sponsors);
		saveState();
		refreshMatchUi();
		return true;
	}
	return false;
}
