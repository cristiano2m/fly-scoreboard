#include "config.hpp"

#define LOG_TAG "[" PLUGIN_NAME "][state]"
#include "fly_score_log.hpp"

#include "fly_score_i18n.hpp"
#include "fly_score_state.hpp"

#include <obs-module.h>
#include <util/platform.h>

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>

static QString moduleBaseDirFromConfigFile()
{
	char *p = obs_module_config_path("plugin.json");
	QString filePath = p ? QString::fromUtf8(p) : QString();
	if (p)
		bfree(p);
	return filePath.isEmpty() ? QDir::homePath() : QFileInfo(filePath).absolutePath();
}

static QString overlay_dir_path(const QString &base_dir)
{
	return QDir(base_dir).absolutePath();
}

static QString overlay_plugin_json(const QString &base_dir)
{
	return QDir(overlay_dir_path(base_dir)).filePath(QStringLiteral("plugin.json"));
}

QString fly_data_dir()
{
	return QDir::cleanPath(moduleBaseDirFromConfigFile());
}

bool fly_ensure_webroot(QString *outBaseDir)
{
	const QString base = fly_data_dir();
	const QString webroot = overlay_dir_path(base);
	QDir().mkpath(webroot);
	if (outBaseDir)
		*outBaseDir = base;
	return true;
}

static FlyTimer makeDefaultMainTimer()
{
	FlyTimer t;
	t.label = fly_i18n("Default.Timer.FirstHalf");
	t.mode = QStringLiteral("countup");
	t.running = false;
	t.initial_ms = 0;
	t.remaining_ms = 0;
	t.last_tick_ms = 0;
	t.visible = true;
	return t;
}

static void ensureDefaultCustomFields(FlyState &st)
{
	if (st.custom_fields.isEmpty()) {
		const QStringList labels = {
			fly_i18n("Default.Field.Score"),       fly_i18n("Default.Field.Fouls"),
			fly_i18n("Default.Field.YellowCards"), fly_i18n("Default.Field.RedCards"),
			fly_i18n("Default.Field.Corners"),
		};
		for (const QString &label : labels) {
			FlyCustomField field;
			field.label = label;
			field.visible = true;
			st.custom_fields.push_back(field);
		}
		return;
	}

	if (st.custom_fields[0].label.isEmpty())
		st.custom_fields[0].label = fly_i18n("Default.Field.Score");
}

static void ensureDefaultSingleStats(FlyState &st)
{
	if (st.single_stats.isEmpty()) {
		FlySingleStat ss;
		ss.label = fly_i18n("Default.Single.Period");
		ss.value = 0;
		ss.visible = true;
		st.single_stats.push_back(ss);
		return;
	}

	if (st.single_stats[0].label.isEmpty())
		st.single_stats[0].label = fly_i18n("Default.Single.Period");
}

static QJsonObject timerToJson(const FlyTimer &t)
{
	QJsonObject o;
	o["label"] = t.label;
	o["mode"] = t.mode;
	o["running"] = t.running;
	o["initial_ms"] = QString::number(t.initial_ms);
	o["remaining_ms"] = QString::number(t.remaining_ms);
	o["last_tick_ms"] = QString::number(t.last_tick_ms);
	o["visible"] = t.visible;
	return o;
}

static FlyTimer timerFromJson(const QJsonObject &o)
{
	FlyTimer t;
	t.label = o.value("label").toString();
	t.mode = o.value("mode").toString("countdown");
	t.running = o.value("running").toBool(false);
	t.initial_ms = o.value("initial_ms").toString("0").toLongLong();
	t.remaining_ms = o.value("remaining_ms").toString("0").toLongLong();
	t.last_tick_ms = o.value("last_tick_ms").toString("0").toLongLong();
	t.visible = o.value("visible").toBool(true);
	return t;
}

static QString msToJson(long long ms)
{
	return QString::number(ms);
}

static long long msFromJson(const QJsonValue &v, long long fallback)
{
	if (v.isDouble())
		return static_cast<long long>(v.toDouble());
	if (v.isString()) {
		bool ok = false;
		const long long parsed = v.toString().toLongLong(&ok);
		return ok ? parsed : fallback;
	}
	return fallback;
}

static QString normalizedTeamKey(const QString &team)
{
	return team == QLatin1String("away") ? QStringLiteral("away") : QStringLiteral("home");
}

static QJsonArray playersToJson(const QVector<FlyPlayer> &players)
{
	QJsonArray arr;
	for (const auto &p : players) {
		QJsonObject o;
		o["number"] = p.number;
		o["name"] = p.name;
		arr.append(o);
	}
	return arr;
}

