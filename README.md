# enthrall
------------

Keyboard/mouse remote control (plus clipboard synchronization) over
the network.

### Basics

Say you have one machine with a keyboard and mouse attached to it, and
you want to use that same keyboard and mouse to control other nearby
machines.  `enthrall` does just that.

The machine with the keyboard and mouse attached is referred to as the
"master" node; the remotely-controlled machines are referred to as
"remotes".  Each participating machine must have the `enthrall` binary
installed; running it on the master connects to each remote over SSH
and runs its installed version.

### Requirements

Both master and remote functionality should work on Linux and FreeBSD
systems running X11 (porting to other *nixes shouldn't be terribly
hard) as well as Mac OS X 10.8 and 10.9 (other versions untested).  To
compile you'll need:

 - GNU `make` (`gmake` on some systems)
 - `flex` (2.5.35 and later known to work)
 - `bison` 2.4 or later
 - On X11 systems: XTest, XInput, and XRandR extensions, `pkg-config`
 - On Mac OS X: Xcode developer tools

Unfortunately the version of bison provided by Apple on Mac OS X is
2.3, which won't work.  MacPorts (and similar OSX package managers)
should have newer versions available that will work, however.

### Installation

Run `make`, then put the resulting `enthrall` binary wherever you like
(somewhere in `$PATH`, perhaps).

### Setup

You'll need to set up non-interactive (e.g. pubkey-based) SSH
authentication between your master and your remotes (and have sshd
running on each remote) so that `enthrall` can log in to each remote
automatically.  Use `ssh-keygen` and `~/.ssh/authorized_keys` as
normal for that.  If you'd like to lock things down a bit further, you
can use a dedicated SSH key and a `command="..."` directive (set to
wherever `enthrall` is installed on that host) on the relevant line of
your `~/.ssh/authorized_keys` file to restrict that key for use only
with `enthrall`.

### Usage

Start `enthrall` from the master node with a single argument: the path
to your config file (if you want, you could even just put a
`#!/path/to/enthrall` shebang line at the top of your config file,
`chmod` it executable, and run it as a little script).  It will
connect to all defined remotes and start an instance of itself on each
one.  You can then use hotkeys defined in your config file (or a
mouse-gesture mechanism) to change which machine receives keyboard and
mouse input.  See `example.conf` for a sample config file illustrating
how various configuration options work.

In the event of errors (e.g. network connection drops), `enthrall`
will attempt to automatically reconnect to any failed remotes, though
it will give up if these attempts fail repeatedly.  You can reset this
and restart the connection-reestablishment attempts with a `reconnect`
action bound to a hotkey, however (see `example.conf`).

### Security

Because `enthrall` does all its network communication over SSH, its
traffic (keystrokes, mouse clicks and motion, clipboard contents)
should be as secure as you have SSH configured to be.  `enthrall`
checks that its config file is owned by the user running it and is not
writable by any other user.

### Notes/Limitations/Known Issues

 - When using `show-focus = dim-inactive`, inactive OS X remotes
   (witnessed on 10.8, 10.9, and 10.12 at least) will sometimes
   spontaneously reset themselves to full brightness.  (I'm 99%
   certain this is simply a "feature" of OSX and not an enthrall bug.)

 - On OSX, having iTerm2 (possibly other applications as well, though
   that's the only one I've noticed) as the foreground application
   prevents `enthrall` from intercepting keyboard events, breaking
   proper operation as a master node.  As a (clumsy) workaround, you
   can just bring a different application to the foreground before
   switching enthrall's focus to a remote.

 - X11 selection (a.k.a. "clipboard", colloquially) management is
   somewhat incomplete; very large copy/paste operations (tens of
   megabytes) don't work, and may lead to a crash.

 - The network protocol is still unstable and may change in
   backwards-incompatible ways from one commit to the next.  You
   should thus (at least for now) always run the same version of
   `enthrall` on all participating machines.  At some point in the
   future the protocol should stabilize, but that point has not yet
   arrived.

### TODO/Planned Features

 - Configurable key-remapping

 - Daemonization

 - Scroll-wheel acceleration (perhaps mouse movement as well)

 - (Optionally) use libssh[2] instead of forking an `ssh` subprocess

### License

`enthrall` is released under the terms of the ISC License (see
`LICENSE`).
