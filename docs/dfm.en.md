[中文](dfm.md) · **English**

# Delta Force plugin

The ornament shows: **nickname, FIREFRONT rank score, liquid assets, illiquid assets, recent gain**
(red arrow up for profit, green arrow down for loss).

## Why the data is fetched on a PC

The bottleneck in this chain is not compute, it is **login**:

- The authorization `code` is valid for **minutes**, and only once;
- It can only be obtained after a browser-based QQ login;
- It must be exchanged for `openid + access_token` (valid 60 days) within seconds.

Doing that on the device means: the screen shows a countdown while the user has to
"copy link → paste → save" in under a minute, and any network hiccup (20 KB of TLS buffers,
a DNS failure) burns the code.

So the plugin uses a **push model**:

```
┌─ PC ──────────────────────────────┐        ┌─ ornament ──────┐
│ tools/df_sync.py                  │        │ Delta Force app │
│  1. exchange code for token (once)│  HTTP  │                 │
│  2. poll Tencent AMS every 5 min  │ ─────> │ POST /api/dfm   │
│  3. POST the result to the device │        │ writes data.json│
└───────────────────────────────────┘        │ draw only       │
                                             └─────────────────┘
```

The ornament makes no network requests and never stores the token — it lives on the PC
(`tools/df_token.json`).

## Usage

```bash
# 1. log in (once, valid 60 days)
python tools/df_sync.py login
#   log in to QQ in the browser, then paste the qccallback link back

# 2. push
python tools/df_sync.py run --host 10.165.41.102
#   the device IP is shown on the ornament's "network" screen

# push once / change interval
python tools/df_sync.py run --host 10.165.41.102 --once
python tools/df_sync.py run --host 10.165.41.102 --interval 600
```

On success the PC prints:

```
  黒人杀手大张伟  rank 5087  liquid 1.74亿  illiquid 5558万  gain -934.1万  -> {"ok":true}
```

The Delta Force screen re-reads the data file every 2 seconds, so it refreshes within 2 seconds
of a push.

## Endpoint

The device exposes a single endpoint; you can also push by hand:

```bash
curl -X POST "http://10.165.41.102/api/dfm?nick=%E6%B5%8B%E8%AF%95&rank=5087&liquid=174192696&noncur=55579713&gain=-9341475&ts=$(date +%s)"
```

| Param | Meaning |
|---|---|
| `nick` | Nickname, UTF-8 URL-encoded |
| `rank` | FIREFRONT rank score |
| `liquid` | Liquid assets (in-game currency) |
| `noncur` | Illiquid assets |
| `gain` | Recent gain; positive shows red ↑, negative green ↓ |
| `ts` | Unix seconds; older than 30 min shows "data N min ago" on screen |

## Where each field comes from

| Shown | Chart | Field |
|---|---|---|
| Nickname | 317814 `userData` | `charac_name` (URL-encoded UTF-8) |
| Rank score | same `careerData` | `rankpoint` |
| Liquid | same | `totalprice - noncurrentasset` |
| Illiquid | same | `noncurrentasset` |
| Recent gain | 316969 `solDetail` | `recentGain` |

## Caveats

- **The token is equivalent to account access**: `df_token.json` can read all game data of that
  account. Never commit it or share it. This repo's `.gitignore` already excludes it.
- The API is an internal Tencent endpoint with no stability guarantee; if fields change the
  script has to follow.
- It can only query **your own** account (openid is issued per application; there is no legitimate
  path to look up someone else by UID).
- Keep the push interval at 5 minutes or more to avoid rate limiting.
