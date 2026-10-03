<p align="center">
	<picture>
		<source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/Falcon-MC/Falcon/main/.github/logo-white.png">
		<img src="https://raw.githubusercontent.com/Falcon-MC/Falcon/main/.github/logo.png" alt="Falcon" width="200">
	</picture>
	<br>
	<b>Falcon Broadcaster</b>
	<br>
	Lists a Minecraft: Bedrock Edition server in the Xbox Live friends list, written in C++17
</p>

<p align="center">
	<a href="https://github.com/Falcon-MC/Broadcaster/actions/workflows/ci.yml"><img src="https://github.com/Falcon-MC/Broadcaster/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
	<img src="https://img.shields.io/badge/minecraft-v1.26.52%20(Bedrock)-56383E" alt="Minecraft">
	<img src="https://img.shields.io/badge/language-C%2B%2B17-00599C" alt="C++17">
	<img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey" alt="Platform">
</p>

## What is this?

An Xbox Live account runs the broadcaster and publishes a multiplayer session. Its friends see the server as a world
in the Friends tab of Minecraft and join it like any friend's world, including on consoles, where custom servers
cannot be added. Every player who joins is sent straight on to the server.

- **Session** - the server is published as a world joinable by friends, over NetherNet with WebSocket or JSON-RPC
  signaling
- **Transfer** - joining players get a minimal game start, then a transfer to the configured server
- **Server status** - the server is queried so the world shows its MOTD and player count
- **Friends** - friend requests are accepted, and friends who stopped joining are removed to keep room in the list
- **Presence** - the account stays online, which friends need to see the world

It is built on [Network](https://github.com/Falcon-MC/Network), which provides the Xbox Live services, the
multiplayer session and the NetherNet transport.

## Getting started

Run `FalconBroadcaster`. The first start writes `broadcaster.json` and exits; set `server.host` and `server.port`
to the server players are sent to, then start it again. It asks you to sign in the broadcasting account with a
Microsoft device code and keeps the token in `cache/token.json`.

```json
{
  "server": { "host": "play.example.net", "port": 19132 },
  "session": {
    "hostName": "",
    "worldName": "",
    "maxPlayers": 20,
    "queryServer": true,
    "updateInterval": 30,
    "signaling": "websocket"
  },
  "friendSync": {
    "enabled": true,
    "updateInterval": 60,
    "acceptRequests": true,
    "removeInactive": true,
    "inactiveDays": 8
  },
  "cache": { "token": "cache/token.json", "playerHistory": "cache/player_history.json" }
}
```

Empty `hostName` and `worldName` show the MOTD the server reports. Another file can be used with
`-config path/to/broadcaster.json`.

Players add the broadcasting account as a friend on Xbox, then join it from the Friends tab.

## Building

Requires CMake 3.16+, a C++17 compiler, zlib and OpenSSL. The first configure needs network access to fetch the
dependencies. On Windows the reference toolchain is MSYS2 UCRT64.

```
cmake -B build -G Ninja
cmake --build build
```

## Related repositories

- [Falcon](https://github.com/Falcon-MC/Falcon) - the server
- [Network](https://github.com/Falcon-MC/Network) - RakNet and NetherNet transport, Xbox Live services
- [Protocol](https://github.com/Falcon-MC/Protocol) - packets and network types
- [NBT](https://github.com/Falcon-MC/NBT) - NBT tags and binary streams

## Licensing information

Falcon Broadcaster is licensed under the [GNU Lesser General Public License v3.0](LICENSE), which supplements the
[GNU General Public License v3.0](COPYING). You may use, modify and redistribute it, as long as changes to it stay
under the same license.

Falcon is not affiliated with Mojang or Microsoft. All brands and trademarks belong to their respective owners.
