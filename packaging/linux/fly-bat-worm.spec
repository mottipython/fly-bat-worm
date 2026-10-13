Name:           fly-bat-worm
Version:        %{version}
Release:        1%{?dist}
Summary:        Fly, Bat, Worm, a screensaver from "Anathem"
License:        MIT
Source0:        fly-bat-worm-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
The Fly, the Bat and the Worm, from Neal Stephenson's novel Anathem: three
creatures with one sense each come upon the same thing, a clockwork trap in a
cave. The Fly sees it as a mosaic, the Bat hears its own cries come back from
its edges, and the Worm knows only the surface it has crawled over. What all
three share is its shape.

On Wayland desktops it starts by itself after five minutes idle.
fly-bat-worm-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/fly-bat-worm

%files
%license LICENSE
%doc README.md
%{_bindir}/fly-bat-worm
%{_bindir}/fly-bat-worm-screensaver
%{_datadir}/applications/fly-bat-worm.desktop
%dir %{_datadir}/screensaver-service
%dir %{_datadir}/screensaver-service/savers
%{_datadir}/screensaver-service/savers/fly-bat-worm.desktop
%{_datadir}/icons/hicolor/256x256/apps/fly-bat-worm.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/fly-bat-worm-screensaver.desktop
