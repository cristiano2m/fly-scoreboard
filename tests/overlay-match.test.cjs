const assert = require("node:assert/strict");
const { readFileSync } = require("node:fs");
const { join } = require("node:path");
const { test } = require("node:test");
const vm = require("node:vm");

const source = readFileSync(join(__dirname, "../data/overlay/script.js"), "utf8");

// Loads script.js in a sandbox without DOM nodes and returns its global functions.
function loadOverlay() {
  const context = vm.createContext({
    Date: { now: () => 0 },
    URLSearchParams,
    WebSocket: class {
      addEventListener() {}
      send() {}
    },
    window: { location: { search: "" } },
    NodeFilter: { SHOW_TEXT: 4 },
    document: {
      body: {},
      createTreeWalker: () => ({ nextNode: () => null }),
      querySelectorAll: () => [],
    },
    fetch: async () => ({ ok: false }),
    setTimeout: () => 1,
    requestAnimationFrame: () => {},
  });
  vm.runInContext(source, context);
  return context;
}

const overlay = loadOverlay();
const kicks = (pattern) => [...pattern].map((c) => ({ result: c === "1" ? "goal" : "miss" }));

test("alert is active only while visible and inside its duration", () => {
  const st = {
    home: { title: "Home FC", logo: "home.png", color: "16711680" },
    away: { title: "Away FC" },
    alert: {
      type: "goal",
      team: "home",
      title: "GOAL",
      player_number: "10",
      player_name: "Juan Perez",
      minute: "23'",
      started_ms: "1000",
      duration_ms: "8000",
      visible: true,
    },
  };

  const shown = overlay.buildAlertView(st, 5000);
  assert.equal(shown.active, true);
  assert.equal(shown.has_player, true);
  assert.equal(shown.team.title, "Home FC");
  assert.equal(shown.team.color, "#ff0000");
  assert.equal(shown.side, "x");
  assert.match(shown.css, /fly-alert--goal/);
  assert.match(shown.css, /is-active/);

  assert.equal(overlay.buildAlertView(st, 9001).active, false);
  assert.equal(overlay.buildAlertView({ ...st, alert: { ...st.alert, visible: false } }, 5000).active, false);
});

test("alert side follows swapped sides", () => {
  const st = { swap_sides: true, alert: { type: "red", team: "home", visible: true, started_ms: 1, duration_ms: 0 } };
  const view = overlay.buildAlertView(st, 10);
  assert.equal(view.side, "y");
  assert.equal(view.active, true);
  assert.equal(view.type, "red");
});

test("team color already converted to hex is kept", () => {
  assert.equal(overlay.teamView({ color: "#00ff00" }).color, "#00ff00");
  assert.equal(overlay.teamView({}).color, "");
});

test("shootout counts goals and misses per team", () => {
  const summary = overlay.shootoutSummary({ rounds: 5, home: kicks("101"), away: kicks("11") });
  assert.equal(summary.home.goals, 2);
  assert.equal(summary.home.misses, 1);
  assert.equal(summary.away.goals, 2);
  assert.equal(summary.away.taken, 2);
  assert.equal(summary.winner, "");
  assert.equal(summary.next, "away");
});

test("shootout ends early when one side cannot be caught", () => {
  // Home 3/3, away 0/3: away can score at most 2 more.
  const summary = overlay.shootoutSummary({ rounds: 5, home: kicks("111"), away: kicks("000") });
  assert.equal(summary.winner, "home");
  assert.equal(summary.next, "");
});

test("shootout is not decided while the trailing side can still draw", () => {
  const summary = overlay.shootoutSummary({ rounds: 5, home: kicks("111"), away: kicks("00") });
  assert.equal(summary.winner, "");
});

test("tie after regulation goes to sudden death", () => {
  const tied = overlay.shootoutSummary({ rounds: 5, home: kicks("11110"), away: kicks("11101") });
  assert.equal(tied.winner, "");
  assert.equal(tied.sudden_death, true);
  assert.equal(tied.next, "home");

  const halfRound = overlay.shootoutSummary({ rounds: 5, home: kicks("111101"), away: kicks("11101") });
  assert.equal(halfRound.winner, "");

  const decided = overlay.shootoutSummary({ rounds: 5, home: kicks("111101"), away: kicks("111010") });
  assert.equal(decided.winner, "home");
});

test("first kicker comes from first_team", () => {
  assert.equal(overlay.shootoutSummary({ first_team: "away", home: [], away: [] }).next, "away");
  assert.equal(overlay.shootoutSummary({ home: [], away: [] }).next, "home");
});

test("shootout view builds marks for every slot and maps swapped sides", () => {
  const view = overlay.buildShootoutView({
    swap_sides: true,
    home: { title: "Home FC" },
    away: { title: "Away FC" },
    shootout: { active: true, visible: true, rounds: 3, home: kicks("10"), away: kicks("1") },
  });
  assert.equal(view.x.title, "Away FC");
  assert.deepEqual([...view.x.marks], ["goal", "pending", "pending"]);
  assert.deepEqual([...view.y.marks], ["goal", "miss", "pending"]);
  assert.equal(view.x.is_next, true);
  assert.match(view.css, /is-active/);
});

test("sponsors rotate by time and hide when empty", () => {
  const st = {
    show_sponsors: true,
    sponsors_rotate_ms: "1000",
    sponsors: [{ name: "A", logo: "a.png" }, { name: "B", logo: "b.png" }, { name: "", logo: "" }],
  };
  assert.equal(overlay.buildSponsorsView(st, 0).current.logo, "a.png");
  assert.equal(overlay.buildSponsorsView(st, 1500).current.logo, "b.png");
  assert.equal(overlay.buildSponsorsView(st, 2500).current.logo, "a.png");
  assert.equal(overlay.buildSponsorsView(st, 0).items.length, 2);
  assert.equal(overlay.buildSponsorsView({ show_sponsors: true, sponsors: [] }, 0).visible, false);
  assert.equal(overlay.buildSponsorsView({ ...st, show_sponsors: false }, 0).visible, false);
});