static QVector<FlyPlayer> playersFromJson(const QJsonValue &v)
{
	QVector<FlyPlayer> out;
	for (const QJsonValue item : v.toArray()) {
		if (!item.isObject())
			continue;
		const QJsonObject o = item.toObject();
		FlyPlayer p;
		p.number = o.value("number").isDouble() ? QString::number(o.value("number").toInt())
							: o.value("number").toString();
		p.name = o.value("name").toString();
		if (!p.number.trimmed().isEmpty() || !p.name.trimmed().isEmpty())
			out.push_back(p);
	}
	return out;
}

static QJsonObject alertToJson(const FlyAlert &a)
{
	QJsonObject o;
	o["type"] = a.type;
	o["team"] = a.team;
	o["title"] = a.title;
	o["player_number"] = a.player_number;
	o["player_name"] = a.player_name;
	o["minute"] = a.minute;
	o["started_ms"] = msToJson(a.started_ms);
	o["duration_ms"] = msToJson(a.duration_ms);
	o["visible"] = a.visible;
	return o;
}

static FlyAlert alertFromJson(const QJsonObject &o)
{
	FlyAlert a;
	a.type = o.value("type").toString();
	a.team = normalizedTeamKey(o.value("team").toString());
	a.title = o.value("title").toString();
	a.player_number = o.value("player_number").toString();
	a.player_name = o.value("player_name").toString();
	a.minute = o.value("minute").toString();
	a.started_ms = msFromJson(o.value("started_ms"), 0);
	a.duration_ms = msFromJson(o.value("duration_ms"), 8000);
	a.visible = o.value("visible").toBool(false);
	return a;
}

static QJsonArray kicksToJson(const QVector<FlyShootoutKick> &kicks)
{
	QJsonArray arr;
	for (const auto &k : kicks) {
		QJsonObject o;
		o["result"] = k.goal ? QStringLiteral("goal") : QStringLiteral("miss");
		o["player"] = k.player;
		arr.append(o);
	}
	return arr;
}

static QVector<FlyShootoutKick> kicksFromJson(const QJsonValue &v)
{
	QVector<FlyShootoutKick> out;
	for (const QJsonValue item : v.toArray()) {
		if (!item.isObject())
			continue;
		const QJsonObject o = item.toObject();
		FlyShootoutKick k;
		k.goal = o.value("result").toString() == QLatin1String("goal");
		k.player = o.value("player").toString();
		out.push_back(k);
	}
	return out;
}

static QJsonObject shootoutToJson(const FlyShootout &so)
{
	QJsonObject o;
	o["active"] = so.active;
	o["visible"] = so.visible;
	o["rounds"] = so.rounds;
	o["first_team"] = so.first_team;
	o["home"] = kicksToJson(so.home);
	o["away"] = kicksToJson(so.away);
	QJsonArray seq;
	for (const auto &team : so.sequence)
		seq.append(team);
	o["sequence"] = seq;
	return o;
}

static FlyShootout shootoutFromJson(const QJsonObject &o)
{
	FlyShootout so;
	so.active = o.value("active").toBool(false);
	so.visible = o.value("visible").toBool(false);
	so.rounds = std::clamp(o.value("rounds").toInt(5), 1, 20);
	so.first_team = normalizedTeamKey(o.value("first_team").toString());
	so.home = kicksFromJson(o.value("home"));
	so.away = kicksFromJson(o.value("away"));
	for (const QJsonValue v : o.value("sequence").toArray())
		so.sequence.push_back(normalizedTeamKey(v.toString()));
	return so;
}

static QJsonArray sponsorsToJson(const QVector<FlySponsor> &sponsors)
{
	QJsonArray arr;
	for (const auto &sp : sponsors) {
		QJsonObject o;
		o["name"] = sp.name;
		o["logo"] = sp.logo;
		arr.append(o);
	}
	return arr;
}

static QVector<FlySponsor> sponsorsFromJson(const QJsonValue &v)
{
	QVector<FlySponsor> out;
	for (const QJsonValue item : v.toArray()) {
		if (!item.isObject())
			continue;
		const QJsonObject o = item.toObject();
		FlySponsor sp;
		sp.name = o.value("name").toString();
		sp.logo = o.value("logo").toString();
		if (!sp.logo.trimmed().isEmpty())
			out.push_back(sp);
	}
	return out;
}

