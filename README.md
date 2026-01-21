# Pseudo SSH Server Payload

This is an *unencrypted* SSH-like server payload for a jailbroken PS5 (firmware <= 9.60 + etaHEN). It does **not** implement the SSH protocol. It only offers:

* TCP listener (default port 2222) providing a minimal interactive session
* Commands: `help`, `install`, `ps`, `exec <prog> [args...]`

## Credits
All credits for the original 

## Deploy

Ensure etaHEN and ELF Loader (elfldr.elf) are running and listening on Port 9021 (Default). Use your favorite tool, like socat, netcatgui, etc, to deliver the payload to your PS5.

For example, if you have command-line socat on your system, you can use command-line:
```bash
socat.exe -t 99999999 - TCP:192.168.50.5:9021 < "ps5-ssh-srvr.elf"
```

This will start the server on your PS5, listening on Port 2222.

Can be traced in KLOG:
`[sshsvr] sshsvr listening on port 2222 (pid=100)`
 
## Usage

Connect via telnet from your system on the same network as PS5:

`telnet <your-IP-address> 2222`

Sample output using sample IP:
```
$ telnet 192.168.50.5 2222
Trying 192.168.50.5...
Connected to 192.168.50.5.
Escape character is '^]'.
Pseudo-SSH (unencrypted) - remote 192.168.50.7
Type 'help' for builtins.
$ 
```

Type `help` to display a list of commands you can use. 

```
$ help
help       - Show help
exit       - Exit session
ls         - List directory
ll         - Alias for ls -l
rm         - Remove files (-r)
cp         - Copy files (-r)
mv         - Move/rename
mkdir      - Create directories (-p)
pwd        - Print working directory
cd         - Change directory
cat        - Show file contents
ps         - List processes
put        - Receive base64 file
get        - Send base64 file
install    - Install PKG via etaHEN DPI (9090/12800)
klogtail   - Tail kernel log (stub)
execelf    - Execute ELF payload
debugelf   - Execute ELF (debug mode)
kill       - Send signal (kill <pid> [sig])
serverctl  - Control server (start/stop/restart/status)
```

## Feature
A feature command not available in the original "shsrv" payload is the ability to INSTALL PKG (PS4 Backups) files via the command-line. This feature uses the etaHEN DPI capability to install the PKG.

By default, the server looks at `/mnt/USB0` for PKG files, and can be simply used as:

`install my-ps4-backup.pkg`

### Usage scenario
Often I have PKGs on my exFAT USB Drive attached to my PS5. Instead of sitting in front of my PS5 and waiting to install them, I'm able to VNC or login to my home PC remotely while I'm at work, and use the server to install PKGs. 

## Build

Ensure the PS5 payload SDK environment variable is set:

```bash
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make
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
