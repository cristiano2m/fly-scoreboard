#include "fly_score_match.hpp"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

QString fly_alert_kind_key(FlyAlertKind kind)
{
	switch (kind) {
	case FlyAlertKind::Goal:
		return QStringLiteral("goal");
	case FlyAlertKind::Yellow:
		return QStringLiteral("yellow");
	case FlyAlertKind::Red:
		return QStringLiteral("red");
	}
	return QStringLiteral("goal");
}

bool fly_alert_kind_from_key(const QString &key, FlyAlertKind &out)
{
	const QString k = key.trimmed().toLower();
	if (k == QLatin1String("goal")) {
		out = FlyAlertKind::Goal;
		return true;
	}
	if (k == QLatin1String("yellow")) {
		out = FlyAlertKind::Yellow;
		return true;
	}
	if (k == QLatin1String("red")) {
		out = FlyAlertKind::Red;
		return true;
	}
	return false;
}

int fly_alert_kind_field_index(FlyAlertKind kind)
{
	switch (kind) {
	case FlyAlertKind::Goal:
		return kFlyFieldScore;
	case FlyAlertKind::Yellow:
		return kFlyFieldYellowCards;
	case FlyAlertKind::Red:
		return kFlyFieldRedCards;
	}
	return kFlyFieldScore;
}

QVector<FlyPlayer> fly_players_from_text(const QString &text)
{
	static const QRegularExpression numbered(QStringLiteral("^#?(\\d{1,3})[\\s.\\-:]+(.+)$"));
	static const QRegularExpression numberOnly(QStringLiteral("^#?(\\d{1,3})$"));

	QVector<FlyPlayer> players;
	const QStringList lines = text.split(QLatin1Char('\n'));
	for (const QString &raw : lines) {
		const QString line = raw.simplified();
		if (line.isEmpty())
			continue;

		FlyPlayer p;
		const auto m = numbered.match(line);
		const auto n = numberOnly.match(line);
		if (m.hasMatch()) {
			p.number = m.captured(1);
			p.name = m.captured(2).trimmed();
		} else if (n.hasMatch()) {
			p.number = n.captured(1);
		} else {
			p.name = line;
		}
		players.push_back(p);
	}
	return players;
}

QString fly_players_to_text(const QVector<FlyPlayer> &players)
{
	QStringList lines;
	for (const auto &p : players) {
		const QString line = (p.number + QLatin1Char(' ') + p.name).trimmed();
		if (!line.isEmpty())
			lines << line;
	}
	return lines.join(QLatin1Char('\n'));
}

QString fly_player_display(const FlyPlayer &player)
{
	if (player.number.isEmpty())
		return player.name;
	if (player.name.isEmpty())
		return QStringLiteral("#%1").arg(player.number);
	return QStringLiteral("%1 - %2").arg(player.number, player.name);
}

QString fly_match_minute(const FlyState &st, qint64 nowMs)
{
	if (st.timers.isEmpty())
		return QString();

	const FlyTimer &t = st.timers[0];
	const qint64 elapsed = (t.running && t.last_tick_ms > 0) ? std::max<qint64>(0, nowMs - t.last_tick_ms) : 0;
	const bool countUp = t.mode == QLatin1String("countup");
	const qint64 value = countUp ? t.remaining_ms + elapsed : std::max<qint64>(0, t.remaining_ms - elapsed);
	const qint64 played = countUp ? value : std::max<qint64>(0, t.initial_ms - value);

	if (played <= 0 && !t.running)
		return QString();

	// Football convention: 22:30 played is the 23rd minute.
	const qint64 minute = played / 60000 + 1;
	return QStringLiteral("%1'").arg(minute);
}

FlyTeam &fly_team_ref(FlyState &st, bool home)
{
	return home ? st.home : st.away;
}

const FlyTeam &fly_team_ref(const FlyState &st, bool home)
{
	return home ? st.home : st.away;
}

QString fly_team_key(bool home)
{
	return home ? QStringLiteral("home") : QStringLiteral("away");
}

void fly_shootout_start(FlyShootout &so, bool homeFirst, int rounds)
{
	so = FlyShootout{};
	so.active = true;
	so.visible = true;
	so.rounds = std::clamp(rounds, 1, 20);
	so.first_team = fly_team_key(homeFirst);
}

void fly_shootout_add_kick(FlyShootout &so, bool home, bool goal, const QString &player)
{
	FlyShootoutKick kick;
	kick.goal = goal;
	kick.player = player;
	(home ? so.home : so.away).push_back(kick);
	so.sequence.push_back(fly_team_key(home));
	so.active = true;
}

bool fly_shootout_undo(FlyShootout &so)
{
	if (so.sequence.isEmpty())
		return false;

	const bool home = so.sequence.back() == QLatin1String("home");
	so.sequence.pop_back();
	auto &kicks = home ? so.home : so.away;
	if (!kicks.isEmpty())
		kicks.pop_back();
	return true;
}

int fly_shootout_goals(const QVector<FlyShootoutKick> &kicks)
{
	return static_cast<int>(
		std::count_if(kicks.cbegin(), kicks.cend(), [](const FlyShootoutKick &k) { return k.goal; }));
}

QString fly_shootout_next_team(const FlyShootout &so)
{
	if (so.home.size() == so.away.size())
		return so.first_team;
	return so.home.size() < so.away.size() ? QStringLiteral("home") : QStringLiteral("away");
}