QJsonObject fly_state_to_json_object(const FlyState &stIn)
{
	FlyState st = stIn;

	ensureDefaultCustomFields(st);
	ensureDefaultSingleStats(st);

	QJsonObject j;
	j["version"] = 5;

	auto teamToJson = [](const FlyTeam &tm) {
		QJsonObject o;
		o["title"] = tm.title;
		o["subtitle"] = tm.subtitle;
		o["logo"] = tm.logo;
		o["color"] = QString::number(tm.color);
		o["players"] = playersToJson(tm.players);

		return o;
	};

	j["home"] = teamToJson(st.home);
	j["away"] = teamToJson(st.away);
	j["swap_sides"] = st.swap_sides;
	j["show_scoreboard"] = st.show_scoreboard;

	QJsonArray cfArr;
	for (const auto &cf : st.custom_fields) {
		QJsonObject o;
		o["label"] = cf.label;
		o["home"] = cf.home;
		o["away"] = cf.away;
		o["visible"] = cf.visible;
		cfArr.append(o);
	}
	j["custom_fields"] = cfArr;

	QJsonArray ssArr;
	for (const auto &ss : st.single_stats) {
		QJsonObject o;
		o["label"] = ss.label;
		o["value"] = ss.value;
		o["visible"] = ss.visible;
		ssArr.append(o);
	}
	j["single_stats"] = ssArr;

	QJsonArray timersArr;
	if (st.timers.isEmpty()) {
		timersArr.append(timerToJson(makeDefaultMainTimer()));
	} else {
		for (const auto &tm : st.timers) {
			timersArr.append(timerToJson(tm));
		}
	}
	j["timers"] = timersArr;

	j["alert"] = alertToJson(st.alert);
	j["alert_duration_ms"] = msToJson(st.alert_duration_ms);
	j["shootout"] = shootoutToJson(st.shootout);
	j["sponsors"] = sponsorsToJson(st.sponsors);
	j["show_sponsors"] = st.show_sponsors;
	j["sponsors_mode"] = st.sponsors_mode;
	j["sponsors_rotate_ms"] = msToJson(st.sponsors_rotate_ms);

	return j;
}

bool fly_state_from_json_object(const QJsonObject &j, FlyState &st)
{
	auto readColor = [](const QJsonObject &o, const char *key, uint32_t def = 0xFFFFFF) -> uint32_t {
		const QJsonValue v = o.value(key);

		if (v.isDouble())
			return static_cast<uint32_t>(v.toInt());

		if (v.isString()) {
			QString s = v.toString().trimmed();
			if (s.startsWith("0x", Qt::CaseInsensitive))
				return s.mid(2).toUInt(nullptr, 16);
			return s.toUInt(nullptr, 10);
		}

		return def;
	};

	auto readTeam = [&](const QJsonObject &o) {
		FlyTeam tm;
		tm.title = o.value("title").toString();
		tm.subtitle = o.value("subtitle").toString();
		tm.logo = o.value("logo").toString();
		tm.color = readColor(o, "color", 0xFFFFFF);
		tm.players = playersFromJson(o.value("players"));
		return tm;
	};

	st.home = readTeam(j.value("home").toObject());
	st.away = readTeam(j.value("away").toObject());
	st.swap_sides = j.value("swap_sides").toBool(false);
	st.show_scoreboard = j.value("show_scoreboard").toBool(true);
	st.custom_fields.clear();

	const QJsonValue cfVal = j.value("custom_fields");
	if (cfVal.isArray()) {
		const QJsonArray cfArr = cfVal.toArray();
		st.custom_fields.reserve(cfArr.size());

		for (const QJsonValue v : cfArr) {
			if (!v.isObject())
				continue;

			const QJsonObject o = v.toObject();

			FlyCustomField cf;
			cf.label = o.value("label").toString();
			cf.home = o.value("home").toInt(0);
			cf.away = o.value("away").toInt(0);
			cf.visible = o.value("visible").toBool(true);

			st.custom_fields.push_back(cf);
		}
	}

	st.single_stats.clear();

	const QJsonValue ssVal = j.value("single_stats");
	if (ssVal.isArray()) {
		const QJsonArray ssArr = ssVal.toArray();
		st.single_stats.reserve(ssArr.size());

		for (const QJsonValue v : ssArr) {
			if (!v.isObject())
				continue;

			const QJsonObject o = v.toObject();

			FlySingleStat ss;
			ss.label = o.value("label").toString();
			ss.value = o.value("value").toInt(0);
			ss.visible = o.value("visible").toBool(true);

			st.single_stats.push_back(ss);
		}
	}

	ensureDefaultSingleStats(st);
	ensureDefaultCustomFields(st);

	st.timers.clear();

	const QJsonValue timersVal = j.value("timers");
	if (timersVal.isArray()) {
		const QJsonArray timersArr = timersVal.toArray();

		if (!timersArr.isEmpty()) {
			st.timers.reserve(timersArr.size());

			for (const QJsonValue v : timersArr) {
				if (!v.isObject())
					continue;

				st.timers.push_back(timerFromJson(v.toObject()));
			}
		}
	} else {
		const QJsonObject tObj = j.value("timer").toObject();
		if (!tObj.isEmpty())
			st.timers.push_back(timerFromJson(tObj));
	}

	if (st.timers.isEmpty())
		st.timers.push_back(makeDefaultMainTimer());

	FlyTimer &main = st.timers[0];
	if (main.mode.isEmpty())
		main.mode = "countdown";

	st.alert = alertFromJson(j.value("alert").toObject());
	st.alert_duration_ms = std::clamp(msFromJson(j.value("alert_duration_ms"), 8000), 1000LL, 120000LL);
	st.shootout = shootoutFromJson(j.value("shootout").toObject());
	st.sponsors = sponsorsFromJson(j.value("sponsors"));
	st.show_sponsors = j.value("show_sponsors").toBool(false);
	st.sponsors_mode = j.value("sponsors_mode").toString() == QLatin1String("all") ? QStringLiteral("all")
										       : QStringLiteral("rotate");
	st.sponsors_rotate_ms = std::clamp(msFromJson(j.value("sponsors_rotate_ms"), 6000), 1000LL, 600000LL);

	return true;
}

