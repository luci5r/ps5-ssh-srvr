# Pseudo SSH Server Payload (Draft)

This is an initial draft of an *unencrypted* SSH-like server payload for a jailbroken PS5 (firmware 5.10 + etaHEN + elfldr). It does **not** implement the SSH protocol. It only offers:

* TCP listener (default port 2222) providing a minimal interactive session
* Commands: `help`, `exit`, `exec <prog> [args...]`
* Very naive program spawn searching a few hard-coded paths

## Build

Ensure the PS5 payload SDK environment variable is set:

```bash
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make -C sshsvr
```

## Deploy

```bash
make -C sshsvr deploy PS5_HOST=your.ps5.ip PS5_PORT=9021
```

## Roadmap Ideas

* Integrate with existing `elfldr` from telnet shell to load arbitrary ELFs
* Reuse builtin command framework from `shsrv`
* Optional compression or simple auth token (not security, just guard)
* Session multiplex (single listener, multiple forked children)
* Progress: parse BGFT/KLOG and print percentage.
* List USB PKGs: add pkgs to scan /mnt/usb0/*.pkg with sizes.
* Uninstall: builtin to remove an installed title cleanly.
* Integrity: optional SHA-256 check before/after install.
* Queueing: support multiple install requests; show status.
* Auth: simple token for pseudo-ssh commands (basic protection).
* Raw transfers: get/put -b to avoid base64 overhead.

## Disclaimer
For personal LAN use only. Not a real SSH implementation; no encryption, no authentication.
