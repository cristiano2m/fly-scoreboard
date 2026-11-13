#pragma once

#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QByteArray>
#include <string>

struct FlyPlayer {
	QString number;
	QString name;
};

struct FlyTeam {
	QString title;
	QString subtitle;
	QString logo;
	uint32_t color = 0xFFFFFF;
	QVector<FlyPlayer> players;
};

struct FlyTimer {
	QString label;
	QString mode;
	bool running = false;
	long long initial_ms = 0;
	long long remaining_ms = 0;
	long long last_tick_ms = 0;
	bool visible = true;
};

struct FlyCustomField {
	QString label;
	int  home    = 0;
	int  away    = 0;
	bool visible = true;
};

struct FlySingleStat {
	QString label;
	int  value   = 0;
	bool visible = true;
};

// On-screen notice for a goal or a card. Timestamps use epoch milliseconds.
struct FlyAlert {
	QString type; // "goal", "yellow" or "red"
	QString team; // "home" or "away"
	QString title;
	QString player_number;
	QString player_name;
	QString minute;
	long long started_ms = 0;
	long long duration_ms = 8000;
	bool visible = false;
};

struct FlyShootoutKick {
	bool goal = false;
	QString player;
};

// Penalty shootout. The overlay derives totals, sudden death and the winner.
struct FlyShootout {
	bool active = false;
	bool visible = false;
	int rounds = 5;
	QString first_team = QStringLiteral("home");
	QVector<FlyShootoutKick> home;
	QVector<FlyShootoutKick> away;
	QVector<QString> sequence; // team of each kick, in order, for undo
};

struct FlySponsor {
	QString name;
	QString logo;
};

struct FlyState {
	FlyTeam home;
	FlyTeam away;

	bool swap_sides      = false;
	bool show_scoreboard = true;

	QVector<FlyCustomField> custom_fields;
	QVector<FlySingleStat> single_stats;
	QVector<FlyTimer> timers;

	FlyAlert alert;
	long long alert_duration_ms = 8000;
	FlyShootout shootout;

	QVector<FlySponsor> sponsors;
	bool show_sponsors = false;
	QString sponsors_mode = QStringLiteral("rotate"); // "rotate" or "all"
	long long sponsors_rotate_ms = 6000;
};

bool     fly_state_read_json(const std::string &base_dir, std::string &out_json);
bool     fly_state_write_json(const std::string &base_dir, const std::string &json);
QJsonObject fly_state_to_json_object(const FlyState &st);
bool     fly_state_from_json_object(const QJsonObject &j, FlyState &st);
QByteArray fly_state_to_json_bytes(const FlyState &st, bool compact = true);
bool     fly_state_load(const QString &base_dir, FlyState &out);
bool     fly_state_save(const QString &base_dir, const FlyState &st);
QString  fly_data_dir();
bool     fly_ensure_webroot(QString *outBaseDir = nullptr);
FlyState fly_state_make_defaults();
bool     fly_state_reset_defaults(const QString &base_dir);
bool fly_state_ensure_json_exists(const QString &base_dir, const FlyState *writeState = nullptr);
