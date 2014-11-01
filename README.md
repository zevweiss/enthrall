# enthrall
--------

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

### Installation

Run `make`, then put the resulting `enthrall` binary wherever you like
(somewhere in `$PATH`, perhaps).

### Setup

You'll need to set up non-interactive (e.g. pubkey-based) SSH
authentication between your master and your remotes so that `enthrall`
can log in to each remote automatically.  Use `ssh-keygen` and
`~/.ssh/authorized_keys` as normal for that.  If you'd like to lock
things down a bit further, you can use a dedicated SSH key and a
`command="..."` directive (set to wherever `enthrall` is installed on
that host) on the relevant line of your `~/.ssh/authorized_keys` file
to restrict that key for use only with `enthrall`.

### Usage

Start `enthrall` from the master node with a single argument: the path
to your config file.  It will connect to all defined remotes and start
an instance of itself on each one.  You can then use hotkeys defined
in your config file (or a mouse-gesture mechanism) to change which
machine receives keyboard and mouse input.  See `example.conf` for a
sample config file illustrating how various configuration options
work.

In the event of errors (e.g. network connection drops), `enthrall`
will attempt to automatically reconnect to any failed remotes, though
it will give up if these attempts fail repeatedly.  You can reset this
and restart the connection-reestablishment attempts with a "reconnect"
action bound to a hotkey, however (see `example.conf`).

### Security

Because `enthrall` does all its network communication over SSH, its
traffic (keystrokes, mouse clicks and motion, clipboard contents)
should be as secure as you have SSH configured to be.  `enthrall`
checks that its config file is owned by the user running it and is not
writable by any other user.

### Notes/Limitations/Known Issues

 - Mac OS X is currently only supported as a remote (X11 should work
   as a master or remote on Linux or FreeBSD).

 - When using `switch-indicator = dim-inactive`, inactive OS X remotes
   will sometimes (for reasons currently unknown) spontaneously reset
   themselves to full brightness.

 - Having windows of certain applications (Chrome or virt-manager for
   example) at screen edges may inhibit `enthrall`'s detection of the
   mouse pointer reaching those edges, disrupting mouse-switch
   fuctionality.

 - X11 selection (a.k.a. "clipboard", colloquially) management is
   somewhat incomplete; very large copy/paste operations (tens of
   megabytes) don't work, and may lead to a crash.

 - Since this is still in its infancy, the network protocol is
   unstable and may change in backwards-incompatible ways from one
   commit to the next.  You should thus (at least for now) always run
   the same version of `enthrall` on all participating machines.

### TODO/Planned Features

 - Support for OS X as a master.

 - Configurable key-remapping

 - Daemonization

 - Configurable debug/logging levels instead of just haphazardly
   squirting things to stderr

### License

`enthrall` is released under the terms of the ISC License (see
`LICENSE`).