bool fly_state_read_json(const std::string &base_dir_s, std::string &out_json)
{
	const QString base_dir = QString::fromStdString(base_dir_s);
	const QString path = overlay_plugin_json(base_dir);
	QFile f(path);
	if (!f.exists() || !f.open(QIODevice::ReadOnly))
		return false;
	const QByteArray data = f.readAll();
	out_json.assign(data.constData(), data.size());
	return true;
}

bool fly_state_write_json(const std::string &base_dir_s, const std::string &json)
{
	const QString base_dir = QString::fromStdString(base_dir_s);
	const QString path = overlay_plugin_json(base_dir);
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly))
		return false;
	const auto data = QByteArray::fromStdString(json);
	return f.write(data) == data.size() && f.commit();
}

static bool write_one_json(const QString &path, const QJsonDocument &doc)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly))
		return false;
	const auto data = doc.toJson(QJsonDocument::Compact);
	return f.write(data) == data.size() && f.commit();
}

bool fly_state_load(const QString &base_dir, FlyState &out)
{
	const QString path = overlay_plugin_json(base_dir);
	QFile f(path);
	if (!f.exists() || !f.open(QIODevice::ReadOnly))
		return false;

	const auto doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isObject())
		return false;

	return fly_state_from_json_object(doc.object(), out);
}

bool fly_state_save(const QString &base_dir, const FlyState &st)
{
	const QJsonDocument doc(fly_state_to_json_object(st));
	const QString path = overlay_plugin_json(base_dir);
	return write_one_json(path, doc);
}

QByteArray fly_state_to_json_bytes(const FlyState &st, bool compact)
{
	const QJsonDocument doc(fly_state_to_json_object(st));
	return doc.toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented);
}

FlyState fly_state_make_defaults()
{
	FlyState st;
	st.home = FlyTeam{};
	st.away = FlyTeam{};
	st.swap_sides = false;
	st.show_scoreboard = true;

	st.custom_fields.clear();
	st.single_stats.clear();
	st.timers.clear();

	ensureDefaultCustomFields(st);
	ensureDefaultSingleStats(st);

	st.timers.push_back(makeDefaultMainTimer());

	return st;
}

bool fly_state_reset_defaults(const QString &base_dir)
{
	const QString pj = overlay_plugin_json(base_dir);
	QFile::remove(pj);

	const FlyState def = fly_state_make_defaults();
	return fly_state_save(base_dir, def);
}

bool fly_state_ensure_json_exists(const QString &base_dir, const FlyState *writeState)
{
	const QString path = overlay_plugin_json(base_dir);
	if (QFileInfo::exists(path))
		return true;

	FlyState st = writeState ? *writeState : fly_state_make_defaults();
	const bool ok = fly_state_save(base_dir, st);

	if (ok)
		LOGI("Created default plugin.json: %s", path.toUtf8().constData());
	else
		LOGW("Failed to create default plugin.json: %s", path.toUtf8().constData());

	return ok;
}
