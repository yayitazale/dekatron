// Copyright (c) 2026 Joseba Egia and Dekatron contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The clock reads POSIX timezones and has no table of names, so the page translates. These are common
// zones, each with the rule that has applied to it for years. Anything else can be typed in by hand.
const DekatronZones = (() => {
  const CET = "CET-1CEST,M3.5.0,M10.5.0/3";
  const EET = "EET-2EEST,M3.5.0/3,M10.5.0/4";
  const WET = "WET0WEST,M3.5.0/1,M10.5.0";
  const US_EAST = "EST5EDT,M3.2.0,M11.1.0";
  const US_CENTRAL = "CST6CDT,M3.2.0,M11.1.0";
  const US_MOUNTAIN = "MST7MDT,M3.2.0,M11.1.0";
  const US_PACIFIC = "PST8PDT,M3.2.0,M11.1.0";
  const AU_EAST = "AEST-10AEDT,M10.1.0,M4.1.0/3";
  const NZ = "NZST-12NZDT,M9.5.0,M4.1.0/3";

  const groups = {
    Europe: [
      ["Europe/Madrid", CET], ["Europe/Paris", CET], ["Europe/Berlin", CET], ["Europe/Rome", CET],
      ["Europe/Amsterdam", CET], ["Europe/Brussels", CET], ["Europe/Vienna", CET], ["Europe/Zurich", CET],
      ["Europe/Stockholm", CET], ["Europe/Oslo", CET], ["Europe/Copenhagen", CET], ["Europe/Prague", CET],
      ["Europe/Warsaw", CET], ["Europe/Budapest", CET], ["Europe/Belgrade", CET],
      ["Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"], ["Europe/Lisbon", WET], ["Atlantic/Canary", WET],
      ["Atlantic/Reykjavik", "GMT0"], ["Europe/Athens", EET], ["Europe/Helsinki", EET], ["Europe/Kyiv", EET],
      ["Europe/Bucharest", EET], ["Europe/Sofia", EET], ["Europe/Istanbul", "<+03>-3"], ["Europe/Moscow", "MSK-3"],
    ],
    Africa: [["Africa/Lagos", "WAT-1"], ["Africa/Johannesburg", "SAST-2"], ["Africa/Nairobi", "EAT-3"]],
    America: [
      ["America/New_York", US_EAST], ["America/Toronto", US_EAST], ["America/Detroit", US_EAST],
      ["America/Chicago", US_CENTRAL], ["America/Winnipeg", US_CENTRAL], ["America/Mexico_City", "CST6"],
      ["America/Denver", US_MOUNTAIN], ["America/Phoenix", "MST7"], ["America/Los_Angeles", US_PACIFIC],
      ["America/Vancouver", US_PACIFIC], ["America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"],
      ["America/Halifax", "AST4ADT,M3.2.0,M11.1.0"], ["Pacific/Honolulu", "HST10"], ["America/Bogota", "<-05>5"],
      ["America/Lima", "<-05>5"], ["America/Caracas", "<-04>4"], ["America/Sao_Paulo", "<-03>3"],
      ["America/Argentina/Buenos_Aires", "<-03>3"], ["America/Montevideo", "<-03>3"],
    ],
    Asia: [
      ["Asia/Tokyo", "JST-9"], ["Asia/Seoul", "KST-9"], ["Asia/Shanghai", "CST-8"], ["Asia/Hong_Kong", "HKT-8"],
      ["Asia/Taipei", "CST-8"], ["Asia/Singapore", "<+08>-8"], ["Asia/Manila", "PST-8"], ["Asia/Bangkok", "<+07>-7"],
      ["Asia/Jakarta", "WIB-7"], ["Asia/Dhaka", "<+06>-6"], ["Asia/Kathmandu", "<+0545>-5:45"],
      ["Asia/Kolkata", "IST-5:30"], ["Asia/Karachi", "PKT-5"], ["Asia/Dubai", "<+04>-4"],
      ["Asia/Tehran", "<+0330>-3:30"], ["Asia/Riyadh", "<+03>-3"],
    ],
    Oceania: [
      ["Australia/Sydney", AU_EAST], ["Australia/Melbourne", AU_EAST], ["Australia/Hobart", AU_EAST],
      ["Australia/Brisbane", "AEST-10"], ["Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"],
      ["Australia/Darwin", "ACST-9:30"], ["Australia/Perth", "AWST-8"], ["Pacific/Auckland", NZ],
    ],
    Other: [["UTC", "UTC0"]],
  };

  const list = Object.entries(groups).flatMap(([group, zones]) => zones.map(([id, tz]) => ({ group, id, tz })));
  const byTz = (tz) => list.find((zone) => zone.tz === tz);

  // The zone of the browser, when it is in the table.
  function detect() {
    let id = "";
    try {
      id = Intl.DateTimeFormat().resolvedOptions().timeZone;
    } catch (error) {
      return null;
    }
    return list.find((zone) => zone.id === id) || null;
  }

  const api = { groups, list, byTz, detect };
  if (typeof module !== "undefined") module.exports = api;
  return api;
})();
