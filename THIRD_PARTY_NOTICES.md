# Third-party notices

TMD-56 Temperature Logger is not an official Amprobe product.

The application source in this repository is covered by the MIT license in `LICENSE`.
Standalone Windows builds and the Linux AppImage also redistribute runtime libraries.
Those libraries are not our code. Their copyright notices and licenses stay with the
binaries. The Windows bundling script copies the license files that MSYS2 ships for
the libraries it actually packages into `THIRD-PARTY-LICENSES/` when those files are
present. Do not strip them.

Typical components and their licenses:

| Component | License | Source |
| --- | --- | --- |
| GTK 4 | LGPL-2.1-or-later | https://gitlab.gnome.org/GNOME/gtk |
| GLib | LGPL-2.1-or-later | https://gitlab.gnome.org/GNOME/glib |
| Cairo | LGPL-2.1 or MPL-1.1 | https://www.cairographics.org/ |
| Pango | LGPL-2.1-or-later | https://gitlab.gnome.org/GNOME/pango |
| HarfBuzz | MIT | https://github.com/harfbuzz/harfbuzz |
| gdk-pixbuf | LGPL-2.1-or-later | https://gitlab.gnome.org/GNOME/gdk-pixbuf |
| libserialport | LGPL-3.0-or-later | https://sigrok.org/wiki/Libserialport |
| Graphene | MIT | https://github.com/ebassi/graphene |

Corresponding source for each LGPL library is published by that project. This
repository does not claim ownership of those libraries.

The Debian package does not bundle them. It depends on the distribution packages instead.
