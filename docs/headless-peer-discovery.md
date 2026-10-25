# Discover the current Headless control peer

The existing local JSON control interface supports a read-only discovery query:

```json
{"request_id":"unique-request-id","query":true,"peer_address":"auto"}
```

Headless returns the connected Direct target when available, otherwise its unique
connected peer. The acknowledgement's `peer_address` contains the resolved address
from the current processor peer list. No connected peer returns `peer_not_found`;
multiple fallback candidates return `multiple_connected_peers`.

Settings requests still require that explicit resolved address; `auto` is only
interpreted for queries. This lets clients discover address changes without
applying edits intended for an old peer to a newly selected peer.

Local integration verification on 2026-10-25 used Bach's production controller:
discovery succeeded; jitter buffer changed from 48 to 53.33 ms, with
`ok=true`, `applied=true`, `settings_persisted=true`, then returned to 48 ms.
The runtime export confirmed both values. Gain, auto-buffer mode, codec, audio
device, device buffer, sample rate, UDP port, setup XML and LaunchAgent were preserved.
This change is confined to the local control interface; audio processing is unchanged.
