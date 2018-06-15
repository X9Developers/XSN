
Debian
====================
This directory contains files used to package xsnd/xsn-qt
for Debian-based Linux systems. If you compile xsnd/xsn-qt yourself, there are some useful files here.

## xsn: URI support ##


xsn-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install xsn-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your xsn-qt binary to `/usr/bin`
and the `../../share/pixmaps/xsn128.png` to `/usr/share/pixmaps`

xsn-qt.protocol (KDE)

