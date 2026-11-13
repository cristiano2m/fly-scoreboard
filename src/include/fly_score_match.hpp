#pragma once

#include <QString>
#include <QVector>

#include "fly_score_state.hpp"

// Match-event helpers shared by the dock, hotkeys and WebSocket commands.
// They only touch FlyState values, so the UI layers stay thin.

enum class FlyAlertKind { Goal, Yellow, Red };

// Index of the team-stat row each alert kind increments (matches the
// canonical keys in script.js: score, fouls, yellow_cards, red_cards).
inline constexpr int kFlyFieldScore = 0;
inline constexpr int kFlyFieldYellowCards = 2;
inline constexpr int kFlyFieldRedCards = 3;

QString fly_alert_kind_key(FlyAlertKind kind);
bool fly_alert_kind_from_key(const QString &key, FlyAlertKind &out);
int fly_alert_kind_field_index(FlyAlertKind kind);

// "10 Juan Perez" per line -> players. Lines without a leading number keep an empty number.
QVector<FlyPlayer> fly_players_from_text(const QString &text);
QString fly_players_to_text(const QVector<FlyPlayer> &players);
QString fly_player_display(const FlyPlayer &player);

// Match minute ("23'") from the main timer, or an empty string when no timer runs.
QString fly_match_minute(const FlyState &st, qint64 nowMs);

FlyTeam &fly_team_ref(FlyState &st, bool home);
const FlyTeam &fly_team_ref(const FlyState &st, bool home);
QString fly_team_key(bool home);

// Penalty shootout helpers.
void fly_shootout_start(FlyShootout &so, bool homeFirst, int rounds);
void fly_shootout_add_kick(FlyShootout &so, bool home, bool goal, const QString &player);
bool fly_shootout_undo(FlyShootout &so);
int fly_shootout_goals(const QVector<FlyShootoutKick> &kicks);
// Team expected to kick next ("home" / "away").
QString fly_shootout_next_team(const FlyShootout &so);
